/* matops.c - matrix arithmetic
 *
 * Operations on 3x3 affine matrices used by the transform pipeline. The
 * chromatic adaptation matrix, the colorant matrix, and per-channel
 * linearization all flow through 3x3 math at assembly or application time.
 */
#include "internal.h"

/* Compute the determinant of a 3x3 matrix. */
float gmt_mat3_det(const float m[9])
{
    return m[0] * (m[4]*m[8] - m[5]*m[7])
         - m[1] * (m[3]*m[8] - m[5]*m[6])
         + m[2] * (m[3]*m[7] - m[4]*m[6]);
}

/* Invert a 3x3 matrix in place. Returns 0 on success, -1 if singular. */
int gmt_mat3_invert(float m[9])
{
    float det = gmt_mat3_det(m);
    float inv[9];
    float id;
    int i;

    if (det == 0.0f || det != det)  /* zero or NaN */
        return -1;

    id = 1.0f / det;
    inv[0] =  (m[4]*m[8] - m[5]*m[7]) * id;
    inv[1] = -(m[1]*m[8] - m[2]*m[7]) * id;
    inv[2] =  (m[1]*m[5] - m[2]*m[4]) * id;
    inv[3] = -(m[3]*m[8] - m[5]*m[6]) * id;
    inv[4] =  (m[0]*m[8] - m[2]*m[6]) * id;
    inv[5] = -(m[0]*m[5] - m[2]*m[3]) * id;
    inv[6] =  (m[3]*m[7] - m[4]*m[6]) * id;
    inv[7] = -(m[0]*m[7] - m[1]*m[6]) * id;
    inv[8] =  (m[0]*m[4] - m[1]*m[3]) * id;

    for (i = 0; i < 9; i++)
        m[i] = inv[i];
    return 0;
}

/* Multiply two 3x3 matrices: out = a * b. `out` may alias neither a nor b. */
void gmt_mat3_mul(const float a[9], const float b[9], float out[9])
{
    int r, c;
    for (r = 0; r < 3; r++)
        for (c = 0; c < 3; c++)
            out[r*3+c] = a[r*3+0]*b[0*3+c]
                       + a[r*3+1]*b[1*3+c]
                       + a[r*3+2]*b[2*3+c];
}

/* Transpose a 3x3 matrix in place. */
void gmt_mat3_transpose(float m[9])
{
    float t;
    t = m[1]; m[1] = m[3]; m[3] = t;
    t = m[2]; m[2] = m[6]; m[6] = t;
    t = m[5]; m[5] = m[7]; m[7] = t;
}

/* Set m to the 3x3 identity. */
void gmt_mat3_identity(float m[9])
{
    int i;
    for (i = 0; i < 9; i++)
        m[i] = 0.0f;
    m[0] = m[4] = m[8] = 1.0f;
}

/* Check whether m is close to the identity within tolerance eps. */
int gmt_mat3_is_identity(const float m[9], float eps)
{
    int r, c;
    for (r = 0; r < 3; r++)
        for (c = 0; c < 3; c++) {
            float expected = (r == c) ? 1.0f : 0.0f;
            float diff = m[r*3+c] - expected;
            if (diff < 0.0f) diff = -diff;
            if (diff > eps)
                return 0;
        }
    return 1;
}

/* Apply a 3x3 matrix to a 3-vector: out = M * v. */
void gmt_mat3_apply(const float m[9], const float v[3], float out[3])
{
    out[0] = m[0]*v[0] + m[1]*v[1] + m[2]*v[2];
    out[1] = m[3]*v[0] + m[4]*v[1] + m[5]*v[2];
    out[2] = m[6]*v[0] + m[7]*v[1] + m[8]*v[2];
}

/* Apply a gmt_matrix (3x3 + offset) to a 3-vector. */
void gmt_matrix_apply(const gmt_matrix *m, const float v[3], float out[3])
{
    out[0] = m->e[0]*v[0] + m->e[1]*v[1] + m->e[2]*v[2] + m->off[0];
    out[1] = m->e[3]*v[0] + m->e[4]*v[1] + m->e[5]*v[2] + m->off[1];
    out[2] = m->e[6]*v[0] + m->e[7]*v[1] + m->e[8]*v[2] + m->off[2];
}

/* Compose two gmt_matrix transforms: out = b(a(x)).
 * out.e = b.e * a.e, out.off = b.e * a.off + b.off */
void gmt_matrix_compose(const gmt_matrix *a, const gmt_matrix *b,
                        gmt_matrix *out)
{
    float tmp[9];
    gmt_mat3_mul(b->e, a->e, tmp);
    {
        int i;
        for (i = 0; i < 9; i++)
            out->e[i] = tmp[i];
    }
    out->off[0] = b->e[0]*a->off[0] + b->e[1]*a->off[1]
                + b->e[2]*a->off[2] + b->off[0];
    out->off[1] = b->e[3]*a->off[0] + b->e[4]*a->off[1]
                + b->e[5]*a->off[2] + b->off[1];
    out->off[2] = b->e[6]*a->off[0] + b->e[7]*a->off[1]
                + b->e[8]*a->off[2] + b->off[2];
    out->rows = 3;
    out->cols = 3;
}

/* Invert a gmt_matrix (3x3 + offset) in place. Returns 0 on success. */
int gmt_matrix_invert(gmt_matrix *m)
{
    float inv_e[9];
    int i;

    for (i = 0; i < 9; i++)
        inv_e[i] = m->e[i];
    if (gmt_mat3_invert(inv_e) != 0)
        return -1;

    /* new offset = -inv_e * old_off */
    {
        float no[3];
        no[0] = -(inv_e[0]*m->off[0] + inv_e[1]*m->off[1]
                + inv_e[2]*m->off[2]);
        no[1] = -(inv_e[3]*m->off[0] + inv_e[4]*m->off[1]
                + inv_e[5]*m->off[2]);
        no[2] = -(inv_e[6]*m->off[0] + inv_e[7]*m->off[1]
                + inv_e[8]*m->off[2]);
        m->off[0] = no[0];
        m->off[1] = no[1];
        m->off[2] = no[2];
    }
    for (i = 0; i < 9; i++)
        m->e[i] = inv_e[i];
    return 0;
}

/* Build a Bradford chromatic adaptation matrix from source white to
 * destination white. Both whites are XYZ tristimulus values. */
void gmt_bradford_adapt(const gmt_xyz *src, const gmt_xyz *dst,
                        gmt_matrix *out)
{
    /* Bradford cone response matrix (fixed). */
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
    float src_lms[3], dst_lms[3];
    float scale[9];
    float tmp[9];
    int i;

    /* Transform whites to LMS cone space. */
    {
        float sv[3] = { src->x, src->y, src->z };
        float dv[3] = { dst->x, dst->y, dst->z };
        gmt_mat3_apply(brad, sv, src_lms);
        gmt_mat3_apply(brad, dv, dst_lms);
    }

    /* Diagonal scaling in LMS. */
    gmt_mat3_identity(scale);
    for (i = 0; i < 3; i++) {
        if (src_lms[i] != 0.0f)
            scale[i*4] = dst_lms[i] / src_lms[i];
    }

    /* Compose: brad_inv * scale * brad */
    gmt_mat3_mul(scale, brad, tmp);
    gmt_mat3_mul(brad_inv, tmp, out->e);
    out->off[0] = out->off[1] = out->off[2] = 0.0f;
    out->rows = out->cols = 3;
}

/* Frobenius norm of a 3x3 matrix. */
float gmt_mat3_norm(const float m[9])
{
    float sum = 0.0f;
    int i;
    for (i = 0; i < 9; i++)
        sum += m[i] * m[i];
    /* sqrt approximation without <math.h>: Newton's method */
    if (sum <= 0.0f)
        return 0.0f;
    {
        float x = sum * 0.5f;
        float y = sum;
        int k;
        for (k = 0; k < 8; k++)
            y = (y + sum / y) * 0.5f;
        (void)x;
        return y;
    }
}

/* Compare two matrices element-wise within tolerance. */
int gmt_matrix_equal(const gmt_matrix *a, const gmt_matrix *b, float eps)
{
    int i;
    if (!a || !b)
        return 0;
    for (i = 0; i < 9; i++) {
        float d = a->e[i] - b->e[i];
        if (d < 0.0f) d = -d;
        if (d > eps) return 0;
    }
    for (i = 0; i < 3; i++) {
        float d = a->off[i] - b->off[i];
        if (d < 0.0f) d = -d;
        if (d > eps) return 0;
    }
    return 1;
}
