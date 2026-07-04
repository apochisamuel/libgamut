/* profile.c - public entry points and the phase pipeline driver
 *
 * gmt_open_memory runs the phases in order; each phase depends on state
 * established by the ones before it. A fatal status stops the pipeline; a
 * non-fatal status is recorded and parsing continues so callers can still
 * inspect what was decoded.
 */
#include "internal.h"
#include <stdlib.h>
#include <string.h>

#define GMT_LIB_VERSION "libgamut 0.1.0"

static gmt_profile *profile_new(void)
{
    gmt_profile *p = (gmt_profile *)calloc(1, sizeof(gmt_profile));
    if (!p)
        return NULL;
    gmt_ctx_init(&p->ctx);
    gmt_pool_init(&p->pool);
    gmt_link_init(&p->link);
    gmt_link_init(&p->link_inv);
    p->status = GMT_OK;
    return p;
}

gmt_profile *gmt_open_memory(const uint8_t *data, size_t size)
{
    gmt_profile *p;

    if (!data || size < GMT_HEADER_SIZE)
        return NULL;

    p = profile_new();
    if (!p)
        return NULL;

    p->buf = (uint8_t *)malloc(size);
    if (!p->buf) {
        free(p);
        return NULL;
    }
    memcpy(p->buf, data, size);
    p->buf_size = size;

    /* Phase 1-2: header and tag directory. */
    if (gmt_parse_header(p) != 0)   goto done;
    if (gmt_parse_tagtable(p) != 0) goto done;

    /* Phase 3-7: element decode (pool interning). */
    if (gmt_parse_elements(p) != 0) goto done;

    /* Phase 3b: colorant table. */
    gmt_parse_colorants(p);

    /* Phase 5: named color table. */
    gmt_parse_namedcolor(p);

    /* Phase 8: assemble the transform. */
    if (gmt_build_link(p) != 0)     goto done;

    /* Phase 8b: identifier verification. */
    gmt_verify_checksum(p);

    /* Phase 9: cross-tag validation (may drop inconsistent elements). */
    gmt_validate(p);

done:
    p->status = p->ctx.status;
    return p;
}

void gmt_close(gmt_profile *p)
{
    if (!p)
        return;
    gmt_link_free(&p->ctx, &p->link);
    gmt_link_free(&p->ctx, &p->link_inv);
    gmt_pool_free(&p->ctx, &p->pool);
    gmt_release(&p->ctx, p->tags);
    if (p->colorants.pool)
        gmt_release(&p->ctx, p->colorants.pool);
    if (p->colorants.names)
        gmt_release(&p->ctx, (void *)p->colorants.names);
    if (p->named.names)
        gmt_release(&p->ctx, (void *)p->named.names);
    if (p->named.prefix)
        gmt_release(&p->ctx, p->named.prefix);
    if (p->named.suffix)
        gmt_release(&p->ctx, p->named.suffix);
    if (p->named.pcs)
        gmt_release(&p->ctx, p->named.pcs);
    free(p->buf);
    free(p);
}

gmt_status gmt_error(const gmt_profile *p)
{
    return p ? p->status : GMT_ERR_NOMEM;
}

gmt_colorspace gmt_device_space(const gmt_profile *p)
{
    return p ? p->device_space : (gmt_colorspace)0;
}

gmt_colorspace gmt_connection_space(const gmt_profile *p)
{
    return p ? p->pcs : (gmt_colorspace)0;
}

unsigned gmt_channel_count(const gmt_profile *p)
{
    return p ? p->channels : 0;
}

uint32_t gmt_version(const gmt_profile *p)
{
    return p ? p->version : 0;
}

size_t gmt_tag_count(const gmt_profile *p)
{
    return p ? p->tag_count : 0;
}

uint32_t gmt_tag_signature(const gmt_profile *p, size_t index)
{
    if (!p || index >= p->tag_count)
        return 0;
    return p->tags[index].sig;
}

unsigned gmt_colorant_count(const gmt_profile *p)
{
    return p ? p->colorants.count : 0;
}

const char *gmt_colorant_name(const gmt_profile *p, unsigned index)
{
    if (!p || index >= p->colorants.count || !p->colorants.names)
        return NULL;
    return p->colorants.names[index];
}

unsigned gmt_named_color_count(const gmt_profile *p)
{
    return (p && p->has_named) ? p->named.count : 0;
}

const char *gmt_named_color_name(const gmt_profile *p, unsigned index)
{
    if (!p || !p->has_named || index >= p->named.count || !p->named.names)
        return NULL;
    return p->named.names[index];
}

gmt_status gmt_apply(const gmt_profile *p, const float *in, float *out)
{
    if (!p || !in || !out)
        return GMT_ERR_RANGE;
    return (gmt_status)gmt_resolve_apply(p, &p->link, in, out);
}

gmt_status gmt_apply_inverse(const gmt_profile *p, const float *in, float *out)
{
    if (!p || !in || !out)
        return GMT_ERR_RANGE;
    if (!p->link_inv.built)
        return GMT_ERR_UNSUPPORTED;
    return (gmt_status)gmt_resolve_apply(p, &p->link_inv, in, out);
}

const char *gmt_library_version(void)
{
    return GMT_LIB_VERSION;
}
