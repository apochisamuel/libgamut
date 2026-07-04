/* serialize.c - profile serialization
 *
 * Write a parsed profile back to a binary buffer. This is used for
 * round-trip testing: open a profile, serialize it, re-open the output,
 * and compare the two parsed representations. The serializer writes the
 * header, tag directory, and element bodies in the order the tags appear.
 */
#include "internal.h"
#include <string.h>
#include <stdlib.h>

static void wr_u32(uint8_t *b, size_t off, uint32_t v)
{
    b[off]   = (uint8_t)(v >> 24);
    b[off+1] = (uint8_t)(v >> 16);
    b[off+2] = (uint8_t)(v >> 8);
    b[off+3] = (uint8_t)(v);
}

static void wr_u16(uint8_t *b, size_t off, uint16_t v)
{
    b[off]   = (uint8_t)(v >> 8);
    b[off+1] = (uint8_t)(v);
}

static void wr_fixed(uint8_t *b, size_t off, float v)
{
    int32_t raw = (int32_t)(v * 65536.0f);
    wr_u32(b, off, (uint32_t)raw);
}

/* Serialize a curve element into the buffer starting at `pos`. Returns the
 * number of bytes written. */
static size_t ser_curve(const gmt_curve *cv, uint8_t *buf, size_t pos,
                        size_t cap)
{
    size_t start = pos;
    uint32_t i;
    if (!cv || pos + 12 > cap)
        return 0;
    wr_u32(buf, pos, GMT_SIG('c','u','r','v'));
    wr_u16(buf, pos+4, cv->fn_type);
    wr_u16(buf, pos+6, 0);
    pos += 8;
    if (cv->fn_type == 0) {
        if (pos + 4 + (size_t)cv->count * 2 > cap) return 0;
        wr_u32(buf, pos, cv->count);
        pos += 4;
        for (i = 0; i < cv->count; i++) {
            uint16_t s = (uint16_t)(cv->samples[i] * 65535.0f + 0.5f);
            wr_u16(buf, pos, s);
            pos += 2;
        }
    } else {
        int n = (cv->fn_type == 1) ? 3 : (cv->fn_type == 2) ? 4 :
                (cv->fn_type == 3) ? 5 : 7;
        int k;
        for (k = 0; k < n; k++) {
            if (pos + 4 > cap) return 0;
            wr_fixed(buf, pos, cv->params[k]);
            pos += 4;
        }
    }
    return pos - start;
}

/* Serialize a CLUT element. */
static size_t ser_clut(const gmt_clut *cl, uint8_t *buf, size_t pos,
                       size_t cap)
{
    size_t start = pos;
    size_t total;
    unsigned d;
    size_t i;

    total = (size_t)cl->point_count * cl->out_ch;
    if (pos + 4 + 4 + cl->in_ch + total * 2 > cap)
        return 0;
    wr_u32(buf, pos, GMT_SIG('c','l','u','t'));
    buf[pos+4] = cl->in_ch;
    buf[pos+5] = cl->out_ch;
    wr_u16(buf, pos+6, 0);
    pos += 8;
    for (d = 0; d < cl->in_ch; d++)
        buf[pos++] = cl->grid[d];
    for (i = 0; i < total; i++) {
        uint16_t v = (uint16_t)(cl->table[i] * 65535.0f + 0.5f);
        wr_u16(buf, pos, v);
        pos += 2;
    }
    return pos - start;
}

/* Serialize an XYZ element. */
static size_t ser_xyz(const gmt_xyz *xyz, uint8_t *buf, size_t pos,
                      size_t cap)
{
    if (pos + 16 > cap)
        return 0;
    wr_u32(buf, pos, GMT_SIG('X','Y','Z',' '));
    wr_fixed(buf, pos+4,  xyz->x);
    wr_fixed(buf, pos+8,  xyz->y);
    wr_fixed(buf, pos+12, xyz->z);
    return 16;
}

/* Serialize a matrix element. */
static size_t ser_matrix(const gmt_matrix *m, uint8_t *buf, size_t pos,
                         size_t cap)
{
    int i;
    if (pos + 4 + 48 > cap)
        return 0;
    wr_u32(buf, pos, GMT_SIG('m','a','t','x'));
    pos += 4;
    for (i = 0; i < 9; i++) {
        wr_fixed(buf, pos, m->e[i]);
        pos += 4;
    }
    for (i = 0; i < 3; i++) {
        wr_fixed(buf, pos, m->off[i]);
        pos += 4;
    }
    return 4 + 48;
}

/* Serialize a text element. */
static size_t ser_text(const char *text, uint8_t *buf, size_t pos,
                       size_t cap)
{
    size_t len;
    if (!text)
        return 0;
    len = strlen(text);
    if (pos + 8 + len > cap)
        return 0;
    wr_u32(buf, pos, GMT_SIG('d','e','s','c'));
    wr_u32(buf, pos+4, (uint32_t)len);
    memcpy(buf + pos + 8, text, len);
    return 8 + len;
}

/* Serialize an element. Returns bytes written, or 0 on failure. */
static size_t ser_element(const gmt_element *el, uint8_t *buf, size_t pos,
                          size_t cap)
{
    if (!el)
        return 0;
    switch (el->kind) {
    case GMT_EL_CURVE:
        return ser_curve(el->u.curve, buf, pos, cap);
    case GMT_EL_CLUT:
        return ser_clut(el->u.clut, buf, pos, cap);
    case GMT_EL_XYZ:
        return ser_xyz(el->u.xyz, buf, pos, cap);
    case GMT_EL_MATRIX:
        return ser_matrix(el->u.matrix, buf, pos, cap);
    case GMT_EL_TEXT:
        return ser_text(el->u.text, buf, pos, cap);
    default:
        return 0;
    }
}

/* Serialize a profile into a caller-provided buffer. Returns the number of
 * bytes written, or 0 on failure. The caller should provide a buffer at
 * least as large as the original profile. */
size_t gmt_serialize(const gmt_profile *p, uint8_t *buf, size_t cap)
{
    size_t td_pos, body_pos, final;
    size_t i;
    uint32_t *offsets;
    uint32_t *sizes;

    if (!p || !buf || cap < GMT_HEADER_SIZE + 4 + p->tag_count * 12)
        return 0;

    offsets = (uint32_t *)calloc(p->tag_count, sizeof(uint32_t));
    sizes   = (uint32_t *)calloc(p->tag_count, sizeof(uint32_t));
    if (!offsets || !sizes) {
        free(offsets);
        free(sizes);
        return 0;
    }

    memset(buf, 0, cap < GMT_HEADER_SIZE ? cap : GMT_HEADER_SIZE);

    /* Header. */
    wr_u32(buf, 4, GMT_MAGIC);
    wr_u32(buf, 8, p->version);
    wr_u32(buf, 12, (uint32_t)p->device_space);
    wr_u32(buf, 16, (uint32_t)p->pcs);
    wr_u32(buf, 20, p->channels);
    wr_u32(buf, 24, p->stored_id);

    /* Tag directory. */
    td_pos = GMT_HEADER_SIZE;
    wr_u32(buf, td_pos, (uint32_t)p->tag_count);
    body_pos = td_pos + 4 + p->tag_count * 12;

    /* Write element bodies and record offsets. */
    for (i = 0; i < p->tag_count; i++) {
        const gmt_tag *t = &p->tags[i];
        gmt_element *el = NULL;
        size_t written;

        /* Check for offset sharing with a previously written tag. */
        {
            size_t j;
            int shared = 0;
            for (j = 0; j < i; j++) {
                if (p->tags[j].offset == t->offset && sizes[j] > 0) {
                    offsets[i] = offsets[j];
                    sizes[i] = sizes[j];
                    shared = 1;
                    break;
                }
            }
            if (shared) continue;
        }

        if (t->pool_idx >= 0)
            el = gmt_pool_at((gmt_elem_pool *)&p->pool, t->pool_idx);
        if (!el) {
            /* Unresolved tags are copied verbatim if possible. */
            if (t->size > 0 && (size_t)t->offset + t->size <= p->buf_size &&
                body_pos + t->size <= cap) {
                memcpy(buf + body_pos, p->buf + t->offset, t->size);
                offsets[i] = (uint32_t)body_pos;
                sizes[i] = t->size;
                body_pos += t->size;
            }
            continue;
        }

        written = ser_element(el, buf, body_pos, cap);
        if (written > 0) {
            offsets[i] = (uint32_t)body_pos;
            sizes[i] = (uint32_t)written;
            body_pos += written;
        }
    }

    /* Write tag directory entries. */
    for (i = 0; i < p->tag_count; i++) {
        size_t ent = td_pos + 4 + i * 12;
        wr_u32(buf, ent + 0, p->tags[i].sig);
        wr_u32(buf, ent + 4, offsets[i]);
        wr_u32(buf, ent + 8, sizes[i]);
    }

    final = body_pos;
    wr_u32(buf, 0, (uint32_t)final);

    free(offsets);
    free(sizes);
    return final;
}
