/* SPDX-License-Identifier: MIT
 * MIT License
 * 
 * Copyright (c) 2026 zeroPL contributors
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

// zeroPL extension v1, introduced on 2026-09-29; not an upstream libmpv API.
// Use MPV_RENDER_API_TYPE_SW with MPV_RENDER_PARAM_SW_FORMAT = "poc-p010".
// SW_POINTER points to PoCP010Frame (not pixel bytes); SW_SIZE is an even
// width/height pair; SW_STRIDE is required by the API but planes use the strides
// below. Buffers must have capacity for height luma rows and height/2 chroma rows.
// Output is limited-range, left-sited P010. hlg selects HLG instead of PQ for
// subtitle composition; callers must match the source transfer and attach the
// corresponding BT.2020/PQ or HLG display metadata. subtitleWhiteNits sets the
// SDR graphics reference white. subtitlePixels is an output diagnostic counter.
// Existing upstream render formats and the public libmpv ABI are unchanged.
#ifndef POC_P010_H
#define POC_P010_H
#include <stdint.h>
#include <stddef.h>

// Private, versioned PoC extension to libmpv's software render target.
// It is used only with the matching PoC framework and format "poc-p010".
typedef struct PoCP010Frame {
    uint32_t magic;
    uint32_t version;
    uint8_t *luma;
    uint8_t *chroma;
    size_t lumaStride;
    size_t chromaStride;
    int32_t hlg;
    float subtitleWhiteNits;
    uint64_t subtitlePixels;
} PoCP010Frame;
#define POC_P010_MAGIC 0x504f4331u
#endif
