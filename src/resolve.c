/* resolve.c - Phase 10: apply the transform
 *
 * The apply phase walks the link cache in application order and runs each
 * stage against the working sample. Curve stages map one channel through the
 * resolved sample table; matrix and CLUT stages transform the whole vector.
 * The stage pointers were resolved during assembly and are read directly.
 */
#include "internal.h"

static float sample_at(const float *samples, uint32_t count, float x)
{
    uint32_t idx;
    if (count == 0)
        return x;
    if (x < 0.0f) x = 0.0f;
    if (x > 1.0f) x = 1.0f;
    idx = (uint32_t)(x * (float)(count - 1) + 0.5f);
    if (idx >= count)
        idx = count - 1;
    return samples[idx];
}

static void apply_curve_stage(const gmt_linkstage *s, float *v)
{
    unsigned ch = s->in_ch;
    /* Map the stage's channel through its resolved sample table. */
    v[ch] = sample_at(s->curve_samples, s->curve_count, v[ch]);
}

static void apply_chad_stage(const gmt_linkstage *s, float *v)
{
    /* Scale the working vector by the resolved white point. */
    float wx = s->white->x;
    float wy = s->white->y;
    float wz = s->white->z;
    if (wy == 0.0f)
        wy = 1.0f;
    v[0] = v[0] * wx / wy;
    v[2] = v[2] * wz / wy;
}

static void apply_matrix_stage(const gmt_linkstage *s, float *v)
{
    const gmt_matrix *m = s->matrix;
    float o[3];
    /* Apply the resolved 3x3 matrix and offset. */
    o[0] = m->e[0]*v[0] + m->e[1]*v[1] + m->e[2]*v[2] + m->off[0];
    o[1] = m->e[3]*v[0] + m->e[4]*v[1] + m->e[5]*v[2] + m->off[1];
    o[2] = m->e[6]*v[0] + m->e[7]*v[1] + m->e[8]*v[2] + m->off[2];
    v[0] = o[0];
    v[1] = o[1];
    v[2] = o[2];
}

int gmt_resolve_apply(const gmt_profile *p, const gmt_linkcache *lc,
                      const float *in, float *out)
{
    float work[GMT_MAX_CHANNELS];
    unsigned i, n;

    if (!lc->built)
        return GMT_ERR_UNSUPPORTED;

    n = lc->in_ch ? lc->in_ch : p->channels;
    if (n > GMT_MAX_CHANNELS)
        n = GMT_MAX_CHANNELS;
    for (i = 0; i < n; i++)
        work[i] = in[i];

    for (i = 0; i < lc->order_count; i++) {
        const gmt_linkstage *s = lc->order[i];
        switch (s->kind) {
        case GMT_ST_CURVE:
            apply_curve_stage(s, work);
            break;
        case GMT_ST_CLUT:
            gmt_mpet_apply_clut(s, work);
            break;
        case GMT_ST_CHAD:
            apply_chad_stage(s, work);
            break;
        case GMT_ST_MATRIX:
            apply_matrix_stage(s, work);
            break;
        default:
            /* handled once those stage kinds are assembled */
            break;
        }
    }

    n = lc->out_ch ? lc->out_ch : p->pcs_channels;
    if (n > GMT_MAX_CHANNELS)
        n = GMT_MAX_CHANNELS;
    for (i = 0; i < n; i++)
        out[i] = work[i];
    return GMT_OK;
}
