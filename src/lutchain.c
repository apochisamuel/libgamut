/* lutchain.c - full AtoB/BtoA lookup chain assembly
 *
 * A complete AtoB or BtoA lookup runs up to five stages: A curves
 * (per-channel input linearization), a CLUT, M curves (connecting the CLUT
 * output to the matrix), a 3x3+offset matrix, and B curves (per-channel
 * output shaping). This module resolves all five stages from the tag body
 * and assembles them into the link cache.
 */
#include "internal.h"
#include <string.h>
#include <stdio.h>

/* Decode a set of per-channel curves embedded within a LUT body at the
 * reader's current position. Returns 0 on success. */
int gmt_decode_channel_curves(gmt_profile *p, gmt_reader *r,
                              uint8_t nch, int32_t *indices)
{
    unsigned i;
    for (i = 0; i < nch; i++) {
        uint32_t off = gmt_rd_u32(r);
        if (off >= GMT_HEADER_SIZE && (size_t)off < p->buf_size)
            indices[i] = gmt_parse_element_at(p, off);
        else
            indices[i] = -1;
    }
    return 0;
}

/* Resolve the full AtoB chain from an element. Returns 0 if a complete
 * chain was assembled (at least one stage), -1 otherwise. */
int gmt_resolve_lut_chain(gmt_profile *p, const gmt_lut *lut,
                          int32_t *out_stages, unsigned *out_count)
{
    unsigned count = 0;
    unsigned i;

    /* A curves: per-input-channel linearization. */
    for (i = 0; i < lut->in_ch && i < GMT_MAX_CHANNELS; i++) {
        if (lut->a_curve[i] >= 0) {
            gmt_element *el = gmt_pool_at(&p->pool, lut->a_curve[i]);
            if (el && el->kind == GMT_EL_CURVE)
                out_stages[count++] = lut->a_curve[i];
        }
    }

    /* CLUT. */
    if (lut->clut >= 0) {
        gmt_element *el = gmt_pool_at(&p->pool, lut->clut);
        if (el && el->kind == GMT_EL_CLUT)
            out_stages[count++] = lut->clut;
    }

    /* Matrix. */
    if (lut->matrix >= 0) {
        gmt_element *el = gmt_pool_at(&p->pool, lut->matrix);
        if (el && el->kind == GMT_EL_MATRIX)
            out_stages[count++] = lut->matrix;
    }

    /* M curves: connecting CLUT to matrix. */
    for (i = 0; i < lut->out_ch && i < GMT_MAX_CHANNELS; i++) {
        if (lut->m_curve[i] >= 0) {
            gmt_element *el = gmt_pool_at(&p->pool, lut->m_curve[i]);
            if (el && el->kind == GMT_EL_CURVE)
                out_stages[count++] = lut->m_curve[i];
        }
    }

    /* B curves: per-output-channel shaping. */
    for (i = 0; i < lut->out_ch && i < GMT_MAX_CHANNELS; i++) {
        if (lut->b_curve[i] >= 0) {
            gmt_element *el = gmt_pool_at(&p->pool, lut->b_curve[i]);
            if (el && el->kind == GMT_EL_CURVE)
                out_stages[count++] = lut->b_curve[i];
        }
    }

    *out_count = count;
    return (count > 0) ? 0 : -1;
}

/* Validate a LUT chain for channel consistency: A-curve count must equal
 * in_ch, CLUT input must equal in_ch, CLUT output must equal out_ch,
 * M/B curve count must equal out_ch. */
int gmt_validate_lut_chain(const gmt_profile *p, const gmt_lut *lut)
{
    unsigned a_count = 0, m_count = 0, b_count = 0;
    unsigned i;

    for (i = 0; i < GMT_MAX_CHANNELS; i++) {
        if (lut->a_curve[i] >= 0) a_count++;
        if (lut->m_curve[i] >= 0) m_count++;
        if (lut->b_curve[i] >= 0) b_count++;
    }

    if (a_count > 0 && a_count != lut->in_ch)
        return 0;

    if (lut->clut >= 0) {
        gmt_element *el = gmt_pool_at(
            (gmt_elem_pool *)&p->pool, lut->clut);
        if (el && el->kind == GMT_EL_CLUT) {
            if (el->u.clut->in_ch != lut->in_ch)
                return 0;
            if (el->u.clut->out_ch != lut->out_ch)
                return 0;
        }
    }

    if (m_count > 0 && m_count != lut->out_ch)
        return 0;
    if (b_count > 0 && b_count != lut->out_ch)
        return 0;

    return 1;
}

/* Compute the total table memory used by a LUT chain (curves + CLUT). */
size_t gmt_lut_chain_memory(const gmt_profile *p, const gmt_lut *lut)
{
    size_t total = 0;
    unsigned i;

    for (i = 0; i < GMT_MAX_CHANNELS; i++) {
        int32_t curves[3] = { lut->a_curve[i], lut->m_curve[i],
                              lut->b_curve[i] };
        int k;
        for (k = 0; k < 3; k++) {
            gmt_element *el;
            if (curves[k] < 0) continue;
            el = gmt_pool_at((gmt_elem_pool *)&p->pool, curves[k]);
            if (el && el->kind == GMT_EL_CURVE && el->u.curve->samples)
                total += (size_t)el->u.curve->count * sizeof(float);
        }
    }
    if (lut->clut >= 0) {
        gmt_element *el = gmt_pool_at(
            (gmt_elem_pool *)&p->pool, lut->clut);
        if (el && el->kind == GMT_EL_CLUT && el->u.clut->table)
            total += (size_t)el->u.clut->point_count
                   * el->u.clut->out_ch * sizeof(float);
    }
    if (lut->matrix >= 0)
        total += sizeof(gmt_matrix);

    return total;
}

/* Print a summary of a LUT chain. */
int gmt_format_lut_chain(const gmt_profile *p, const gmt_lut *lut,
                         char *buf, size_t cap)
{
    unsigned a = 0, m = 0, b = 0;
    unsigned i;
    int has_clut, has_matrix;

    (void)p;

    if (!lut || cap < 128)
        return -1;

    for (i = 0; i < GMT_MAX_CHANNELS; i++) {
        if (lut->a_curve[i] >= 0) a++;
        if (lut->m_curve[i] >= 0) m++;
        if (lut->b_curve[i] >= 0) b++;
    }
    has_clut = (lut->clut >= 0) ? 1 : 0;
    has_matrix = (lut->matrix >= 0) ? 1 : 0;

    snprintf(buf, cap, "%u->%u A=%u CLUT=%s Matrix=%s M=%u B=%u",
             lut->in_ch, lut->out_ch,
             a, has_clut ? "yes" : "no",
             has_matrix ? "yes" : "no", m, b);
    return 0;
}
