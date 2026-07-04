/* clutops.c - CLUT extraction and manipulation operations
 *
 * Operations on parsed CLUTs: extract sub-tables by fixing one or more
 * input dimensions, project a multi-channel output down to fewer channels,
 * compute per-node gradients, and build difference tables between two
 * CLUTs of the same geometry.
 */
#include "internal.h"
#include <string.h>

/* Compute flat index from multi-dimensional node indices. */
static uint32_t node_flat(const gmt_clut *cl, const uint32_t *n)
{
    uint32_t idx = 0, stride = 1;
    unsigned d;
    for (d = 0; d < cl->in_ch; d++) {
        idx += n[d] * stride;
        stride *= cl->grid[d];
    }
    return idx;
}

/* Decompose a flat index back into per-dimension node indices. */
static void flat_to_nodes(const gmt_clut *cl, uint32_t flat, uint32_t *nodes)
{
    unsigned d;
    uint32_t rem = flat;
    for (d = 0; d < cl->in_ch; d++) {
        nodes[d] = rem % cl->grid[d];
        rem /= cl->grid[d];
    }
}

/* Extract a 1D slice of a 3D CLUT by fixing two of the three input
 * dimensions. `vary_dim` is the dimension that varies; the other two are
 * fixed at `fixed_vals[0]` and `fixed_vals[1]`. Output is written into
 * `slice`, which must hold grid[vary_dim] * out_ch floats. */
int gmt_clut_extract_1d(const gmt_clut *cl, unsigned vary_dim,
                        const uint32_t fixed_vals[2], float *slice)
{
    uint32_t nodes[3];
    uint32_t gi, fi;
    unsigned d;
    unsigned fix_idx = 0;

    if (cl->in_ch != 3 || vary_dim >= 3)
        return -1;

    for (d = 0; d < 3; d++) {
        if (d == vary_dim)
            continue;
        nodes[d] = fixed_vals[fix_idx++];
        if (nodes[d] >= cl->grid[d])
            nodes[d] = cl->grid[d] - 1;
    }

    for (gi = 0; gi < cl->grid[vary_dim]; gi++) {
        nodes[vary_dim] = gi;
        fi = node_flat(cl, nodes);
        for (d = 0; d < cl->out_ch; d++)
            slice[(size_t)gi * cl->out_ch + d] =
                cl->table[(size_t)fi * cl->out_ch + d];
    }
    return 0;
}

/* Extract a 2D slice by fixing one dimension. Output holds
 * grid[dim_a] * grid[dim_b] * out_ch floats. */
int gmt_clut_extract_2d(const gmt_clut *cl, unsigned fixed_dim,
                        uint32_t fixed_val, unsigned dim_a, unsigned dim_b,
                        float *slice)
{
    uint32_t nodes[3];
    uint32_t ai, bi, fi;
    unsigned d;
    size_t pos = 0;

    if (cl->in_ch != 3 || fixed_dim >= 3 || dim_a >= 3 || dim_b >= 3)
        return -1;
    if (fixed_val >= cl->grid[fixed_dim])
        fixed_val = cl->grid[fixed_dim] - 1;
    nodes[fixed_dim] = fixed_val;

    for (ai = 0; ai < cl->grid[dim_a]; ai++) {
        nodes[dim_a] = ai;
        for (bi = 0; bi < cl->grid[dim_b]; bi++) {
            nodes[dim_b] = bi;
            fi = node_flat(cl, nodes);
            for (d = 0; d < cl->out_ch; d++)
                slice[pos++] = cl->table[(size_t)fi * cl->out_ch + d];
        }
    }
    return 0;
}

/* Project a CLUT's output channels down to a single channel. Writes
 * point_count floats into `out`. */
int gmt_clut_project(const gmt_clut *cl, unsigned channel, float *out)
{
    uint32_t i;
    if (channel >= cl->out_ch)
        return -1;
    for (i = 0; i < cl->point_count; i++)
        out[i] = cl->table[(size_t)i * cl->out_ch + channel];
    return 0;
}

/* Compute the forward difference (gradient) along one input dimension for
 * one output channel. Output holds (grid[dim]-1) * product(other grids)
 * floats. Returns number of values written. */
uint32_t gmt_clut_gradient(const gmt_clut *cl, unsigned dim,
                           unsigned channel, float *out)
{
    uint32_t i, count = 0;
    uint32_t nodes[GMT_MAX_CHANNELS];
    uint32_t stride = 1;
    unsigned d;

    if (dim >= cl->in_ch || channel >= cl->out_ch)
        return 0;
    for (d = 0; d < dim; d++)
        stride *= cl->grid[d];

    for (i = 0; i < cl->point_count; i++) {
        flat_to_nodes(cl, i, nodes);
        if (nodes[dim] + 1 >= cl->grid[dim])
            continue;
        {
            uint32_t next_flat = i + stride;
            float cur = cl->table[(size_t)i * cl->out_ch + channel];
            float nxt = cl->table[(size_t)next_flat * cl->out_ch + channel];
            out[count++] = nxt - cur;
        }
    }
    return count;
}

/* Compute the element-wise difference between two CLUTs of the same
 * geometry. Output holds point_count * out_ch floats. */
int gmt_clut_diff(const gmt_clut *a, const gmt_clut *b, float *out)
{
    size_t total, i;
    if (a->in_ch != b->in_ch || a->out_ch != b->out_ch)
        return -1;
    if (a->point_count != b->point_count)
        return -1;
    total = (size_t)a->point_count * a->out_ch;
    for (i = 0; i < total; i++)
        out[i] = a->table[i] - b->table[i];
    return 0;
}

/* Compute the maximum absolute difference between two CLUTs. */
float gmt_clut_max_diff(const gmt_clut *a, const gmt_clut *b)
{
    float max_d = 0.0f;
    size_t total, i;
    if (a->in_ch != b->in_ch || a->out_ch != b->out_ch ||
        a->point_count != b->point_count)
        return 999.0f;
    total = (size_t)a->point_count * a->out_ch;
    for (i = 0; i < total; i++) {
        float d = a->table[i] - b->table[i];
        if (d < 0.0f) d = -d;
        if (d > max_d) max_d = d;
    }
    return max_d;
}

/* Blend two CLUTs: out = a * (1-t) + b * t. */
int gmt_clut_blend(const gmt_clut *a, const gmt_clut *b,
                   float t, float *out)
{
    size_t total, i;
    if (a->in_ch != b->in_ch || a->out_ch != b->out_ch ||
        a->point_count != b->point_count)
        return -1;
    total = (size_t)a->point_count * a->out_ch;
    for (i = 0; i < total; i++)
        out[i] = a->table[i] * (1.0f - t) + b->table[i] * t;
    return 0;
}

/* Scale all CLUT values uniformly. */
void gmt_clut_scale(float *table, uint32_t point_count,
                    uint8_t out_ch, float factor)
{
    size_t total = (size_t)point_count * out_ch;
    size_t i;
    for (i = 0; i < total; i++)
        table[i] *= factor;
}

/* Invert a 1D CLUT (single input, single output channel) by swapping
 * the roles of input and output. Writes inv_count values. */
int gmt_clut_invert_1d(const gmt_clut *cl, float *inv, uint32_t inv_count)
{
    uint32_t oi;
    if (cl->in_ch != 1 || cl->out_ch != 1 || cl->point_count < 2)
        return -1;
    for (oi = 0; oi < inv_count; oi++) {
        float y = (float)oi / (float)(inv_count - 1);
        uint32_t lo = 0, hi = cl->point_count - 1;
        while (hi - lo > 1) {
            uint32_t mid = (lo + hi) / 2;
            if (cl->table[mid] < y) lo = mid; else hi = mid;
        }
        if (cl->table[hi] != cl->table[lo]) {
            float t = (y - cl->table[lo]) / (cl->table[hi] - cl->table[lo]);
            inv[oi] = ((float)lo + t) / (float)(cl->point_count - 1);
        } else {
            inv[oi] = (float)lo / (float)(cl->point_count - 1);
        }
    }
    return 0;
}

/* Count nodes where any output channel exceeds the unit range. */
uint32_t gmt_clut_out_of_range(const gmt_clut *cl)
{
    uint32_t count = 0;
    uint32_t i;
    unsigned d;
    for (i = 0; i < cl->point_count; i++) {
        for (d = 0; d < cl->out_ch; d++) {
            float v = cl->table[(size_t)i * cl->out_ch + d];
            if (v < 0.0f || v > 1.0f) {
                count++;
                break;
            }
        }
    }
    return count;
}

/* Compute the total variation of the CLUT (sum of absolute differences
 * between adjacent nodes along all dimensions). */
float gmt_clut_total_variation(const gmt_clut *cl, unsigned channel)
{
    float tv = 0.0f;
    uint32_t i;
    uint32_t nodes[GMT_MAX_CHANNELS];
    unsigned d;
    uint32_t strides[GMT_MAX_CHANNELS];

    if (channel >= cl->out_ch)
        return 0.0f;

    strides[0] = 1;
    for (d = 1; d < cl->in_ch; d++)
        strides[d] = strides[d-1] * cl->grid[d-1];

    for (i = 0; i < cl->point_count; i++) {
        flat_to_nodes(cl, i, nodes);
        for (d = 0; d < cl->in_ch; d++) {
            if (nodes[d] + 1 < cl->grid[d]) {
                uint32_t next = i + strides[d];
                float diff = cl->table[(size_t)next * cl->out_ch + channel]
                           - cl->table[(size_t)i * cl->out_ch + channel];
                if (diff < 0.0f) diff = -diff;
                tv += diff;
            }
        }
    }
    return tv;
}
