/* header.c - Phase 1: profile header
 *
 * The header is a fixed 128-byte block. It establishes the format metadata
 * every later phase depends on: overall size, version, the device color
 * space, the profile connection space, and the device channel count.
 */
#include "internal.h"

static unsigned space_channels(gmt_colorspace cs, unsigned nch)
{
    switch (cs) {
    case GMT_CS_GRAY: return 1;
    case GMT_CS_RGB:  return 3;
    case GMT_CS_CMYK: return 4;
    case GMT_CS_LAB:  return 3;
    case GMT_CS_XYZ:  return 3;
    case GMT_CS_NCH:  return nch;
    default:          return 0;
    }
}

int gmt_parse_header(gmt_profile *p)
{
    gmt_reader r;
    uint32_t size, magic, version, dev, pcs, nch;

    gmt_rd_init(&r, p->buf, p->buf_size);
    if (!gmt_rd_ok(&r, GMT_HEADER_SIZE)) {
        gmt_fail_fatal(&p->ctx, GMT_ERR_TRUNCATED);
        return -1;
    }

    size    = gmt_rd_u32(&r);
    magic   = gmt_rd_u32(&r);
    version = gmt_rd_u32(&r);
    dev     = gmt_rd_u32(&r);
    pcs     = gmt_rd_u32(&r);
    nch     = gmt_rd_u32(&r);
    p->stored_id = gmt_rd_u32(&r);      /* recorded profile identifier */

    if (magic != GMT_MAGIC) {
        gmt_fail_fatal(&p->ctx, GMT_ERR_MAGIC);
        return -1;
    }
    if (version < GMT_MIN_VERSION || version > GMT_MAX_VERSION) {
        gmt_fail_fatal(&p->ctx, GMT_ERR_VERSION);
        return -1;
    }
    /* The stored size is advisory: it must not exceed the buffer, but a
     * profile trailing extra bytes is tolerated. */
    if (size > p->buf_size)
        gmt_fail(&p->ctx, GMT_ERR_RANGE);

    if (dev < GMT_CS_GRAY || dev > GMT_CS_NCH ||
        pcs < GMT_CS_GRAY || pcs > GMT_CS_NCH) {
        gmt_fail_fatal(&p->ctx, GMT_ERR_UNSUPPORTED);
        return -1;
    }
    if (nch == 0 || nch > GMT_MAX_CHANNELS) {
        gmt_fail_fatal(&p->ctx, GMT_ERR_RANGE);
        return -1;
    }

    p->version      = version;
    p->device_space = (gmt_colorspace)dev;
    p->pcs          = (gmt_colorspace)pcs;
    p->channels     = space_channels((gmt_colorspace)dev, nch);
    p->pcs_channels = space_channels((gmt_colorspace)pcs, nch);
    if (p->channels == 0 || p->pcs_channels == 0) {
        gmt_fail_fatal(&p->ctx, GMT_ERR_UNSUPPORTED);
        return -1;
    }
    return 0;
}
