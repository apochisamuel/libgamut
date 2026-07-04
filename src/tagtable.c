/* tagtable.c - Phase 2: tag directory
 *
 * Immediately after the 128-byte header the directory lists the tags in the
 * profile. It begins with a count, then that many (signature, offset, size)
 * triples. Offsets are measured from the start of the profile. Two entries
 * are permitted to name the same offset; the element phase resolves each
 * offset to a single element.
 */
#include "internal.h"
#include <stdlib.h>

#define GMT_TAGDIR_OFFSET GMT_HEADER_SIZE
#define GMT_MAX_TAGS      4096u

int gmt_parse_tagtable(gmt_profile *p)
{
    gmt_reader r;
    uint32_t count, i;

    gmt_rd_init(&r, p->buf, p->buf_size);
    gmt_rd_seek(&r, GMT_TAGDIR_OFFSET);
    if (!gmt_rd_ok(&r, 4)) {
        gmt_fail_fatal(&p->ctx, GMT_ERR_TRUNCATED);
        return -1;
    }

    count = gmt_rd_u32(&r);
    if (count > GMT_MAX_TAGS) {
        gmt_fail_fatal(&p->ctx, GMT_ERR_TAGDIR);
        return -1;
    }
    if (count == 0) {
        p->tags = NULL;
        p->tag_count = 0;
        return 0;
    }
    if (!gmt_rd_ok(&r, (size_t)count * 12u)) {
        gmt_fail_fatal(&p->ctx, GMT_ERR_TRUNCATED);
        return -1;
    }

    p->tags = (gmt_tag *)gmt_alloc(&p->ctx, (size_t)count * sizeof(gmt_tag));
    if (!p->tags)
        return -1;

    for (i = 0; i < count; i++) {
        gmt_tag *t = &p->tags[i];
        t->sig    = gmt_rd_u32(&r);
        t->offset = gmt_rd_u32(&r);
        t->size   = gmt_rd_u32(&r);
        t->pool_idx = -1;

        /* An entry whose body leaves the buffer is noted but not fatal;
         * the element phase will skip bodies it cannot read. */
        if (t->offset < GMT_HEADER_SIZE ||
            (size_t)t->offset > p->buf_size ||
            (size_t)t->size > p->buf_size - t->offset) {
            gmt_fail(&p->ctx, GMT_ERR_RANGE);
            t->size = 0;
        }
    }

    p->tag_count = count;
    return 0;
}

int gmt_has_tag(const gmt_profile *p, uint32_t signature)
{
    size_t i;
    if (!p)
        return 0;
    for (i = 0; i < p->tag_count; i++)
        if (p->tags[i].sig == signature)
            return 1;
    return 0;
}

static const gmt_tag *find_tag(const gmt_profile *p, uint32_t sig)
{
    size_t i;
    for (i = 0; i < p->tag_count; i++)
        if (p->tags[i].sig == sig)
            return &p->tags[i];
    return NULL;
}

/* Exposed to later phases that resolve tags by signature. */
const gmt_tag *gmt_find_tag(const gmt_profile *p, uint32_t sig);
const gmt_tag *gmt_find_tag(const gmt_profile *p, uint32_t sig)
{
    return find_tag(p, sig);
}
