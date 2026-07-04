/* curvegen.c - curve generation utilities
 *
 * Generate standard tone-reproduction curves for testing and profile
 * construction. Includes sRGB, power-law gamma, linear identity, and
 * custom table generation.
 */
#include "internal.h"
#include <string.h>

/* Generate a linear identity curve table with `count` samples. */
void gmt_gen_identity(float *samples, uint32_t count)
{
    uint32_t i;
    if (count < 2) return;
    for (i = 0; i < count; i++)
        samples[i] = (float)i / (float)(count - 1);
}

/* Generate a power-law gamma curve. Approximation without <math.h>:
 * x^g ≈ exp(g * ln(x)) using a polynomial approximation of ln/exp. */
static float approx_pow(float x, float g)
{
    float ln_x, result;
    int k;
    if (x <= 0.0f) return 0.0f;
    if (x >= 1.0f) return 1.0f;
    /* ln(x) for x in (0,1]: use the series ln(1-u) ≈ -u - u^2/2 - u^3/3
     * where u = 1 - x */
    {
        float u = 1.0f - x;
        float u2 = u * u;
        float u3 = u2 * u;
        float u4 = u3 * u;
        ln_x = -(u + u2 * 0.5f + u3 / 3.0f + u4 * 0.25f);
    }
    /* exp(g * ln_x) via Taylor series */
    {
        float t = g * ln_x;
        float term = 1.0f;
        result = 1.0f;
        for (k = 1; k <= 12; k++) {
            term *= t / (float)k;
            result += term;
        }
    }
    if (result < 0.0f) result = 0.0f;
    if (result > 1.0f) result = 1.0f;
    return result;
}

void gmt_gen_gamma(float *samples, uint32_t count, float gamma)
{
    uint32_t i;
    if (count < 2) return;
    for (i = 0; i < count; i++) {
        float x = (float)i / (float)(count - 1);
        samples[i] = approx_pow(x, gamma);
    }
}

/* Generate the sRGB forward transfer function (linear → sRGB). */
void gmt_gen_srgb(float *samples, uint32_t count)
{
    uint32_t i;
    if (count < 2) return;
    for (i = 0; i < count; i++) {
        float x = (float)i / (float)(count - 1);
        if (x <= 0.0031308f)
            samples[i] = 12.92f * x;
        else
            samples[i] = 1.055f * approx_pow(x, 1.0f / 2.4f) - 0.055f;
        if (samples[i] < 0.0f) samples[i] = 0.0f;
        if (samples[i] > 1.0f) samples[i] = 1.0f;
    }
}

/* Generate the inverse sRGB transfer function (sRGB → linear). */
void gmt_gen_srgb_inv(float *samples, uint32_t count)
{
    uint32_t i;
    if (count < 2) return;
    for (i = 0; i < count; i++) {
        float x = (float)i / (float)(count - 1);
        if (x <= 0.04045f)
            samples[i] = x / 12.92f;
        else
            samples[i] = approx_pow((x + 0.055f) / 1.055f, 2.4f);
        if (samples[i] < 0.0f) samples[i] = 0.0f;
        if (samples[i] > 1.0f) samples[i] = 1.0f;
    }
}

/* Generate the L* curve (CIE lightness). */
void gmt_gen_Lstar(float *samples, uint32_t count)
{
    uint32_t i;
    if (count < 2) return;
    for (i = 0; i < count; i++) {
        float Y = (float)i / (float)(count - 1);
        float fY;
        if (Y > 0.008856f) {
            fY = approx_pow(Y, 1.0f / 3.0f);
        } else {
            fY = 7.787f * Y + 16.0f / 116.0f;
        }
        samples[i] = (116.0f * fY - 16.0f) / 100.0f;
        if (samples[i] < 0.0f) samples[i] = 0.0f;
        if (samples[i] > 1.0f) samples[i] = 1.0f;
    }
}

/* Invert a monotonically increasing curve table. Output has `out_count`
 * entries uniformly spaced in y, mapped to x values. */
void gmt_gen_invert_table(const float *fwd, uint32_t fwd_count,
                          float *inv, uint32_t inv_count)
{
    uint32_t oi;
    if (fwd_count < 2 || inv_count < 2) return;
    for (oi = 0; oi < inv_count; oi++) {
        float y = (float)oi / (float)(inv_count - 1);
        /* Binary search for x such that fwd(x) ≈ y. */
        uint32_t lo = 0, hi = fwd_count - 1;
        while (hi - lo > 1) {
            uint32_t mid = (lo + hi) / 2;
            if (fwd[mid] < y)
                lo = mid;
            else
                hi = mid;
        }
        /* Linear interpolation within [lo, hi]. */
        if (fwd[hi] != fwd[lo]) {
            float t = (y - fwd[lo]) / (fwd[hi] - fwd[lo]);
            inv[oi] = ((float)lo + t) / (float)(fwd_count - 1);
        } else {
            inv[oi] = (float)lo / (float)(fwd_count - 1);
        }
    }
}

/* Compose two curve tables: out[i] = b(a(i / (count-1))). */
void gmt_gen_compose(const float *a, uint32_t a_count,
                     const float *b, uint32_t b_count,
                     float *out, uint32_t out_count)
{
    uint32_t i;
    if (a_count < 2 || b_count < 2 || out_count < 2) return;
    for (i = 0; i < out_count; i++) {
        float x = (float)i / (float)(out_count - 1);
        float ya, yb;
        /* Evaluate a(x) */
        {
            float pos = x * (float)(a_count - 1);
            uint32_t lo = (uint32_t)pos;
            uint32_t hi = lo + 1;
            float frac;
            if (hi >= a_count) { lo = a_count - 2; hi = a_count - 1; }
            frac = pos - (float)lo;
            ya = a[lo] * (1.0f - frac) + a[hi] * frac;
        }
        /* Evaluate b(ya) */
        {
            float pos = ya * (float)(b_count - 1);
            uint32_t lo = (uint32_t)pos;
            uint32_t hi = lo + 1;
            float frac;
            if (pos < 0.0f) pos = 0.0f;
            lo = (uint32_t)pos;
            if (hi >= b_count) { lo = b_count - 2; hi = b_count - 1; }
            frac = pos - (float)lo;
            yb = b[lo] * (1.0f - frac) + b[hi] * frac;
        }
        out[i] = yb;
    }
}

/* Resample a curve table to a different size using linear interpolation. */
void gmt_gen_resample(const float *src, uint32_t src_count,
                      float *dst, uint32_t dst_count)
{
    uint32_t i;
    if (src_count < 2 || dst_count < 2) return;
    for (i = 0; i < dst_count; i++) {
        float x = (float)i / (float)(dst_count - 1);
        float pos = x * (float)(src_count - 1);
        uint32_t lo = (uint32_t)pos;
        uint32_t hi = lo + 1;
        float frac;
        if (hi >= src_count) { lo = src_count - 2; hi = src_count - 1; }
        frac = pos - (float)lo;
        dst[i] = src[lo] * (1.0f - frac) + src[hi] * frac;
    }
}

/* Generate a posterization curve: step function with `levels` steps. */
void gmt_gen_posterize(float *samples, uint32_t count, unsigned levels)
{
    uint32_t i;
    float step;
    if (count < 2 || levels < 2) {
        gmt_gen_identity(samples, count);
        return;
    }
    step = 1.0f / (float)(levels - 1);
    for (i = 0; i < count; i++) {
        float x = (float)i / (float)(count - 1);
        unsigned bin = (unsigned)(x * (float)(levels - 1) + 0.5f);
        if (bin >= levels) bin = levels - 1;
        samples[i] = (float)bin * step;
    }
}
