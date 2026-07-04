/* headutil.c - extended header field parsing
 *
 * Additional fields from the profile header beyond the ones the core
 * pipeline requires. These are used by the inspection tool and for
 * diagnostic cross-checks.
 */
#include "internal.h"
#include <string.h>
#include <stdio.h>

typedef struct gmt_header_ext {
    uint32_t preferred_cmm;
    uint32_t profile_class;
    uint32_t creator;
    uint32_t rendering_intent;
    uint32_t flags;
    uint32_t attributes_hi;
    uint32_t attributes_lo;
    uint16_t year, month, day;
    uint16_t hour, minute, second;
    gmt_xyz  illuminant;
} gmt_header_ext;

int gmt_parse_header_ext(const uint8_t *buf, size_t size, gmt_header_ext *out)
{
    gmt_reader r;
    if (size < GMT_HEADER_SIZE)
        return -1;
    memset(out, 0, sizeof(*out));
    gmt_rd_init(&r, buf, size);

    /* Skip the fields already parsed by the core header phase. */
    gmt_rd_seek(&r, 28);
    out->preferred_cmm = gmt_rd_u32(&r);
    out->profile_class = gmt_rd_u32(&r);

    /* Date/time at offset 36. */
    gmt_rd_seek(&r, 36);
    out->year   = gmt_rd_u16(&r);
    out->month  = gmt_rd_u16(&r);
    out->day    = gmt_rd_u16(&r);
    out->hour   = gmt_rd_u16(&r);
    out->minute = gmt_rd_u16(&r);
    out->second = gmt_rd_u16(&r);

    /* Rendering intent at offset 64. */
    gmt_rd_seek(&r, 64);
    out->rendering_intent = gmt_rd_u32(&r);

    /* PCS illuminant at offset 68. */
    out->illuminant.x = gmt_rd_fixed(&r);
    out->illuminant.y = gmt_rd_fixed(&r);
    out->illuminant.z = gmt_rd_fixed(&r);

    /* Creator at offset 80. */
    gmt_rd_seek(&r, 80);
    out->creator = gmt_rd_u32(&r);

    /* Flags at offset 84. */
    out->flags = gmt_rd_u32(&r);

    /* Attributes at offset 88. */
    gmt_rd_seek(&r, 88);
    out->attributes_hi = gmt_rd_u32(&r);
    out->attributes_lo = gmt_rd_u32(&r);

    return r.overrun ? -1 : 0;
}

/* Validate the rendering intent field. Values 0-3 are defined. */
int gmt_validate_intent(uint32_t intent)
{
    return (intent <= 3) ? 1 : 0;
}

/* Check whether the PCS illuminant is close to D50. */
int gmt_validate_illuminant(const gmt_xyz *illum, float eps)
{
    float dx = illum->x - 0.9642f;
    float dy = illum->y - 1.0000f;
    float dz = illum->z - 0.8249f;
    if (dx < 0.0f) dx = -dx;
    if (dy < 0.0f) dy = -dy;
    if (dz < 0.0f) dz = -dz;
    return (dx < eps && dy < eps && dz < eps);
}

/* Format the date/time as "YYYY-MM-DD HH:MM:SS". */
int gmt_format_datetime(const gmt_header_ext *h, char *buf, size_t cap)
{
    if (cap < 20)
        return -1;
    snprintf(buf, cap, "%04u-%02u-%02u %02u:%02u:%02u",
             h->year, h->month, h->day,
             h->hour, h->minute, h->second);
    return 0;
}

/* Format a profile class signature as a string. */
const char *gmt_profile_class_str(uint32_t cls)
{
    switch (cls) {
    case 0x73636E72: return "Input (scnr)";
    case 0x6D6E7472: return "Display (mntr)";
    case 0x70727472: return "Output (prtr)";
    case 0x6C696E6B: return "DeviceLink (link)";
    case 0x73706163: return "ColorSpace (spac)";
    case 0x61627374: return "Abstract (abst)";
    case 0x6E6D636C: return "NamedColor (nmcl)";
    default:         return "Unknown";
    }
}

/* Format a rendering intent as a string. */
const char *gmt_intent_str(uint32_t intent)
{
    switch (intent) {
    case 0: return "Perceptual";
    case 1: return "Relative colorimetric";
    case 2: return "Saturation";
    case 3: return "Absolute colorimetric";
    default: return "Unknown";
    }
}

/* Validate the date/time fields for sanity. */
int gmt_validate_datetime(const gmt_header_ext *h)
{
    if (h->month < 1 || h->month > 12)
        return 0;
    if (h->day < 1 || h->day > 31)
        return 0;
    if (h->hour > 23 || h->minute > 59 || h->second > 59)
        return 0;
    if (h->year < 1900 || h->year > 2100)
        return 0;
    return 1;
}
