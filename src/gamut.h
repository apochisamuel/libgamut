/* libgamut - color profile parsing library
 *
 * Public API. A profile is parsed from an in-memory buffer in a series of
 * phases (see ARCHITECTURE.md). Once opened, a profile exposes tag metadata,
 * colorant information, and a device-to-connection-space transform.
 */
#ifndef GAMUT_H
#define GAMUT_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Opaque profile handle. */
typedef struct gmt_profile gmt_profile;

/* Error codes reported through gmt_error(). */
typedef enum {
    GMT_OK = 0,
    GMT_ERR_TRUNCATED,      /* buffer ended before a structure completed   */
    GMT_ERR_MAGIC,          /* header signature did not match              */
    GMT_ERR_VERSION,        /* profile version outside supported range     */
    GMT_ERR_RANGE,          /* an offset or length left the profile bounds */
    GMT_ERR_TAGDIR,         /* tag directory malformed                     */
    GMT_ERR_ELEMENT,        /* an element body failed to parse             */
    GMT_ERR_CONSISTENCY,    /* cross-tag validation flagged a mismatch     */
    GMT_ERR_UNSUPPORTED,    /* a recognized but unhandled construct        */
    GMT_ERR_NOMEM           /* allocation failed                           */
} gmt_status;

/* Profile/device color space identifiers (subset). */
typedef enum {
    GMT_CS_GRAY = 1,
    GMT_CS_RGB,
    GMT_CS_CMYK,
    GMT_CS_LAB,
    GMT_CS_XYZ,
    GMT_CS_NCH          /* N-channel; channel count taken from header */
} gmt_colorspace;

/* Parse a profile from a memory buffer. Returns NULL on hard failure; on
 * success the handle is valid even if non-fatal inconsistencies were noted
 * (query gmt_error() to inspect). The buffer is not retained; all needed
 * data is copied or decoded into owned allocations. */
gmt_profile *gmt_open_memory(const uint8_t *data, size_t size);

/* Release a profile and everything it owns. Safe on NULL. */
void gmt_close(gmt_profile *p);

/* Last status recorded on the profile. */
gmt_status gmt_error(const gmt_profile *p);

/* Header-level queries. */
gmt_colorspace gmt_device_space(const gmt_profile *p);
gmt_colorspace gmt_connection_space(const gmt_profile *p);
unsigned       gmt_channel_count(const gmt_profile *p);
uint32_t       gmt_version(const gmt_profile *p);

/* Tag directory queries. */
size_t   gmt_tag_count(const gmt_profile *p);
uint32_t gmt_tag_signature(const gmt_profile *p, size_t index);
int      gmt_has_tag(const gmt_profile *p, uint32_t signature);

/* Colorant queries. Names are borrowed from the profile and remain valid
 * until gmt_close(). Returns NULL if the index is out of range. */
unsigned    gmt_colorant_count(const gmt_profile *p);
const char *gmt_colorant_name(const gmt_profile *p, unsigned index);

/* Look up a colorant by name. Returns its index, or -1 if not found. */
int         gmt_colorant_index(const gmt_profile *p, const char *name);

/* Named-color queries. */
unsigned    gmt_named_color_count(const gmt_profile *p);
const char *gmt_named_color_name(const gmt_profile *p, unsigned index);

/* Run the assembled device-to-connection-space transform on one sample.
 * `in` has gmt_channel_count() components, `out` has connection-space
 * component count. Returns GMT_OK or an error status. */
gmt_status gmt_apply(const gmt_profile *p, const float *in, float *out);

/* Run the inverse (connection-space-to-device) transform if present. */
gmt_status gmt_apply_inverse(const gmt_profile *p, const float *in, float *out);

/* Library version string. */
const char *gmt_library_version(void);

#ifdef __cplusplus
}
#endif

#endif /* GAMUT_H */
