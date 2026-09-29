// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 zeroPL contributors
// Added on 2026-09-29 for P010 output and HDR subtitle composition.
// Included in the zeroPL fork of video/out/libmpv_sw.c.
static int poc_render(struct render_backend *ctx, int *sz, PoCP010Frame *target,
                      struct vo_frame *frame)
{
    struct priv *p = ctx->priv;
    if (!target || target->magic != POC_P010_MAGIC || target->version != 1 ||
        !target->luma || !target->chroma || sz[0] <= 0 || sz[1] <= 0 ||
        sz[0] % 2 || sz[1] % 2 || target->lumaStride < sz[0] * 2 ||
        target->chromaStride < sz[0] * 2 || target->lumaStride > INT_MAX ||
        target->chromaStride > INT_MAX || !isfinite(target->subtitleWhiteNits) ||
        target->subtitleWhiteNits <= 0 || target->subtitleWhiteNits > 1000)
        return MPV_ERROR_INVALID_PARAMETER;

    if (p->dst_params.imgfmt != IMGFMT_P010 || p->dst_params.w != sz[0] || p->dst_params.h != sz[1])
        p->anything_changed = true;
    // RGB accepts single-pixel letterboxing; 4:2:0 requires even crop origins
    // and extents. PiP frequently requests sizes such as 638 x 358.
    struct mp_rect output_rc = p->dst_rc;
    output_rc.x0 = MP_ALIGN_UP(output_rc.x0, 2);
    output_rc.y0 = MP_ALIGN_UP(output_rc.y0, 2);
    output_rc.x1 = MP_ALIGN_DOWN(output_rc.x1, 2);
    output_rc.y1 = MP_ALIGN_DOWN(output_rc.y1, 2);
    if (mp_rect_w(output_rc) <= 0 || mp_rect_h(output_rc) <= 0) return MPV_ERROR_INVALID_PARAMETER;
    if (p->anything_changed) {
        p->dst_params = p->src_params;
        p->dst_params.imgfmt = IMGFMT_P010;
        p->dst_params.w = sz[0];
        p->dst_params.h = sz[1];
        p->dst_params.repr.levels = PL_COLOR_LEVELS_LIMITED;
        p->dst_params.repr.bits = (struct pl_bit_encoding){0};
        p->dst_params.chroma_location = PL_CHROMA_LEFT;
        mp_image_params_guess_csp(&p->dst_params);
        if (p->src_params.imgfmt) {
            p->sws->src = p->src_params;
            p->sws->src.w = mp_rect_w(p->src_rc);
            p->sws->src.h = mp_rect_h(p->src_rc);
            p->sws->dst = p->dst_params;
            p->sws->dst.w = mp_rect_w(output_rc);
            p->sws->dst.h = mp_rect_h(output_rc);
            if (mp_sws_reinit(p->sws) < 0) return MPV_ERROR_UNSUPPORTED;
        }
        p->anything_changed = false;
    }
    struct mp_image dst = {0};
    mp_image_set_params(&dst, &p->dst_params);
    dst.planes[0] = target->luma;
    dst.planes[1] = target->chroma;
    dst.stride[0] = target->lumaStride;
    dst.stride[1] = target->chromaStride;
    struct mp_image *img = frame->current;
    if (img) {
        mp_image_clear_rc_inv(&dst, output_rc);
        struct mp_image source = *img, cropped = dst;
        struct mp_rect rc = p->src_rc;
        rc.x0 = MP_ALIGN_DOWN(rc.x0, source.fmt.align_x);
        rc.y0 = MP_ALIGN_DOWN(rc.y0, source.fmt.align_y);
        mp_image_crop_rc(&source, rc);
        mp_image_crop_rc(&cropped, output_rc);
        if (mp_sws_scale(p->sws, &cropped, &source) < 0) return MPV_ERROR_GENERIC;
    } else {
        mp_image_clear(&dst, 0, 0, dst.w, dst.h);
    }
    target->subtitlePixels = 0;
    if (p->osd) {
        if (!p->poc_subs) p->poc_subs = mp_draw_sub_alloc(p, ctx->global);
        struct sub_bitmap_list *list = osd_render(p->osd, p->osd_rc, img ? img->pts : 0, 0, mp_draw_sub_formats);
        if (!list->num_items) { talloc_free(list); return 0; }
        struct mp_rect active[32], modified_rc;
        int count = 0, modified = 0;
        struct mp_image *overlay = mp_draw_sub_overlay(p->poc_subs, list, active, 32, &count, &modified_rc, 1, &modified);
        if (overlay && count) {
            if (!p->poc_lut) { p->poc_lut = talloc(p, PoCHDRLUT); poc_init_lut(p->poc_lut); }
            // Chroma alignment can make formerly disjoint rectangles overlap.
            // Merge those before composition so no pixel is blended twice.
            for (int i = 0; i < count; i++) {
                active[i].x0 &= ~1; active[i].y0 &= ~1;
                active[i].x1 = (active[i].x1 + 1) & ~1;
                active[i].y1 = (active[i].y1 + 1) & ~1;
            }
            bool merged;
            do {
                merged = false;
                for (int i = 0; i < count && !merged; i++) for (int j = i + 1; j < count; j++) {
                    struct mp_rect *a = &active[i], *b = &active[j];
                    if (a->x0 < b->x1 && b->x0 < a->x1 && a->y0 < b->y1 && b->y0 < a->y1) {
                        a->x0 = MPMIN(a->x0, b->x0); a->y0 = MPMIN(a->y0, b->y0);
                        a->x1 = MPMAX(a->x1, b->x1); a->y1 = MPMAX(a->y1, b->y1);
                        active[j] = active[--count]; merged = true; break;
                    }
                }
            } while (merged);
            for (int i = 0; i < count; i++)
                poc_composite(p->poc_lut, target, dst.w, dst.h, overlay->planes[0], overlay->stride[0],
                              active[i].x0, active[i].y0, active[i].x1, active[i].y1);
        }
        talloc_free(list);
        if (!overlay) return MPV_ERROR_GENERIC;
    }
    return 0;
}
