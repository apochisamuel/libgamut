/* format.c - string formatting utilities
 *
 * Helpers for rendering profile data as human-readable text. Used by the
 * inspection tool and the query API to present tag signatures, fixed-point
 * values, XYZ tristimulus values, and raw byte sequences.
 */
#include "internal.h"
#include <stdio.h>
#include <string.h>
#include <ctype.h>

/* Render a four-byte tag signature as a printable string. Non-printable
 * bytes are replaced with a dot. */
void gmt_sig_str(uint32_t sig, char out[5])
{
    int i;
    for (i = 0; i < 4; i++) {
        unsigned char c = (unsigned char)(sig >> (24 - i * 8));
        out[i] = isprint(c) ? (char)c : '.';
    }
    out[4] = '\0';
}

/* Format a signature as a hex string: "0x72545243". */
int gmt_sig_hex(uint32_t sig, char *buf, size_t cap)
{
    if (cap < 11)
        return -1;
    snprintf(buf, cap, "0x%08X", sig);
    return 0;
}

/* Format an s15Fixed16 value as a decimal string with four fractional
 * digits. The raw 32-bit integer is converted to a float first. */
int gmt_format_fixed(int32_t raw, char *buf, size_t cap)
{
    float v = (float)raw / 65536.0f;
    if (cap < 16)
        return -1;
    snprintf(buf, cap, "%.4f", (double)v);
    return 0;
}

/* Format a float with the given number of decimal places. */
int gmt_format_float(float v, int decimals, char *buf, size_t cap)
{
    if (cap < 20 || decimals < 0 || decimals > 8)
        return -1;
    snprintf(buf, cap, "%.*f", decimals, (double)v);
    return 0;
}

/* Format an XYZ tristimulus value. */
int gmt_format_xyz(const gmt_xyz *xyz, char *buf, size_t cap)
{
    if (!xyz || cap < 48)
        return -1;
    snprintf(buf, cap, "(%.4f, %.4f, %.4f)",
             (double)xyz->x, (double)xyz->y, (double)xyz->z);
    return 0;
}

/* Format a 3x3 matrix as three lines. Each line contains three values
 * separated by spaces, with an optional offset column. */
int gmt_format_matrix(const gmt_matrix *m, char *buf, size_t cap)
{
    int n;
    if (!m || cap < 256)
        return -1;
    n = snprintf(buf, cap,
        "[ %8.4f %8.4f %8.4f | %8.4f ]\n"
        "[ %8.4f %8.4f %8.4f | %8.4f ]\n"
        "[ %8.4f %8.4f %8.4f | %8.4f ]",
        (double)m->e[0], (double)m->e[1], (double)m->e[2], (double)m->off[0],
        (double)m->e[3], (double)m->e[4], (double)m->e[5], (double)m->off[1],
        (double)m->e[6], (double)m->e[7], (double)m->e[8], (double)m->off[2]);
    return (n > 0 && (size_t)n < cap) ? 0 : -1;
}

/* Hex-dump up to `max_bytes` of raw data. Each line shows an offset, hex
 * bytes, and a printable-ASCII sidebar. */
int gmt_hexdump(const uint8_t *data, size_t size, size_t max_bytes,
                char *buf, size_t cap)
{
    size_t off, col, written = 0;
    size_t limit = (size < max_bytes) ? size : max_bytes;

    if (!data || !buf || cap < 80)
        return -1;
    buf[0] = '\0';

    for (off = 0; off < limit; off += 16) {
        char line[80];
        char hex[50];
        char asc[18];
        size_t row_end = off + 16;
        int hx = 0;
        int ax = 0;

        if (row_end > limit)
            row_end = limit;

        for (col = off; col < off + 16; col++) {
            if (col < row_end) {
                hx += snprintf(hex + hx, sizeof(hex) - (size_t)hx,
                               "%02X ", data[col]);
                asc[ax++] = isprint(data[col]) ? (char)data[col] : '.';
            } else {
                hx += snprintf(hex + hx, sizeof(hex) - (size_t)hx, "   ");
                asc[ax++] = ' ';
            }
        }
        asc[ax] = '\0';
        snprintf(line, sizeof(line), "%06zX  %-48s |%s|\n", off, hex, asc);

        {
            size_t llen = strlen(line);
            if (written + llen + 1 >= cap)
                break;
            memcpy(buf + written, line, llen);
            written += llen;
        }
    }
    buf[written] = '\0';
    return 0;
}

/* Format a color space enum as a string. */
const char *gmt_colorspace_str(gmt_colorspace cs)
{
    switch (cs) {
    case GMT_CS_GRAY: return "Gray";
    case GMT_CS_RGB:  return "RGB";
    case GMT_CS_CMYK: return "CMYK";
    case GMT_CS_LAB:  return "Lab";
    case GMT_CS_XYZ:  return "XYZ";
    case GMT_CS_NCH:  return "N-Channel";
    default:          return "Unknown";
    }
}

/* Format a status code as a string. */
const char *gmt_status_str(gmt_status s)
{
    switch (s) {
    case GMT_OK:              return "OK";
    case GMT_ERR_TRUNCATED:   return "Truncated";
    case GMT_ERR_MAGIC:       return "Bad magic";
    case GMT_ERR_VERSION:     return "Version mismatch";
    case GMT_ERR_RANGE:       return "Out of range";
    case GMT_ERR_TAGDIR:      return "Bad tag directory";
    case GMT_ERR_ELEMENT:     return "Element decode error";
    case GMT_ERR_CONSISTENCY: return "Cross-tag inconsistency";
    case GMT_ERR_UNSUPPORTED: return "Unsupported construct";
    case GMT_ERR_NOMEM:       return "Out of memory";
    default:                  return "Unknown error";
    }
}

/* Format a version number as "major.minor.patch". */
int gmt_format_version(uint32_t v, char *buf, size_t cap)
{
    unsigned major = (v >> 24) & 0xFF;
    unsigned minor = (v >> 16) & 0xFF;
    unsigned patch = v & 0xFFFF;
    if (cap < 16)
        return -1;
    snprintf(buf, cap, "%u.%u.%u", major, minor, patch);
    return 0;
}

/* Compare two signatures. Returns 0 if equal, negative if a < b, positive
 * if a > b. Used for sorting tag directories. */
int gmt_sig_cmp(uint32_t a, uint32_t b)
{
    if (a < b) return -1;
    if (a > b) return 1;
    return 0;
}

/* Check whether a byte range [offset, offset+size) fits within [0, total). */
int gmt_range_valid(uint32_t offset, uint32_t size, size_t total)
{
    if ((size_t)offset > total)
        return 0;
    if ((size_t)size > total - (size_t)offset)
        return 0;
    return 1;
}

/* Count the number of tags in a profile that share the same data offset. */
unsigned gmt_count_shared_offset(const gmt_profile *p, uint32_t offset)
{
    unsigned count = 0;
    size_t i;
    if (!p)
        return 0;
    for (i = 0; i < p->tag_count; i++)
        if (p->tags[i].offset == offset)
            count++;
    return count;
}

/* Format a tag entry as a summary string. */
int gmt_format_tag(const gmt_tag *t, char *buf, size_t cap)
{
    char sig[5];
    if (!t || cap < 64)
        return -1;
    gmt_sig_str(t->sig, sig);
    snprintf(buf, cap, "'%s' offset=%u size=%u pool=%d",
             sig, t->offset, t->size, t->pool_idx);
    return 0;
}

/* Format a curve element as a summary. */
int gmt_format_curve_summary(const gmt_curve *cv, char *buf, size_t cap)
{
    if (!cv || cap < 64)
        return -1;
    if (cv->fn_type == 0)
        snprintf(buf, cap, "sampled table, %u entries", cv->count);
    else
        snprintf(buf, cap, "parametric type %u, gamma=%.4f",
                 cv->fn_type, (double)cv->params[0]);
    return 0;
}

/* Format a CLUT as a summary. */
int gmt_format_clut_summary(const gmt_clut *cl, char *buf, size_t cap)
{
    char grid[128];
    int pos = 0;
    unsigned i;
    if (!cl || cap < 128)
        return -1;
    for (i = 0; i < cl->in_ch && i < GMT_MAX_CHANNELS; i++) {
        if (i > 0) grid[pos++] = 'x';
        pos += snprintf(grid + pos, sizeof(grid) - (size_t)pos,
                        "%u", cl->grid[i]);
    }
    grid[pos] = '\0';
    snprintf(buf, cap, "%u->%u grid=%s points=%u",
             cl->in_ch, cl->out_ch, grid, cl->point_count);
    return 0;
}
