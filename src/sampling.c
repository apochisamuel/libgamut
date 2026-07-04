/* sampling.c - profile evaluation and sampling utilities
 *
 * Run the profile transform over structured sets of input points for
 * analysis and diagnostics. Includes uniform grid sweeps across the device
 * space, per-channel ramp evaluation, gamut boundary probing, and
 * neutral-axis extraction.
 */
#include "internal.h"
#include <string.h>

/* Evaluate the transform at uniformly spaced points along one device
 * channel while holding the others at a fixed value. Output holds
 * `steps` rows of `pcs_ch` floats. Returns 0 on success. */
int gmt_sample_ramp(const gmt_profile *p, unsigned channel, float fixed,
                    unsigned steps, float *output)
{
    unsigned nch = p ? p->channels : 0;
    unsigned pcs_ch = p ? p->pcs_channels : 0;
    float in[GMT_MAX_CHANNELS];
    unsigned i, c;

    if (!p || nch == 0 || nch > GMT_MAX_CHANNELS || steps < 2)
        return -1;

    for (c = 0; c < nch; c++)
        in[c] = fixed;

    for (i = 0; i < steps; i++) {
        float v = (float)i / (float)(steps - 1);
        in[channel] = v;
        if (gmt_apply(p, in, output + (size_t)i * pcs_ch) != GMT_OK)
            memset(output + (size_t)i * pcs_ch, 0,
                   (size_t)pcs_ch * sizeof(float));
    }
    return 0;
}

/* Evaluate the transform along the neutral axis (equal device values). */
int gmt_sample_neutral(const gmt_profile *p, unsigned steps, float *output)
{
    unsigned nch = p ? p->channels : 0;
    unsigned pcs_ch = p ? p->pcs_channels : 0;
    float in[GMT_MAX_CHANNELS];
    unsigned i, c;

    if (!p || nch == 0 || nch > GMT_MAX_CHANNELS || steps < 2)
        return -1;

    for (i = 0; i < steps; i++) {
        float v = (float)i / (float)(steps - 1);
        for (c = 0; c < nch; c++)
            in[c] = v;
        if (gmt_apply(p, in, output + (size_t)i * pcs_ch) != GMT_OK)
            memset(output + (size_t)i * pcs_ch, 0,
                   (size_t)pcs_ch * sizeof(float));
    }
    return 0;
}

/* Evaluate the transform on a uniform 3D grid in the device space (RGB
 * profiles). Output holds grid^3 rows of pcs_ch floats. */
int gmt_sample_grid3(const gmt_profile *p, unsigned grid, float *output)
{
    unsigned nch = p ? p->channels : 0;
    unsigned pcs_ch = p ? p->pcs_channels : 0;
    float in[GMT_MAX_CHANNELS];
    unsigned ri, gi, bi;
    size_t idx = 0;

    if (!p || nch < 3 || nch > GMT_MAX_CHANNELS || grid < 2)
        return -1;

    for (ri = 0; ri < grid; ri++)
        for (gi = 0; gi < grid; gi++)
            for (bi = 0; bi < grid; bi++) {
                unsigned c;
                in[0] = (float)ri / (float)(grid - 1);
                in[1] = (float)gi / (float)(grid - 1);
                in[2] = (float)bi / (float)(grid - 1);
                for (c = 3; c < nch; c++)
                    in[c] = 0.0f;
                if (gmt_apply(p, in, output + idx * pcs_ch) != GMT_OK)
                    memset(output + idx * pcs_ch, 0,
                           (size_t)pcs_ch * sizeof(float));
                idx++;
            }
    return 0;
}

/* Evaluate the transform on a set of primary and secondary device colors
 * (the six "RGBCMY" corners plus black and white). Output holds 8 rows. */
int gmt_sample_primaries(const gmt_profile *p, float *output)
{
    static const float corners[8][3] = {
        {0,0,0}, {1,0,0}, {0,1,0}, {0,0,1},
        {1,1,0}, {1,0,1}, {0,1,1}, {1,1,1}
    };
    unsigned nch = p ? p->channels : 0;
    unsigned pcs_ch = p ? p->pcs_channels : 0;
    float in[GMT_MAX_CHANNELS];
    int i;
    unsigned c;

    if (!p || nch < 3 || nch > GMT_MAX_CHANNELS)
        return -1;

    for (i = 0; i < 8; i++) {
        for (c = 0; c < nch; c++)
            in[c] = (c < 3) ? corners[i][c] : 0.0f;
        if (gmt_apply(p, in, output + (size_t)i * pcs_ch) != GMT_OK)
            memset(output + (size_t)i * pcs_ch, 0,
                   (size_t)pcs_ch * sizeof(float));
    }
    return 0;
}

/* Compute the maximum and mean PCS output for each channel across a ramp
 * sweep. This characterizes the tone response of each device channel. */
int gmt_sample_channel_response(const gmt_profile *p, unsigned channel,
                                unsigned steps,
                                float *out_max, float *out_mean)
{
    unsigned nch = p ? p->channels : 0;
    unsigned pcs_ch = p ? p->pcs_channels : 0;
    float in[GMT_MAX_CHANNELS], out[GMT_MAX_CHANNELS];
    unsigned i, c;
    float sums[GMT_MAX_CHANNELS];

    if (!p || nch == 0 || nch > GMT_MAX_CHANNELS || steps < 2)
        return -1;

    for (c = 0; c < pcs_ch; c++) {
        out_max[c] = 0.0f;
        sums[c] = 0.0f;
    }

    for (c = 0; c < nch; c++)
        in[c] = 0.0f;

    for (i = 0; i < steps; i++) {
        float v = (float)i / (float)(steps - 1);
        in[channel] = v;
        if (gmt_apply(p, in, out) != GMT_OK)
            continue;
        for (c = 0; c < pcs_ch; c++) {
            float val = out[c];
            if (val < 0.0f) val = -val;
            if (val > out_max[c]) out_max[c] = val;
            sums[c] += val;
        }
    }

    for (c = 0; c < pcs_ch; c++)
        out_mean[c] = sums[c] / (float)steps;
    return 0;
}

/* Compute the Jacobian of the transform at a given device input point
 * using forward finite differences. The Jacobian is nch-by-pcs_ch. */
int gmt_sample_jacobian(const gmt_profile *p, const float *at,
                        float *jacobian, float eps)
{
    unsigned nch = p ? p->channels : 0;
    unsigned pcs_ch = p ? p->pcs_channels : 0;
    float in[GMT_MAX_CHANNELS], out0[GMT_MAX_CHANNELS], out1[GMT_MAX_CHANNELS];
    unsigned c, pc;

    if (!p || nch == 0 || nch > GMT_MAX_CHANNELS || eps <= 0.0f)
        return -1;

    for (c = 0; c < nch; c++)
        in[c] = at[c];
    if (gmt_apply(p, in, out0) != GMT_OK)
        return -1;

    for (c = 0; c < nch; c++) {
        float saved = in[c];
        in[c] = saved + eps;
        if (in[c] > 1.0f) in[c] = 1.0f;
        if (gmt_apply(p, in, out1) != GMT_OK) {
            in[c] = saved;
            continue;
        }
        for (pc = 0; pc < pcs_ch; pc++)
            jacobian[(size_t)c * pcs_ch + pc] =
                (out1[pc] - out0[pc]) / eps;
        in[c] = saved;
    }
    return 0;
}

/* Estimate the gamut boundary at a given PCS lightness by probing the
 * transform inverse at many hue angles. Returns the number of boundary
 * points written (up to max_points). */
unsigned gmt_sample_gamut_boundary(const gmt_profile *p, float L,
                                   unsigned hue_steps,
                                   float *boundary_ab,
                                   unsigned max_points)
{
    unsigned count = 0;
    unsigned hi;
    float in[GMT_MAX_CHANNELS], out[GMT_MAX_CHANNELS];
    unsigned nch = p ? p->channels : 0;

    if (!p || nch == 0 || nch > GMT_MAX_CHANNELS || hue_steps == 0)
        return 0;

    for (hi = 0; hi < hue_steps && count < max_points; hi++) {
        float angle = (float)hi / (float)hue_steps * 6.2832f;
        float step;
        /* sin/cos approximation */
        float sa = angle - angle*angle*angle/6.0f
                 + angle*angle*angle*angle*angle/120.0f;
        float ca = 1.0f - angle*angle/2.0f
                 + angle*angle*angle*angle/24.0f;
        float best_c = 0.0f;

        /* Binary search for the maximum chroma at this hue. */
        for (step = 64.0f; step >= 0.5f; step *= 0.5f) {
            float test_c = best_c + step;
            float test_a = test_c * ca;
            float test_b = test_c * sa;
            unsigned c;
            int ok;

            in[0] = L;
            in[1] = test_a;
            in[2] = test_b;
            for (c = 3; c < nch; c++)
                in[c] = 0.0f;

            if (gmt_apply_inverse(p, in, out) != GMT_OK)
                continue;

            ok = 1;
            for (c = 0; c < nch; c++) {
                if (out[c] < -0.01f || out[c] > 1.01f) {
                    ok = 0;
                    break;
                }
            }
            if (ok)
                best_c = test_c;
        }

        boundary_ab[count * 2 + 0] = best_c * ca;
        boundary_ab[count * 2 + 1] = best_c * sa;
        count++;
    }
    return count;
}

/* Compute the smoothness of the neutral axis by measuring the maximum
 * PCS deviation between adjacent steps. */
float gmt_neutral_smoothness(const gmt_profile *p, unsigned steps)
{
    unsigned pcs_ch = p ? p->pcs_channels : 0;
    float prev[GMT_MAX_CHANNELS], cur[GMT_MAX_CHANNELS];
    float in[GMT_MAX_CHANNELS];
    float max_dev = 0.0f;
    unsigned nch = p ? p->channels : 0;
    unsigned i, c;

    if (!p || nch == 0 || nch > GMT_MAX_CHANNELS || steps < 3)
        return 0.0f;

    for (c = 0; c < nch; c++)
        in[c] = 0.0f;
    gmt_apply(p, in, prev);

    for (i = 1; i < steps; i++) {
        float v = (float)i / (float)(steps - 1);
        for (c = 0; c < nch; c++)
            in[c] = v;
        if (gmt_apply(p, in, cur) != GMT_OK)
            continue;
        for (c = 0; c < pcs_ch; c++) {
            float d = cur[c] - prev[c];
            if (d < 0.0f) d = -d;
            if (d > max_dev) max_dev = d;
        }
        for (c = 0; c < pcs_ch; c++)
            prev[c] = cur[c];
    }
    return max_dev;
}
