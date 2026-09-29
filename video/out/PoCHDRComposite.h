// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 zeroPL contributors
// Added on 2026-09-29 for P010 output and HDR subtitle composition.
// SDR graphics composition in BT.2100 display-linear light.
#ifndef POC_HDR_COMPOSITE_H
#define POC_HDR_COMPOSITE_H
#include <math.h>
#include "mpv/zeropl_p010.h"

static inline float poc_clamp(float x, float lo, float hi) {
    return fminf(hi, fmaxf(lo, x));
}
static inline float poc_pq_decode(float v) {
    float p = powf(poc_clamp(v, 0, 1), 1.0f / 78.84375f);
    return 10000 * powf(fmaxf(p - .8359375f, 0) / (18.8515625f - 18.6875f * p), 1.0f / .1593017578125f);
}
static inline float poc_pq_encode(float nits) {
    float p = powf(poc_clamp(nits / 10000, 0, 1), .1593017578125f);
    return powf((.8359375f + 18.8515625f * p) / (1 + 18.6875f * p), 78.84375f);
}
static inline float poc_hlg_decode(float v) {
    v = fmaxf(v, 0);
    return v <= .5f ? v * v / 3 : (expf((v - .55991073f) / .17883277f) + .28466892f) / 12;
}
static inline float poc_hlg_encode(float v) {
    v = fmaxf(v, 0);
    return v <= 1.0f / 12 ? sqrtf(3 * v) : .17883277f * logf(12 * v - .28466892f) + .55991073f;
}
static inline float poc_luma(const float *rgb) {
    return .2627f * rgb[0] + .6780f * rgb[1] + .0593f * rgb[2];
}
typedef struct PoCHDRLUT {
    float pqDecode[4097], pqEncode[16385], srgbDecode[4097];
} PoCHDRLUT;

static void poc_init_lut(PoCHDRLUT *lut) {
    for (int i = 0; i <= 4096; i++) {
        float v = i / 4096.0f;
        lut->pqDecode[i] = poc_pq_decode(v);
        lut->srgbDecode[i] = v <= .04045f ? v / 12.92f : powf((v + .055f) / 1.055f, 2.4f);
    }
    for (int i = 0; i <= 16384; i++) {
        float v = i / 16384.0f;
        lut->pqEncode[i] = poc_pq_encode(10000 * v * v);
    }
}
static inline float poc_lookup(const float *table, int size, float value) {
    float index = poc_clamp(value, 0, 1) * size;
    int base = (int)index;
    if (base >= size) return table[size];
    return table[base] + (table[base + 1] - table[base]) * (index - base);
}
static inline void poc_to_light(float *rgb, int hlg) {
    for (int c = 0; c < 3; c++) rgb[c] = hlg ? poc_hlg_decode(rgb[c]) : poc_pq_decode(rgb[c]);
    if (hlg) {
        // Reference HLG OOTF: 1000 cd/m2 peak, system gamma 1.2.
        float gain = 1000 * powf(fmaxf(poc_luma(rgb), 0), .2f);
        for (int c = 0; c < 3; c++) rgb[c] *= gain;
    }
}
static inline void poc_from_light(float *rgb, int hlg) {
    if (hlg) {
        float gain = 1000 * powf(fmaxf(poc_luma(rgb) / 1000, 1e-20f), 1.0f / 6);
        for (int c = 0; c < 3; c++) rgb[c] = poc_hlg_encode(rgb[c] / gain);
    } else {
        for (int c = 0; c < 3; c++) rgb[c] = poc_pq_encode(rgb[c]);
    }
}
static inline void poc_graphics_light(const uint8_t *bgra, float white, float *rgb) {
    float srgb[3];
    for (int c = 0; c < 3; c++) {
        float v = poc_clamp((float)bgra[2 - c] / bgra[3], 0, 1);
        srgb[c] = v <= .04045f ? v / 12.92f : powf((v + .055f) / 1.055f, 2.4f);
    }
    // Linear BT.709 to BT.2020, D65 to D65.
    rgb[0] = white * (.6274039f * srgb[0] + .3292830f * srgb[1] + .0433131f * srgb[2]);
    rgb[1] = white * (.0690973f * srgb[0] + .9195404f * srgb[1] + .0113623f * srgb[2]);
    rgb[2] = white * (.0163914f * srgb[0] + .0880133f * srgb[1] + .8955953f * srgb[2]);
}
static inline void poc_graphics_fast(const PoCHDRLUT *lut, const uint8_t *bgra, float white, float *rgb) {
    float r = poc_lookup(lut->srgbDecode, 4096, (float)bgra[2] / bgra[3]);
    float g = poc_lookup(lut->srgbDecode, 4096, (float)bgra[1] / bgra[3]);
    float b = poc_lookup(lut->srgbDecode, 4096, (float)bgra[0] / bgra[3]);
    rgb[0] = white * (.6274039f * r + .3292830f * g + .0433131f * b);
    rgb[1] = white * (.0690973f * r + .9195404f * g + .0113623f * b);
    rgb[2] = white * (.0163914f * r + .0880133f * g + .8955953f * b);
}
static inline uint16_t poc_y_code(float y) {
    return (uint16_t)lroundf(poc_clamp(64 + y * 876, 64, 940)) << 6;
}
static inline uint16_t poc_c_code(float c) {
    return (uint16_t)lroundf(poc_clamp(512 + c * 896, 64, 960)) << 6;
}

// Bounds are clamped and expanded to complete chroma blocks. Pixels outside
// subtitle coverage retain their original luma; untouched blocks are bit exact.
static void poc_composite(const PoCHDRLUT *lut, PoCP010Frame *frame, int width, int height,
                          const uint8_t *overlay, int overlayStride,
                          int x0, int y0, int x1, int y1) {
    x0 = (int)poc_clamp(x0, 0, width) & ~1;
    y0 = (int)poc_clamp(y0, 0, height) & ~1;
    x1 = (int)poc_clamp((x1 + 1) & ~1, 0, width);
    y1 = (int)poc_clamp((y1 + 1) & ~1, 0, height);
    for (int y = y0; y < y1; y += 2) {
        uint16_t *uv = (uint16_t *)(frame->chroma + (y / 2) * frame->chromaStride);
        for (int x = x0; x < x1; x += 2) {
            float cb = ((uv[x] >> 6) - 512.0f) / 896;
            float cr = ((uv[x + 1] >> 6) - 512.0f) / 896;
            float sumCB = 0, sumCR = 0;
            int changed = 0;
            for (int dy = 0; dy < 2; dy++) {
                uint16_t *luma = (uint16_t *)(frame->luma + (y + dy) * frame->lumaStride);
                for (int dx = 0; dx < 2; dx++) {
                    const uint8_t *osd = overlay + (y + dy) * overlayStride + (x + dx) * 4;
                    if (!osd[3]) { sumCB += cb; sumCR += cr; continue; }
                    float yp = ((luma[x + dx] >> 6) - 64.0f) / 876;
                    float rgb[3] = {yp + 1.4746f * cr, yp - .1645531268f * cb - .5713531268f * cr, yp + 1.8814f * cb};
                    float graphics[3], alpha = osd[3] / 255.0f;
                    poc_graphics_fast(lut, osd, frame->subtitleWhiteNits, graphics);
                    if (osd[3] == 255) {
                        for (int c = 0; c < 3; c++) rgb[c] = graphics[c];
                    } else {
                        if (frame->hlg) poc_to_light(rgb, 1);
                        else for (int c = 0; c < 3; c++) rgb[c] = poc_lookup(lut->pqDecode, 4096, rgb[c]);
                        for (int c = 0; c < 3; c++) rgb[c] = rgb[c] * (1 - alpha) + graphics[c] * alpha;
                    }
                    if (frame->hlg) poc_from_light(rgb, 1);
                    else for (int c = 0; c < 3; c++) rgb[c] = poc_lookup(lut->pqEncode, 16384, sqrtf(fmaxf(rgb[c], 0) / 10000));
                    float outY = poc_luma(rgb);
                    luma[x + dx] = poc_y_code(outY);
                    sumCB += (rgb[2] - outY) / 1.8814f;
                    sumCR += (rgb[0] - outY) / 1.4746f;
                    changed++;
                }
            }
            if (changed) {
                uv[x] = poc_c_code(sumCB / 4);
                uv[x + 1] = poc_c_code(sumCR / 4);
                frame->subtitlePixels += changed;
            }
        }
    }
}
#endif
