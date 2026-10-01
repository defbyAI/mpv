// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 zeroPL contributors
// Added on 2026-10-01: synchronous, refcounted views of caller-owned planes.
#ifndef MP_ZEROPL_SWSCALE_H
#define MP_ZEROPL_SWSCALE_H

#include <limits.h>
#include <stdint.h>
#include <libavutil/buffer.h>
#include <libavutil/error.h>
#include <libavutil/frame.h>
#include <libavutil/imgutils.h>
#include <libavutil/pixdesc.h>
#include <libswscale/swscale.h>

static inline void zeropl_borrowed_free(void *opaque, uint8_t *data)
{
    // The caller retains and locks the pixels for the entire synchronous call.
    (void)opaque;
    (void)data;
}

static inline int zeropl_borrowed_frame(AVFrame *frame, enum AVPixelFormat fmt,
                                      int width, int height,
                                      uint8_t *const planes[4], const int strides[4],
                                      int readonly)
{
    av_frame_unref(frame);
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(fmt);
    int row_bytes[4];
    if (!desc || width <= 0 || height <= 0 ||
        (desc->flags & (AV_PIX_FMT_FLAG_PAL | AV_PIX_FMT_FLAG_HWACCEL)) ||
        av_image_fill_linesizes(row_bytes, fmt, width) < 0)
        return AVERROR(EINVAL);

    frame->format = fmt;
    frame->width = width;
    frame->height = height;
    for (int p = 0; p < 4; p++) {
        if (!row_bytes[p])
            continue;
        int rows = (p == 1 || p == 2)
            ? AV_CEIL_RSHIFT(height, desc->log2_chroma_h) : height;
        int64_t stride = strides[p];
        int64_t magnitude = stride < 0 ? -stride : stride;
        if (!planes[p] || magnitude < row_bytes[p] || magnitude > INT_MAX)
            goto invalid;
        uint64_t offset = (uint64_t)(rows - 1) * magnitude;
        uint64_t span = offset + row_bytes[p];
        uintptr_t address = (uintptr_t)planes[p];
        if (span > SIZE_MAX || (stride < 0 && address < offset))
            goto invalid;
        uintptr_t start = stride < 0 ? address - offset : address;
        if (start > UINTPTR_MAX - span)
            goto invalid;
        frame->buf[p] = av_buffer_create((uint8_t *)start, (size_t)span,
            zeropl_borrowed_free, NULL, readonly ? AV_BUFFER_FLAG_READONLY : 0);
        if (!frame->buf[p]) {
            av_frame_unref(frame);
            return AVERROR(ENOMEM);
        }
        frame->data[p] = planes[p];
        frame->linesize[p] = strides[p];
    }
    if (!frame->buf[0])
        goto invalid;
    return 0;
invalid:
    av_frame_unref(frame);
    return AVERROR(EINVAL);
}

static inline int zeropl_scale_borrowed(struct SwsContext *sws,
                                       AVFrame *source, AVFrame *destination,
                                       enum AVPixelFormat sfmt, int sw, int sh,
                                       uint8_t *const sp[4], const int ss[4],
                                       enum AVPixelFormat dfmt, int dw, int dh,
                                       uint8_t *const dp[4], const int ds[4])
{
    int result = zeropl_borrowed_frame(source, sfmt, sw, sh, sp, ss, 1);
    if (result >= 0)
        result = zeropl_borrowed_frame(destination, dfmt, dw, dh, dp, ds, 0);
    if (result >= 0) {
        result = sws_scale_frame(sws, destination, source);
        // A failed frame_start can retain only the source. End even on error.
        sws_frame_end(sws);
        for (int p = 0; p < 4; p++) {
            if (destination->data[p] != dp[p] && destination->data[p])
                result = AVERROR(EINVAL);
        }
        if (result >= 0)
            result = dh;
    }
    av_frame_unref(source);
    av_frame_unref(destination);
    return result;
}

#endif
