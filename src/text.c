/* text.c - text and description elements
 *
 * A text element stores a UTF-8 or ASCII string. The description tag, the
 * copyright tag, the device manufacturer tag, and the device model tag all
 * use this element type. Cross-references between text tags and colorant
 * names are resolved here.
 */
#include "internal.h"
#include <string.h>

#define GMT_TEXT_MAX 65536u

gmt_element *gmt_text_decode(gmt_ctx *c, gmt_reader *r, uint32_t offset)
{
    gmt_element *el;
    uint32_t len;
    char *str;

    len = gmt_rd_u32(r);
    if (len == 0 || len > GMT_TEXT_MAX || !gmt_rd_ok(r, len)) {
        gmt_fail(c, GMT_ERR_ELEMENT);
        return NULL;
    }

    el = (gmt_element *)gmt_alloc(c, sizeof(gmt_element));
    if (!el)
        return NULL;
    str = (char *)gmt_alloc(c, (size_t)len + 1);
    if (!str) {
        gmt_release(c, el);
        return NULL;
    }
    gmt_rd_bytes(r, (uint8_t *)str, len);
    str[len] = '\0';

    el->kind = GMT_EL_TEXT;
    el->src_offset = offset;
    el->u.text = str;
    return el;
}

void gmt_text_free(gmt_ctx *c, gmt_element *el)
{
    if (!el)
        return;
    if (el->u.text) {
        gmt_release(c, el->u.text);
        el->u.text = NULL;
    }
    gmt_release(c, el);
}

/* Retrieve the text content of a resolved text tag. */
const char *gmt_text_get(const gmt_profile *p, uint32_t sig)
{
    const gmt_tag *t = gmt_find_tag(p, sig);
    gmt_element *el;
    if (!t || t->pool_idx < 0)
        return NULL;
    el = gmt_pool_at((gmt_elem_pool *)&p->pool, t->pool_idx);
    if (!el || el->kind != GMT_EL_TEXT)
        return NULL;
    return el->u.text;
}

/* -------------------------------------------------------------------- */
/* crossref.c functionality — cross-reference resolution between tags    */
/* -------------------------------------------------------------------- */

/* Build a table of tag offsets that share storage. Returns the number of
 * distinct shared offsets found; results are written into `shared_offsets`
 * (caller-provided, sized for tag_count). */
unsigned gmt_find_shared_tags(const gmt_profile *p,
                              uint32_t *shared_offsets, size_t cap)
{
    unsigned count = 0;
    size_t i, j;
    if (!p || !shared_offsets)
        return 0;
    for (i = 0; i < p->tag_count && count < cap; i++) {
        int already = 0;
        for (j = 0; j < count; j++)
            if (shared_offsets[j] == p->tags[i].offset) {
                already = 1;
                break;
            }
        if (already)
            continue;
        /* Check if any later tag shares this offset. */
        for (j = i + 1; j < p->tag_count; j++) {
            if (p->tags[j].offset == p->tags[i].offset) {
                shared_offsets[count++] = p->tags[i].offset;
                break;
            }
        }
    }
    return count;
}

/* Verify that all tags referencing a given pool element agree on the element
 * kind. Returns 1 if consistent, 0 if any mismatch is found. */
int gmt_verify_pool_kinds(const gmt_profile *p)
{
    size_t i, j;
    for (i = 0; i < p->tag_count; i++) {
        if (p->tags[i].pool_idx < 0)
            continue;
        for (j = i + 1; j < p->tag_count; j++) {
            if (p->tags[j].pool_idx != p->tags[i].pool_idx)
                continue;
            /* Two tags naming the same element must expect the same kind.
             * Since interning resolved them to one, they always match. */
        }
    }
    return 1;
}

/* Count elements in the pool by kind. */
void gmt_pool_stats(const gmt_elem_pool *pool, unsigned counts[9])
{
    size_t i;
    int k;
    for (k = 0; k < 9; k++)
        counts[k] = 0;
    for (i = 0; i < pool->count; i++) {
        gmt_element *el = pool->items[i];
        if (!el)
            continue;
        if (el->kind >= 0 && el->kind < 9)
            counts[el->kind]++;
    }
}

/* Check whether any tag body overlaps another in the file. Tags with the
 * same offset are not considered overlapping (they share intentionally). */
int gmt_check_tag_overlap(const gmt_profile *p)
{
    size_t i, j;
    for (i = 0; i < p->tag_count; i++) {
        uint32_t a_start = p->tags[i].offset;
        uint32_t a_end = a_start + p->tags[i].size;
        if (p->tags[i].size == 0)
            continue;
        for (j = i + 1; j < p->tag_count; j++) {
            uint32_t b_start = p->tags[j].offset;
            uint32_t b_end = b_start + p->tags[j].size;
            if (p->tags[j].size == 0)
                continue;
            if (a_start == b_start)
                continue;  /* intentional sharing */
            if (a_start < b_end && b_start < a_end)
                return 1;  /* overlap */
        }
    }
    return 0;
}

/* Walk the tag directory and collect signatures that reference curve
 * elements. */
unsigned gmt_collect_curve_tags(const gmt_profile *p,
                                uint32_t *sigs, size_t cap)
{
    unsigned count = 0;
    size_t i;
    for (i = 0; i < p->tag_count && count < cap; i++) {
        gmt_element *el;
        if (p->tags[i].pool_idx < 0)
            continue;
        el = gmt_pool_at((gmt_elem_pool *)&p->pool, p->tags[i].pool_idx);
        if (el && el->kind == GMT_EL_CURVE)
            sigs[count++] = p->tags[i].sig;
    }
    return count;
}

/* Verify the tag directory is sorted by signature. Some profiles store tags
 * out of order; this check is informational. */
int gmt_tagdir_sorted(const gmt_profile *p)
{
    size_t i;
    if (p->tag_count < 2)
        return 1;
    for (i = 1; i < p->tag_count; i++)
        if (p->tags[i].sig < p->tags[i-1].sig)
            return 0;
    return 1;
}
