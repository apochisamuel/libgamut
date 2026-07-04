/* clutgen.c - CLUT generation utilities
 *
 * Generate multi-dimensional lookup tables for testing and profile
 * construction. Includes identity CLUTs, gradient fills, and custom
 * per-node generation.
 */
#include "internal.h"
#include <string.h>

/* Generate an identity CLUT: each node maps its grid coordinate directly
 * to the output. The input and output channel counts must be equal. */
int gmt_gen_identity_clut(float *table, uint8_t nch,
                          const uint8_t *grid, uint32_t point_count)
{
    uint32_t i;
    uint32_t nodes[GMT_MAX_CHANNELS];
    uint32_t strides[GMT_MAX_CHANNELS];
    unsigned d;

    if (nch == 0 || nch > GMT_MAX_CHANNELS)
        return -1;

    strides[0] = 1;
    for (d = 1; d < nch; d++)
        strides[d] = strides[d-1] * grid[d-1];

    for (i = 0; i < point_count; i++) {
        uint32_t rem = i;
        for (d = nch; d > 0; d--) {
            nodes[d-1] = rem / strides[d-1];
            rem %= strides[d-1];
        }
        for (d = 0; d < nch; d++) {
            float val = (grid[d] > 1) ?
                (float)nodes[d] / (float)(grid[d] - 1) : 0.0f;
            table[(size_t)i * nch + d] = val;
        }
    }
    return 0;
}

/* Generate a gradient CLUT: the output channels carry a linear gradient
 * from `lo` to `hi` along the first input dimension. */
int gmt_gen_gradient_clut(float *table, uint8_t in_ch, uint8_t out_ch,
                          const uint8_t *grid, uint32_t point_count,
                          float lo, float hi)
{
    uint32_t i;
    unsigned d;
    uint32_t stride0 = 1;

    if (in_ch == 0 || out_ch == 0 || in_ch > GMT_MAX_CHANNELS)
        return -1;

    for (i = 0; i < point_count; i++) {
        uint32_t node0 = (i / stride0) % grid[0];
        float t = (grid[0] > 1) ?
            (float)node0 / (float)(grid[0] - 1) : 0.0f;
        float val = lo + t * (hi - lo);
        for (d = 0; d < out_ch; d++)
            table[(size_t)i * out_ch + d] = val;
    }
    return 0;
}

/* Compute the total number of nodes from a grid specification. */
uint32_t gmt_grid_point_count(const uint8_t *grid, uint8_t nch)
{
    uint32_t product = 1;
    unsigned d;
    for (d = 0; d < nch; d++) {
        product *= grid[d];
        if (product > 262144)
            return 0;
    }
    return product;
}

/* Validate that a grid is uniform (all dimensions have the same count). */
int gmt_grid_is_uniform(const uint8_t *grid, uint8_t nch)
{
    unsigned d;
    if (nch < 2) return 1;
    for (d = 1; d < nch; d++)
        if (grid[d] != grid[0])
            return 0;
    return 1;
}

/* Apply a per-channel gain to a CLUT in place. */
void gmt_clut_apply_gain(float *table, uint32_t point_count,
                         uint8_t out_ch, const float *gains)
{
    uint32_t i;
    unsigned d;
    for (i = 0; i < point_count; i++)
        for (d = 0; d < out_ch; d++)
            table[(size_t)i * out_ch + d] *= gains[d];
}

/* Apply a per-channel offset to a CLUT in place. */
void gmt_clut_apply_offset(float *table, uint32_t point_count,
                           uint8_t out_ch, const float *offsets)
{
    uint32_t i;
    unsigned d;
    for (i = 0; i < point_count; i++)
        for (d = 0; d < out_ch; d++)
            table[(size_t)i * out_ch + d] += offsets[d];
}

/* Clamp all CLUT values to [0, 1]. */
void gmt_clut_clamp(float *table, uint32_t point_count, uint8_t out_ch)
{
    size_t total = (size_t)point_count * out_ch;
    size_t i;
    for (i = 0; i < total; i++) {
        if (table[i] < 0.0f) table[i] = 0.0f;
        if (table[i] > 1.0f) table[i] = 1.0f;
    }
}

/* Compute the range of values in each output channel. */
void gmt_clut_range(const float *table, uint32_t point_count,
                    uint8_t out_ch, float *mins, float *maxs)
{
    unsigned d;
    uint32_t i;
    for (d = 0; d < out_ch; d++) {
        mins[d] = 1.0f;
        maxs[d] = 0.0f;
    }
    for (i = 0; i < point_count; i++)
        for (d = 0; d < out_ch; d++) {
            float v = table[(size_t)i * out_ch + d];
            if (v < mins[d]) mins[d] = v;
            if (v > maxs[d]) maxs[d] = v;
        }
}

/* Compute the mean value in each output channel. */
void gmt_clut_mean(const float *table, uint32_t point_count,
                   uint8_t out_ch, float *means)
{
    unsigned d;
    uint32_t i;
    for (d = 0; d < out_ch; d++)
        means[d] = 0.0f;
    if (point_count == 0) return;
    for (i = 0; i < point_count; i++)
        for (d = 0; d < out_ch; d++)
            means[d] += table[(size_t)i * out_ch + d];
    for (d = 0; d < out_ch; d++)
        means[d] /= (float)point_count;
}

/* Check if a CLUT is close to an identity mapping within tolerance. */
int gmt_clut_is_identity(const float *table, uint8_t nch,
                         const uint8_t *grid, uint32_t point_count,
                         float eps)
{
    uint32_t i;
    uint32_t strides[GMT_MAX_CHANNELS];
    unsigned d;

    if (nch == 0 || nch > GMT_MAX_CHANNELS)
        return 0;

    strides[0] = 1;
    for (d = 1; d < nch; d++)
        strides[d] = strides[d-1] * grid[d-1];

    for (i = 0; i < point_count; i++) {
        uint32_t rem = i;
        for (d = nch; d > 0; d--) {
            float expected = (grid[d-1] > 1) ?
                (float)(rem / strides[d-1]) / (float)(grid[d-1] - 1) : 0.0f;
            float actual = table[(size_t)i * nch + (d-1)];
            float diff = actual - expected;
            if (diff < 0.0f) diff = -diff;
            if (diff > eps) return 0;
            rem %= strides[d-1];
        }
    }
    return 1;
}
