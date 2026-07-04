/* clut.c - color lookup table elements
 *
 * A CLUT stores an n-dimensional grid of output samples. The input channel
 * count sets the number of grid dimensions; each dimension has its own node
 * count. The total node count is the product of the per-dimension counts and
 * each node stores out_ch samples. Both the AtoB lookup element and the
 * multi-process sequence may name a CLUT element by its offset.
 */
#include "internal.h"
#include <string.h>

#define GMT_CLUT_MAX_POINTS 262144u   /* product of grid dimensions cap    */

gmt_element *gmt_clut_decode(gmt_ctx *c, gmt_reader *r, uint32_t offset)
{
    gmt_element *el;
    gmt_clut *cl;
    uint8_t in_ch, out_ch;
    uint32_t points = 1;
    size_t total, i;

    in_ch  = gmt_rd_u8(r);
    out_ch = gmt_rd_u8(r);
    (void)gmt_rd_u16(r);                /* reserved */

    if (in_ch == 0 || in_ch > GMT_MAX_CHANNELS ||
        out_ch == 0 || out_ch > GMT_MAX_CHANNELS) {
        gmt_fail(c, GMT_ERR_ELEMENT);
        return NULL;
    }

    el = (gmt_element *)gmt_alloc(c, sizeof(gmt_element));
    if (!el)
        return NULL;
    cl = (gmt_clut *)gmt_alloc(c, sizeof(gmt_clut));
    if (!cl) {
        gmt_release(c, el);
        return NULL;
    }
    el->kind = GMT_EL_CLUT;
    el->src_offset = offset;
    el->u.clut = cl;
    cl->in_ch = in_ch;
    cl->out_ch = out_ch;

    for (i = 0; i < in_ch; i++) {
        uint8_t g = gmt_rd_u8(r);
        if (g < 2) {
            gmt_fail(c, GMT_ERR_ELEMENT);
            gmt_release(c, cl);
            gmt_release(c, el);
            return NULL;
        }
        cl->grid[i] = g;
        points *= g;
        if (points > GMT_CLUT_MAX_POINTS) {
            gmt_fail(c, GMT_ERR_ELEMENT);
            gmt_release(c, cl);
            gmt_release(c, el);
            return NULL;
        }
    }
    for (; i < GMT_MAX_CHANNELS; i++)
        cl->grid[i] = 0;

    cl->point_count = points;
    total = (size_t)points * out_ch;
    if (total == 0 || !gmt_rd_ok(r, total * 2u)) {
        gmt_fail(c, GMT_ERR_ELEMENT);
        gmt_release(c, cl);
        gmt_release(c, el);
        return NULL;
    }

    cl->table = (float *)gmt_alloc(c, total * sizeof(float));
    if (!cl->table) {
        gmt_release(c, cl);
        gmt_release(c, el);
        return NULL;
    }
    for (i = 0; i < total; i++) {
        uint16_t v = gmt_rd_u16(r);
        cl->table[i] = (float)v / 65535.0f;
    }
    return el;
}

void gmt_clut_free(gmt_ctx *c, gmt_element *el)
{
    if (!el)
        return;
    if (el->u.clut) {
        gmt_release(c, el->u.clut->table);
        gmt_release(c, el->u.clut);
        el->u.clut = NULL;
    }
    gmt_release(c, el);
}

/* Compute the flat index for a node in the multi-dimensional grid. */
static uint32_t clut_flat_index(const gmt_clut *cl, const uint32_t *nodes)
{
    uint32_t idx = 0;
    uint32_t stride = 1;
    unsigned d;
    for (d = 0; d < cl->in_ch; d++) {
        idx += nodes[d] * stride;
        stride *= cl->grid[d];
    }
    return idx;
}

/* Nearest-node sampling of the CLUT. */
void gmt_clut_sample_nearest(const gmt_clut *cl, const float *input,
                             float *output)
{
    uint32_t nodes[GMT_MAX_CHANNELS];
    uint32_t flat;
    unsigned d;

    for (d = 0; d < cl->in_ch; d++) {
        float x = input[d];
        uint8_t g = cl->grid[d];
        uint32_t n;
        if (x < 0.0f) x = 0.0f;
        if (x > 1.0f) x = 1.0f;
        n = (uint32_t)(x * (float)(g - 1) + 0.5f);
        if (n >= g) n = g - 1;
        nodes[d] = n;
    }
    flat = clut_flat_index(cl, nodes);
    for (d = 0; d < cl->out_ch; d++)
        output[d] = cl->table[(size_t)flat * cl->out_ch + d];
}

/* Trilinear interpolation for a 3-dimensional CLUT (the most common case).
 * Falls back to nearest-node for other dimensionalities. */
void gmt_clut_sample_trilinear(const gmt_clut *cl, const float *input,
                               float *output)
{
    float fx[3], fy[3];
    uint32_t lo[3], hi[3];
    unsigned d;

    if (cl->in_ch != 3 || cl->out_ch == 0) {
        gmt_clut_sample_nearest(cl, input, output);
        return;
    }

    for (d = 0; d < 3; d++) {
        float x = input[d];
        float pos;
        uint8_t g = cl->grid[d];
        if (x < 0.0f) x = 0.0f;
        if (x > 1.0f) x = 1.0f;
        pos = x * (float)(g - 1);
        lo[d] = (uint32_t)pos;
        if (lo[d] >= (uint32_t)(g - 1))
            lo[d] = (uint32_t)(g - 2 < g ? g - 2 : 0);
        hi[d] = lo[d] + 1;
        if (hi[d] >= g) hi[d] = g - 1;
        fx[d] = pos - (float)lo[d];
        fy[d] = 1.0f - fx[d];
    }

    /* Eight corners of the interpolation cube. */
    for (d = 0; d < cl->out_ch; d++) {
        uint32_t n[3];
        float c000, c001, c010, c011, c100, c101, c110, c111;
        float c00, c01, c10, c11, c0, c1;

        n[0] = lo[0]; n[1] = lo[1]; n[2] = lo[2];
        c000 = cl->table[(size_t)clut_flat_index(cl, n) * cl->out_ch + d];
        n[2] = hi[2];
        c001 = cl->table[(size_t)clut_flat_index(cl, n) * cl->out_ch + d];
        n[1] = hi[1]; n[2] = lo[2];
        c010 = cl->table[(size_t)clut_flat_index(cl, n) * cl->out_ch + d];
        n[2] = hi[2];
        c011 = cl->table[(size_t)clut_flat_index(cl, n) * cl->out_ch + d];
        n[0] = hi[0]; n[1] = lo[1]; n[2] = lo[2];
        c100 = cl->table[(size_t)clut_flat_index(cl, n) * cl->out_ch + d];
        n[2] = hi[2];
        c101 = cl->table[(size_t)clut_flat_index(cl, n) * cl->out_ch + d];
        n[1] = hi[1]; n[2] = lo[2];
        c110 = cl->table[(size_t)clut_flat_index(cl, n) * cl->out_ch + d];
        n[2] = hi[2];
        c111 = cl->table[(size_t)clut_flat_index(cl, n) * cl->out_ch + d];

        c00 = c000 * fy[2] + c001 * fx[2];
        c01 = c010 * fy[2] + c011 * fx[2];
        c10 = c100 * fy[2] + c101 * fx[2];
        c11 = c110 * fy[2] + c111 * fx[2];
        c0  = c00 * fy[1] + c01 * fx[1];
        c1  = c10 * fy[1] + c11 * fx[1];
        output[d] = c0 * fy[0] + c1 * fx[0];
    }
}

/* Validate CLUT grid consistency: each grid dimension must be >= 2 and the
 * product must equal point_count. */
int gmt_clut_validate_grid(const gmt_clut *cl)
{
    uint32_t product = 1;
    unsigned d;
    for (d = 0; d < cl->in_ch; d++) {
        if (cl->grid[d] < 2)
            return 0;
        product *= cl->grid[d];
        if (product > cl->point_count)
            return 0;
    }
    return (product == cl->point_count) ? 1 : 0;
}

/* Compute the total memory footprint of a CLUT's sample table in bytes. */
size_t gmt_clut_table_bytes(const gmt_clut *cl)
{
    return (size_t)cl->point_count * cl->out_ch * sizeof(float);
}
