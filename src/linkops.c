/* linkops.c - transform chain analysis and optimization
 *
 * Analyze the assembled link cache for optimization opportunities: detect
 * identity stages that can be skipped, adjacent curves that can be composed,
 * and matrix chains that can be collapsed. Provides diagnostic information
 * about the transform pipeline.
 */
#include "internal.h"
#include <string.h>
#include <stdio.h>

/* Check whether a curve stage is an identity (passes input through
 * unchanged). An identity curve has either no samples or samples that
 * follow y=x within tolerance. */
int gmt_link_stage_is_identity_curve(const gmt_linkstage *s, float eps)
{
    uint32_t i;
    if (s->kind != GMT_ST_CURVE)
        return 0;
    if (!s->curve_samples || s->curve_count == 0)
        return 1;  /* no table = pass-through */
    for (i = 0; i < s->curve_count; i++) {
        float expected = (float)i / (float)(s->curve_count - 1);
        float diff = s->curve_samples[i] - expected;
        if (diff < 0.0f) diff = -diff;
        if (diff > eps)
            return 0;
    }
    return 1;
}

/* Check whether a matrix stage is an identity matrix. */
int gmt_link_stage_is_identity_matrix(const gmt_linkstage *s, float eps)
{
    int r, c;
    if (s->kind != GMT_ST_MATRIX || !s->matrix)
        return 0;
    for (r = 0; r < 3; r++)
        for (c = 0; c < 3; c++) {
            float expected = (r == c) ? 1.0f : 0.0f;
            float diff = s->matrix->e[r*3+c] - expected;
            if (diff < 0.0f) diff = -diff;
            if (diff > eps) return 0;
        }
    for (c = 0; c < 3; c++) {
        float diff = s->matrix->off[c];
        if (diff < 0.0f) diff = -diff;
        if (diff > eps) return 0;
    }
    return 1;
}

/* Count the number of identity stages in the link cache. */
unsigned gmt_link_identity_count(const gmt_linkcache *lc, float eps)
{
    unsigned count = 0;
    size_t i;
    for (i = 0; i < lc->count; i++) {
        if (gmt_link_stage_is_identity_curve(&lc->stages[i], eps))
            count++;
        if (gmt_link_stage_is_identity_matrix(&lc->stages[i], eps))
            count++;
    }
    return count;
}

/* Count adjacent curve stages (candidates for composition). */
unsigned gmt_link_adjacent_curves(const gmt_linkcache *lc)
{
    unsigned count = 0;
    size_t i;
    for (i = 1; i < lc->count; i++) {
        if (lc->stages[i-1].kind == GMT_ST_CURVE &&
            lc->stages[i].kind == GMT_ST_CURVE &&
            lc->stages[i-1].out_ch == lc->stages[i].in_ch)
            count++;
    }
    return count;
}

/* Count adjacent matrix stages (candidates for collapsing). */
unsigned gmt_link_adjacent_matrices(const gmt_linkcache *lc)
{
    unsigned count = 0;
    size_t i;
    for (i = 1; i < lc->count; i++) {
        if (lc->stages[i-1].kind == GMT_ST_MATRIX &&
            lc->stages[i].kind == GMT_ST_MATRIX)
            count++;
    }
    return count;
}

/* Compute the total sample memory used by curve stages. */
size_t gmt_link_curve_memory(const gmt_linkcache *lc)
{
    size_t total = 0;
    size_t i;
    for (i = 0; i < lc->count; i++)
        if (lc->stages[i].kind == GMT_ST_CURVE && lc->stages[i].curve_samples)
            total += (size_t)lc->stages[i].curve_count * sizeof(float);
    return total;
}

/* Compute the total table memory used by CLUT stages. */
size_t gmt_link_clut_memory(const gmt_linkcache *lc)
{
    size_t total = 0;
    size_t i;
    for (i = 0; i < lc->count; i++) {
        if (lc->stages[i].kind == GMT_ST_CLUT && lc->stages[i].clut_table) {
            uint32_t pts = 1;
            unsigned d;
            for (d = 0; d < lc->stages[i].in_ch; d++)
                pts *= lc->stages[i].grid[d] ? lc->stages[i].grid[d] : 1;
            total += (size_t)pts * lc->stages[i].out_ch * sizeof(float);
        }
    }
    return total;
}

/* Verify channel continuity through the transform chain. Returns 1 if
 * each stage's input channels match the previous stage's output. */
int gmt_link_chain_valid(const gmt_linkcache *lc)
{
    size_t i;
    if (lc->count < 2)
        return 1;
    for (i = 1; i < lc->count; i++) {
        uint8_t prev_out = lc->stages[i-1].out_ch;
        uint8_t cur_in = lc->stages[i].in_ch;
        /* Stages processing the same channel independently (curves) have
         * in_ch == out_ch == the channel index, not the total. For curves,
         * skip the chain continuity check. */
        if (lc->stages[i].kind == GMT_ST_CURVE ||
            lc->stages[i-1].kind == GMT_ST_CURVE)
            continue;
        if (cur_in != prev_out)
            return 0;
    }
    return 1;
}

/* Count stages by kind. */
void gmt_link_kind_counts(const gmt_linkcache *lc,
                          unsigned *curves, unsigned *cluts,
                          unsigned *matrices, unsigned *chads)
{
    size_t i;
    *curves = *cluts = *matrices = *chads = 0;
    for (i = 0; i < lc->count; i++) {
        switch (lc->stages[i].kind) {
        case GMT_ST_CURVE:  (*curves)++;  break;
        case GMT_ST_CLUT:   (*cluts)++;   break;
        case GMT_ST_MATRIX: (*matrices)++; break;
        case GMT_ST_CHAD:   (*chads)++;   break;
        }
    }
}

/* Estimate the computational cost of the transform as a weighted sum of
 * stage complexities. */
float gmt_link_complexity(const gmt_linkcache *lc)
{
    float cost = 0.0f;
    size_t i;
    for (i = 0; i < lc->count; i++) {
        switch (lc->stages[i].kind) {
        case GMT_ST_CURVE:
            cost += 1.0f + (float)lc->stages[i].curve_count * 0.001f;
            break;
        case GMT_ST_CLUT: {
            uint32_t pts = 1;
            unsigned d;
            for (d = 0; d < lc->stages[i].in_ch; d++)
                pts *= lc->stages[i].grid[d] ? lc->stages[i].grid[d] : 1;
            cost += 10.0f + (float)pts * 0.01f;
            break;
        }
        case GMT_ST_MATRIX:
            cost += 5.0f;  /* 9 multiplies + 9 adds + 3 offsets */
            break;
        case GMT_ST_CHAD:
            cost += 3.0f;  /* 3 divides + 2 multiplies */
            break;
        }
    }
    return cost;
}

/* Print a stage-by-stage summary of the transform chain. */
int gmt_link_format_chain(const gmt_linkcache *lc, char *buf, size_t cap)
{
    size_t pos = 0;
    size_t i;
    int n;
    const char *names[] = { "?", "curve", "matrix", "clut", "chad" };

    if (!lc || cap < 64) return -1;

    n = snprintf(buf, cap, "Transform: %zu stages, %u->%u ch\n",
                 lc->count, lc->in_ch, lc->out_ch);
    if (n > 0) pos += (size_t)n;

    for (i = 0; i < lc->count && pos < cap - 64; i++) {
        const gmt_linkstage *s = &lc->stages[i];
        int kind_idx = (int)s->kind;
        if (kind_idx < 0 || kind_idx > 4) kind_idx = 0;
        n = snprintf(buf + pos, cap - pos,
                     "  [%zu] %s %u->%u",
                     i, names[kind_idx], s->in_ch, s->out_ch);
        if (n > 0) pos += (size_t)n;
        if (s->kind == GMT_ST_CURVE) {
            n = snprintf(buf + pos, cap - pos, " samples=%u",
                         s->curve_count);
            if (n > 0) pos += (size_t)n;
        }
        if (pos < cap - 2) { buf[pos++] = '\n'; buf[pos] = '\0'; }
    }
    return 0;
}
