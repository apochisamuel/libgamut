/* compare.c - profile comparison
 *
 * Compare two parsed profiles at the tag level and the element level. The
 * comparison reports which tags are present in one profile but not the
 * other, which tags reference different data, and whether the element
 * contents (curve samples, matrix coefficients, CLUT tables) match within
 * a tolerance.
 */
#include "internal.h"
#include <string.h>

typedef enum {
    GMT_CMP_MATCH = 0,
    GMT_CMP_MISSING_LEFT,
    GMT_CMP_MISSING_RIGHT,
    GMT_CMP_DIFFER
} gmt_cmp_result;

typedef struct gmt_cmp_entry {
    uint32_t       sig;
    gmt_cmp_result result;
    float          max_diff;    /* for numeric elements */
} gmt_cmp_entry;

#define GMT_CMP_MAX 256

typedef struct gmt_cmp_report {
    gmt_cmp_entry entries[GMT_CMP_MAX];
    unsigned      count;
    unsigned      match;
    unsigned      differ;
    unsigned      missing_left;
    unsigned      missing_right;
} gmt_cmp_report;

static int find_tag_sig(const gmt_profile *p, uint32_t sig)
{
    size_t i;
    for (i = 0; i < p->tag_count; i++)
        if (p->tags[i].sig == sig)
            return (int)i;
    return -1;
}

static float compare_curves(const gmt_curve *a, const gmt_curve *b)
{
    float max_d = 0.0f;
    uint32_t i;
    if (a->fn_type != b->fn_type)
        return 999.0f;
    if (a->fn_type != 0) {
        int k;
        for (k = 0; k < 7; k++) {
            float d = a->params[k] - b->params[k];
            if (d < 0.0f) d = -d;
            if (d > max_d) max_d = d;
        }
        return max_d;
    }
    if (a->count != b->count)
        return 999.0f;
    for (i = 0; i < a->count; i++) {
        float d = a->samples[i] - b->samples[i];
        if (d < 0.0f) d = -d;
        if (d > max_d) max_d = d;
    }
    return max_d;
}

static float compare_matrices(const gmt_matrix *a, const gmt_matrix *b)
{
    float max_d = 0.0f;
    int i;
    for (i = 0; i < 9; i++) {
        float d = a->e[i] - b->e[i];
        if (d < 0.0f) d = -d;
        if (d > max_d) max_d = d;
    }
    for (i = 0; i < 3; i++) {
        float d = a->off[i] - b->off[i];
        if (d < 0.0f) d = -d;
        if (d > max_d) max_d = d;
    }
    return max_d;
}

static float compare_cluts(const gmt_clut *a, const gmt_clut *b)
{
    float max_d = 0.0f;
    size_t i, total;
    if (a->in_ch != b->in_ch || a->out_ch != b->out_ch)
        return 999.0f;
    if (a->point_count != b->point_count)
        return 999.0f;
    total = (size_t)a->point_count * a->out_ch;
    for (i = 0; i < total; i++) {
        float d = a->table[i] - b->table[i];
        if (d < 0.0f) d = -d;
        if (d > max_d) max_d = d;
    }
    return max_d;
}

static float compare_elements(const gmt_element *a, const gmt_element *b)
{
    if (!a || !b || a->kind != b->kind)
        return 999.0f;
    switch (a->kind) {
    case GMT_EL_CURVE:
        return compare_curves(a->u.curve, b->u.curve);
    case GMT_EL_MATRIX:
        return compare_matrices(a->u.matrix, b->u.matrix);
    case GMT_EL_CLUT:
        return compare_cluts(a->u.clut, b->u.clut);
    case GMT_EL_XYZ: {
        float d, max_d = 0.0f;
        d = a->u.xyz->x - b->u.xyz->x; if (d<0) d=-d; if (d>max_d) max_d=d;
        d = a->u.xyz->y - b->u.xyz->y; if (d<0) d=-d; if (d>max_d) max_d=d;
        d = a->u.xyz->z - b->u.xyz->z; if (d<0) d=-d; if (d>max_d) max_d=d;
        return max_d;
    }
    case GMT_EL_TEXT:
        if (a->u.text && b->u.text)
            return (strcmp(a->u.text, b->u.text) == 0) ? 0.0f : 999.0f;
        return 999.0f;
    default:
        return 0.0f;
    }
}

void gmt_compare(const gmt_profile *a, const gmt_profile *b,
                 gmt_cmp_report *rpt)
{
    size_t i;
    memset(rpt, 0, sizeof(*rpt));

    /* Tags in A: check if they exist and match in B. */
    for (i = 0; i < a->tag_count && rpt->count < GMT_CMP_MAX; i++) {
        uint32_t sig = a->tags[i].sig;
        int bi = find_tag_sig(b, sig);
        gmt_cmp_entry *e = &rpt->entries[rpt->count++];
        e->sig = sig;
        if (bi < 0) {
            e->result = GMT_CMP_MISSING_RIGHT;
            e->max_diff = 0.0f;
            rpt->missing_right++;
        } else {
            gmt_element *ea = gmt_pool_at((gmt_elem_pool *)&a->pool,
                                          a->tags[i].pool_idx);
            gmt_element *eb = gmt_pool_at((gmt_elem_pool *)&b->pool,
                                          b->tags[bi].pool_idx);
            e->max_diff = compare_elements(ea, eb);
            if (e->max_diff < 0.001f) {
                e->result = GMT_CMP_MATCH;
                rpt->match++;
            } else {
                e->result = GMT_CMP_DIFFER;
                rpt->differ++;
            }
        }
    }

    /* Tags in B not in A. */
    for (i = 0; i < b->tag_count && rpt->count < GMT_CMP_MAX; i++) {
        uint32_t sig = b->tags[i].sig;
        if (find_tag_sig(a, sig) >= 0)
            continue;
        {
            gmt_cmp_entry *e = &rpt->entries[rpt->count++];
            e->sig = sig;
            e->result = GMT_CMP_MISSING_LEFT;
            e->max_diff = 0.0f;
            rpt->missing_left++;
        }
    }
}
