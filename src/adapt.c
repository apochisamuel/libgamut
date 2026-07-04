/* adapt.c - chromatic adaptation transforms
 *
 * Compute chromatic adaptation matrices for transforming colorimetric data
 * between illuminants. Four methods are provided: XYZ scaling (simplest),
 * von Kries (cone-response diagonal), Bradford (the ICC standard), and
 * CAT02 (used by CIECAM02).
 */
#include "internal.h"
#include <string.h>

/* Standard illuminant white points. */
static const gmt_xyz wp_d50 = { 0.9642f, 1.0000f, 0.8249f };
static const gmt_xyz wp_d65 = { 0.9505f, 1.0000f, 1.0890f };
static const gmt_xyz wp_a   = { 1.0985f, 1.0000f, 0.3558f };

const gmt_xyz *gmt_wp_d50(void) { return &wp_d50; }
const gmt_xyz *gmt_wp_d65(void) { return &wp_d65; }
const gmt_xyz *gmt_wp_a(void)   { return &wp_a;   }

/* 3x3 matrix helpers (local copies to keep this TU self-contained). */
static void mat_mul(const float a[9], const float b[9], float out[9])
{
    int r, c;
    for (r = 0; r < 3; r++)
        for (c = 0; c < 3; c++)
            out[r*3+c] = a[r*3+0]*b[0*3+c]
                       + a[r*3+1]*b[1*3+c]
                       + a[r*3+2]*b[2*3+c];
}

static void mat_apply(const float m[9], const float v[3], float out[3])
{
    out[0] = m[0]*v[0] + m[1]*v[1] + m[2]*v[2];
    out[1] = m[3]*v[0] + m[4]*v[1] + m[5]*v[2];
    out[2] = m[6]*v[0] + m[7]*v[1] + m[8]*v[2];
}

static void mat_ident(float m[9])
{
    int i;
    for (i = 0; i < 9; i++) m[i] = 0.0f;
    m[0] = m[4] = m[8] = 1.0f;
}

/* Invert a 3x3 matrix. Returns 0 on success. */
static int mat_inv(const float m[9], float out[9])
{
    float det = m[0]*(m[4]*m[8]-m[5]*m[7])
              - m[1]*(m[3]*m[8]-m[5]*m[6])
              + m[2]*(m[3]*m[7]-m[4]*m[6]);
    float id;
    if (det == 0.0f) return -1;
    id = 1.0f / det;
    out[0] =  (m[4]*m[8]-m[5]*m[7]) * id;
    out[1] = -(m[1]*m[8]-m[2]*m[7]) * id;
    out[2] =  (m[1]*m[5]-m[2]*m[4]) * id;
    out[3] = -(m[3]*m[8]-m[5]*m[6]) * id;
    out[4] =  (m[0]*m[8]-m[2]*m[6]) * id;
    out[5] = -(m[0]*m[5]-m[2]*m[3]) * id;
    out[6] =  (m[3]*m[7]-m[4]*m[6]) * id;
    out[7] = -(m[0]*m[7]-m[1]*m[6]) * id;
    out[8] =  (m[0]*m[4]-m[1]*m[3]) * id;
    return 0;
}

/* Build a diagonal-scaling adaptation matrix from cone/sensor responses. */
static void build_adapt(const float cone[9], const float cone_inv[9],
                        const gmt_xyz *src, const gmt_xyz *dst,
                        float out[9])
{
    float src_c[3], dst_c[3], scale[9], tmp[9];
    float sv[3] = { src->x, src->y, src->z };
    float dv[3] = { dst->x, dst->y, dst->z };
    int i;

    mat_apply(cone, sv, src_c);
    mat_apply(cone, dv, dst_c);

    mat_ident(scale);
    for (i = 0; i < 3; i++)
        scale[i*4] = (src_c[i] != 0.0f) ? dst_c[i] / src_c[i] : 1.0f;

    mat_mul(scale, cone, tmp);
    mat_mul(cone_inv, tmp, out);
}

/* XYZ scaling: the simplest adaptation, equivalent to a diagonal matrix
 * in XYZ space. */
void gmt_adapt_xyz_scaling(const gmt_xyz *src, const gmt_xyz *dst,
                           float out[9])
{
    mat_ident(out);
    out[0] = (src->x != 0.0f) ? dst->x / src->x : 1.0f;
    out[4] = (src->y != 0.0f) ? dst->y / src->y : 1.0f;
    out[8] = (src->z != 0.0f) ? dst->z / src->z : 1.0f;
}

/* Von Kries adaptation using the Hunt-Pointer-Estevez cone fundamentals. */
void gmt_adapt_vonkries(const gmt_xyz *src, const gmt_xyz *dst,
                        float out[9])
{
    static const float hpe[9] = {
         0.38971f,  0.68898f, -0.07868f,
        -0.22981f,  1.18340f,  0.04641f,
         0.00000f,  0.00000f,  1.00000f
    };
    static const float hpe_inv[9] = {
         1.91020f, -1.11212f,  0.20191f,
         0.37095f,  0.62905f,  0.00000f,
         0.00000f,  0.00000f,  1.00000f
    };
    build_adapt(hpe, hpe_inv, src, dst, out);
}

/* Bradford adaptation (ICC standard). */
void gmt_adapt_bradford(const gmt_xyz *src, const gmt_xyz *dst,
                        float out[9])
{
    static const float brad[9] = {
         0.8951f,  0.2664f, -0.1614f,
        -0.7502f,  1.7135f,  0.0367f,
         0.0389f, -0.0685f,  1.0296f
    };
    static const float brad_inv[9] = {
         0.9870f, -0.1471f,  0.1600f,
         0.4323f,  0.5184f,  0.0493f,
        -0.0085f,  0.0400f,  0.9685f
    };
    build_adapt(brad, brad_inv, src, dst, out);
}

/* CAT02 adaptation (CIECAM02). */
void gmt_adapt_cat02(const gmt_xyz *src, const gmt_xyz *dst,
                     float out[9])
{
    static const float cat02[9] = {
         0.7328f,  0.4296f, -0.1624f,
        -0.7036f,  1.6975f,  0.0061f,
         0.0030f,  0.0136f,  0.9834f
    };
    float cat02_inv[9];
    if (mat_inv(cat02, cat02_inv) != 0) {
        mat_ident(out);
        return;
    }
    build_adapt(cat02, cat02_inv, src, dst, out);
}

/* Apply a 3x3 adaptation matrix to an XYZ value. */
void gmt_adapt_apply(const float M[9], const float xyz[3], float out[3])
{
    mat_apply(M, xyz, out);
}

/* Adapt an XYZ value from one illuminant to another using Bradford. */
void gmt_adapt_bradford_xyz(const float xyz[3],
                            const gmt_xyz *src, const gmt_xyz *dst,
                            float out[3])
{
    float M[9];
    gmt_adapt_bradford(src, dst, M);
    mat_apply(M, xyz, out);
}

/* Compute the D50-to-D65 Bradford matrix (a common default). */
void gmt_adapt_d50_to_d65(float out[9])
{
    gmt_adapt_bradford(&wp_d50, &wp_d65, out);
}

/* Compute the D65-to-D50 Bradford matrix. */
void gmt_adapt_d65_to_d50(float out[9])
{
    gmt_adapt_bradford(&wp_d65, &wp_d50, out);
}

/* Compare two adaptation matrices element-wise within tolerance. */
int gmt_adapt_matrices_equal(const float a[9], const float b[9], float eps)
{
    int i;
    for (i = 0; i < 9; i++) {
        float d = a[i] - b[i];
        if (d < 0.0f) d = -d;
        if (d > eps) return 0;
    }
    return 1;
}
