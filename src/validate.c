/* validate.c - Phase 9: cross-tag consistency
 *
 * After all elements are decoded and the transform is assembled, the
 * validator checks relationships that span more than one tag. Some checks
 * are purely diagnostic; others perform a selective cleanup of the elements
 * that failed the check so the profile no longer advertises data it cannot
 * honor. Cleanup is per-check, targeting only the elements a given check
 * owns.
 */
#include "internal.h"

/* Free the tone-curve element that the three tone tags resolved to and
 * detach those tags from it. */
static void free_trc_curves(gmt_profile *p, int32_t idx)
{
    gmt_element *el = gmt_pool_at(&p->pool, idx);
    size_t i;
    static const uint32_t trc_sig[3] = {
        GMT_SIG('r', 'T', 'R', 'C'),
        GMT_SIG('g', 'T', 'R', 'C'),
        GMT_SIG('b', 'T', 'R', 'C')
    };

    if (el) {
        gmt_curve_free(&p->ctx, el);
        p->pool.items[idx] = NULL;
    }
    for (i = 0; i < p->tag_count; i++) {
        uint32_t s = p->tags[i].sig;
        if (s == trc_sig[0] || s == trc_sig[1] || s == trc_sig[2])
            p->tags[i].pool_idx = -1;
    }
}

/* An RGB profile carries three per-channel tone curves. When the three tone
 * tags resolve to one element the profile does not provide independent
 * per-channel reproduction, which the connection-space math requires, so the
 * tone data is dropped. */
static void validate_trc(gmt_profile *p)
{
    const gmt_tag *tr, *tg, *tb;

    if (p->device_space != GMT_CS_RGB)
        return;

    tr = gmt_find_tag(p, GMT_SIG('r', 'T', 'R', 'C'));
    tg = gmt_find_tag(p, GMT_SIG('g', 'T', 'R', 'C'));
    tb = gmt_find_tag(p, GMT_SIG('b', 'T', 'R', 'C'));
    if (!tr || !tg || !tb)
        return;
    if (tr->pool_idx < 0 || tg->pool_idx < 0 || tb->pool_idx < 0)
        return;

    if (tr->pool_idx == tg->pool_idx && tg->pool_idx == tb->pool_idx) {
        gmt_fail(&p->ctx, GMT_ERR_CONSISTENCY);
        free_trc_curves(p, tr->pool_idx);
    }
}

/* The AtoB lookup declares the dimensionality of its CLUT. When the profile
 * also carries a colorant table, the colorant count and the CLUT input
 * dimension describe the same device, so a disagreement means the lookup
 * cannot be driven from the colorant data and its table is dropped. */
static void validate_colorants(gmt_profile *p)
{
    const gmt_tag *a;
    gmt_element *le, *ce;
    int32_t cidx;

    if (p->colorants.count == 0)
        return;

    a = gmt_find_tag(p, GMT_SIG('A', '2', 'B', '0'));
    if (!a || a->pool_idx < 0)
        return;
    le = gmt_pool_at(&p->pool, a->pool_idx);
    if (!le || le->kind != GMT_EL_LUT)
        return;

    cidx = le->u.lut->clut;
    ce = gmt_pool_at(&p->pool, cidx);
    if (!ce || ce->kind != GMT_EL_CLUT)
        return;

    if (p->colorants.count != ce->u.clut->in_ch) {
        gmt_fail(&p->ctx, GMT_ERR_CONSISTENCY);
        gmt_lut_release_clut(p, le);
    }
}

/* A multi-process matrix stage must produce as many channels as the sequence
 * declares. When a matrix stage's dimensions do not match the sequence, the
 * adaptation matrix it runs is considered inconsistent and dropped through
 * its primary owner. */
static void validate_matrix_chain(gmt_profile *p)
{
    const gmt_tag *mt;
    gmt_element *me;
    gmt_mpet *mp;
    uint32_t si;

    mt = gmt_find_tag(p, GMT_SIG('D', '2', 'B', '0'));
    if (!mt || mt->pool_idx < 0)
        return;
    me = gmt_pool_at(&p->pool, mt->pool_idx);
    if (!me || me->kind != GMT_EL_MPET)
        return;
    mp = me->u.mpet;

    for (si = 0; si < mp->stage_count; si++) {
        gmt_mp_stage *st = &mp->stages[si];
        gmt_element *xe;
        if (st->op != GMT_MP_MATRIX || st->elem < 0)
            continue;
        xe = gmt_pool_at(&p->pool, st->elem);
        if (!xe || xe->kind != GMT_EL_MATRIX)
            continue;
        if (mp->out_ch != xe->u.matrix->rows) {
            gmt_fail(&p->ctx, GMT_ERR_CONSISTENCY);
            gmt_matrix_release_chad(p);
        }
    }
}

int gmt_validate(gmt_profile *p)
{
    validate_trc(p);
    validate_colorants(p);
    validate_matrix_chain(p);
    /* Further cross-tag checks run here as later phases are added. */
    return 0;
}
