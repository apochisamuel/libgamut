/* gamutmap.c - gamut mapping and boundary operations
 *
 * Operations for handling colors that fall outside the destination gamut.
 * The mapping module implements several strategies: clipping to the nearest
 * in-gamut color, chroma reduction, lightness compression, and a combined
 * perceptual mapping that preserves relative relationships.
 */
#include "internal.h"
#include <string.h>

/* Clamp a value to [0, 1]. */
static float gm_clamp01(float v)
{
    if (v < 0.0f) return 0.0f;
    if (v > 1.0f) return 1.0f;
    return v;
}

/* Simple per-channel clip. Each channel is independently clamped to [0, 1].
 * This is the fastest strategy but introduces hue shifts for out-of-gamut
 * colors. */
void gmt_gamut_clip(float *values, unsigned nch)
{
    unsigned i;
    for (i = 0; i < nch; i++)
        values[i] = gm_clamp01(values[i]);
}

/* Chroma-preserving clip in Lab space. The lightness is clamped to
 * [0, 100] and the chroma (a, b) is scaled proportionally if the result
 * would exceed a simple gamut boundary approximation. */
void gmt_gamut_chroma_clip(float lab[3])
{
    float L = lab[0];
    float a = lab[1];
    float b = lab[2];
    float c2 = a * a + b * b;
    float max_c2;

    /* Clamp lightness. */
    if (L < 0.0f) L = 0.0f;
    if (L > 100.0f) L = 100.0f;

    /* Approximate gamut boundary: max chroma decreases linearly from the
     * midpoint toward L=0 and L=100. */
    {
        float half = (L < 50.0f) ? L : 100.0f - L;
        float max_c = half * 2.5f;  /* rough sRGB-ish boundary */
        max_c2 = max_c * max_c;
    }

    if (c2 > max_c2 && c2 > 0.0f) {
        /* Newton sqrt of the ratio */
        float ratio = max_c2 / c2;
        float scale = ratio;
        int k;
        for (k = 0; k < 4; k++)
            scale = (scale + ratio / scale) * 0.5f;
        a *= scale;
        b *= scale;
    }
    lab[0] = L;
    lab[1] = a;
    lab[2] = b;
}

/* Lightness compression: compress the lightness range [0, 100] into
 * [min_L, max_L] using a sigmoid-like curve. Preserves shadow and
 * highlight detail at the cost of reduced overall contrast. */
void gmt_gamut_compress_L(float *L, float min_L, float max_L)
{
    float range = max_L - min_L;
    float t, s;
    if (range <= 0.0f) {
        *L = min_L;
        return;
    }
    t = *L / 100.0f;  /* normalize to [0, 1] */
    /* Sigmoid: s(t) = t^g / (t^g + (1-t)^g) with g = 1.5 */
    {
        float tg = t * t * t;       /* t^3 as proxy for t^1.5 squared */
        float og = (1.0f - t);
        float ogg = og * og * og;
        if (tg + ogg > 0.0f)
            s = tg / (tg + ogg);
        else
            s = 0.5f;
    }
    *L = min_L + s * range;
}

/* Perceptual gamut mapping: combined lightness compression and chroma
 * reduction. This is a simplified version of the strategy used by
 * perceptual rendering intent transforms. */
void gmt_gamut_perceptual(float lab[3], float target_L_min,
                          float target_L_max, float chroma_scale)
{
    gmt_gamut_compress_L(&lab[0], target_L_min, target_L_max);
    lab[1] *= chroma_scale;
    lab[2] *= chroma_scale;
    gmt_gamut_chroma_clip(lab);
}

/* Check whether a Lab color is within the approximate sRGB gamut. */
int gmt_gamut_in_srgb(const float lab[3])
{
    float L = lab[0];
    float c2 = lab[1] * lab[1] + lab[2] * lab[2];
    float half, max_c;
    if (L < 0.0f || L > 100.0f) return 0;
    half = (L < 50.0f) ? L : 100.0f - L;
    max_c = half * 2.5f;
    return (c2 <= max_c * max_c) ? 1 : 0;
}

/* Compute the gamut volume as a count of in-gamut test points over a
 * uniform grid in Lab space. */
float gmt_gamut_volume(const gmt_profile *p, unsigned grid_size)
{
    unsigned in_count = 0, total = 0;
    unsigned li, ai, bi;
    float in[GMT_MAX_CHANNELS], out[GMT_MAX_CHANNELS];
    unsigned nch = p ? p->channels : 0;

    if (!p || nch == 0 || nch > GMT_MAX_CHANNELS || grid_size < 2)
        return 0.0f;

    for (li = 0; li < grid_size; li++)
        for (ai = 0; ai < grid_size; ai++)
            for (bi = 0; bi < grid_size; bi++) {
                float L = (float)li / (float)(grid_size - 1) * 100.0f;
                float a = (float)ai / (float)(grid_size - 1) * 256.0f - 128.0f;
                float b = (float)bi / (float)(grid_size - 1) * 256.0f - 128.0f;
                unsigned c;
                int ok = 1;

                /* Map Lab -> device via inverse. */
                in[0] = L; in[1] = a; in[2] = b;
                for (c = 3; c < nch; c++) in[c] = 0.0f;
                if (gmt_apply_inverse(p, in, out) != GMT_OK)
                    continue;

                for (c = 0; c < nch; c++) {
                    if (out[c] < -0.001f || out[c] > 1.001f) {
                        ok = 0;
                        break;
                    }
                }
                if (ok) in_count++;
                total++;
            }

    return (total > 0) ? (float)in_count / (float)total : 0.0f;
}

/* Map an array of Lab colors through a perceptual intent simulation. */
void gmt_gamut_map_array(float *lab, size_t count,
                         float L_min, float L_max, float c_scale)
{
    size_t i;
    for (i = 0; i < count; i++)
        gmt_gamut_perceptual(lab + i * 3, L_min, L_max, c_scale);
}

/* Compute the maximum chroma at a given lightness over a set of Lab
 * samples. This approximates the gamut boundary at that lightness. */
float gmt_gamut_max_chroma_at_L(const float *lab, size_t count, float L,
                                float L_tol)
{
    float max_c2 = 0.0f;
    size_t i;
    for (i = 0; i < count; i++) {
        float dL = lab[i * 3] - L;
        float c2;
        if (dL < 0.0f) dL = -dL;
        if (dL > L_tol)
            continue;
        c2 = lab[i * 3 + 1] * lab[i * 3 + 1]
           + lab[i * 3 + 2] * lab[i * 3 + 2];
        if (c2 > max_c2)
            max_c2 = c2;
    }
    /* sqrt via Newton */
    if (max_c2 <= 0.0f) return 0.0f;
    {
        float y = max_c2;
        int k;
        for (k = 0; k < 8; k++)
            y = (y + max_c2 / y) * 0.5f;
        return y;
    }
}
