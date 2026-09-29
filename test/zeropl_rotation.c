// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 zeroPL contributors
// Added on 2026-09-29: verify software rotation with synthetic pixels only.
#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "mpv/client.h"
#include "mpv/render.h"
#include "mpv/zeropl_p010.h"

static void option(mpv_handle *mpv, const char *name, const char *value)
{
    int result = mpv_set_option_string(mpv, name, value);
    if (result < 0) {
        fprintf(stderr, "%s: %s\n", name, mpv_error_string(result));
        abort();
    }
}

static int sample(const uint8_t *pixels, int w, int h, int quadrant, int p010)
{
    int x = (quadrant % 2 ? 3 : 1) * w / 4;
    int y = (quadrant / 2 ? 3 : 1) * h / 4;
    if (p010)
        return ((((const uint16_t *)pixels)[y * w + x] >> 6) - 64) * 255 / 876;
    return pixels[(y * w + x) * 4];
}

static void check_rotation(const char *path, int rotation, int p010)
{
    mpv_handle *mpv = mpv_create();
    assert(mpv);
    option(mpv, "config", "no");
    option(mpv, "vo", "libmpv");
    option(mpv, "hwdec", "no");
    option(mpv, "audio", "no");
    option(mpv, "pause", "yes");
    option(mpv, "keep-open", "always");
    option(mpv, "osd-level", "0");
    char angle[16];
    snprintf(angle, sizeof(angle), "%d", rotation);
    option(mpv, "video-rotate", angle);
    assert(mpv_initialize(mpv) >= 0);
    mpv_render_context *context = NULL;
    mpv_render_param init[] = {
        {MPV_RENDER_PARAM_API_TYPE, MPV_RENDER_API_TYPE_SW}, {0}
    };
    assert(mpv_render_context_create(&context, mpv, init) >= 0);
    const char *load[] = {"loadfile", path, NULL};
    assert(mpv_command_async(mpv, 0, load) >= 0);
    int size[] = {rotation % 180 ? 32 : 64, rotation % 180 ? 64 : 32};
    uint8_t pixels[64 * 32 * 4] = {0};
    uint8_t chroma[64 * 32] = {0};
    size_t stride = size[0] * (p010 ? 2 : 4);
    PoCP010Frame frame = {
        .magic = POC_P010_MAGIC, .version = 1,
        .luma = pixels, .chroma = chroma,
        .lumaStride = stride, .chromaStride = stride, .subtitleWhiteNits = 203,
    };
    char *format = p010 ? "poc-p010" : "rgb0";
    int block = 0;
    mpv_render_param params[] = {
        {MPV_RENDER_PARAM_SW_SIZE, size}, {MPV_RENDER_PARAM_SW_FORMAT, format},
        {MPV_RENDER_PARAM_SW_STRIDE, &stride},
        {MPV_RENDER_PARAM_SW_POINTER, p010 ? (void *)&frame : pixels},
        {MPV_RENDER_PARAM_BLOCK_FOR_TARGET_TIME, &block}, {0}
    };
    const int expected[4][4] = {{32, 96, 160, 224}, {160, 32, 224, 96},
                               {224, 160, 96, 32}, {96, 224, 32, 160}};
    int matched = 0;
    for (int attempt = 0; attempt < 500 && !matched; attempt++) {
        mpv_wait_event(mpv, 0.01);
        if (!(mpv_render_context_update(context) & MPV_RENDER_UPDATE_FRAME))
            continue;
        assert(mpv_render_context_render(context, params) >= 0);
        mpv_render_context_report_swap(context);
        matched = 1;
        for (int q = 0; q < 4; q++)
            matched &= abs(sample(pixels, size[0], size[1], q, p010) -
                           expected[rotation / 90][q]) < 12;
    }
    if (!matched) {
        fprintf(stderr, "%s rotation %d: got %d %d %d %d\n", format, rotation,
                sample(pixels, size[0], size[1], 0, p010),
                sample(pixels, size[0], size[1], 1, p010),
                sample(pixels, size[0], size[1], 2, p010),
                sample(pixels, size[0], size[1], 3, p010));
    }
    mpv_render_context_free(context);
    mpv_terminate_destroy(mpv);
    assert(matched);
}

int main(void)
{
    char path[] = "/tmp/zeropl-rotation-XXXXXX";
    int fd = mkstemp(path);
    assert(fd >= 0);
    FILE *file = fdopen(fd, "wb");
    assert(file);
    fputs("P6\n64 32\n255\n", file);
    for (int y = 0; y < 32; y++) for (int x = 0; x < 64; x++) {
        uint8_t value = 32 + (x >= 32 ? 64 : 0) + (y >= 16 ? 128 : 0);
        uint8_t rgb[] = {value, value, value};
        assert(fwrite(rgb, sizeof(rgb), 1, file) == 1);
    }
    assert(fclose(file) == 0);
    for (int p010 = 0; p010 <= 1; p010++)
        for (int rotation = 0; rotation < 360; rotation += 90)
            check_rotation(path, rotation, p010);
    assert(unlink(path) == 0);
    puts("PASS: software RGB/P010 rotation at 0/90/180/270 degrees");
    return 0;
}
