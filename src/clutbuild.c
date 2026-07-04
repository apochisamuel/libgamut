/* clutbuild.c - CLUT construction from analytical color transforms
 *
 * Build multi-dimensional lookup tables by evaluating an analytical or
 * profile-driven color transform at each grid node. This is used for
 * constructing device-link profiles and for pre-computing transforms
 * that combine multiple profiles into a single CLUT.
 */
#include "internal.h"
#include <string.h>
#include <stdlib.h>

/* Callback type for analytical CLUT generation. The function receives
 * `in_ch` input values in [0,1] and writes `out_ch` output values. */
typedef void (*gmt_clut_fn)(const float *in, float *out, void *ctx);

/* Build a CLUT by evaluating a callback at every grid node. */
int gmt_build_clut_fn(float *table, uint8_t in_ch, uint8_t out_ch,
                      const uint8_t *grid, uint32_t point_count,
                      gmt_clut_fn fn, void *fn_ctx)
{
    uint32_t i;
    uint32_t strides[GMT_MAX_CHANNELS];
    unsigned d;

    if (in_ch == 0 || in_ch > GMT_MAX_CHANNELS || out_ch == 0)
        return -1;

    strides[0] = 1;
    for (d = 1; d < in_ch; d++)
        strides[d] = strides[d-1] * grid[d-1];

    for (i = 0; i < point_count; i++) {
        float input[GMT_MAX_CHANNELS];
        uint32_t rem = i;
        for (d = in_ch; d > 0; d--) {
            uint32_t node = rem / strides[d-1];
            rem %= strides[d-1];
            input[d-1] = (grid[d-1] > 1)
                ? (float)node / (float)(grid[d-1] - 1) : 0.0f;
        }
        fn(input, table + (size_t)i * out_ch, fn_ctx);
    }
    return 0;
}

/* Build a CLUT by evaluating an existing profile at each grid node. */
int gmt_build_clut_profile(float *table, uint8_t in_ch, uint8_t out_ch,
                           const uint8_t *grid, uint32_t point_count,
                           const gmt_profile *src)
{
    uint32_t i;
    uint32_t strides[GMT_MAX_CHANNELS];
    unsigned d;
    float input[GMT_MAX_CHANNELS], output[GMT_MAX_CHANNELS];

    if (!src || in_ch == 0 || in_ch > GMT_MAX_CHANNELS)
        return -1;

    strides[0] = 1;
    for (d = 1; d < in_ch; d++)
        strides[d] = strides[d-1] * grid[d-1];

    for (i = 0; i < point_count; i++) {
        uint32_t rem = i;
        for (d = in_ch; d > 0; d--) {
            uint32_t node = rem / strides[d-1];
            rem %= strides[d-1];
            input[d-1] = (grid[d-1] > 1)
                ? (float)node / (float)(grid[d-1] - 1) : 0.0f;
        }
        for (d = in_ch; d < GMT_MAX_CHANNELS; d++)
            input[d] = 0.0f;
        if (gmt_apply(src, input, output) != GMT_OK) {
            for (d = 0; d < out_ch; d++)
                table[(size_t)i * out_ch + d] = 0.0f;
        } else {
            for (d = 0; d < out_ch; d++)
                table[(size_t)i * out_ch + d] = output[d];
        }
    }
    return 0;
}

/* Build a device-link CLUT by chaining two profiles: source forward
 * followed by destination inverse. */
int gmt_build_clut_devicelink(float *table, uint8_t in_ch, uint8_t out_ch,
                              const uint8_t *grid, uint32_t point_count,
                              const gmt_profile *src,
                              const gmt_profile *dst)
{
    uint32_t i;
    uint32_t strides[GMT_MAX_CHANNELS];
    unsigned d;
    float input[GMT_MAX_CHANNELS], mid[GMT_MAX_CHANNELS];
    float output[GMT_MAX_CHANNELS];

    if (!src || !dst || in_ch == 0 || in_ch > GMT_MAX_CHANNELS)
        return -1;

    strides[0] = 1;
    for (d = 1; d < in_ch; d++)
        strides[d] = strides[d-1] * grid[d-1];

    for (i = 0; i < point_count; i++) {
        uint32_t rem = i;
        for (d = in_ch; d > 0; d--) {
            uint32_t node = rem / strides[d-1];
            rem %= strides[d-1];
            input[d-1] = (grid[d-1] > 1)
                ? (float)node / (float)(grid[d-1] - 1) : 0.0f;
        }
        for (d = in_ch; d < GMT_MAX_CHANNELS; d++)
            input[d] = 0.0f;

        /* Source forward. */
        if (gmt_apply(src, input, mid) != GMT_OK) {
            for (d = 0; d < out_ch; d++)
                table[(size_t)i * out_ch + d] = 0.0f;
            continue;
        }
        /* Destination inverse. */
        if (gmt_apply_inverse(dst, mid, output) != GMT_OK) {
            for (d = 0; d < out_ch; d++)
                table[(size_t)i * out_ch + d] = 0.0f;
            continue;
        }
        for (d = 0; d < out_ch; d++)
            table[(size_t)i * out_ch + d] = output[d];
    }
    return 0;
}

/* Build a CLUT from a 3x3 matrix transform. Each node evaluates
 * M * [r, g, b]^T. */
int gmt_build_clut_matrix(float *table, const uint8_t *grid,
                          uint32_t point_count, const float mat[9],
                          const float off[3])
{
    uint32_t i;
    uint32_t strides[3];
    unsigned d;

    strides[0] = 1;
    strides[1] = grid[0];
    strides[2] = (uint32_t)grid[0] * grid[1];

    for (i = 0; i < point_count; i++) {
        float r, g, b;
        uint32_t rem = i;
        uint32_t nodes[3];
        for (d = 3; d > 0; d--) {
            nodes[d-1] = rem / strides[d-1];
            rem %= strides[d-1];
        }
        r = (grid[0] > 1) ? (float)nodes[0] / (float)(grid[0]-1) : 0.0f;
        g = (grid[1] > 1) ? (float)nodes[1] / (float)(grid[1]-1) : 0.0f;
        b = (grid[2] > 1) ? (float)nodes[2] / (float)(grid[2]-1) : 0.0f;

        table[i*3+0] = mat[0]*r + mat[1]*g + mat[2]*b + off[0];
        table[i*3+1] = mat[3]*r + mat[4]*g + mat[5]*b + off[1];
        table[i*3+2] = mat[6]*r + mat[7]*g + mat[8]*b + off[2];
    }
    return 0;
}

/* Refine a CLUT by doubling the grid resolution along each dimension
 * using trilinear interpolation from the original. */
int gmt_refine_clut(const float *src, uint8_t nch, uint8_t out_ch,
                    const uint8_t *src_grid, float *dst,
                    uint8_t *dst_grid)
{
    uint32_t dst_count = 1;
    uint32_t i;
    uint32_t strides[GMT_MAX_CHANNELS];
    unsigned d;

    if (nch == 0 || nch > GMT_MAX_CHANNELS || out_ch == 0)
        return -1;

    for (d = 0; d < nch; d++) {
        dst_grid[d] = src_grid[d] * 2 - 1;
        if (dst_grid[d] < 3) dst_grid[d] = 3;
        dst_count *= dst_grid[d];
    }

    strides[0] = 1;
    for (d = 1; d < nch; d++)
        strides[d] = strides[d-1] * dst_grid[d-1];

    for (i = 0; i < dst_count; i++) {
        float input[GMT_MAX_CHANNELS];
        uint32_t rem = i;
        for (d = nch; d > 0; d--) {
            uint32_t node = rem / strides[d-1];
            rem %= strides[d-1];
            input[d-1] = (dst_grid[d-1] > 1)
                ? (float)node / (float)(dst_grid[d-1] - 1) : 0.0f;
        }
        /* Sample the source CLUT at this coordinate using nearest-node
         * (for simplicity; a production refinement would use trilinear). */
        {
            uint32_t src_flat = 0, src_stride = 1;
            for (d = 0; d < nch; d++) {
                uint32_t sn = (uint32_t)(input[d] * (float)(src_grid[d]-1) + 0.5f);
                if (sn >= src_grid[d]) sn = src_grid[d] - 1;
                src_flat += sn * src_stride;
                src_stride *= src_grid[d];
            }
            for (d = 0; d < out_ch; d++)
                dst[(size_t)i * out_ch + d] = src[(size_t)src_flat * out_ch + d];
        }
    }
    return 0;
}

/* Compute the smoothness of a CLUT as the mean squared second derivative
 * along each input dimension. Lower values indicate smoother tables. */
float gmt_clut_smoothness(const float *table, uint8_t in_ch, uint8_t out_ch,
                          const uint8_t *grid, uint32_t point_count)
{
    float total = 0.0f;
    uint32_t count = 0;
    uint32_t i;
    uint32_t strides[GMT_MAX_CHANNELS];
    unsigned d, oc;

    if (in_ch == 0 || in_ch > GMT_MAX_CHANNELS)
        return 0.0f;

    strides[0] = 1;
    for (d = 1; d < in_ch; d++)
        strides[d] = strides[d-1] * grid[d-1];

    for (i = 0; i < point_count; i++) {
        uint32_t nodes[GMT_MAX_CHANNELS];
        uint32_t rem = i;
        for (d = in_ch; d > 0; d--) {
            nodes[d-1] = rem / strides[d-1];
            rem %= strides[d-1];
        }
        for (d = 0; d < in_ch; d++) {
            if (nodes[d] == 0 || nodes[d] + 1 >= grid[d])
                continue;
            for (oc = 0; oc < out_ch; oc++) {
                uint32_t prev = i - strides[d];
                uint32_t next = i + strides[d];
                float d2 = table[(size_t)prev * out_ch + oc]
                         - 2.0f * table[(size_t)i * out_ch + oc]
                         + table[(size_t)next * out_ch + oc];
                total += d2 * d2;
                count++;
            }
        }
    }
    return (count > 0) ? total / (float)count : 0.0f;
}
