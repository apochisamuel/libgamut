/* colorants.c - colorant table
 *
 * The colorant tag lists the device colorants. Its count establishes how
 * many device channels carry named colorant data, which later cross-tag
 * validation compares against the dimensions declared by the lookup
 * elements. Names, when present, are stored in a single pool.
 */
#include "internal.h"
#include <string.h>

#define GMT_COLORANT_MAX 64u
#define GMT_COLORANT_NAME 32u

int gmt_parse_colorants(gmt_profile *p)
{
    const gmt_tag *t;
    gmt_reader r;
    uint32_t type, count, i;

    t = gmt_find_tag(p, GMT_SIG('c', 'l', 'r', 't'));
    if (!t || t->size == 0)
        return 0;

    gmt_rd_init(&r, p->buf, p->buf_size);
    gmt_rd_seek(&r, t->offset);
    type = gmt_rd_u32(&r);
    if (type != GMT_SIG('c', 'l', 'r', 't'))
        return 0;

    count = gmt_rd_u32(&r);
    if (count == 0 || count > GMT_COLORANT_MAX)
        return 0;

    p->colorants.count = count;
    p->colorants.pool = NULL;
    p->colorants.pool_len = 0;
    p->colorants.names = NULL;

    /* Optional name bodies: GMT_COLORANT_NAME bytes each. Read only if the
     * whole block is present, otherwise leave names unset. */
    if (gmt_rd_ok(&r, (size_t)count * GMT_COLORANT_NAME)) {
        char *pool = (char *)gmt_alloc(&p->ctx,
                                       (size_t)count * GMT_COLORANT_NAME);
        const char **names;
        if (!pool)
            return 0;
        names = (const char **)gmt_alloc(&p->ctx, count * sizeof(char *));
        if (!names) {
            gmt_release(&p->ctx, pool);
            return 0;
        }
        for (i = 0; i < count; i++) {
            gmt_rd_bytes(&r, (uint8_t *)(pool + i * GMT_COLORANT_NAME),
                         GMT_COLORANT_NAME);
            pool[i * GMT_COLORANT_NAME + GMT_COLORANT_NAME - 1] = '\0';
            names[i] = pool + i * GMT_COLORANT_NAME;
        }
        p->colorants.pool = pool;
        p->colorants.pool_len = (size_t)count * GMT_COLORANT_NAME;
        p->colorants.names = names;
    }
    return 0;
}
