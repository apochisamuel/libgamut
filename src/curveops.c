/* curveops.c - curve analysis and manipulation
 *
 * Operations on sampled tone curves beyond simple evaluation: derivative
 * estimation, smoothness metrics, inflection point detection, RMS error
 * computation, and curve fitting quality assessment.
 */
#include "internal.h"
#include <string.h>

/* Compute the first derivative of a sampled curve using central
 * differences. Output has `count` values (forward/backward at ends). */
void gmt_curve_derivative(const float *samples, uint32_t count, float *deriv)
{
    uint32_t i;
    float dx;
    if (count < 2) return;
    dx = 1.0f / (float)(count - 1);
    /* Forward difference at the start. */
    deriv[0] = (samples[1] - samples[0]) / dx;
    /* Central differences in the interior. */
    for (i = 1; i + 1 < count; i++)
        deriv[i] = (samples[i+1] - samples[i-1]) / (2.0f * dx);
    /* Backward difference at the end. */
    deriv[count-1] = (samples[count-1] - samples[count-2]) / dx;
}

/* Compute the second derivative using the central difference of the
 * first derivative. */
void gmt_curve_second_deriv(const float *samples, uint32_t count,
                            float *d2)
{
    float *d1;
    float dx;
    uint32_t i;
    if (count < 3) {
        if (count > 0) d2[0] = 0.0f;
        if (count > 1) d2[1] = 0.0f;
        return;
    }
    d1 = (float *)gmt_alloc(NULL, (size_t)count * sizeof(float));
    if (!d1) { memset(d2, 0, (size_t)count * sizeof(float)); return; }
    gmt_curve_derivative(samples, count, d1);
    dx = 1.0f / (float)(count - 1);
    d2[0] = (d1[1] - d1[0]) / dx;
    for (i = 1; i + 1 < count; i++)
        d2[i] = (d1[i+1] - d1[i-1]) / (2.0f * dx);
    d2[count-1] = (d1[count-1] - d1[count-2]) / dx;
    gmt_release(NULL, d1);
}

/* Count inflection points (sign changes in the second derivative). */
unsigned gmt_curve_inflections(const float *samples, uint32_t count)
{
    unsigned inf_count = 0;
    uint32_t i;
    if (count < 4) return 0;
    for (i = 2; i + 1 < count; i++) {
        /* Approximate d2 at i-1 and i. */
        float d2_prev = samples[i-2] - 2.0f * samples[i-1] + samples[i];
        float d2_cur  = samples[i-1] - 2.0f * samples[i]   + samples[i+1];
        if ((d2_prev > 0.0f && d2_cur < 0.0f) ||
            (d2_prev < 0.0f && d2_cur > 0.0f))
            inf_count++;
    }
    return inf_count;
}

/* Compute the RMS error between two sampled curves of the same length. */
float gmt_curve_rms_error(const float *a, const float *b, uint32_t count)
{
    float sum = 0.0f;
    uint32_t i;
    float y;
    int k;
    if (count == 0) return 0.0f;
    for (i = 0; i < count; i++) {
        float d = a[i] - b[i];
        sum += d * d;
    }
    sum /= (float)count;
    /* sqrt via Newton */
    if (sum <= 0.0f) return 0.0f;
    y = sum;
    for (k = 0; k < 8; k++)
        y = (y + sum / y) * 0.5f;
    return y;
}

/* Compute the maximum absolute error between two curves. */
float gmt_curve_max_error(const float *a, const float *b, uint32_t count)
{
    float max_d = 0.0f;
    uint32_t i;
    for (i = 0; i < count; i++) {
        float d = a[i] - b[i];
        if (d < 0.0f) d = -d;
        if (d > max_d) max_d = d;
    }
    return max_d;
}

/* Measure smoothness as the mean absolute second derivative. Lower values
 * indicate smoother curves. */
float gmt_curve_smoothness(const float *samples, uint32_t count)
{
    float sum = 0.0f;
    uint32_t i;
    if (count < 3) return 0.0f;
    for (i = 1; i + 1 < count; i++) {
        float d2 = samples[i-1] - 2.0f * samples[i] + samples[i+1];
        if (d2 < 0.0f) d2 = -d2;
        sum += d2;
    }
    return sum / (float)(count - 2);
}

/* Check whether a curve is convex (non-negative second derivative). */
int gmt_curve_is_convex(const float *samples, uint32_t count)
{
    uint32_t i;
    if (count < 3) return 1;
    for (i = 1; i + 1 < count; i++) {
        float d2 = samples[i-1] - 2.0f * samples[i] + samples[i+1];
        if (d2 < -1e-6f) return 0;
    }
    return 1;
}

/* Check whether a curve is concave (non-positive second derivative). */
int gmt_curve_is_concave(const float *samples, uint32_t count)
{
    uint32_t i;
    if (count < 3) return 1;
    for (i = 1; i + 1 < count; i++) {
        float d2 = samples[i-1] - 2.0f * samples[i] + samples[i+1];
        if (d2 > 1e-6f) return 0;
    }
    return 1;
}

/* Compute the integral of |curve - identity| (how far from linear). */
float gmt_curve_nonlinearity(const float *samples, uint32_t count)
{
    float sum = 0.0f;
    uint32_t i;
    if (count < 2) return 0.0f;
    for (i = 0; i < count; i++) {
        float x = (float)i / (float)(count - 1);
        float d = samples[i] - x;
        if (d < 0.0f) d = -d;
        sum += d;
    }
    return sum / (float)count;
}

/* Smooth a sampled curve using a simple 3-point moving average. */
void gmt_curve_smooth(float *samples, uint32_t count, unsigned passes)
{
    unsigned p;
    uint32_t i;
    float prev, cur, next;
    for (p = 0; p < passes; p++) {
        prev = samples[0];
        for (i = 1; i + 1 < count; i++) {
            cur = samples[i];
            next = samples[i + 1];
            samples[i] = (prev + cur + next) / 3.0f;
            prev = cur;
        }
    }
}

/* Fit a gamma exponent to a sampled curve by least squares. Returns the
 * gamma value that minimizes the RMS error. Uses a simple grid search. */
float gmt_curve_fit_gamma(const float *samples, uint32_t count)
{
    float best_g = 1.0f, best_err = 1e30f;
    int gi;
    if (count < 4) return 1.0f;
    /* Search g from 0.1 to 5.0 in steps of 0.01. */
    for (gi = 10; gi <= 500; gi++) {
        float g = (float)gi * 0.01f;
        float err = 0.0f;
        uint32_t i;
        for (i = 0; i < count; i++) {
            float x = (float)i / (float)(count - 1);
            /* Approximate x^g using the same method as curvegen. */
            float expected = x;
            if (x > 0.0f && x < 1.0f) {
                float u = 1.0f - x;
                float ln_x = -(u + u*u*0.5f + u*u*u/3.0f);
                float t = g * ln_x;
                float term = 1.0f, sum = 1.0f;
                int k;
                for (k = 1; k <= 10; k++) {
                    term *= t / (float)k;
                    sum += term;
                }
                expected = sum;
                if (expected < 0.0f) expected = 0.0f;
                if (expected > 1.0f) expected = 1.0f;
            }
            {
                float d = samples[i] - expected;
                err += d * d;
            }
        }
        err /= (float)count;
        if (err < best_err) {
            best_err = err;
            best_g = g;
        }
    }
    return best_g;
}

/* Compute the effective gamma at the midpoint (x=0.5). Returns
 * ln(curve(0.5)) / ln(0.5), or 1.0 if the midpoint value is out of range. */
float gmt_curve_midpoint_gamma(const float *samples, uint32_t count)
{
    float mid_val;
    float pos;
    uint32_t lo, hi;
    float frac;
    if (count < 2) return 1.0f;
    pos = 0.5f * (float)(count - 1);
    lo = (uint32_t)pos;
    hi = lo + 1;
    if (hi >= count) { lo = count - 2; hi = count - 1; }
    frac = pos - (float)lo;
    mid_val = samples[lo] * (1.0f - frac) + samples[hi] * frac;
    /* ln(mid_val) / ln(0.5) = -ln(mid_val) / ln(2) */
    if (mid_val <= 0.0f || mid_val >= 1.0f)
        return 1.0f;
    {
        /* Approximate ln using series. */
        float u = 1.0f - mid_val;
        float ln_mid = -(u + u*u*0.5f + u*u*u/3.0f + u*u*u*u*0.25f);
        float ln_half = -0.6931f;
        if (ln_half == 0.0f) return 1.0f;
        return ln_mid / ln_half;
    }
}
