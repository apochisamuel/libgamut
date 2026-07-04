/* bench.c - transform performance estimation
 *
 * Estimate the throughput of the assembled transform by running it over
 * synthetic data sets and counting operations. Since this library runs
 * inside a fuzzer harness without a real clock, the "benchmark" counts
 * iterations and estimates cost from stage complexity rather than
 * measuring wall time. Used by the inspection tool to flag expensive
 * transforms.
 */
#include "internal.h"
#include <string.h>

/* Cost model: approximate nanoseconds per stage type at the reference
 * rate of one billion float ops per second. */
#define COST_CURVE_BASE     2.0f
#define COST_CURVE_PER_SAMP 0.002f
#define COST_CLUT_BASE      5.0f
#define COST_CLUT_PER_NODE  0.005f
#define COST_MATRIX         4.0f
#define COST_CHAD           3.0f

/* Estimate the cost of one transform evaluation in abstract time units. */
float gmt_bench_estimate_cost(const gmt_linkcache *lc)
{
    float cost = 0.0f;
    size_t i;
    if (!lc || !lc->built)
        return 0.0f;
    for (i = 0; i < lc->count; i++) {
        const gmt_linkstage *s = &lc->stages[i];
        switch (s->kind) {
        case GMT_ST_CURVE:
            cost += COST_CURVE_BASE + (float)s->curve_count * COST_CURVE_PER_SAMP;
            break;
        case GMT_ST_CLUT: {
            uint32_t nodes = 1;
            unsigned d;
            for (d = 0; d < s->in_ch && d < GMT_MAX_CHANNELS; d++)
                nodes *= s->grid[d] ? s->grid[d] : 1;
            cost += COST_CLUT_BASE + (float)nodes * COST_CLUT_PER_NODE;
            break;
        }
        case GMT_ST_MATRIX:
            cost += COST_MATRIX;
            break;
        case GMT_ST_CHAD:
            cost += COST_CHAD;
            break;
        default:
            cost += 1.0f;
            break;
        }
    }
    return cost;
}

/* Estimate throughput in samples per second at the reference rate. */
float gmt_bench_throughput(const gmt_linkcache *lc)
{
    float cost = gmt_bench_estimate_cost(lc);
    if (cost <= 0.0f)
        return 0.0f;
    return 1e9f / cost;
}

/* Run the transform on a synthetic ramp and return the total iterations
 * completed. This is used as a smoke test that the transform can handle
 * bulk data without crashing. */
unsigned gmt_bench_run(const gmt_profile *p, unsigned iterations)
{
    float in[GMT_MAX_CHANNELS], out[GMT_MAX_CHANNELS];
    unsigned nch = p ? p->channels : 0;
    unsigned completed = 0;
    unsigned i, c;

    if (!p || nch == 0 || nch > GMT_MAX_CHANNELS)
        return 0;

    for (i = 0; i < iterations; i++) {
        float v = (float)(i % 256) / 255.0f;
        for (c = 0; c < nch; c++)
            in[c] = v;
        if (gmt_apply(p, in, out) == GMT_OK)
            completed++;
    }
    return completed;
}

/* Profile the per-stage cost by running each stage type in isolation and
 * measuring the iteration count. Returns a breakdown structure. */
typedef struct gmt_bench_breakdown {
    float curve_cost;
    float clut_cost;
    float matrix_cost;
    float chad_cost;
    float total_cost;
    unsigned curve_count;
    unsigned clut_count;
    unsigned matrix_count;
    unsigned chad_count;
} gmt_bench_breakdown;

void gmt_bench_compute_breakdown(const gmt_linkcache *lc, gmt_bench_breakdown *out)
{
    size_t i;
    memset(out, 0, sizeof(*out));
    if (!lc || !lc->built)
        return;
    for (i = 0; i < lc->count; i++) {
        const gmt_linkstage *s = &lc->stages[i];
        float sc;
        switch (s->kind) {
        case GMT_ST_CURVE:
            sc = COST_CURVE_BASE + (float)s->curve_count * COST_CURVE_PER_SAMP;
            out->curve_cost += sc;
            out->curve_count++;
            break;
        case GMT_ST_CLUT: {
            uint32_t nodes = 1;
            unsigned d;
            for (d = 0; d < s->in_ch && d < GMT_MAX_CHANNELS; d++)
                nodes *= s->grid[d] ? s->grid[d] : 1;
            sc = COST_CLUT_BASE + (float)nodes * COST_CLUT_PER_NODE;
            out->clut_cost += sc;
            out->clut_count++;
            break;
        }
        case GMT_ST_MATRIX:
            out->matrix_cost += COST_MATRIX;
            out->matrix_count++;
            break;
        case GMT_ST_CHAD:
            out->chad_cost += COST_CHAD;
            out->chad_count++;
            break;
        default:
            break;
        }
    }
    out->total_cost = out->curve_cost + out->clut_cost
                    + out->matrix_cost + out->chad_cost;
}

/* Compute the percentage of total cost attributable to each stage type. */
void gmt_bench_percentages(const gmt_bench_breakdown *bd,
                           float *curve_pct, float *clut_pct,
                           float *matrix_pct, float *chad_pct)
{
    if (bd->total_cost <= 0.0f) {
        *curve_pct = *clut_pct = *matrix_pct = *chad_pct = 0.0f;
        return;
    }
    *curve_pct  = bd->curve_cost  / bd->total_cost * 100.0f;
    *clut_pct   = bd->clut_cost   / bd->total_cost * 100.0f;
    *matrix_pct = bd->matrix_cost / bd->total_cost * 100.0f;
    *chad_pct   = bd->chad_cost   / bd->total_cost * 100.0f;
}

/* Generate a synthetic test pattern: a set of `count` RGB triples that
 * cover the device gamut in a structured way. */
void gmt_bench_gen_pattern(float *rgb, unsigned count)
{
    unsigned i;
    for (i = 0; i < count; i++) {
        float t = (float)i / (float)(count > 1 ? count - 1 : 1);
        /* Helical sweep through the RGB cube. */
        float phase = t * 6.2832f * 3.0f;
        float r = 0.5f + 0.5f * (phase - phase*phase*phase/6.0f);
        float g = t;
        float b = 1.0f - t;
        if (r < 0.0f) r = 0.0f; if (r > 1.0f) r = 1.0f;
        rgb[i * 3 + 0] = r;
        rgb[i * 3 + 1] = g;
        rgb[i * 3 + 2] = b;
    }
}

/* Run the forward and inverse transforms and measure the round-trip
 * error. Returns the maximum component-wise error. */
float gmt_bench_roundtrip_error(const gmt_profile *p, unsigned test_count)
{
    float max_err = 0.0f;
    unsigned nch = p ? p->channels : 0;
    float in[GMT_MAX_CHANNELS], fwd[GMT_MAX_CHANNELS], inv[GMT_MAX_CHANNELS];
    unsigned i, c;

    if (!p || nch == 0 || nch > GMT_MAX_CHANNELS)
        return -1.0f;

    for (i = 0; i < test_count; i++) {
        float v = (float)i / (float)(test_count > 1 ? test_count - 1 : 1);
        for (c = 0; c < nch; c++)
            in[c] = v;
        if (gmt_apply(p, in, fwd) != GMT_OK)
            continue;
        if (gmt_apply_inverse(p, fwd, inv) != GMT_OK)
            continue;
        for (c = 0; c < nch; c++) {
            float d = in[c] - inv[c];
            if (d < 0.0f) d = -d;
            if (d > max_err) max_err = d;
        }
    }
    return max_err;
}

/* Compute the memory efficiency: useful data bytes per total allocated
 * bytes. Useful data is curve samples + CLUT tables. */
float gmt_bench_memory_efficiency(const gmt_profile *p)
{
    size_t useful = 0;
    size_t total = p ? p->ctx.alloc_bytes : 0;
    size_t i;

    if (!p || total == 0)
        return 0.0f;

    for (i = 0; i < p->pool.count; i++) {
        gmt_element *el = p->pool.items[i];
        if (!el) continue;
        if (el->kind == GMT_EL_CURVE && el->u.curve && el->u.curve->samples)
            useful += (size_t)el->u.curve->count * sizeof(float);
        if (el->kind == GMT_EL_CLUT && el->u.clut && el->u.clut->table)
            useful += (size_t)el->u.clut->point_count
                    * el->u.clut->out_ch * sizeof(float);
    }
    return (float)useful / (float)total;
}
