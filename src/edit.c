/* edit.c - profile editing operations
 *
 * Modify a parsed profile by adding, removing, or replacing tags and their
 * elements. These operations work on the in-memory representation; the
 * serializer converts the result back to a binary buffer. The editor is
 * used by the round-trip test and by tools that adjust profile metadata.
 */
#include "internal.h"
#include <string.h>
#include <stdlib.h>

/* Remove a tag by signature. Returns 0 if the tag was found and removed,
 * -1 if it was not present. The element in the pool is left intact; other
 * tags may reference the same element. */
int gmt_edit_remove_tag(gmt_profile *p, uint32_t sig)
{
    size_t i, j;
    int found = -1;
    for (i = 0; i < p->tag_count; i++) {
        if (p->tags[i].sig == sig) {
            found = 0;
            for (j = i; j + 1 < p->tag_count; j++)
                p->tags[j] = p->tags[j + 1];
            p->tag_count--;
            break;
        }
    }
    return found;
}

/* Add a tag that references an existing pool element. Returns 0 on success,
 * -1 if the tag array cannot grow. */
int gmt_edit_add_tag(gmt_profile *p, uint32_t sig, int32_t pool_idx)
{
    gmt_tag *nt;
    size_t new_count = p->tag_count + 1;

    nt = (gmt_tag *)gmt_realloc(&p->ctx, p->tags,
                                new_count * sizeof(gmt_tag));
    if (!nt)
        return -1;
    p->tags = nt;
    p->tags[p->tag_count].sig = sig;
    p->tags[p->tag_count].offset = 0;
    p->tags[p->tag_count].size = 0;
    p->tags[p->tag_count].pool_idx = pool_idx;
    p->tag_count = new_count;
    return 0;
}

/* Replace the pool element of a tag. The previous element is left intact. */
int gmt_edit_replace_element(gmt_profile *p, uint32_t sig, int32_t new_idx)
{
    size_t i;
    for (i = 0; i < p->tag_count; i++) {
        if (p->tags[i].sig == sig) {
            p->tags[i].pool_idx = new_idx;
            return 0;
        }
    }
    return -1;
}

/* Set the profile's device color space. */
void gmt_edit_set_device_space(gmt_profile *p, gmt_colorspace cs,
                               unsigned nch)
{
    p->device_space = cs;
    p->channels = nch;
}

/* Set the profile connection space. */
void gmt_edit_set_pcs(gmt_profile *p, gmt_colorspace cs)
{
    p->pcs = cs;
    switch (cs) {
    case GMT_CS_LAB: p->pcs_channels = 3; break;
    case GMT_CS_XYZ: p->pcs_channels = 3; break;
    default:         p->pcs_channels = 3; break;
    }
}

/* Set the profile version. */
void gmt_edit_set_version(gmt_profile *p, uint32_t version)
{
    p->version = version;
}

/* Create a new curve element and add it to the pool. Returns the pool
 * index, or -1 on failure. */
int32_t gmt_edit_add_curve(gmt_profile *p, const float *samples,
                           uint32_t count)
{
    gmt_element *el;
    gmt_curve *cv;
    float *copy;
    uint32_t i;

    if (count < 2 || count > 65536 || !samples)
        return -1;

    el = (gmt_element *)gmt_alloc(&p->ctx, sizeof(gmt_element));
    if (!el) return -1;
    cv = (gmt_curve *)gmt_alloc(&p->ctx, sizeof(gmt_curve));
    if (!cv) { gmt_release(&p->ctx, el); return -1; }
    copy = (float *)gmt_alloc(&p->ctx, (size_t)count * sizeof(float));
    if (!copy) {
        gmt_release(&p->ctx, cv);
        gmt_release(&p->ctx, el);
        return -1;
    }
    for (i = 0; i < count; i++)
        copy[i] = samples[i];

    cv->fn_type = 0;
    cv->count = count;
    cv->samples = copy;
    memset(cv->params, 0, sizeof(cv->params));

    el->kind = GMT_EL_CURVE;
    el->src_offset = 0;
    el->hold = 0;
    el->u.curve = cv;

    if (!gmt_pool_add(&p->ctx, &p->pool, el)) {
        gmt_release(&p->ctx, copy);
        gmt_release(&p->ctx, cv);
        gmt_release(&p->ctx, el);
        return -1;
    }
    return (int32_t)(p->pool.count - 1);
}

/* Create a gamma curve and add it to the pool. */
int32_t gmt_edit_add_gamma(gmt_profile *p, float gamma)
{
    gmt_element *el;
    gmt_curve *cv;

    el = (gmt_element *)gmt_alloc(&p->ctx, sizeof(gmt_element));
    if (!el) return -1;
    cv = (gmt_curve *)gmt_alloc(&p->ctx, sizeof(gmt_curve));
    if (!cv) { gmt_release(&p->ctx, el); return -1; }

    cv->fn_type = 1;
    cv->count = 0;
    cv->samples = NULL;
    cv->params[0] = gamma;
    cv->params[1] = 1.0f;
    cv->params[2] = 0.0f;
    memset(&cv->params[3], 0, 4 * sizeof(float));

    el->kind = GMT_EL_CURVE;
    el->src_offset = 0;
    el->hold = 0;
    el->u.curve = cv;

    if (!gmt_pool_add(&p->ctx, &p->pool, el)) {
        gmt_release(&p->ctx, cv);
        gmt_release(&p->ctx, el);
        return -1;
    }
    return (int32_t)(p->pool.count - 1);
}

/* Create an XYZ element and add it to the pool. */
int32_t gmt_edit_add_xyz(gmt_profile *p, float x, float y, float z)
{
    gmt_element *el;
    gmt_xyz *xyz;

    el = (gmt_element *)gmt_alloc(&p->ctx, sizeof(gmt_element));
    if (!el) return -1;
    xyz = (gmt_xyz *)gmt_alloc(&p->ctx, sizeof(gmt_xyz));
    if (!xyz) { gmt_release(&p->ctx, el); return -1; }

    xyz->x = x;
    xyz->y = y;
    xyz->z = z;
    el->kind = GMT_EL_XYZ;
    el->src_offset = 0;
    el->hold = 0;
    el->u.xyz = xyz;

    if (!gmt_pool_add(&p->ctx, &p->pool, el)) {
        gmt_release(&p->ctx, xyz);
        gmt_release(&p->ctx, el);
        return -1;
    }
    return (int32_t)(p->pool.count - 1);
}

/* Create a matrix element and add it to the pool. */
int32_t gmt_edit_add_matrix(gmt_profile *p, const float e[9],
                            const float off[3])
{
    gmt_element *el;
    gmt_matrix *m;

    el = (gmt_element *)gmt_alloc(&p->ctx, sizeof(gmt_element));
    if (!el) return -1;
    m = (gmt_matrix *)gmt_alloc(&p->ctx, sizeof(gmt_matrix));
    if (!m) { gmt_release(&p->ctx, el); return -1; }

    memcpy(m->e, e, 9 * sizeof(float));
    memcpy(m->off, off, 3 * sizeof(float));
    m->rows = m->cols = 3;
    el->kind = GMT_EL_MATRIX;
    el->src_offset = 0;
    el->hold = 0;
    el->u.matrix = m;

    if (!gmt_pool_add(&p->ctx, &p->pool, el)) {
        gmt_release(&p->ctx, m);
        gmt_release(&p->ctx, el);
        return -1;
    }
    return (int32_t)(p->pool.count - 1);
}

/* Create a text element and add it to the pool. */
int32_t gmt_edit_add_text(gmt_profile *p, const char *text)
{
    gmt_element *el;
    char *copy;
    size_t len;

    if (!text) return -1;
    len = strlen(text);
    if (len == 0 || len > 65536) return -1;

    el = (gmt_element *)gmt_alloc(&p->ctx, sizeof(gmt_element));
    if (!el) return -1;
    copy = (char *)gmt_alloc(&p->ctx, len + 1);
    if (!copy) { gmt_release(&p->ctx, el); return -1; }
    memcpy(copy, text, len + 1);

    el->kind = GMT_EL_TEXT;
    el->src_offset = 0;
    el->hold = 0;
    el->u.text = copy;

    if (!gmt_pool_add(&p->ctx, &p->pool, el)) {
        gmt_release(&p->ctx, copy);
        gmt_release(&p->ctx, el);
        return -1;
    }
    return (int32_t)(p->pool.count - 1);
}

/* Duplicate a profile by serializing and re-parsing. Returns a new handle
 * or NULL. */
gmt_profile *gmt_edit_clone(const gmt_profile *p)
{
    size_t cap;
    uint8_t *buf;
    size_t written;
    gmt_profile *clone;

    if (!p) return NULL;
    cap = p->buf_size * 2;
    if (cap < 4096) cap = 4096;
    buf = (uint8_t *)malloc(cap);
    if (!buf) return NULL;

    written = gmt_serialize(p, buf, cap);
    if (written == 0) {
        free(buf);
        return NULL;
    }
    clone = gmt_open_memory(buf, written);
    free(buf);
    return clone;
}
