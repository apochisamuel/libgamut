/* queryext.c - extended profile query helpers
 *
 * Additional query functions that give callers access to derived data:
 * profile summaries, element counts by kind, transform stage inspection,
 * and diagnostic accessors for the link cache.
 */
#include "internal.h"
#include <string.h>
#include <stdio.h>

/* Return the number of elements in the interning pool. */
size_t gmt_pool_count(const gmt_profile *p)
{
    return p ? p->pool.count : 0;
}

/* Return the element kind at a given pool index. */
gmt_el_kind gmt_pool_kind_at(const gmt_profile *p, size_t index)
{
    gmt_element *el;
    if (!p || index >= p->pool.count)
        return GMT_EL_NONE;
    el = p->pool.items[index];
    return el ? el->kind : GMT_EL_NONE;
}

/* Return the number of assembled transform stages. */
size_t gmt_link_stage_count(const gmt_profile *p)
{
    return p ? p->link.count : 0;
}

/* Return the kind of a transform stage. */
gmt_stage_kind gmt_link_stage_kind(const gmt_profile *p, size_t index)
{
    if (!p || index >= p->link.count)
        return (gmt_stage_kind)0;
    return p->link.stages[index].kind;
}

/* Return the input channel count of a stage. */
unsigned gmt_link_stage_in(const gmt_profile *p, size_t index)
{
    if (!p || index >= p->link.count)
        return 0;
    return p->link.stages[index].in_ch;
}

/* Return the output channel count of a stage. */
unsigned gmt_link_stage_out(const gmt_profile *p, size_t index)
{
    if (!p || index >= p->link.count)
        return 0;
    return p->link.stages[index].out_ch;
}

/* Check whether the forward transform is assembled. */
int gmt_link_is_built(const gmt_profile *p)
{
    return (p && p->link.built) ? 1 : 0;
}

/* Check whether the inverse transform is assembled. */
int gmt_link_inv_is_built(const gmt_profile *p)
{
    return (p && p->link_inv.built) ? 1 : 0;
}

/* Produce a one-line summary of the profile. */
int gmt_profile_summary(const gmt_profile *p, char *buf, size_t cap)
{
    const char *dev_str, *pcs_str;
    if (!p || cap < 128)
        return -1;

    switch (p->device_space) {
    case GMT_CS_GRAY: dev_str = "Gray"; break;
    case GMT_CS_RGB:  dev_str = "RGB";  break;
    case GMT_CS_CMYK: dev_str = "CMYK"; break;
    case GMT_CS_LAB:  dev_str = "Lab";  break;
    case GMT_CS_XYZ:  dev_str = "XYZ";  break;
    case GMT_CS_NCH:  dev_str = "NCH";  break;
    default:          dev_str = "?";    break;
    }
    switch (p->pcs) {
    case GMT_CS_GRAY: pcs_str = "Gray"; break;
    case GMT_CS_RGB:  pcs_str = "RGB";  break;
    case GMT_CS_CMYK: pcs_str = "CMYK"; break;
    case GMT_CS_LAB:  pcs_str = "Lab";  break;
    case GMT_CS_XYZ:  pcs_str = "XYZ";  break;
    case GMT_CS_NCH:  pcs_str = "NCH";  break;
    default:          pcs_str = "?";    break;
    }
    snprintf(buf, cap, "%s -> %s, %u ch, %zu tags, %zu elements, "
             "%zu stages, status=%d",
             dev_str, pcs_str, p->channels,
             p->tag_count, p->pool.count,
             p->link.count, (int)p->status);
    return 0;
}

/* Run the transform on a set of test points and compute the maximum
 * component-wise deviation between forward(x) and a reference output.
 * Returns the maximum deviation, or -1.0f on error. */
float gmt_transform_max_dev(const gmt_profile *p,
                            const float *inputs, const float *expected,
                            size_t npoints, unsigned nch)
{
    float max_dev = 0.0f;
    size_t i;
    float out[GMT_MAX_CHANNELS];

    if (!p || !inputs || !expected || nch == 0 || nch > GMT_MAX_CHANNELS)
        return -1.0f;

    for (i = 0; i < npoints; i++) {
        unsigned c;
        gmt_status st = gmt_apply(p, inputs + i * nch, out);
        if (st != GMT_OK)
            return -1.0f;
        for (c = 0; c < nch; c++) {
            float d = out[c] - expected[i * nch + c];
            if (d < 0.0f) d = -d;
            if (d > max_dev) max_dev = d;
        }
    }
    return max_dev;
}

/* Count resolved tags (tags that have a valid pool index). */
size_t gmt_resolved_tag_count(const gmt_profile *p)
{
    size_t count = 0;
    size_t i;
    if (!p)
        return 0;
    for (i = 0; i < p->tag_count; i++)
        if (p->tags[i].pool_idx >= 0)
            count++;
    return count;
}

/* Return the hold count for a pool element (how many tags reference it). */
unsigned gmt_pool_hold_at(const gmt_profile *p, size_t index)
{
    gmt_element *el;
    if (!p || index >= p->pool.count)
        return 0;
    el = p->pool.items[index];
    return el ? el->hold : 0;
}

/* Compute total memory used by pool elements (approximate). */
size_t gmt_pool_memory(const gmt_profile *p)
{
    size_t total = 0;
    size_t i;
    if (!p)
        return 0;
    for (i = 0; i < p->pool.count; i++) {
        gmt_element *el = p->pool.items[i];
        if (!el) continue;
        total += sizeof(gmt_element);
        switch (el->kind) {
        case GMT_EL_CURVE:
            if (el->u.curve) {
                total += sizeof(gmt_curve);
                total += (size_t)el->u.curve->count * sizeof(float);
            }
            break;
        case GMT_EL_CLUT:
            if (el->u.clut) {
                total += sizeof(gmt_clut);
                total += (size_t)el->u.clut->point_count
                       * el->u.clut->out_ch * sizeof(float);
            }
            break;
        case GMT_EL_MATRIX:
            total += sizeof(gmt_matrix);
            break;
        case GMT_EL_LUT:
            total += sizeof(gmt_lut);
            break;
        case GMT_EL_MPET:
            if (el->u.mpet) {
                total += sizeof(gmt_mpet);
                total += (size_t)el->u.mpet->stage_count
                       * sizeof(gmt_mp_stage);
            }
            break;
        case GMT_EL_XYZ:
            total += sizeof(gmt_xyz);
            break;
        case GMT_EL_TEXT:
            if (el->u.text)
                total += strlen(el->u.text) + 1;
            break;
        default:
            break;
        }
    }
    return total;
}
