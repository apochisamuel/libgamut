/* metrics.c - profile quality metrics
 *
 * Compute quantitative quality metrics over a parsed profile's transform:
 * total ink coverage (TAC), gray balance deviation, TRC linearity and
 * smoothness, colorant separation, and overall profile grade. These
 * metrics are used by the inspection tool and for automated quality
 * gating.
 */
#include "internal.h"
#include <string.h>

/* Compute Total Area Coverage (TAC): the maximum sum of device channel
 * values across a grid of PCS inputs. Relevant for CMYK profiles where
 * excessive ink causes bleed-through. */
float gmt_metric_tac(const gmt_profile *p, unsigned grid_size)
{
    float max_tac = 0.0f;
    unsigned nch = p ? p->channels : 0;
    float in[GMT_MAX_CHANNELS], out[GMT_MAX_CHANNELS];
    unsigned ri, gi, bi;

    if (!p || nch < 3 || nch > GMT_MAX_CHANNELS || grid_size < 2)
        return 0.0f;

    for (ri = 0; ri < grid_size; ri++)
        for (gi = 0; gi < grid_size; gi++)
            for (bi = 0; bi < grid_size; bi++) {
                float tac = 0.0f;
                unsigned c;
                in[0] = (float)ri / (float)(grid_size - 1);
                in[1] = (float)gi / (float)(grid_size - 1);
                in[2] = (float)bi / (float)(grid_size - 1);
                for (c = 3; c < nch; c++)
                    in[c] = 0.0f;
                if (gmt_apply_inverse(p, in, out) != GMT_OK)
                    continue;
                for (c = 0; c < nch; c++) {
                    float v = out[c];
                    if (v < 0.0f) v = 0.0f;
                    if (v > 1.0f) v = 1.0f;
                    tac += v;
                }
                if (tac > max_tac)
                    max_tac = tac;
            }

    return max_tac * 100.0f;   /* percentage */
}

/* Compute gray balance deviation: how far the neutral axis deviates from
 * the achromatic axis in the PCS. Returns the maximum Delta-a/Delta-b
 * across a ramp of equal device values. */
float gmt_metric_gray_balance(const gmt_profile *p, unsigned steps)
{
    float max_dev = 0.0f;
    unsigned nch = p ? p->channels : 0;
    float in[GMT_MAX_CHANNELS], out[GMT_MAX_CHANNELS];
    unsigned i, c;

    if (!p || nch == 0 || nch > GMT_MAX_CHANNELS || steps < 2)
        return 0.0f;

    for (i = 0; i < steps; i++) {
        float v = (float)i / (float)(steps - 1);
        float dev;
        for (c = 0; c < nch; c++)
            in[c] = v;
        if (gmt_apply(p, in, out) != GMT_OK)
            continue;
        /* Assume PCS has at least 3 channels; a* and b* are channels 1,2. */
        if (p->pcs_channels >= 3) {
            float a_dev = out[1];  /* ideal neutral: a* = 0 */
            float b_dev = out[2];  /* ideal neutral: b* = 0 */
            if (a_dev < 0.0f) a_dev = -a_dev;
            if (b_dev < 0.0f) b_dev = -b_dev;
            dev = (a_dev > b_dev) ? a_dev : b_dev;
            if (dev > max_dev) max_dev = dev;
        }
    }
    return max_dev;
}

/* Compute per-channel TRC linearity: the RMS deviation of each TRC from
 * a straight line. Returns the average across channels. */
float gmt_metric_trc_linearity(const gmt_profile *p, unsigned steps)
{
    float total_dev = 0.0f;
    unsigned nch = p ? p->channels : 0;
    unsigned ch, i;
    unsigned valid_channels = 0;

    if (!p || nch == 0 || nch > GMT_MAX_CHANNELS || steps < 2)
        return 0.0f;

    for (ch = 0; ch < nch && ch < 3; ch++) {
        float sum_sq = 0.0f;
        float in[GMT_MAX_CHANNELS], out[GMT_MAX_CHANNELS];

        for (i = 0; i < steps; i++) {
            unsigned c;
            float v = (float)i / (float)(steps - 1);
            float expected = v;
            float actual;
            for (c = 0; c < nch; c++)
                in[c] = 0.0f;
            in[ch] = v;
            if (gmt_apply(p, in, out) != GMT_OK)
                continue;
            actual = (ch < p->pcs_channels) ? out[ch] : 0.0f;
            {
                float d = actual - expected;
                sum_sq += d * d;
            }
        }
        {
            /* sqrt via Newton */
            float rms = sum_sq / (float)steps;
            float y = rms;
            int k;
            if (y > 0.0f) {
                for (k = 0; k < 8; k++)
                    y = (y + rms / y) * 0.5f;
            }
            total_dev += y;
            valid_channels++;
        }
    }
    return (valid_channels > 0) ? total_dev / (float)valid_channels : 0.0f;
}

/* Compute the maximum channel crosstalk: how much changing one input
 * channel affects other output channels. */
float gmt_metric_crosstalk(const gmt_profile *p, unsigned steps)
{
    float max_ct = 0.0f;
    unsigned nch = p ? p->channels : 0;
    unsigned pcs_ch = p ? p->pcs_channels : 0;
    float in0[GMT_MAX_CHANNELS], out0[GMT_MAX_CHANNELS];
    float in1[GMT_MAX_CHANNELS], out1[GMT_MAX_CHANNELS];
    unsigned ch, i, pc;

    if (!p || nch == 0 || nch > GMT_MAX_CHANNELS || steps < 2)
        return 0.0f;

    for (ch = 0; ch < nch && ch < 4; ch++) {
        for (i = 1; i < steps; i++) {
            float v0 = (float)(i - 1) / (float)(steps - 1);
            float v1 = (float)i / (float)(steps - 1);
            unsigned c;

            for (c = 0; c < nch; c++)
                in0[c] = in1[c] = 0.5f;
            in0[ch] = v0;
            in1[ch] = v1;

            if (gmt_apply(p, in0, out0) != GMT_OK)
                continue;
            if (gmt_apply(p, in1, out1) != GMT_OK)
                continue;

            for (pc = 0; pc < pcs_ch; pc++) {
                if (pc == ch)
                    continue;  /* skip the driven channel */
                {
                    float d = out1[pc] - out0[pc];
                    if (d < 0.0f) d = -d;
                    if (d > max_ct) max_ct = d;
                }
            }
        }
    }
    return max_ct;
}

/* Compute an overall quality grade from the individual metrics.
 * Grade is 0.0 (poor) to 1.0 (excellent). */
float gmt_metric_grade(float gray_balance, float trc_linearity,
                       float crosstalk)
{
    float score = 1.0f;

    /* Penalty for gray balance deviation. */
    if (gray_balance > 5.0f)
        score -= 0.3f;
    else if (gray_balance > 2.0f)
        score -= 0.1f;

    /* Penalty for TRC nonlinearity. */
    if (trc_linearity > 0.1f)
        score -= 0.2f;
    else if (trc_linearity > 0.05f)
        score -= 0.1f;

    /* Penalty for channel crosstalk. */
    if (crosstalk > 0.1f)
        score -= 0.2f;
    else if (crosstalk > 0.05f)
        score -= 0.1f;

    if (score < 0.0f) score = 0.0f;
    return score;
}

/* Batch compute all metrics for a profile. */
typedef struct gmt_metrics {
    float tac;
    float gray_balance;
    float trc_linearity;
    float crosstalk;
    float grade;
} gmt_metrics;

void gmt_compute_metrics(const gmt_profile *p, unsigned grid,
                         gmt_metrics *out)
{
    memset(out, 0, sizeof(*out));
    if (!p) return;
    out->tac = gmt_metric_tac(p, grid > 8 ? 8 : grid);
    out->gray_balance = gmt_metric_gray_balance(p, grid);
    out->trc_linearity = gmt_metric_trc_linearity(p, grid);
    out->crosstalk = gmt_metric_crosstalk(p, grid);
    out->grade = gmt_metric_grade(out->gray_balance, out->trc_linearity,
                                  out->crosstalk);
}
