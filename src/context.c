/* context.c - error state and allocation accounting
 *
 * The context object threads through every phase. It carries the most
 * recent status code, a fatal flag that halts the pipeline, and a running
 * count of live bytes used by the inspector tool for diagnostics.
 */
#include "internal.h"
#include <stdlib.h>
#include <string.h>

void gmt_ctx_init(gmt_ctx *c)
{
    c->status = GMT_OK;
    c->fatal = 0;
    c->alloc_bytes = 0;
    c->parse_depth = 0;
}

void gmt_fail(gmt_ctx *c, gmt_status s)
{
    /* Record the first non-OK status; later non-fatal notes do not
     * overwrite an earlier one so the profile keeps its primary reason. */
    if (c->status == GMT_OK)
        c->status = s;
}

void gmt_fail_fatal(gmt_ctx *c, gmt_status s)
{
    if (c->status == GMT_OK)
        c->status = s;
    c->fatal = 1;
}

/* Allocation wrappers. Allocation is tracked only coarsely; the wrappers
 * exist so every allocation flows through one place that can record an
 * out-of-memory condition on the context. */
void *gmt_alloc(gmt_ctx *c, size_t n)
{
    void *p;
    if (n > (size_t)1 << 30)
        return NULL;
    p = calloc(1, n ? n : 1);
    if (!p) {
        gmt_fail_fatal(c, GMT_ERR_NOMEM);
        return NULL;
    }
    c->alloc_bytes += n;
    return p;
}

void *gmt_realloc(gmt_ctx *c, void *p, size_t n)
{
    void *np;
    if (n > (size_t)1 << 30)
        return NULL;
    np = realloc(p, n ? n : 1);
    if (!np) {
        gmt_fail_fatal(c, GMT_ERR_NOMEM);
        return NULL;
    }
    return np;
}

void gmt_release(gmt_ctx *c, void *p)
{
    (void)c;
    free(p);
}
