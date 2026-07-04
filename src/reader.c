/* reader.c - low-level big-endian byte reader
 *
 * Every multi-byte field in the profile is stored most-significant byte
 * first. The reader tracks a cursor and sets an overrun flag the first time
 * a read would pass the end of the buffer; callers check gmt_rd_ok() before
 * consuming a structure and gmt_rd_* return zero once overrun is set.
 */
#include "internal.h"
#include <string.h>

void gmt_rd_init(gmt_reader *r, const uint8_t *base, size_t size)
{
    r->base = base;
    r->size = size;
    r->pos = 0;
    r->overrun = 0;
}

void gmt_rd_seek(gmt_reader *r, size_t off)
{
    if (off > r->size) {
        r->overrun = 1;
        r->pos = r->size;
        return;
    }
    r->pos = off;
}

int gmt_rd_ok(const gmt_reader *r, size_t need)
{
    if (r->overrun)
        return 0;
    if (need > r->size)
        return 0;
    return r->pos <= r->size - need;
}

uint8_t gmt_rd_u8(gmt_reader *r)
{
    if (!gmt_rd_ok(r, 1)) {
        r->overrun = 1;
        return 0;
    }
    return r->base[r->pos++];
}

uint16_t gmt_rd_u16(gmt_reader *r)
{
    uint16_t v;
    if (!gmt_rd_ok(r, 2)) {
        r->overrun = 1;
        return 0;
    }
    v = (uint16_t)((r->base[r->pos] << 8) | r->base[r->pos + 1]);
    r->pos += 2;
    return v;
}

uint32_t gmt_rd_u32(gmt_reader *r)
{
    uint32_t v;
    if (!gmt_rd_ok(r, 4)) {
        r->overrun = 1;
        return 0;
    }
    v = ((uint32_t)r->base[r->pos] << 24) |
        ((uint32_t)r->base[r->pos + 1] << 16) |
        ((uint32_t)r->base[r->pos + 2] << 8) |
        ((uint32_t)r->base[r->pos + 3]);
    r->pos += 4;
    return v;
}

int32_t gmt_rd_s15f16(gmt_reader *r)
{
    return (int32_t)gmt_rd_u32(r);
}

float gmt_rd_fixed(gmt_reader *r)
{
    int32_t raw = (int32_t)gmt_rd_u32(r);
    return (float)raw / 65536.0f;
}

void gmt_rd_bytes(gmt_reader *r, uint8_t *dst, size_t n)
{
    if (!gmt_rd_ok(r, n)) {
        r->overrun = 1;
        memset(dst, 0, n);
        return;
    }
    memcpy(dst, r->base + r->pos, n);
    r->pos += n;
}
