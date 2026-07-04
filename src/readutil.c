/* readutil.c - extended byte reader utilities
 *
 * Additional reading helpers beyond the core big-endian integer decoders.
 * These handle alignment, string extraction, peeking without advancing the
 * cursor, and sub-reader construction for bounded region parsing.
 */
#include "internal.h"
#include <string.h>

/* Align the reader position to a multiple of `align` bytes. */
void gmt_rd_align(gmt_reader *r, unsigned align)
{
    size_t mod;
    if (align < 2)
        return;
    mod = r->pos % align;
    if (mod != 0) {
        size_t skip = (size_t)align - mod;
        if (r->pos + skip > r->size) {
            r->overrun = 1;
            r->pos = r->size;
            return;
        }
        r->pos += skip;
    }
}

/* Skip `n` bytes. */
void gmt_rd_skip(gmt_reader *r, size_t n)
{
    if (r->pos + n > r->size) {
        r->overrun = 1;
        r->pos = r->size;
        return;
    }
    r->pos += n;
}

/* Read a null-terminated ASCII string of at most `max` bytes (including
 * the terminator). Returns the number of bytes consumed (including the
 * null), or 0 if the string is not terminated within `max` bytes. */
size_t gmt_rd_string(gmt_reader *r, char *dst, size_t max)
{
    size_t start = r->pos;
    size_t i;

    for (i = 0; i < max; i++) {
        if (r->pos >= r->size) {
            r->overrun = 1;
            dst[0] = '\0';
            return 0;
        }
        dst[i] = (char)r->base[r->pos++];
        if (dst[i] == '\0')
            return r->pos - start;
    }
    /* Not terminated within max bytes; rewind. */
    r->pos = start;
    dst[0] = '\0';
    return 0;
}

/* Peek at the next uint32 without advancing the cursor. Returns 0 and sets
 * the overrun flag if fewer than 4 bytes remain. */
uint32_t gmt_rd_peek_u32(gmt_reader *r)
{
    uint32_t v;
    size_t saved = r->pos;
    v = gmt_rd_u32(r);
    if (!r->overrun)
        r->pos = saved;
    return v;
}

/* Peek at the next uint16 without advancing the cursor. */
uint16_t gmt_rd_peek_u16(gmt_reader *r)
{
    uint16_t v;
    size_t saved = r->pos;
    v = gmt_rd_u16(r);
    if (!r->overrun)
        r->pos = saved;
    return v;
}

/* Create a sub-reader that covers [offset, offset + length) of the parent
 * reader's underlying buffer. This is used for parsing element bodies where
 * the size is known from the tag directory. */
int gmt_rd_sub(const gmt_reader *parent, size_t offset, size_t length,
               gmt_reader *sub)
{
    if (offset > parent->size || length > parent->size - offset) {
        sub->base = parent->base;
        sub->size = 0;
        sub->pos = 0;
        sub->overrun = 1;
        return -1;
    }
    sub->base = parent->base + offset;
    sub->size = length;
    sub->pos = 0;
    sub->overrun = 0;
    return 0;
}

/* Return the number of bytes remaining from the current position. */
size_t gmt_rd_remaining(const gmt_reader *r)
{
    if (r->overrun || r->pos >= r->size)
        return 0;
    return r->size - r->pos;
}

/* Read a big-endian uint64. */
uint64_t gmt_rd_u64(gmt_reader *r)
{
    uint64_t hi, lo;
    hi = gmt_rd_u32(r);
    lo = gmt_rd_u32(r);
    return (hi << 32) | lo;
}

/* Read an s15Fixed16 value and return it as a float, with bounds checking.
 * Values outside the representable range are clamped. */
float gmt_rd_fixed_clamped(gmt_reader *r, float lo, float hi)
{
    float v = gmt_rd_fixed(r);
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

/* Read a u8Fixed8 value (unsigned 8.8 fixed point). */
float gmt_rd_u8f8(gmt_reader *r)
{
    uint16_t raw = gmt_rd_u16(r);
    return (float)raw / 256.0f;
}

/* Read a u16Fixed16 value (unsigned 16.16 fixed point). */
float gmt_rd_u16f16(gmt_reader *r)
{
    uint32_t raw = gmt_rd_u32(r);
    return (float)raw / 65536.0f;
}

/* Compare two readers' positions to check whether they agree. Used for
 * cross-validation between independent parses of the same region. */
int gmt_rd_positions_equal(const gmt_reader *a, const gmt_reader *b)
{
    return (a->pos == b->pos && a->overrun == b->overrun);
}
