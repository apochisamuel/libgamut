/* tagutil.c - tag directory utilities
 *
 * Helpers for working with the parsed tag directory: sorting, duplicate
 * detection, statistics, and iteration. These are used by the validation
 * phase, the inspection tool, and the cross-reference analysis.
 */
#include "internal.h"
#include <string.h>

/* Sort the tag array by signature using insertion sort (small arrays). */
void gmt_sort_tags(gmt_tag *tags, size_t count)
{
    size_t i, j;
    for (i = 1; i < count; i++) {
        gmt_tag key = tags[i];
        j = i;
        while (j > 0 && tags[j - 1].sig > key.sig) {
            tags[j] = tags[j - 1];
            j--;
        }
        tags[j] = key;
    }
}

/* Find duplicate signatures in the tag directory. Fills `dups` with the
 * signatures that appear more than once. Returns the count of duplicates. */
unsigned gmt_find_duplicate_sigs(const gmt_tag *tags, size_t count,
                                 uint32_t *dups, size_t cap)
{
    unsigned nd = 0;
    size_t i, j;
    for (i = 0; i < count && nd < cap; i++) {
        int is_dup = 0;
        /* Check if already recorded. */
        for (j = 0; j < nd; j++)
            if (dups[j] == tags[i].sig) { is_dup = 1; break; }
        if (is_dup)
            continue;
        /* Check if another entry has the same sig. */
        for (j = i + 1; j < count; j++)
            if (tags[j].sig == tags[i].sig) {
                dups[nd++] = tags[i].sig;
                break;
            }
    }
    return nd;
}

/* Compute tag directory statistics. */
typedef struct gmt_tagdir_stats {
    size_t   total_tags;
    size_t   resolved;       /* tags with pool_idx >= 0 */
    size_t   unresolved;
    size_t   shared;         /* tags sharing an offset with another */
    uint32_t min_offset;
    uint32_t max_end;        /* highest offset + size */
    size_t   total_data;     /* sum of all tag sizes */
} gmt_tagdir_stats;

void gmt_tagdir_statistics(const gmt_profile *p, gmt_tagdir_stats *out)
{
    size_t i, j;
    memset(out, 0, sizeof(*out));
    out->total_tags = p->tag_count;
    out->min_offset = 0xFFFFFFFFu;

    for (i = 0; i < p->tag_count; i++) {
        const gmt_tag *t = &p->tags[i];
        if (t->pool_idx >= 0)
            out->resolved++;
        else
            out->unresolved++;

        if (t->size > 0) {
            if (t->offset < out->min_offset)
                out->min_offset = t->offset;
            if (t->offset + t->size > out->max_end)
                out->max_end = t->offset + t->size;
            out->total_data += t->size;
        }

        /* Check sharing. */
        for (j = 0; j < p->tag_count; j++) {
            if (j != i && p->tags[j].offset == t->offset &&
                t->size > 0) {
                out->shared++;
                break;
            }
        }
    }
}

/* Find a tag by pool index (reverse lookup). */
const gmt_tag *gmt_find_tag_by_pool(const gmt_profile *p, int32_t idx)
{
    size_t i;
    for (i = 0; i < p->tag_count; i++)
        if (p->tags[i].pool_idx == idx)
            return &p->tags[i];
    return NULL;
}

/* Iterate over tags that resolved to a particular element kind. */
unsigned gmt_tags_of_kind(const gmt_profile *p, gmt_el_kind kind,
                          uint32_t *sigs, size_t cap)
{
    unsigned count = 0;
    size_t i;
    for (i = 0; i < p->tag_count && count < cap; i++) {
        gmt_element *el;
        if (p->tags[i].pool_idx < 0)
            continue;
        el = gmt_pool_at((gmt_elem_pool *)&p->pool, p->tags[i].pool_idx);
        if (el && el->kind == kind)
            sigs[count++] = p->tags[i].sig;
    }
    return count;
}

/* Compute the coverage of the tag data region as a fraction of the profile
 * size. A well-formed profile has high coverage (most bytes belong to a tag).
 * A low ratio may indicate padding or undeclared data. */
float gmt_tag_coverage(const gmt_profile *p)
{
    size_t covered = 0;
    size_t pos;
    size_t i;

    if (p->buf_size == 0)
        return 0.0f;

    /* Brute force: mark each byte that belongs to a tag body. Since this
     * is diagnostic code, simplicity is preferred over a bitmap approach. */
    for (pos = GMT_HEADER_SIZE; pos < p->buf_size; pos++) {
        for (i = 0; i < p->tag_count; i++) {
            if (p->tags[i].size == 0)
                continue;
            if (pos >= p->tags[i].offset &&
                pos < p->tags[i].offset + p->tags[i].size) {
                covered++;
                break;
            }
        }
    }
    return (float)covered / (float)p->buf_size;
}

/* Check whether any tag body extends past the profile size recorded in the
 * header. Returns the count of overrunning tags. */
unsigned gmt_check_tag_bounds(const gmt_profile *p)
{
    unsigned bad = 0;
    size_t i;
    for (i = 0; i < p->tag_count; i++) {
        const gmt_tag *t = &p->tags[i];
        if (t->size == 0)
            continue;
        if ((size_t)t->offset + t->size > p->buf_size)
            bad++;
    }
    return bad;
}
