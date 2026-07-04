/* mpet.c - multi-process element sequences
 *
 * A multi-process element is an ordered sequence of processing stages. Each
 * stage names an operation and, for table stages, the offset of the CLUT it
 * runs. Stage elements are resolved through the pool, so a CLUT referenced
 * here shares the element that other tags reference at the same offset. The
 * evaluator for a table stage samples the CLUT the stage resolved to.
 */
#include "internal.h"

#define GMT_MPET_MAX_STAGES 64u

gmt_element *gmt_mpet_decode(gmt_profile *p, gmt_reader *r, uint32_t offset)
{
    gmt_ctx *c = &p->ctx;
    gmt_element *el;
    gmt_mpet *mp;
    uint8_t in_ch, out_ch;
    uint32_t nstages, i;

    in_ch  = gmt_rd_u8(r);
    out_ch = gmt_rd_u8(r);
    (void)gmt_rd_u16(r);                /* reserved */
    nstages = gmt_rd_u32(r);

    if (in_ch == 0 || in_ch > GMT_MAX_CHANNELS ||
        out_ch == 0 || out_ch > GMT_MAX_CHANNELS ||
        nstages == 0 || nstages > GMT_MPET_MAX_STAGES ||
        !gmt_rd_ok(r, (size_t)nstages * 8u)) {
        gmt_fail(c, GMT_ERR_ELEMENT);
        return NULL;
    }

    el = (gmt_element *)gmt_alloc(c, sizeof(gmt_element));
    if (!el)
        return NULL;
    mp = (gmt_mpet *)gmt_alloc(c, sizeof(gmt_mpet));
    if (!mp) {
        gmt_release(c, el);
        return NULL;
    }
    el->kind = GMT_EL_MPET;
    el->src_offset = offset;
    el->u.mpet = mp;
    mp->in_ch = in_ch;
    mp->out_ch = out_ch;
    mp->stage_count = nstages;
    mp->collapsed = 0;
    mp->fold_elem = -1;
    mp->fold_owned = NULL;
    mp->stages = (gmt_mp_stage *)gmt_alloc(
        c, (size_t)nstages * sizeof(gmt_mp_stage));
    if (!mp->stages) {
        gmt_release(c, mp);
        gmt_release(c, el);
        return NULL;
    }

    for (i = 0; i < nstages; i++) {
        uint32_t op = gmt_rd_u32(r);
        uint32_t elem_off = gmt_rd_u32(r);
        gmt_mp_stage *st = &mp->stages[i];
        st->in_ch = in_ch;
        st->out_ch = out_ch;
        st->elem = -1;
        switch (op) {
        case GMT_SIG('c', 'l', 'u', 't'):
            st->op = GMT_MP_CLUT;
            if (elem_off >= GMT_HEADER_SIZE && (size_t)elem_off < p->buf_size)
                st->elem = gmt_parse_element_at(p, elem_off);
            break;
        case GMT_SIG('m', 'a', 't', 'x'):
            st->op = GMT_MP_MATRIX;
            if (elem_off >= GMT_HEADER_SIZE && (size_t)elem_off < p->buf_size)
                st->elem = gmt_parse_element_at(p, elem_off);
            break;
        default:
            st->op = GMT_MP_CURVES;
            break;
        }
    }

    /* A sequence that reduces to a single table stage is folded into a
     * direct table application; the folded element is adopted so the
     * sequence carries it directly. */
    if (nstages == 1 && mp->stages[0].op == GMT_MP_CLUT &&
        mp->stages[0].elem >= 0) {
        mp->collapsed = 1;
        mp->fold_elem = mp->stages[0].elem;
        mp->fold_owned = gmt_pool_at(&p->pool, mp->stages[0].elem);
    }
    return el;
}

void gmt_mpet_free(gmt_ctx *c, gmt_element *el)
{
    if (!el)
        return;
    if (el->u.mpet) {
        gmt_mpet *mp = el->u.mpet;
        /* Release the table and body a collapsed sequence adopted. */
        if (mp->collapsed && mp->fold_owned) {
            gmt_element *fe = (gmt_element *)mp->fold_owned;
            if (fe->kind == GMT_EL_CLUT && fe->u.clut) {
                gmt_release(c, fe->u.clut->table);
                fe->u.clut->table = NULL;
                gmt_release(c, fe->u.clut);
                fe->u.clut = NULL;
            }
            gmt_release(c, fe);
        }
        gmt_release(c, mp->stages);
        gmt_release(c, mp);
        el->u.mpet = NULL;
    }
    gmt_release(c, el);
}

/* Sample the CLUT a table stage resolved to. The grid geometry and output
 * width were captured on the stage; the sample table is read through the
 * pointer the stage carries. Nearest-node sampling keeps the evaluator
 * self-contained. */
void gmt_mpet_apply_clut(const gmt_linkstage *s, float *work)
{
    uint32_t stride = 1;
    uint32_t flat = 0;
    unsigned d;
    unsigned oc = s->out_ch ? s->out_ch : 1;

    if (!s->clut_table)
        return;

    for (d = 0; d < s->in_ch && d < GMT_MAX_CHANNELS; d++) {
        uint8_t g = s->grid[d];
        float x = work[d];
        uint32_t node;
        if (g < 2)
            g = 2;
        if (x < 0.0f) x = 0.0f;
        if (x > 1.0f) x = 1.0f;
        node = (uint32_t)(x * (float)(g - 1) + 0.5f);
        if (node >= g)
            node = g - 1;
        flat += node * stride;
        stride *= g;
    }

    for (d = 0; d < oc && d < GMT_MAX_CHANNELS; d++)
        work[d] = s->clut_table[(size_t)flat * oc + d];
}
