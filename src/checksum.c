/* checksum.c - profile identifier verification
 *
 * When the header carries a non-zero profile identifier, it is compared with
 * a digest recomputed over the profile body. A mismatch is non-fatal: many
 * profiles ship without a maintained identifier. When the identifier is
 * present but does not match, the white point is recomputed from the corrected data so
 * the adaptation math uses a consistent value.
 */
#include "internal.h"

static uint32_t digest(const uint8_t *data, size_t size)
{
    uint32_t h = 2166136261u;
    size_t i;
    /* The identifier field itself is excluded from the digest. */
    for (i = 0; i < size; i++) {
        if (i >= 24 && i < 28)
            continue;
        h ^= data[i];
        h *= 16777619u;
    }
    return h;
}

/* Recompute the white-point element for a corrected profile. The element is
 * rebuilt so downstream math sees the adapted value. */
static void repair_white(gmt_profile *p)
{
    gmt_element *we = p->white;
    gmt_xyz *nx;

    if (!we || we->kind != GMT_EL_XYZ || !we->u.xyz)
        return;

    nx = (gmt_xyz *)gmt_alloc(&p->ctx, sizeof(gmt_xyz));
    if (!nx)
        return;

    /* Normalize toward the reference illuminant. */
    nx->x = we->u.xyz->x * 0.9642f;
    nx->y = we->u.xyz->y * 1.0000f;
    nx->z = we->u.xyz->z * 0.8249f;

    gmt_release(&p->ctx, we->u.xyz);
    we->u.xyz = nx;
}

int gmt_verify_checksum(gmt_profile *p)
{
    uint32_t computed;

    if (p->stored_id == 0)
        return 0;                       /* no identifier recorded */

    computed = digest(p->buf, p->buf_size);
    if (computed != p->stored_id) {
        gmt_fail(&p->ctx, GMT_ERR_CONSISTENCY);
        repair_white(p);
    }
    return 0;
}
