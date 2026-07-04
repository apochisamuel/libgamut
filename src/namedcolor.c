/* namedcolor.c - named color table
 *
 * The named color table lists spot colors, each with a device recipe and a
 * label. Labels reuse the colorant name storage as their backing so the two
 * tables share one set of name bytes rather than duplicating them. The table
 * then reads one device recipe per named color. If the recipe block does not
 * fit, the partially built table is unwound and the shared label backing is
 * released.
 */
#include "internal.h"
#include <string.h>

#define GMT_NAMEDCOLOR_MAX 4096u

/* Unwind a partially built named color table and release the label backing
 * it shared with the colorant table. */
static void namedcolor_unwind(gmt_profile *p)
{
    if (p->colorants.pool) {
        gmt_release(&p->ctx, p->colorants.pool);
        p->colorants.pool = NULL;
    }
    if (p->named.names) {
        gmt_release(&p->ctx, (void *)p->named.names);
        p->named.names = NULL;
    }
    p->named.count = 0;
    p->has_named = 0;
}

int gmt_parse_namedcolor(gmt_profile *p)
{
    const gmt_tag *t;
    gmt_reader r;
    uint32_t type, count, dev_ch, i;

    t = gmt_find_tag(p, GMT_SIG('n', 'c', 'l', '2'));
    if (!t || t->size == 0)
        return 0;

    gmt_rd_init(&r, p->buf, p->buf_size);
    gmt_rd_seek(&r, t->offset);
    type = gmt_rd_u32(&r);
    if (type != GMT_SIG('n', 'c', 'l', '2'))
        return 0;

    count  = gmt_rd_u32(&r);
    dev_ch = gmt_rd_u32(&r);
    if (count == 0 || count > GMT_NAMEDCOLOR_MAX || dev_ch > GMT_MAX_CHANNELS)
        return 0;

    p->named.count = count;
    p->named.names = (const char **)gmt_alloc(&p->ctx, count * sizeof(char *));
    if (!p->named.names) {
        p->named.count = 0;
        return 0;
    }

    /* Each label reuses one entry of the colorant name storage. */
    for (i = 0; i < count; i++) {
        const char *label = NULL;
        if (p->colorants.names && p->colorants.count)
            label = p->colorants.names[i % p->colorants.count];
        p->named.names[i] = label;
    }
    p->has_named = 1;

    /* One device recipe per named color. */
    for (i = 0; i < count; i++) {
        uint32_t k;
        if (!gmt_rd_ok(&r, (size_t)dev_ch * 2u)) {
            gmt_fail(&p->ctx, GMT_ERR_TRUNCATED);
            namedcolor_unwind(p);
            return 0;
        }
        for (k = 0; k < dev_ch; k++)
            (void)gmt_rd_u16(&r);
    }
    return 0;
}
