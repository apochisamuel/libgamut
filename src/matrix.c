/* matrix.c - matrix elements
 *
 * A matrix element stores a 3x3 transform and a 3-element offset as
 * s15Fixed16 numbers. The chromatic adaptation tag holds the profile's
 * adaptation matrix; a multi-process element may run the same matrix as one
 * of its stages, resolving it through the element pool at the same offset.
 */
#include "internal.h"

gmt_element *gmt_matrix_decode(gmt_ctx *c, gmt_reader *r, uint32_t offset)
{
    gmt_element *el;
    gmt_matrix *m;
    int i;

    if (!gmt_rd_ok(r, 12 * 4)) {
        gmt_fail(c, GMT_ERR_ELEMENT);
        return NULL;
    }

    el = (gmt_element *)gmt_alloc(c, sizeof(gmt_element));
    if (!el)
        return NULL;
    m = (gmt_matrix *)gmt_alloc(c, sizeof(gmt_matrix));
    if (!m) {
        gmt_release(c, el);
        return NULL;
    }
    el->kind = GMT_EL_MATRIX;
    el->src_offset = offset;
    el->u.matrix = m;
    for (i = 0; i < 9; i++)
        m->e[i] = gmt_rd_fixed(r);
    for (i = 0; i < 3; i++)
        m->off[i] = gmt_rd_fixed(r);
    m->rows = 3;
    m->cols = 3;
    return el;
}

void gmt_matrix_free(gmt_ctx *c, gmt_element *el)
{
    if (!el)
        return;
    if (el->u.matrix) {
        gmt_release(c, el->u.matrix);
        el->u.matrix = NULL;
    }
    gmt_release(c, el);
}

/* Drop the profile's adaptation matrix through its primary owner, the chad
 * tag. Used by the validator when a matrix stage is found to be
 * dimensionally inconsistent. */
void gmt_matrix_release_chad(gmt_profile *p)
{
    const gmt_tag *ct = gmt_find_tag(p, GMT_SIG('c', 'h', 'a', 'd'));
    gmt_element *me;

    if (!ct || ct->pool_idx < 0)
        return;
    me = gmt_pool_at(&p->pool, ct->pool_idx);
    if (me && me->kind == GMT_EL_MATRIX) {
        gmt_matrix_free(&p->ctx, me);
        p->pool.items[ct->pool_idx] = NULL;
    }
}
