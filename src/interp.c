/* interp.c - multi-dimensional interpolation
 *
 * Advanced interpolation methods for color lookup tables. Beyond the
 * trilinear method in clut.c, this module provides tetrahedral (the method
 * used by high-quality CMMs) and a linear-prism variant. All methods operate
 * on the gmt_clut structure.
 */
#include "internal.h"

/* Compute a flat index into the CLUT table for a given node vector. */
static uint32_t flat_idx(const gmt_clut *cl, const uint32_t *n)
{
    uint32_t idx = 0, stride = 1;
    unsigned d;
    for (d = 0; d < cl->in_ch; d++) {
        idx += n[d] * stride;
        stride *= cl->grid[d];
    }
    return idx;
}

/* Read one output sample from the table. */
static float tbl(const gmt_clut *cl, const uint32_t *n, unsigned och)
{
    uint32_t fi = flat_idx(cl, n);
    return cl->table[(size_t)fi * cl->out_ch + och];
}

/* Tetrahedral interpolation for 3D CLUTs. This divides each grid cube into
 * six tetrahedra and picks the one that contains the sample point, then
 * barycentric-interpolates within it. */
void gmt_interp_tetrahedral(const gmt_clut *cl, const float *in, float *out)
{
    float fx[3], fy[3];
    uint32_t lo[3], hi[3];
    unsigned d, oc;

    if (cl->in_ch != 3) {
        /* Fall back for non-3D CLUTs. */
        for (oc = 0; oc < cl->out_ch; oc++)
            out[oc] = 0.0f;
        return;
    }

    for (d = 0; d < 3; d++) {
        float x = in[d];
        float pos;
        uint8_t g = cl->grid[d];
        if (x < 0.0f) x = 0.0f;
        if (x > 1.0f) x = 1.0f;
        pos = x * (float)(g - 1);
        lo[d] = (uint32_t)pos;
        if (lo[d] >= (uint32_t)(g - 1))
            lo[d] = (uint32_t)(g - 1) - 1;
        hi[d] = lo[d] + 1;
        if (hi[d] >= g) hi[d] = g - 1;
        fx[d] = pos - (float)lo[d];
        fy[d] = 1.0f - fx[d];
    }

    /* Determine which tetrahedron contains the point. The six tetrahedra
     * are defined by the ordering of the fractional coordinates. */
    for (oc = 0; oc < cl->out_ch; oc++) {
        uint32_t n0[3] = { lo[0], lo[1], lo[2] };
        uint32_t n1[3], n2[3], n3[3];
        float c0, c1, c2, c3;
        float w0, w1, w2, w3;

        n3[0] = hi[0]; n3[1] = hi[1]; n3[2] = hi[2];
        c0 = tbl(cl, n0, oc);
        c3 = tbl(cl, n3, oc);

        if (fx[0] >= fx[1]) {
            if (fx[1] >= fx[2]) {
                /* Tetrahedron 1: r >= g >= b */
                n1[0] = hi[0]; n1[1] = lo[1]; n1[2] = lo[2];
                n2[0] = hi[0]; n2[1] = hi[1]; n2[2] = lo[2];
                c1 = tbl(cl, n1, oc);
                c2 = tbl(cl, n2, oc);
                w0 = fy[0];
                w1 = fx[0] - fx[1];
                w2 = fx[1] - fx[2];
                w3 = fx[2];
            } else if (fx[0] >= fx[2]) {
                /* Tetrahedron 2: r >= b >= g */
                n1[0] = hi[0]; n1[1] = lo[1]; n1[2] = lo[2];
                n2[0] = hi[0]; n2[1] = lo[1]; n2[2] = hi[2];
                c1 = tbl(cl, n1, oc);
                c2 = tbl(cl, n2, oc);
                w0 = fy[0];
                w1 = fx[0] - fx[2];
                w2 = fx[2] - fx[1];
                w3 = fx[1];
            } else {
                /* Tetrahedron 3: b >= r >= g */
                n1[0] = lo[0]; n1[1] = lo[1]; n1[2] = hi[2];
                n2[0] = hi[0]; n2[1] = lo[1]; n2[2] = hi[2];
                c1 = tbl(cl, n1, oc);
                c2 = tbl(cl, n2, oc);
                w0 = fy[2];
                w1 = fx[2] - fx[0];
                w2 = fx[0] - fx[1];
                w3 = fx[1];
            }
        } else {
            if (fx[0] >= fx[2]) {
                /* Tetrahedron 4: g >= r >= b */
                n1[0] = lo[0]; n1[1] = hi[1]; n1[2] = lo[2];
                n2[0] = hi[0]; n2[1] = hi[1]; n2[2] = lo[2];
                c1 = tbl(cl, n1, oc);
                c2 = tbl(cl, n2, oc);
                w0 = fy[1];
                w1 = fx[1] - fx[0];
                w2 = fx[0] - fx[2];
                w3 = fx[2];
            } else if (fx[1] >= fx[2]) {
                /* Tetrahedron 5: g >= b >= r */
                n1[0] = lo[0]; n1[1] = hi[1]; n1[2] = lo[2];
                n2[0] = lo[0]; n2[1] = hi[1]; n2[2] = hi[2];
                c1 = tbl(cl, n1, oc);
                c2 = tbl(cl, n2, oc);
                w0 = fy[1];
                w1 = fx[1] - fx[2];
                w2 = fx[2] - fx[0];
                w3 = fx[0];
            } else {
                /* Tetrahedron 6: b >= g >= r */
                n1[0] = lo[0]; n1[1] = lo[1]; n1[2] = hi[2];
                n2[0] = lo[0]; n2[1] = hi[1]; n2[2] = hi[2];
                c1 = tbl(cl, n1, oc);
                c2 = tbl(cl, n2, oc);
                w0 = fy[2];
                w1 = fx[2] - fx[1];
                w2 = fx[1] - fx[0];
                w3 = fx[0];
            }
        }
        out[oc] = w0 * c0 + w1 * c1 + w2 * c2 + w3 * c3;
    }
}

/* Bilinear interpolation for 2D CLUTs. */
void gmt_interp_bilinear(const gmt_clut *cl, const float *in, float *out)
{
    float fx, fy;
    uint32_t lx, ly, hx, hy;
    unsigned oc;

    if (cl->in_ch != 2) {
        for (oc = 0; oc < cl->out_ch; oc++)
            out[oc] = 0.0f;
        return;
    }

    {
        float x = in[0], y = in[1];
        float px, py;
        if (x < 0.0f) x = 0.0f; if (x > 1.0f) x = 1.0f;
        if (y < 0.0f) y = 0.0f; if (y > 1.0f) y = 1.0f;
        px = x * (float)(cl->grid[0] - 1);
        py = y * (float)(cl->grid[1] - 1);
        lx = (uint32_t)px; if (lx >= cl->grid[0]-1) lx = cl->grid[0]-2;
        ly = (uint32_t)py; if (ly >= cl->grid[1]-1) ly = cl->grid[1]-2;
        hx = lx + 1; hy = ly + 1;
        fx = px - (float)lx;
        fy = py - (float)ly;
    }

    for (oc = 0; oc < cl->out_ch; oc++) {
        uint32_t n00[2] = { lx, ly };
        uint32_t n10[2] = { hx, ly };
        uint32_t n01[2] = { lx, hy };
        uint32_t n11[2] = { hx, hy };
        float c00 = tbl(cl, n00, oc);
        float c10 = tbl(cl, n10, oc);
        float c01 = tbl(cl, n01, oc);
        float c11 = tbl(cl, n11, oc);
        float c0 = c00 * (1.0f - fx) + c10 * fx;
        float c1 = c01 * (1.0f - fx) + c11 * fx;
        out[oc] = c0 * (1.0f - fy) + c1 * fy;
    }
}

/* N-dimensional nearest-node sampling (general case). */
void gmt_interp_nearest(const gmt_clut *cl, const float *in, float *out)
{
    uint32_t nodes[GMT_MAX_CHANNELS];
    uint32_t fi;
    unsigned d;

    for (d = 0; d < cl->in_ch && d < GMT_MAX_CHANNELS; d++) {
        float x = in[d];
        uint8_t g = cl->grid[d];
        uint32_t n;
        if (x < 0.0f) x = 0.0f;
        if (x > 1.0f) x = 1.0f;
        n = (uint32_t)(x * (float)(g - 1) + 0.5f);
        if (n >= g) n = g - 1;
        nodes[d] = n;
    }
    fi = flat_idx(cl, nodes);
    for (d = 0; d < cl->out_ch; d++)
        out[d] = cl->table[(size_t)fi * cl->out_ch + d];
}

/* Dispatch interpolation by dimensionality: tetrahedral for 3D, bilinear
 * for 2D, nearest for all others. */
void gmt_interp_auto(const gmt_clut *cl, const float *in, float *out)
{
    switch (cl->in_ch) {
    case 2:  gmt_interp_bilinear(cl, in, out);    break;
    case 3:  gmt_interp_tetrahedral(cl, in, out);  break;
    default: gmt_interp_nearest(cl, in, out);      break;
    }
}

/* Evaluate a CLUT at multiple input points. */
void gmt_interp_batch(const gmt_clut *cl, const float *inputs,
                      float *outputs, size_t count)
{
    size_t i;
    for (i = 0; i < count; i++)
        gmt_interp_auto(cl, inputs + i * cl->in_ch,
                         outputs + i * cl->out_ch);
}

/* Compute the maximum interpolation error between nearest and tetrahedral
 * over a uniform test grid. */
float gmt_interp_max_error(const gmt_clut *cl, unsigned test_grid)
{
    float max_err = 0.0f;
    unsigned xi, yi, zi;
    float in[3], near_out[GMT_MAX_CHANNELS], tet_out[GMT_MAX_CHANNELS];
    unsigned d;

    if (cl->in_ch != 3 || test_grid < 2)
        return 0.0f;

    for (xi = 0; xi < test_grid; xi++)
        for (yi = 0; yi < test_grid; yi++)
            for (zi = 0; zi < test_grid; zi++) {
                in[0] = (float)xi / (float)(test_grid - 1);
                in[1] = (float)yi / (float)(test_grid - 1);
                in[2] = (float)zi / (float)(test_grid - 1);
                gmt_interp_nearest(cl, in, near_out);
                gmt_interp_tetrahedral(cl, in, tet_out);
                for (d = 0; d < cl->out_ch; d++) {
                    float err = near_out[d] - tet_out[d];
                    if (err < 0.0f) err = -err;
                    if (err > max_err) max_err = err;
                }
            }
    return max_err;
}
