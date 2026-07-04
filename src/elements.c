/* elements.c - Phase 3: element pool and parse dispatch
 *
 * Each tag body is decoded into an element. Elements are kept in a pool
 * keyed by the file offset they were decoded from. When several tags name
 * the same offset, the first decode populates the pool and subsequent tags
 * resolve to the element already present, so one element backs every tag
 * that points at that offset.
 */
#include "internal.h"
#include <stdlib.h>

#define GMT_POOL_START 8

void gmt_pool_init(gmt_elem_pool *pool)
{
    pool->items = NULL;
    pool->count = 0;
    pool->cap = 0;
}

int32_t gmt_pool_find(gmt_elem_pool *pool, uint32_t src_offset)
{
    size_t i;
    for (i = 0; i < pool->count; i++)
        if (pool->items[i] && pool->items[i]->src_offset == src_offset)
            return (int32_t)i;
    return -1;
}

gmt_element *gmt_pool_get(gmt_elem_pool *pool, uint32_t src_offset)
{
    int32_t idx = gmt_pool_find(pool, src_offset);
    return idx < 0 ? NULL : pool->items[idx];
}

gmt_element *gmt_pool_at(gmt_elem_pool *pool, int32_t index)
{
    if (index < 0 || (size_t)index >= pool->count)
        return NULL;
    return pool->items[index];
}

gmt_element *gmt_pool_add(gmt_ctx *c, gmt_elem_pool *pool, gmt_element *el)
{
    if (pool->count == pool->cap) {
        size_t ncap = pool->cap ? pool->cap * 2 : GMT_POOL_START;
        gmt_element **ni = (gmt_element **)gmt_realloc(
            c, pool->items, ncap * sizeof(gmt_element *));
        if (!ni)
            return NULL;
        pool->items = ni;
        pool->cap = ncap;
    }
    pool->items[pool->count++] = el;
    return el;
}

/* Decode the element that begins at `offset`, or return the index of the
 * element already decoded for that offset. Returns a pool index or -1. */
int32_t gmt_parse_element_at(gmt_profile *p, uint32_t offset)
{
    gmt_reader r;
    uint32_t type;
    gmt_element *el = NULL;
    int32_t idx;

    idx = gmt_pool_find(&p->pool, offset);
    if (idx >= 0) {
        gmt_element *ex = p->pool.items[idx];
        if (ex)
            ex->hold++;         /* another tag resolves to this element */
        return idx;
    }

    if ((size_t)offset + 4 > p->buf_size)
        return -1;

    /* Some elements reference other elements by offset; bound the nesting
     * so a self-referential offset cannot recurse without limit. */
    if (p->ctx.parse_depth >= 8)
        return -1;
    p->ctx.parse_depth++;

    gmt_rd_init(&r, p->buf, p->buf_size);
    gmt_rd_seek(&r, offset);
    type = gmt_rd_u32(&r);

    switch (type) {
    case GMT_SIG('c', 'u', 'r', 'v'):
        el = gmt_curve_decode(&p->ctx, &r, offset);
        break;
    case GMT_SIG('c', 'l', 'u', 't'):
        el = gmt_clut_decode(&p->ctx, &r, offset);
        break;
    case GMT_SIG('m', 'A', 'B', ' '):
        el = gmt_lut_decode(p, &r, offset);
        break;
    case GMT_SIG('m', 'p', 'e', 't'):
        el = gmt_mpet_decode(p, &r, offset);
        break;
    case GMT_SIG('X', 'Y', 'Z', ' '):
        el = gmt_xyz_decode(&p->ctx, &r, offset);
        break;
    case GMT_SIG('m', 'a', 't', 'x'):
        el = gmt_matrix_decode(&p->ctx, &r, offset);
        break;
    case GMT_SIG('d', 'e', 's', 'c'):
    case GMT_SIG('c', 'p', 'r', 't'):
    case GMT_SIG('d', 'm', 'n', 'd'):
    case GMT_SIG('d', 'm', 'd', 'd'):
        el = gmt_text_decode(&p->ctx, &r, offset);
        break;
    default:
        /* Unrecognized element bodies are skipped; later phases treat the
         * tag as unresolved. */
        p->ctx.parse_depth--;
        return -1;
    }

    p->ctx.parse_depth--;

    if (!el)
        return -1;

    el->src_offset = offset;
    el->hold = 1;
    if (!gmt_pool_add(&p->ctx, &p->pool, el)) {
        /* pool grow failed; the caller records the fatal status */
        return -1;
    }
    return (int32_t)(p->pool.count - 1);
}

int gmt_parse_elements(gmt_profile *p)
{
    size_t i;
    gmt_pool_init(&p->pool);

    for (i = 0; i < p->tag_count; i++) {
        gmt_tag *t = &p->tags[i];
        if (t->size == 0)
            continue;
        t->pool_idx = gmt_parse_element_at(p, t->offset);
    }
    return p->ctx.fatal ? -1 : 0;
}

void gmt_pool_free(gmt_ctx *c, gmt_elem_pool *pool)
{
    size_t i;
    for (i = 0; i < pool->count; i++) {
        gmt_element *el = pool->items[i];
        if (!el)
            continue;
        switch (el->kind) {
        case GMT_EL_CURVE:
            gmt_curve_free(c, el);
            break;
        case GMT_EL_CLUT:
            gmt_clut_free(c, el);
            break;
        case GMT_EL_LUT:
            gmt_lut_free(c, el);
            break;
        case GMT_EL_MPET:
            gmt_mpet_free(c, el);
            break;
        case GMT_EL_XYZ:
            gmt_xyz_free(c, el);
            break;
        case GMT_EL_MATRIX:
            gmt_matrix_free(c, el);
            break;
        case GMT_EL_TEXT:
            gmt_text_free(c, el);
            break;
        default:
            gmt_release(c, el);
            break;
        }
        pool->items[i] = NULL;
    }
    gmt_release(c, pool->items);
    pool->items = NULL;
    pool->count = pool->cap = 0;
}
