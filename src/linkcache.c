/* linkcache.c - Phase 8: transform assembly
 *
 * The link cache holds the ordered sequence of stages that make up the
 * device-to-connection-space transform. Stages are appended to a growable
 * value array; the application order records the stages in the sequence the
 * apply phase walks them. Each stage keeps the resolved pointers it needs so
 * the apply phase does not repeat element lookups.
 */
#include "internal.h"
#include <stdlib.h>

#define GMT_STAGE_START 4

void gmt_link_init(gmt_linkcache *lc)
{
    lc->stages = NULL;
    lc->count = lc->cap = 0;
    lc->order = NULL;
    lc->order_count = lc->order_cap = 0;
    lc->in_ch = lc->out_ch = 0;
    lc->built = 0;
}

static gmt_linkstage *stage_grow(gmt_ctx *c, gmt_linkcache *lc)
{
    if (lc->count == lc->cap) {
        size_t ncap = lc->cap ? lc->cap * 2 : GMT_STAGE_START;
        gmt_linkstage *ns = (gmt_linkstage *)gmt_realloc(
            c, lc->stages, ncap * sizeof(gmt_linkstage));
        if (!ns)
            return NULL;
        lc->stages = ns;
        lc->cap = ncap;
    }
    return &lc->stages[lc->count++];
}

static int order_append(gmt_ctx *c, gmt_linkcache *lc, gmt_linkstage *s)
{
    if (lc->order_count == lc->order_cap) {
        size_t ncap = lc->order_cap ? lc->order_cap * 2 : GMT_STAGE_START;
        gmt_linkstage **no = (gmt_linkstage **)gmt_realloc(
            c, lc->order, ncap * sizeof(gmt_linkstage *));
        if (!no)
            return -1;
        lc->order = no;
        lc->order_cap = ncap;
    }
    lc->order[lc->order_count++] = s;
    return 0;
}

static int push_curve_stage(gmt_ctx *c, gmt_linkcache *lc,
                            const gmt_curve *cv, uint8_t ch)
{
    gmt_linkstage *s = stage_grow(c, lc);
    if (!s)
        return -1;
    s->kind = GMT_ST_CURVE;
    s->in_ch = ch;
    s->out_ch = ch;
    s->curve_samples = cv ? cv->samples : NULL;
    s->curve_count = cv ? cv->count : 0;
    s->matrix = NULL;
    s->clut_table = NULL;
    s->white = NULL;
    /* Record this stage in application order as it is added. */
    return order_append(c, lc, s);
}

static int push_clut_stage(gmt_ctx *c, gmt_linkcache *lc, const gmt_clut *cl)
{
    gmt_linkstage *s = stage_grow(c, lc);
    unsigned i;
    if (!s)
        return -1;
    s->kind = GMT_ST_CLUT;
    s->in_ch = cl->in_ch;
    s->out_ch = cl->out_ch;
    s->curve_samples = NULL;
    s->curve_count = 0;
    s->matrix = NULL;
    s->clut_table = cl->table;          /* resolved sample pointer */
    for (i = 0; i < GMT_MAX_CHANNELS; i++)
        s->grid[i] = cl->grid[i];
    s->white = NULL;
    /* Record this stage in application order as it is added. */
    return order_append(c, lc, s);
}

static int push_matrix_stage(gmt_ctx *c, gmt_linkcache *lc,
                             const gmt_matrix *m)
{
    gmt_linkstage *s = stage_grow(c, lc);
    unsigned i;
    if (!s)
        return -1;
    s->kind = GMT_ST_MATRIX;
    s->in_ch = 3;
    s->out_ch = 3;
    s->curve_samples = NULL;
    s->curve_count = 0;
    s->matrix = m;              /* resolved matrix pointer */
    s->clut_table = NULL;
    for (i = 0; i < GMT_MAX_CHANNELS; i++)
        s->grid[i] = 0;
    s->white = NULL;
    return order_append(c, lc, s);
}

static int push_chad_stage(gmt_ctx *c, gmt_linkcache *lc, const gmt_xyz *w)
{
    gmt_linkstage *s = stage_grow(c, lc);
    unsigned i;
    if (!s)
        return -1;
    s->kind = GMT_ST_CHAD;
    s->in_ch = 3;
    s->out_ch = 3;
    s->curve_samples = NULL;
    s->curve_count = 0;
    s->matrix = NULL;
    s->clut_table = NULL;
    for (i = 0; i < GMT_MAX_CHANNELS; i++)
        s->grid[i] = 0;
    s->white = w;               /* resolved white-point pointer */
    return order_append(c, lc, s);
}

int gmt_build_link(gmt_profile *p)
{
    static const uint32_t trc_sig[3] = {
        GMT_SIG('r', 'T', 'R', 'C'),
        GMT_SIG('g', 'T', 'R', 'C'),
        GMT_SIG('b', 'T', 'R', 'C')
    };
    gmt_linkcache *lc = &p->link;
    unsigned ch;

    gmt_link_init(lc);

    if (p->device_space == GMT_CS_RGB) {
        /* One curve stage per channel, each resolved from its tone tag. */
        for (ch = 0; ch < 3; ch++) {
            const gmt_tag *t = gmt_find_tag(p, trc_sig[ch]);
            const gmt_curve *cv = NULL;
            if (t && t->pool_idx >= 0) {
                gmt_element *el = gmt_pool_at(&p->pool, t->pool_idx);
                if (el && el->kind == GMT_EL_CURVE)
                    cv = el->u.curve;
            }
            if (push_curve_stage(&p->ctx, lc, cv, (uint8_t)ch) != 0)
                return -1;
        }
        lc->in_ch = 3;
        lc->out_ch = 3;
    } else if (p->device_space == GMT_CS_GRAY) {
        const gmt_tag *t = gmt_find_tag(p, GMT_SIG('k', 'T', 'R', 'C'));
        const gmt_curve *cv = NULL;
        if (t && t->pool_idx >= 0) {
            gmt_element *el = gmt_pool_at(&p->pool, t->pool_idx);
            if (el && el->kind == GMT_EL_CURVE)
                cv = el->u.curve;
        }
        if (push_curve_stage(&p->ctx, lc, cv, 0) != 0)
            return -1;
        lc->in_ch = lc->out_ch = 1;
    }

    /* A multi-process element assembles a table stage from the CLUT its
     * sequence resolves to. */
    {
        const gmt_tag *mt = gmt_find_tag(p, GMT_SIG('D', '2', 'B', '0'));
        if (mt && mt->pool_idx >= 0) {
            gmt_element *me = gmt_pool_at(&p->pool, mt->pool_idx);
            if (me && me->kind == GMT_EL_MPET) {
                gmt_mpet *mp = me->u.mpet;
                uint32_t si;
                for (si = 0; si < mp->stage_count; si++) {
                    gmt_mp_stage *st = &mp->stages[si];
                    if (st->op == GMT_MP_CLUT && st->elem >= 0) {
                        gmt_element *ce = gmt_pool_at(&p->pool, st->elem);
                        if (ce && ce->kind == GMT_EL_CLUT) {
                            if (push_clut_stage(&p->ctx, lc,
                                                ce->u.clut) != 0)
                                return -1;
                            /* The sequence's declared channel width is
                             * authoritative for how the stage is driven. */
                            lc->stages[lc->count - 1].out_ch = mp->out_ch;
                            lc->in_ch = ce->u.clut->in_ch;
                            lc->out_ch = mp->out_ch;
                        }
                    } else if (st->op == GMT_MP_MATRIX && st->elem >= 0) {
                        gmt_element *me = gmt_pool_at(&p->pool, st->elem);
                        if (me && me->kind == GMT_EL_MATRIX) {
                            if (push_matrix_stage(&p->ctx, lc,
                                                  me->u.matrix) != 0)
                                return -1;
                            if (lc->in_ch == 0) {
                                lc->in_ch = 3;
                                lc->out_ch = 3;
                            }
                        }
                    }
                }
            }
        }
    }

    /* A chromatic adaptation stage reads the profile white point. */
    {
        const gmt_tag *wt = gmt_find_tag(p, GMT_SIG('w', 't', 'p', 't'));
        if (wt && wt->pool_idx >= 0) {
            gmt_element *we = gmt_pool_at(&p->pool, wt->pool_idx);
            if (we && we->kind == GMT_EL_XYZ) {
                p->white = we;
                if (push_chad_stage(&p->ctx, lc, we->u.xyz) != 0)
                    return -1;
                if (lc->in_ch == 0) { lc->in_ch = 3; lc->out_ch = 3; }
            }
        }
    }

    lc->built = 1;
    return 0;
}

void gmt_link_free(gmt_ctx *c, gmt_linkcache *lc)
{
    gmt_release(c, lc->stages);
    gmt_release(c, lc->order);
    lc->stages = NULL;
    lc->order = NULL;
    lc->count = lc->cap = 0;
    lc->order_count = lc->order_cap = 0;
    lc->built = 0;
}
