/* valext.c - extended validation checks
 *
 * Additional cross-tag consistency checks beyond the core validation phase.
 * These checks are informational and do not modify the profile; they record
 * notes that the inspection tool or the caller can query.
 */
#include "internal.h"
#include <string.h>

typedef struct gmt_val_note {
    gmt_status  severity;
    const char *message;
} gmt_val_note;

#define GMT_MAX_NOTES 64

typedef struct gmt_val_report {
    gmt_val_note notes[GMT_MAX_NOTES];
    unsigned     count;
} gmt_val_report;

static void note(gmt_val_report *rpt, gmt_status sev, const char *msg)
{
    if (rpt->count < GMT_MAX_NOTES) {
        rpt->notes[rpt->count].severity = sev;
        rpt->notes[rpt->count].message = msg;
        rpt->count++;
    }
}

/* Check that required tags for the device space are present. */
static void check_required_tags(const gmt_profile *p, gmt_val_report *rpt)
{
    if (p->device_space == GMT_CS_RGB) {
        if (!gmt_has_tag(p, GMT_SIG('r','T','R','C')))
            note(rpt, GMT_ERR_CONSISTENCY, "RGB profile missing rTRC");
        if (!gmt_has_tag(p, GMT_SIG('g','T','R','C')))
            note(rpt, GMT_ERR_CONSISTENCY, "RGB profile missing gTRC");
        if (!gmt_has_tag(p, GMT_SIG('b','T','R','C')))
            note(rpt, GMT_ERR_CONSISTENCY, "RGB profile missing bTRC");
    } else if (p->device_space == GMT_CS_GRAY) {
        if (!gmt_has_tag(p, GMT_SIG('k','T','R','C')))
            note(rpt, GMT_ERR_CONSISTENCY, "Gray profile missing kTRC");
    }
}

/* Check that curve elements have valid sample counts. */
static void check_curves(const gmt_profile *p, gmt_val_report *rpt)
{
    size_t i;
    for (i = 0; i < p->pool.count; i++) {
        gmt_element *el = p->pool.items[i];
        if (!el || el->kind != GMT_EL_CURVE)
            continue;
        if (el->u.curve->fn_type == 0) {
            if (el->u.curve->count < 2)
                note(rpt, GMT_ERR_ELEMENT, "Curve has fewer than 2 samples");
            if (el->u.curve->count > 65536)
                note(rpt, GMT_ERR_ELEMENT, "Curve has excessive sample count");
        } else if (el->u.curve->fn_type > 4) {
            note(rpt, GMT_ERR_ELEMENT, "Curve has unknown parametric type");
        }
    }
}

/* Check that CLUT elements have valid grid geometry. */
static void check_cluts(const gmt_profile *p, gmt_val_report *rpt)
{
    size_t i;
    for (i = 0; i < p->pool.count; i++) {
        gmt_element *el = p->pool.items[i];
        unsigned d;
        uint32_t product;
        if (!el || el->kind != GMT_EL_CLUT)
            continue;
        product = 1;
        for (d = 0; d < el->u.clut->in_ch; d++) {
            if (el->u.clut->grid[d] < 2)
                note(rpt, GMT_ERR_ELEMENT, "CLUT grid dimension below 2");
            product *= el->u.clut->grid[d];
        }
        if (product != el->u.clut->point_count)
            note(rpt, GMT_ERR_ELEMENT, "CLUT grid product mismatch");
    }
}

/* Check matrix elements for singularity. */
static void check_matrices(const gmt_profile *p, gmt_val_report *rpt)
{
    size_t i;
    for (i = 0; i < p->pool.count; i++) {
        gmt_element *el = p->pool.items[i];
        float det;
        if (!el || el->kind != GMT_EL_MATRIX)
            continue;
        det = el->u.matrix->e[0] * (el->u.matrix->e[4]*el->u.matrix->e[8]
            - el->u.matrix->e[5]*el->u.matrix->e[7])
            - el->u.matrix->e[1] * (el->u.matrix->e[3]*el->u.matrix->e[8]
            - el->u.matrix->e[5]*el->u.matrix->e[6])
            + el->u.matrix->e[2] * (el->u.matrix->e[3]*el->u.matrix->e[7]
            - el->u.matrix->e[4]*el->u.matrix->e[6]);
        if (det == 0.0f)
            note(rpt, GMT_ERR_CONSISTENCY, "Singular adaptation matrix");
        if (det < 0.0f)
            note(rpt, GMT_ERR_CONSISTENCY, "Negative-determinant matrix");
    }
}

/* Check that the link cache chain has compatible channel counts. */
static void check_link_chain(const gmt_profile *p, gmt_val_report *rpt)
{
    size_t i;
    uint8_t prev_out = 0;
    if (!p->link.built || p->link.count == 0)
        return;
    prev_out = p->link.stages[0].in_ch;
    for (i = 0; i < p->link.count; i++) {
        const gmt_linkstage *s = &p->link.stages[i];
        if (i > 0 && s->in_ch != prev_out)
            note(rpt, GMT_ERR_CONSISTENCY, "Transform chain channel mismatch");
        prev_out = s->out_ch;
    }
}

/* Check element hold counts against tag references. */
static void check_hold_counts(const gmt_profile *p, gmt_val_report *rpt)
{
    size_t i, j;
    for (i = 0; i < p->pool.count; i++) {
        gmt_element *el = p->pool.items[i];
        unsigned refs = 0;
        if (!el)
            continue;
        for (j = 0; j < p->tag_count; j++)
            if (p->tags[j].pool_idx == (int32_t)i)
                refs++;
        if (refs != el->hold)
            note(rpt, GMT_ERR_CONSISTENCY, "Element hold count disagrees "
                 "with tag reference count");
    }
}

/* Check for tags with zero size. */
static void check_empty_tags(const gmt_profile *p, gmt_val_report *rpt)
{
    size_t i;
    for (i = 0; i < p->tag_count; i++)
        if (p->tags[i].size == 0)
            note(rpt, GMT_ERR_RANGE, "Tag has zero size");
}

/* Run all extended validation checks and return the report. */
void gmt_validate_extended(const gmt_profile *p, gmt_val_report *rpt)
{
    memset(rpt, 0, sizeof(*rpt));
    if (!p)
        return;
    check_required_tags(p, rpt);
    check_curves(p, rpt);
    check_cluts(p, rpt);
    check_matrices(p, rpt);
    check_link_chain(p, rpt);
    check_hold_counts(p, rpt);
    check_empty_tags(p, rpt);
}

/* Curve monotonicity check: verify that a sampled curve is non-decreasing. */
int gmt_curve_is_monotonic(const gmt_curve *cv)
{
    uint32_t i;
    if (!cv || cv->fn_type != 0 || cv->count < 2)
        return 1;  /* parametric or trivial = considered monotonic */
    for (i = 1; i < cv->count; i++)
        if (cv->samples[i] < cv->samples[i - 1])
            return 0;
    return 1;
}

/* Curve identity check: verify that a sampled curve is approximately
 * the identity function y=x. */
int gmt_curve_is_identity(const gmt_curve *cv, float eps)
{
    uint32_t i;
    if (!cv || cv->fn_type != 0 || cv->count < 2)
        return 0;
    for (i = 0; i < cv->count; i++) {
        float expected = (float)i / (float)(cv->count - 1);
        float diff = cv->samples[i] - expected;
        if (diff < 0.0f) diff = -diff;
        if (diff > eps)
            return 0;
    }
    return 1;
}

/* Curve inverse evaluation via binary search on a monotonic sampled curve.
 * Returns the input x in [0,1] such that curve(x) ≈ y. */
float gmt_curve_invert(const gmt_curve *cv, float y)
{
    float lo = 0.0f, hi = 1.0f, mid;
    int iter;
    if (!cv || cv->fn_type != 0 || cv->count < 2)
        return y;
    if (y <= cv->samples[0])
        return 0.0f;
    if (y >= cv->samples[cv->count - 1])
        return 1.0f;
    for (iter = 0; iter < 32; iter++) {
        float pos, frac;
        uint32_t idx, idx2;
        float val;
        mid = (lo + hi) * 0.5f;
        pos = mid * (float)(cv->count - 1);
        idx = (uint32_t)pos;
        idx2 = (idx + 1 < cv->count) ? idx + 1 : idx;
        frac = pos - (float)idx;
        val = cv->samples[idx] * (1.0f - frac) + cv->samples[idx2] * frac;
        if (val < y)
            lo = mid;
        else
            hi = mid;
    }
    return (lo + hi) * 0.5f;
}

/* Compute the area under a sampled curve using the trapezoidal rule. */
float gmt_curve_area(const gmt_curve *cv)
{
    float area = 0.0f;
    uint32_t i;
    if (!cv || cv->fn_type != 0 || cv->count < 2)
        return 0.5f;
    for (i = 1; i < cv->count; i++) {
        float dx = 1.0f / (float)(cv->count - 1);
        area += (cv->samples[i - 1] + cv->samples[i]) * dx * 0.5f;
    }
    return area;
}
