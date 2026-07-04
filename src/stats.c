/* stats.c - profile statistics and diagnostics
 *
 * Compute summary statistics over parsed profile data: element counts,
 * memory usage, sharing ratios, and quality metrics. Used by the
 * inspection tool and for regression testing.
 */
#include "internal.h"
#include <string.h>
#include <stdio.h>

typedef struct gmt_profile_stats {
    /* Tag statistics. */
    unsigned total_tags;
    unsigned resolved_tags;
    unsigned shared_tags;
    unsigned curve_tags;
    unsigned clut_tags;
    unsigned matrix_tags;
    unsigned xyz_tags;
    unsigned text_tags;
    unsigned lut_tags;
    unsigned mpet_tags;
    unsigned other_tags;

    /* Element pool statistics. */
    unsigned pool_elements;
    unsigned pool_curves;
    unsigned pool_cluts;
    unsigned pool_matrices;
    unsigned pool_xyzs;
    unsigned pool_texts;
    unsigned pool_luts;
    unsigned pool_mpets;

    /* Memory statistics. */
    size_t total_alloc;
    size_t curve_samples_mem;
    size_t clut_table_mem;
    size_t text_mem;

    /* Transform statistics. */
    unsigned link_stages;
    unsigned link_curve_stages;
    unsigned link_clut_stages;
    unsigned link_matrix_stages;
    unsigned link_chad_stages;

    /* Quality metrics. */
    float tag_coverage;
    int is_sorted;
    int has_duplicates;
} gmt_profile_stats;

void gmt_compute_stats(const gmt_profile *p, gmt_profile_stats *s)
{
    size_t i;
    memset(s, 0, sizeof(*s));
    if (!p) return;

    s->total_tags = (unsigned)p->tag_count;
    s->total_alloc = p->ctx.alloc_bytes;

    /* Per-tag analysis. */
    for (i = 0; i < p->tag_count; i++) {
        gmt_element *el;
        if (p->tags[i].pool_idx < 0) {
            s->other_tags++;
            continue;
        }
        s->resolved_tags++;
        el = gmt_pool_at((gmt_elem_pool *)&p->pool, p->tags[i].pool_idx);
        if (!el) { s->other_tags++; continue; }
        switch (el->kind) {
        case GMT_EL_CURVE:  s->curve_tags++;  break;
        case GMT_EL_CLUT:   s->clut_tags++;   break;
        case GMT_EL_MATRIX: s->matrix_tags++; break;
        case GMT_EL_XYZ:    s->xyz_tags++;    break;
        case GMT_EL_TEXT:   s->text_tags++;   break;
        case GMT_EL_LUT:    s->lut_tags++;    break;
        case GMT_EL_MPET:   s->mpet_tags++;   break;
        default:            s->other_tags++;  break;
        }
        /* Check sharing. */
        {
            size_t j;
            for (j = i + 1; j < p->tag_count; j++)
                if (p->tags[j].offset == p->tags[i].offset &&
                    p->tags[i].size > 0) {
                    s->shared_tags++;
                    break;
                }
        }
    }

    /* Pool element analysis. */
    s->pool_elements = (unsigned)p->pool.count;
    for (i = 0; i < p->pool.count; i++) {
        gmt_element *el = p->pool.items[i];
        if (!el) continue;
        switch (el->kind) {
        case GMT_EL_CURVE:
            s->pool_curves++;
            if (el->u.curve && el->u.curve->samples)
                s->curve_samples_mem +=
                    (size_t)el->u.curve->count * sizeof(float);
            break;
        case GMT_EL_CLUT:
            s->pool_cluts++;
            if (el->u.clut && el->u.clut->table)
                s->clut_table_mem +=
                    (size_t)el->u.clut->point_count
                    * el->u.clut->out_ch * sizeof(float);
            break;
        case GMT_EL_MATRIX: s->pool_matrices++; break;
        case GMT_EL_XYZ:    s->pool_xyzs++;     break;
        case GMT_EL_TEXT:
            s->pool_texts++;
            if (el->u.text)
                s->text_mem += strlen(el->u.text) + 1;
            break;
        case GMT_EL_LUT:    s->pool_luts++;  break;
        case GMT_EL_MPET:   s->pool_mpets++; break;
        default: break;
        }
    }

    /* Transform stage analysis. */
    s->link_stages = (unsigned)p->link.count;
    for (i = 0; i < p->link.count; i++) {
        switch (p->link.stages[i].kind) {
        case GMT_ST_CURVE:  s->link_curve_stages++;  break;
        case GMT_ST_CLUT:   s->link_clut_stages++;   break;
        case GMT_ST_MATRIX: s->link_matrix_stages++; break;
        case GMT_ST_CHAD:   s->link_chad_stages++;   break;
        default: break;
        }
    }

    /* Tag directory quality. */
    s->is_sorted = 1;
    for (i = 1; i < p->tag_count; i++)
        if (p->tags[i].sig < p->tags[i-1].sig) {
            s->is_sorted = 0;
            break;
        }

    s->has_duplicates = 0;
    for (i = 0; i < p->tag_count && !s->has_duplicates; i++) {
        size_t j;
        for (j = i + 1; j < p->tag_count; j++)
            if (p->tags[j].sig == p->tags[i].sig) {
                s->has_duplicates = 1;
                break;
            }
    }
}

/* Format profile statistics as a multi-line report. */
int gmt_format_stats(const gmt_profile_stats *s, char *buf, size_t cap)
{
    int n;
    if (!s || cap < 512)
        return -1;
    n = snprintf(buf, cap,
        "Tags: %u total, %u resolved, %u shared\n"
        "  curves=%u cluts=%u matrices=%u xyz=%u text=%u lut=%u mpet=%u\n"
        "Pool: %u elements\n"
        "  curves=%u cluts=%u matrices=%u xyz=%u texts=%u luts=%u mpets=%u\n"
        "Memory: %zu alloc, %zu curve, %zu clut, %zu text\n"
        "Transform: %u stages (curve=%u clut=%u matrix=%u chad=%u)\n"
        "Quality: sorted=%s duplicates=%s\n",
        s->total_tags, s->resolved_tags, s->shared_tags,
        s->curve_tags, s->clut_tags, s->matrix_tags, s->xyz_tags,
        s->text_tags, s->lut_tags, s->mpet_tags,
        s->pool_elements,
        s->pool_curves, s->pool_cluts, s->pool_matrices, s->pool_xyzs,
        s->pool_texts, s->pool_luts, s->pool_mpets,
        s->total_alloc, s->curve_samples_mem, s->clut_table_mem, s->text_mem,
        s->link_stages, s->link_curve_stages, s->link_clut_stages,
        s->link_matrix_stages, s->link_chad_stages,
        s->is_sorted ? "yes" : "no",
        s->has_duplicates ? "yes" : "no");
    return (n > 0 && (size_t)n < cap) ? 0 : -1;
}
