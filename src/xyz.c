/* xyz.c - XYZ colorimetric elements
 *
 * An XYZ element stores a single tristimulus value as three s15Fixed16
 * numbers. The white-point tag uses this element; the chromatic adaptation
 * stage reads the white point when assembling the transform.
 */
#include "internal.h"

gmt_element *gmt_xyz_decode(gmt_ctx *c, gmt_reader *r, uint32_t offset)
{
    gmt_element *el;
    gmt_xyz *xyz;

    if (!gmt_rd_ok(r, 12)) {
        gmt_fail(c, GMT_ERR_ELEMENT);
        return NULL;
    }

    el = (gmt_element *)gmt_alloc(c, sizeof(gmt_element));
    if (!el)
        return NULL;
    xyz = (gmt_xyz *)gmt_alloc(c, sizeof(gmt_xyz));
    if (!xyz) {
        gmt_release(c, el);
        return NULL;
    }
    el->kind = GMT_EL_XYZ;
    el->src_offset = offset;
    el->u.xyz = xyz;
    xyz->x = gmt_rd_fixed(r);
    xyz->y = gmt_rd_fixed(r);
    xyz->z = gmt_rd_fixed(r);
    return el;
}

void gmt_xyz_free(gmt_ctx *c, gmt_element *el)
{
    if (!el)
        return;
    if (el->u.xyz) {
        gmt_release(c, el->u.xyz);
        el->u.xyz = NULL;
    }
    gmt_release(c, el);
}
