// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 zeroPL contributors
// Added on 2026-10-01: synthetic exact-output and borrowed-plane regression.
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <libavutil/opt.h>
#include "video/zeropl_swscale.h"

static AVFrame *image(enum AVPixelFormat fmt, int w, int h)
{
    AVFrame *f = av_frame_alloc();
    assert(f);
    f->format = fmt; f->width = w; f->height = h;
    assert(av_frame_get_buffer(f, 64) == 0);
    return f;
}

static int rows(AVFrame *f, int p)
{
    const AVPixFmtDescriptor *d = av_pix_fmt_desc_get(f->format);
    return p == 1 || p == 2 ? AV_CEIL_RSHIFT(f->height, d->log2_chroma_h) : f->height;
}

static void pattern(AVFrame *f, unsigned seed)
{
    const AVPixFmtDescriptor *d = av_pix_fmt_desc_get(f->format);
    int bytes[4];
    assert(av_image_fill_linesizes(bytes, f->format, f->width) >= 0);
    for (int p = 0; p < 4; p++) {
        for (int y = 0; bytes[p] && y < rows(f, p); y++) {
            uint8_t *row = f->data[p] + (ptrdiff_t)y * f->linesize[p];
            memset(row, 0xcd, f->linesize[p]);
            for (int x = 0; x < bytes[p]; x++) {
                seed = seed * 1664525u + 1013904223u;
                row[x] = seed >> 24;
            }
            if (d->comp[0].depth > 8) {
                uint16_t *words = (uint16_t *)row;
                for (int x = 0; x < bytes[p] / 2; x++) {
                    words[x] &= (1 << d->comp[0].depth) - 1;
                    if (f->format == AV_PIX_FMT_P010LE)
                        words[x] <<= 6;
                }
            }
        }
    }
}

static void flip(AVFrame *f)
{
    for (int p = 0; p < 4; p++) {
        if (f->data[p]) {
            f->data[p] += (ptrdiff_t)(rows(f, p) - 1) * f->linesize[p];
            f->linesize[p] = -f->linesize[p];
        }
    }
}

static struct SwsContext *scaler(AVFrame *src, AVFrame *dst, int workers,
                                 int matrix, int range)
{
    struct SwsContext *c = sws_alloc_context();
    assert(c);
    assert(av_opt_set_int(c, "srcw", src->width, 0) == 0);
    assert(av_opt_set_int(c, "srch", src->height, 0) == 0);
    assert(av_opt_set_int(c, "src_format", src->format, 0) == 0);
    assert(av_opt_set_int(c, "dstw", dst->width, 0) == 0);
    assert(av_opt_set_int(c, "dsth", dst->height, 0) == 0);
    assert(av_opt_set_int(c, "dst_format", dst->format, 0) == 0);
    assert(av_opt_set_int(c, "threads", workers, 0) == 0);
    assert(av_opt_set_int(c, "sws_flags", SWS_BILINEAR | SWS_FULL_CHR_H_INT |
                          SWS_FULL_CHR_H_INP | SWS_ACCURATE_RND, 0) == 0);
    assert(av_opt_set_int(c, "src_h_chr_pos", 0, 0) == 0);
    assert(av_opt_set_int(c, "dst_h_chr_pos", 0, 0) == 0);
    const int *coeff = sws_getCoefficients(matrix);
    sws_setColorspaceDetails(c, coeff, range, coeff, 0, 0, 1 << 16, 1 << 16);
    assert(sws_init_context(c, NULL, NULL) == 0);
    if (workers > 1)
        sws_setColorspaceDetails(c, coeff, range, coeff, 0, 0, 1 << 16, 1 << 16);
    return c;
}

static void compare(AVFrame *a, AVFrame *b)
{
    int bytes[4];
    assert(av_image_fill_linesizes(bytes, a->format, a->width) >= 0);
    for (int p = 0; p < 4; p++) {
        for (int y = 0; bytes[p] && y < rows(a, p); y++) {
            if (memcmp(a->data[p] + (ptrdiff_t)y * a->linesize[p],
                       b->data[p] + (ptrdiff_t)y * b->linesize[p], bytes[p])) {
                fprintf(stderr, "different %s output: plane=%d row=%d\n",
                        av_get_pix_fmt_name(a->format), p, y);
                abort();
            }
        }
    }
}

int main(void)
{
    const enum AVPixelFormat inputs[] = {AV_PIX_FMT_NV12, AV_PIX_FMT_P010LE,
        AV_PIX_FMT_YUV420P, AV_PIX_FMT_YUV420P10LE, AV_PIX_FMT_YUV422P10LE,
        AV_PIX_FMT_YUV444P12LE, AV_PIX_FMT_BGRA};
    const enum AVPixelFormat outputs[] = {AV_PIX_FMT_BGRA, AV_PIX_FMT_P010LE};
    AVFrame *view_src = av_frame_alloc(), *view_dst = av_frame_alloc();
    assert(view_src && view_dst);
    int cases = 0;
    for (unsigned i = 0; i < sizeof(inputs) / sizeof(inputs[0]); i++) {
        for (unsigned o = 0; o < sizeof(outputs) / sizeof(outputs[0]); o++) {
            for (int variant = 0; variant < 4; variant++) {
                AVFrame *src = image(inputs[i], 1936, 1094);
                AVFrame *a = image(outputs[o], variant & 1 ? 1920 : 638,
                                               variant & 1 ? 1080 : 358);
                AVFrame *b = image(outputs[o], a->width, a->height);
                pattern(src, 17 + variant);
                if (variant & 2) { flip(src); flip(a); flip(b); }
                struct SwsContext *legacy = scaler(src, a, 1,
                    variant & 1 ? SWS_CS_BT2020 : SWS_CS_ITU709, variant & 1);
                assert(sws_scale(legacy, (const uint8_t *const *)src->data,
                    src->linesize, 0, src->height, a->data, a->linesize) == a->height);
                for (int workers = 2; workers <= 4; workers += 2) {
                    struct SwsContext *threaded = scaler(src, b, workers,
                        variant & 1 ? SWS_CS_BT2020 : SWS_CS_ITU709, variant & 1);
                    for (int repetition = 0; repetition < 3; repetition++) {
                        assert(zeropl_scale_borrowed(threaded, view_src, view_dst,
                            src->format, src->width, src->height, src->data, src->linesize,
                            b->format, b->width, b->height, b->data, b->linesize) == b->height);
                        assert(!view_src->buf[0] && !view_dst->buf[0]);
                        compare(a, b);
                    }
                    sws_freeContext(threaded);
                    cases++;
                }
                sws_freeContext(legacy);
                av_frame_free(&src); av_frame_free(&a); av_frame_free(&b);
            }
        }
    }
    uint8_t bytes[64] = {0};
    uint8_t *bad_planes[4] = {bytes};
    int bad_strides[4] = {3};
    assert(zeropl_borrowed_frame(view_src, AV_PIX_FMT_BGRA, 16, 1,
                                bad_planes, bad_strides, 1) < 0);
    assert(!view_src->buf[0]);
    av_frame_free(&view_src); av_frame_free(&view_dst);
    printf("zeroPL borrowed scaler: %d exact-output cases passed\n", cases);
    return 0;
}
