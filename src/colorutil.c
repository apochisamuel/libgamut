/* colorutil.c - colorant and named-color utilities
 *
 * Operations on colorant tables and named colors: name-based matching,
 * standard ICC colorant name recognition, PCS coordinate extraction, and
 * colorant ordering validation.
 */
#include "internal.h"
#include <string.h>

/* Standard CMYK colorant names. */
static const char *cmyk_names[4] = { "Cyan", "Magenta", "Yellow", "Black" };
static const char *rgb_names[3]  = { "Red", "Green", "Blue" };

/* Check whether a colorant name matches one of the standard CMYK names
 * (case-insensitive first-letter match). Returns the CMYK index (0-3) or
 * -1 if unrecognized. */
int gmt_colorant_cmyk_index(const char *name)
{
    int i;
    if (!name || name[0] == '\0')
        return -1;
    for (i = 0; i < 4; i++) {
        if (name[0] == cmyk_names[i][0] ||
            name[0] == (cmyk_names[i][0] + 32))  /* lower-case */
            return i;
    }
    return -1;
}

/* Check whether a colorant name matches one of the standard RGB names. */
int gmt_colorant_rgb_index(const char *name)
{
    int i;
    if (!name || name[0] == '\0')
        return -1;
    for (i = 0; i < 3; i++) {
        if (name[0] == rgb_names[i][0] ||
            name[0] == (rgb_names[i][0] + 32))
            return i;
    }
    return -1;
}

/* Validate the colorant order: for a CMYK profile, the colorants should
 * appear in C-M-Y-K order. Returns 1 if correct, 0 if misordered. */
int gmt_validate_colorant_order(const gmt_colorants *c, gmt_colorspace cs)
{
    unsigned i;
    if (cs == GMT_CS_CMYK && c->count >= 4 && c->names) {
        for (i = 0; i < 4; i++) {
            if (gmt_colorant_cmyk_index(c->names[i]) != (int)i)
                return 0;
        }
    } else if (cs == GMT_CS_RGB && c->count >= 3 && c->names) {
        for (i = 0; i < 3; i++) {
            if (gmt_colorant_rgb_index(c->names[i]) != (int)i)
                return 0;
        }
    }
    return 1;
}

/* Compute the total string storage used by colorant names. */
size_t gmt_colorant_pool_size(const gmt_colorants *c)
{
    return c ? c->pool_len : 0;
}

/* Count how many colorant names are non-empty. */
unsigned gmt_colorant_named_count(const gmt_colorants *c)
{
    unsigned n = 0;
    unsigned i;
    if (!c || !c->names)
        return 0;
    for (i = 0; i < c->count; i++)
        if (c->names[i] && c->names[i][0] != '\0')
            n++;
    return n;
}

/* Case-insensitive byte-by-byte comparison (ASCII only). */
static int ci_cmp(const char *a, const char *b)
{
    while (*a && *b) {
        char ca = *a, cb = *b;
        if (ca >= 'A' && ca <= 'Z') ca += 32;
        if (cb >= 'A' && cb <= 'Z') cb += 32;
        if (ca != cb) return (int)(unsigned char)ca - (int)(unsigned char)cb;
        a++; b++;
    }
    return (int)(unsigned char)*a - (int)(unsigned char)*b;
}

/* Case-insensitive colorant name search. */
int gmt_colorant_find_ci(const gmt_colorants *c, const char *name)
{
    unsigned i;
    if (!c || !name || !c->names)
        return -1;
    for (i = 0; i < c->count; i++)
        if (c->names[i] && ci_cmp(c->names[i], name) == 0)
            return (int)i;
    return -1;
}

/* Check for duplicate colorant names. */
int gmt_colorant_has_duplicates(const gmt_colorants *c)
{
    unsigned i, j;
    if (!c || !c->names)
        return 0;
    for (i = 0; i < c->count; i++)
        for (j = i + 1; j < c->count; j++)
            if (c->names[i] && c->names[j] &&
                strcmp(c->names[i], c->names[j]) == 0)
                return 1;
    return 0;
}

/* Format a colorant list as a comma-separated string. */
int gmt_format_colorants(const gmt_colorants *c, char *buf, size_t cap)
{
    size_t pos = 0;
    unsigned i;
    if (!c || !c->names || cap < 16) {
        if (cap > 0) buf[0] = '\0';
        return -1;
    }
    buf[0] = '\0';
    for (i = 0; i < c->count && pos < cap - 2; i++) {
        const char *n = c->names[i];
        size_t nlen;
        if (i > 0 && pos < cap - 2) {
            buf[pos++] = ',';
            buf[pos++] = ' ';
        }
        if (!n) n = "?";
        nlen = strlen(n);
        if (pos + nlen >= cap - 1) break;
        memcpy(buf + pos, n, nlen);
        pos += nlen;
    }
    buf[pos] = '\0';
    return 0;
}

/* Named-color lookup by name (case-sensitive). */
int gmt_named_color_find(const gmt_profile *p, const char *name)
{
    unsigned i;
    if (!p || !p->has_named || !name || !p->named.names)
        return -1;
    for (i = 0; i < p->named.count; i++)
        if (p->named.names[i] && strcmp(p->named.names[i], name) == 0)
            return (int)i;
    return -1;
}

/* Named-color case-insensitive lookup. */
int gmt_named_color_find_ci(const gmt_profile *p, const char *name)
{
    unsigned i;
    if (!p || !p->has_named || !name || !p->named.names)
        return -1;
    for (i = 0; i < p->named.count; i++)
        if (p->named.names[i] && ci_cmp(p->named.names[i], name) == 0)
            return (int)i;
    return -1;
}

/* Verify that named-color PCS values are within the valid L*a*b* range. */
int gmt_validate_named_pcs(const gmt_namedcolor *nc)
{
    unsigned i;
    if (!nc || !nc->pcs)
        return 1;
    for (i = 0; i < nc->count; i++) {
        float L = nc->pcs[i * 3 + 0];
        float a = nc->pcs[i * 3 + 1];
        float b = nc->pcs[i * 3 + 2];
        if (L < 0.0f || L > 100.0f)
            return 0;
        if (a < -128.0f || a > 127.0f)
            return 0;
        if (b < -128.0f || b > 127.0f)
            return 0;
    }
    return 1;
}
