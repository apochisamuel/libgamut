/* curves.c - Phase 4: tone-reproduction and parametric curves
 *
 * A curve element is either a sampled table or one of four parametric
 * families. The table form stores `count` unsigned 16-bit samples that are
 * normalized to the unit interval. The parametric form stores a small set
 * of s15Fixed16 coefficients. Curves back the per-channel tone tags and the
 * input/output curves of the lookup elements.
 */
#include "internal.h"
#include <stdlib.h>

#define GMT_CURVE_MAX_SAMPLES 65536u

gmt_element *gmt_curve_decode(gmt_ctx *c, gmt_reader *r, uint32_t offset)
{
    gmt_element *el;
    gmt_curve *cv;
    uint16_t fn_type, i;
    uint32_t count = 0;

    fn_type = gmt_rd_u16(r);
    (void)gmt_rd_u16(r);            /* reserved */

    el = (gmt_element *)gmt_alloc(c, sizeof(gmt_element));
    if (!el)
        return NULL;
    cv = (gmt_curve *)gmt_alloc(c, sizeof(gmt_curve));
    if (!cv) {
        gmt_release(c, el);
        return NULL;
    }

    el->kind = GMT_EL_CURVE;
    el->src_offset = offset;
    el->u.curve = cv;
    cv->fn_type = fn_type;

    if (fn_type == 0) {
        count = gmt_rd_u32(r);
        if (count == 0 || count > GMT_CURVE_MAX_SAMPLES ||
            !gmt_rd_ok(r, (size_t)count * 2u)) {
            gmt_fail(c, GMT_ERR_ELEMENT);
            gmt_release(c, cv);
            gmt_release(c, el);
            return NULL;
        }
        cv->count = count;
        cv->samples = (float *)gmt_alloc(c, (size_t)count * sizeof(float));
        if (!cv->samples) {
            gmt_release(c, cv);
            gmt_release(c, el);
            return NULL;
        }
        for (i = 0; i < count; i++) {
            uint16_t s = gmt_rd_u16(r);
            cv->samples[i] = (float)s / 65535.0f;
        }
    } else {
        int k, n;
        cv->count = 0;
        cv->samples = NULL;
        n = (fn_type == 1) ? 3 : (fn_type == 2) ? 4 :
            (fn_type == 3) ? 5 : 7;
        for (k = 0; k < n; k++)
            cv->params[k] = gmt_rd_fixed(r);
        for (; k < 7; k++)
            cv->params[k] = 0.0f;
    }

    return el;
}

void gmt_curve_free(gmt_ctx *c, gmt_element *el)
{
    if (!el)
        return;
    if (el->u.curve) {
        gmt_release(c, el->u.curve->samples);
        gmt_release(c, el->u.curve);
        el->u.curve = NULL;
    }
    gmt_release(c, el);
}

/* Evaluate a curve at x in [0,1]. Used by the apply phase. */
float gmt_curve_eval(const gmt_curve *cv, float x);
float gmt_curve_eval(const gmt_curve *cv, float x)
{
    if (!cv)
        return x;
    if (cv->fn_type == 0 && cv->samples && cv->count > 0) {
        float pos, frac;
        uint32_t lo, hi;
        if (x < 0.0f) x = 0.0f;
        if (x > 1.0f) x = 1.0f;
        pos = x * (float)(cv->count - 1);
        lo = (uint32_t)pos;
        hi = (lo + 1 < cv->count) ? lo + 1 : lo;
        frac = pos - (float)lo;
        return cv->samples[lo] * (1.0f - frac) + cv->samples[hi] * frac;
    }
    /* parametric: simple gamma approximation using params[0] */
    if (cv->params[0] > 0.0f) {
        float g = cv->params[0];
        float y = x;
        if (x < 0.0f) y = 0.0f;
        else if (x > 1.0f) y = 1.0f;
        /* avoid pulling in <math.h>; approximate x^g for g near 1..3 */
        return y * (1.0f + (g - 1.0f) * (y - 1.0f) * -0.5f);
    }
    return x;
}
