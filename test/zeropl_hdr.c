// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 zeroPL contributors
// Synthetic host tests for the exact compositor compiled into the iPad PoC.
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "video/out/PoCHDRComposite.h"

static void near(float actual, float expected, float tolerance) {
    if (fabsf(actual - expected) > tolerance) {
        fprintf(stderr, "actual %.6f expected %.6f tolerance %.6f\n", actual, expected, tolerance);
        assert(0);
    }
}
int main(void) {
    PoCHDRLUT lut;
    poc_init_lut(&lut);
    for (int i = 0; i <= 10000; i++) {
        float v = i / 10000.0f;
        near(poc_lookup(lut.pqEncode, 16384, sqrtf(v)), poc_pq_encode(10000 * v), .00003f);
        float light = poc_lookup(lut.pqDecode, 4096, v);
        // Interpolation close to PQ black stays below 0.1 of a 10-bit code.
        near(poc_pq_encode(light), v, .00008f);
    }
    for (int hlg = 0; hlg < 2; hlg++) {
        uint16_t y[16], uv[8];
        uint8_t osd[64] = {0};
        PoCP010Frame frame = {.luma=(uint8_t *)y, .chroma=(uint8_t *)uv,
            .lumaStride=8, .chromaStride=8, .hlg=hlg, .subtitleWhiteNits=203};
        float white[3] = {203,203,203};
        poc_from_light(white, hlg);
        near(white[0], hlg ? .749877f : .580689f, .00001f);
        poc_to_light(white, hlg);
        near(white[0], 203, .02f);
        for (int i = 0; i < 16; i++) y[i] = poc_y_code(0);
        for (int i = 0; i < 8; i++) uv[i] = 512 << 6;
        poc_composite(&lut, &frame, 4, 4, osd, 16, -4, -4, 7, 7);
        assert(frame.subtitlePixels == 0);
        for (int i = 0; i < 16; i++) assert(y[i] == 64 << 6);
        // Opaque, half-transparent, and gray graphics over neutral 100-nit video.
        for (int test = 0; test < 3; test++) {
            float background[3] = {100,100,100};
            poc_from_light(background, hlg);
            for (int i = 0; i < 16; i++) y[i] = poc_y_code(background[0]);
            memset(osd, 0, sizeof osd);
            int alpha = test == 1 ? 128 : 255;
            int channel = test == 2 ? 128 : alpha;
            for (int dy = 0; dy < 2; dy++) for (int dx = 0; dx < 2; dx++) {
                uint8_t *p = osd + dy * 16 + dx * 4;
                p[0]=p[1]=p[2]=channel; p[3]=alpha;
            }
            frame.subtitlePixels = 0;
            uint16_t unchanged = y[15];
            poc_composite(&lut, &frame, 4, 4, osd, 16, 0, 0, 1, 1);
            assert(frame.subtitlePixels == 4 && y[15] == unchanged);
            float result[3];
            for (int c = 0; c < 3; c++) result[c] = ((y[0] >> 6) - 64.0f) / 876;
            poc_to_light(result, hlg);
            float expected = test == 0 ? 203 : test == 1 ? 100 * (127.0f / 255) + 203 * (128.0f / 255) : 43.81968f;
            near(result[0], expected, 1.5f);
            assert(uv[0] == 512 << 6 && uv[1] == 512 << 6);
        }
        // Saturated graphics must change primaries before HDR encoding.
        uint8_t red[4] = {0,0,255,255}; float rgb[3];
        poc_graphics_light(red, 203, rgb);
        near(rgb[0], 127.36299f, .001f);
        near(rgb[1], 14.02675f, .001f);
        near(rgb[2], 3.32745f, .001f);
        float original[3]; memcpy(original, rgb, sizeof rgb);
        poc_from_light(rgb, hlg); poc_to_light(rgb, hlg);
        for (int c = 0; c < 3; c++) near(rgb[c], original[c], .03f);
    }
    puts("PASS: PQ/HLG reference white, linear alpha, gray, gamut conversion, transparent pixels, bounds and chroma");
}
