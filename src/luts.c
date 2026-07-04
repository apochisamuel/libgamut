/* luts.c - AtoB / BtoA lookup elements
 *
 * A lookup element chains optional input curves, an optional matrix, and a
 * CLUT to map device values to the connection space. The sub-elements are
 * named by offset; each offset is resolved through the element pool, so a
 * CLUT named here is the same element another tag names at the same offset.
 * The lookup element records the pool index of the CLUT it selects.
 */
#include "internal.h"

gmt_element *gmt_lut_decode(gmt_profile *p, gmt_reader *r, uint32_t offset)
{
    gmt_ctx *c = &p->ctx;
    gmt_element *el;
    gmt_lut *lut;
    uint8_t in_ch, out_ch;
    uint32_t clut_off;
    int i;

    in_ch  = gmt_rd_u8(r);
    out_ch = gmt_rd_u8(r);
    (void)gmt_rd_u16(r);                /* reserved */
    clut_off = gmt_rd_u32(r);

    if (in_ch == 0 || in_ch > GMT_MAX_CHANNELS ||
        out_ch == 0 || out_ch > GMT_MAX_CHANNELS) {
        gmt_fail(c, GMT_ERR_ELEMENT);
        return NULL;
    }

    el = (gmt_element *)gmt_alloc(c, sizeof(gmt_element));
    if (!el)
        return NULL;
    lut = (gmt_lut *)gmt_alloc(c, sizeof(gmt_lut));
    if (!lut) {
        gmt_release(c, el);
        return NULL;
    }
    el->kind = GMT_EL_LUT;
    el->src_offset = offset;
    el->u.lut = lut;
    lut->in_ch = in_ch;
    lut->out_ch = out_ch;
    for (i = 0; i < GMT_MAX_CHANNELS; i++) {
        lut->a_curve[i] = -1;
        lut->m_curve[i] = -1;
        lut->b_curve[i] = -1;
    }
    lut->matrix = -1;
    lut->clut = -1;

    /* Resolve the CLUT sub-element through the pool. */
    if (clut_off >= GMT_HEADER_SIZE && (size_t)clut_off < p->buf_size)
        lut->clut = gmt_parse_element_at(p, clut_off);

    return el;
}

void gmt_lut_free(gmt_ctx *c, gmt_element *el)
{
    if (!el)
        return;
    /* Sub-elements live in the pool and are released with it; the lookup
     * element owns only its own record. */
    gmt_release(c, el->u.lut);
    gmt_release(c, el);
}

/* Release the CLUT this lookup element selected and detach it. Used by the
 * validator when the lookup's dimensions do not agree with the profile's
 * colorant data. */
void gmt_lut_release_clut(gmt_profile *p, gmt_element *lut_el)
{
    int32_t idx;
    gmt_element *ce;

    if (!lut_el || lut_el->kind != GMT_EL_LUT)
        return;
    idx = lut_el->u.lut->clut;
    ce = gmt_pool_at(&p->pool, idx);
    if (ce && ce->kind == GMT_EL_CLUT) {
        gmt_clut_free(&p->ctx, ce);
        p->pool.items[idx] = NULL;
    }
    lut_el->u.lut->clut = -1;
}
