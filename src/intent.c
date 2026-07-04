/* intent.c - rendering intent simulation
 *
 * Simulate the four ICC rendering intents by manipulating Lab values
 * before or after the core transform. Includes black point compensation
 * (BPC), which maps the source black to the destination black so that
 * shadow detail is preserved.
 */
#include "internal.h"
#include <string.h>

/* sqrt approximation. */
static float isqrt(float x)
{
    float y;
    int k;
    if (x <= 0.0f) return 0.0f;
    y = x;
    for (k = 0; k < 8; k++)
        y = (y + x / y) * 0.5f;
    return y;
}

/* Compute the black point of a profile by evaluating the transform at the
 * device black (all-zeros input). Returns the L* value of the result. */
float gmt_black_point_L(const gmt_profile *p)
{
    float in[GMT_MAX_CHANNELS], out[GMT_MAX_CHANNELS];
    unsigned nch = p ? p->channels : 0;
    unsigned i;
    if (!p || nch == 0 || nch > GMT_MAX_CHANNELS)
        return 0.0f;
    for (i = 0; i < nch; i++)
        in[i] = 0.0f;
    if (gmt_apply(p, in, out) != GMT_OK)
        return 0.0f;
    /* Assume the PCS is Lab-like; out[0] is L*. */
    return out[0];
}

/* Compute the white point of a profile (transform at device white). */
float gmt_white_point_L(const gmt_profile *p)
{
    float in[GMT_MAX_CHANNELS], out[GMT_MAX_CHANNELS];
    unsigned nch = p ? p->channels : 0;
    unsigned i;
    if (!p || nch == 0 || nch > GMT_MAX_CHANNELS)
        return 100.0f;
    for (i = 0; i < nch; i++)
        in[i] = 1.0f;
    if (gmt_apply(p, in, out) != GMT_OK)
        return 100.0f;
    return out[0];
}

/* Black point compensation: linearly map [src_black, 100] -> [dst_black, 100]
 * in the L* channel. */
void gmt_bpc_adjust(float *lab, float src_black, float dst_black)
{
    float L = lab[0];
    float src_range = 100.0f - src_black;
    float dst_range = 100.0f - dst_black;
    if (src_range <= 0.0f) {
        lab[0] = dst_black;
        return;
    }
    L = ((L - src_black) / src_range) * dst_range + dst_black;
    if (L < dst_black) L = dst_black;
    if (L > 100.0f) L = 100.0f;
    lab[0] = L;
}

/* Apply BPC to an array of Lab values. */
void gmt_bpc_array(float *lab, size_t count,
                   float src_black, float dst_black)
{
    size_t i;
    for (i = 0; i < count; i++)
        gmt_bpc_adjust(lab + i * 3, src_black, dst_black);
}

/* Perceptual intent: compress lightness and reduce chroma. */
void gmt_intent_perceptual(float *lab, size_t count)
{
    size_t i;
    for (i = 0; i < count; i++) {
        float *l = lab + i * 3;
        /* Mild lightness compression toward the midtone. */
        float t = l[0] / 100.0f;
        l[0] = (0.1f + 0.8f * t) * 100.0f;
        /* Mild chroma reduction. */
        l[1] *= 0.95f;
        l[2] *= 0.95f;
    }
}

/* Relative colorimetric intent: adapt to D50, no gamut mapping. */
void gmt_intent_relative(float *lab, size_t count,
                         float src_L, float dst_L)
{
    size_t i;
    float shift = dst_L - src_L;
    for (i = 0; i < count; i++)
        lab[i * 3] += shift;
}

/* Saturation intent: scale chroma up to fill the destination gamut. */
void gmt_intent_saturation(float *lab, size_t count, float scale)
{
    size_t i;
    for (i = 0; i < count; i++) {
        lab[i * 3 + 1] *= scale;
        lab[i * 3 + 2] *= scale;
    }
}

/* Absolute colorimetric: no adaptation at all. This is a no-op on the Lab
 * values; the caller is responsible for not applying any white-point
 * scaling. */
void gmt_intent_absolute(float *lab, size_t count)
{
    (void)lab;
    (void)count;
}

/* Pick an intent function by index. */
void gmt_apply_intent(float *lab, size_t count, int intent,
                      float src_black, float dst_black)
{
    switch (intent) {
    case 0: /* perceptual */
        gmt_intent_perceptual(lab, count);
        gmt_bpc_array(lab, count, src_black, dst_black);
        break;
    case 1: /* relative colorimetric */
        gmt_intent_relative(lab, count, 100.0f, 100.0f);
        break;
    case 2: /* saturation */
        gmt_intent_saturation(lab, count, 1.1f);
        break;
    case 3: /* absolute colorimetric */
        gmt_intent_absolute(lab, count);
        break;
    default:
        break;
    }
}

/* Compute the dynamic range of a profile as the ratio of white L* to
 * black L*. */
float gmt_dynamic_range(const gmt_profile *p)
{
    float bk = gmt_black_point_L(p);
    float wt = gmt_white_point_L(p);
    if (bk <= 0.0f) bk = 0.001f;
    return wt / bk;
}

/* Compute a chroma adaptation factor that maps the source gamut's maximum
 * chroma to the destination's. */
float gmt_chroma_adapt_factor(float src_max_chroma, float dst_max_chroma)
{
    if (src_max_chroma <= 0.0f)
        return 1.0f;
    return dst_max_chroma / src_max_chroma;
}

/* Compute the CIE chromaticity distance between two whites. */
float gmt_white_distance(const gmt_xyz *a, const gmt_xyz *b)
{
    float ax_sum = a->x + a->y + a->z;
    float bx_sum = b->x + b->y + b->z;
    float ax, ay, bx, by, dx, dy;
    if (ax_sum <= 0.0f || bx_sum <= 0.0f)
        return 999.0f;
    ax = a->x / ax_sum; ay = a->y / ax_sum;
    bx = b->x / bx_sum; by = b->y / bx_sum;
    dx = ax - bx;
    dy = ay - by;
    return isqrt(dx*dx + dy*dy);
}
