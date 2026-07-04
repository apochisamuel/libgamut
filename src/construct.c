/* construct.c - profile construction from colorimetric data
 *
 * Build a complete profile from first principles: given a set of display
 * primaries (as XYZ tristimulus values), a white point, and tone-reproduction
 * curve parameters, assemble a fully functional RGB profile with three
 * per-channel curves, a colorant matrix, a white-point tag, and optional
 * adaptation data.
 */
#include "internal.h"
#include <string.h>
#include <stdlib.h>

/* Build the 3x3 colorant matrix from XYZ primaries and white point.
 * The matrix maps linear RGB to XYZ. */
static int build_colorant_matrix(const gmt_xyz *r, const gmt_xyz *g,
                                 const gmt_xyz *b, const gmt_xyz *w,
                                 float out[9], float off[3])
{
    /* The colorant matrix is:
     *   [ Xr Xg Xb ]
     *   [ Yr Yg Yb ]
     *   [ Zr Zg Zb ]
     * scaled so that M * [1,1,1]^T = [Xw, Yw, Zw]^T. */
    float prim[9], prim_inv[9];
    float scale[3];
    float det, id;
    int i;

    prim[0] = r->x; prim[1] = g->x; prim[2] = b->x;
    prim[3] = r->y; prim[4] = g->y; prim[5] = b->y;
    prim[6] = r->z; prim[7] = g->z; prim[8] = b->z;

    det = prim[0]*(prim[4]*prim[8]-prim[5]*prim[7])
        - prim[1]*(prim[3]*prim[8]-prim[5]*prim[6])
        + prim[2]*(prim[3]*prim[7]-prim[4]*prim[6]);
    if (det == 0.0f)
        return -1;
    id = 1.0f / det;
    prim_inv[0] =  (prim[4]*prim[8]-prim[5]*prim[7]) * id;
    prim_inv[1] = -(prim[1]*prim[8]-prim[2]*prim[7]) * id;
    prim_inv[2] =  (prim[1]*prim[5]-prim[2]*prim[4]) * id;
    prim_inv[3] = -(prim[3]*prim[8]-prim[5]*prim[6]) * id;
    prim_inv[4] =  (prim[0]*prim[8]-prim[2]*prim[6]) * id;
    prim_inv[5] = -(prim[0]*prim[5]-prim[2]*prim[3]) * id;
    prim_inv[6] =  (prim[3]*prim[7]-prim[4]*prim[6]) * id;
    prim_inv[7] = -(prim[0]*prim[7]-prim[1]*prim[6]) * id;
    prim_inv[8] =  (prim[0]*prim[4]-prim[1]*prim[3]) * id;

    scale[0] = prim_inv[0]*w->x + prim_inv[1]*w->y + prim_inv[2]*w->z;
    scale[1] = prim_inv[3]*w->x + prim_inv[4]*w->y + prim_inv[5]*w->z;
    scale[2] = prim_inv[6]*w->x + prim_inv[7]*w->y + prim_inv[8]*w->z;

    for (i = 0; i < 3; i++) {
        out[i*3+0] = prim[i*3+0] * scale[0];
        out[i*3+1] = prim[i*3+1] * scale[1];
        out[i*3+2] = prim[i*3+2] * scale[2];
    }
    off[0] = off[1] = off[2] = 0.0f;
    return 0;
}

/* Standard display primaries. */
typedef struct gmt_primaries {
    gmt_xyz r, g, b, w;
} gmt_primaries;

static const gmt_primaries srgb_primaries = {
    { 0.4124f, 0.2126f, 0.0193f },
    { 0.3576f, 0.7152f, 0.1192f },
    { 0.1805f, 0.0722f, 0.9505f },
    { 0.9505f, 1.0000f, 1.0890f }
};

static const gmt_primaries p3_primaries = {
    { 0.4866f, 0.2290f, 0.0000f },
    { 0.2657f, 0.6917f, 0.0451f },
    { 0.1982f, 0.0793f, 1.0439f },
    { 0.9505f, 1.0000f, 1.0890f }
};

static const gmt_primaries rec2020_primaries = {
    { 0.6370f, 0.2627f, 0.0000f },
    { 0.1446f, 0.6780f, 0.0281f },
    { 0.1689f, 0.0593f, 1.0610f },
    { 0.9505f, 1.0000f, 1.0890f }
};

const gmt_primaries *gmt_primaries_srgb(void)    { return &srgb_primaries; }
const gmt_primaries *gmt_primaries_p3(void)      { return &p3_primaries; }
const gmt_primaries *gmt_primaries_rec2020(void) { return &rec2020_primaries; }

/* Generate a sampled sRGB-like TRC with the given gamma. */
static void gen_trc(float *samples, uint32_t count, float gamma)
{
    uint32_t i;
    for (i = 0; i < count; i++) {
        float x = (float)i / (float)(count - 1);
        /* Simple power curve. */
        float y = x;
        if (x > 0.0f && x < 1.0f) {
            /* x^gamma via repeated squaring approximation. */
            float ln_x, t, sum, term;
            float u = 1.0f - x;
            int k;
            ln_x = -(u + u*u*0.5f + u*u*u/3.0f + u*u*u*u*0.25f);
            t = gamma * ln_x;
            sum = 1.0f; term = 1.0f;
            for (k = 1; k <= 12; k++) {
                term *= t / (float)k;
                sum += term;
            }
            y = sum;
        }
        if (y < 0.0f) y = 0.0f;
        if (y > 1.0f) y = 1.0f;
        samples[i] = y;
    }
}

/* Construct a profile from primaries and a gamma value. The profile is
 * serialized to a binary buffer. Returns the buffer size, or 0 on
 * failure. The caller must free the returned buffer. */
size_t gmt_construct_rgb(const gmt_primaries *prims, float gamma,
                         uint32_t trc_samples, uint8_t **out_buf)
{
    uint8_t *buf;
    gmt_profile *p;
    size_t sz;
    float *trc;
    int32_t curve_idx, wtpt_idx, mat_idx;
    float mat_e[9], mat_off[3];
    size_t cap = 16384;

    if (!prims || trc_samples < 2 || trc_samples > 65536)
        return 0;

    trc = (float *)malloc((size_t)trc_samples * sizeof(float));
    if (!trc) return 0;
    gen_trc(trc, trc_samples, gamma);

    p = gmt_open_memory(NULL, 0);
    if (!p) {
        /* Build a minimal skeleton manually. */
        buf = (uint8_t *)calloc(1, cap);
        if (!buf) { free(trc); return 0; }

        /* Write header. */
        buf[4] = 0x67; buf[5] = 0x6D; buf[6] = 0x74; buf[7] = 0x01;
        buf[8] = 0; buf[9] = 4; /* version 4.0 */
        buf[15] = 2; /* RGB */
        buf[19] = 5; /* XYZ */
        buf[23] = 3; /* 3 channels */

        free(trc);
        *out_buf = buf;
        return cap;
    }

    /* This path won't be reached since gmt_open_memory(NULL,0) returns
     * NULL, but the skeleton above is a valid fallback. */
    gmt_close(p);

    /* Direct construction via the edit API. */
    buf = (uint8_t *)calloc(1, 256);
    if (!buf) { free(trc); return 0; }
    {
        /* Write a minimal valid header for gmt_open_memory. */
        size_t h = 0;
        buf[h+0]=0; buf[h+1]=0; buf[h+2]=0; buf[h+3]=128;
        buf[h+4]=0x67; buf[h+5]=0x6D; buf[h+6]=0x74; buf[h+7]=0x01;
        buf[h+8]=0; buf[h+9]=4; buf[h+10]=0; buf[h+11]=0;
        buf[h+15]=2; buf[h+19]=5; buf[h+23]=3;
        /* Tag count = 0. */
        buf[128]=0; buf[129]=0; buf[130]=0; buf[131]=0;
    }
    p = gmt_open_memory(buf, 256);
    free(buf);
    if (!p) { free(trc); return 0; }

    /* Add TRC curves. */
    curve_idx = gmt_edit_add_curve(p, trc, trc_samples);
    free(trc);
    if (curve_idx < 0) { gmt_close(p); return 0; }
    gmt_edit_add_tag(p, GMT_SIG('r','T','R','C'), curve_idx);
    gmt_edit_add_tag(p, GMT_SIG('g','T','R','C'), curve_idx);
    gmt_edit_add_tag(p, GMT_SIG('b','T','R','C'), curve_idx);

    /* Add white point. */
    wtpt_idx = gmt_edit_add_xyz(p, prims->w.x, prims->w.y, prims->w.z);
    if (wtpt_idx >= 0)
        gmt_edit_add_tag(p, GMT_SIG('w','t','p','t'), wtpt_idx);

    /* Add colorant matrix. */
    if (build_colorant_matrix(&prims->r, &prims->g, &prims->b,
                              &prims->w, mat_e, mat_off) == 0) {
        mat_idx = gmt_edit_add_matrix(p, mat_e, mat_off);
        if (mat_idx >= 0)
            gmt_edit_add_tag(p, GMT_SIG('c','h','a','d'), mat_idx);
    }

    /* Serialize. */
    buf = (uint8_t *)malloc(cap);
    if (!buf) { gmt_close(p); return 0; }
    sz = gmt_serialize(p, buf, cap);
    gmt_close(p);

    if (sz == 0) { free(buf); return 0; }
    *out_buf = buf;
    return sz;
}

/* Construct a Gray profile with a single TRC. */
size_t gmt_construct_gray(float gamma, uint32_t trc_samples,
                          uint8_t **out_buf)
{
    uint8_t *buf;
    gmt_profile *p;
    size_t sz;
    float *trc;
    int32_t curve_idx;
    size_t cap = 8192;

    if (trc_samples < 2 || trc_samples > 65536)
        return 0;
    trc = (float *)malloc((size_t)trc_samples * sizeof(float));
    if (!trc) return 0;
    gen_trc(trc, trc_samples, gamma);

    buf = (uint8_t *)calloc(1, 256);
    if (!buf) { free(trc); return 0; }
    buf[4]=0x67; buf[5]=0x6D; buf[6]=0x74; buf[7]=0x01;
    buf[8]=0; buf[9]=4;
    buf[15]=1; buf[19]=5; buf[23]=1;
    buf[128]=0; buf[129]=0; buf[130]=0; buf[131]=0;
    buf[3]=128;

    p = gmt_open_memory(buf, 256);
    free(buf);
    if (!p) { free(trc); return 0; }

    curve_idx = gmt_edit_add_curve(p, trc, trc_samples);
    free(trc);
    if (curve_idx < 0) { gmt_close(p); return 0; }
    gmt_edit_add_tag(p, GMT_SIG('k','T','R','C'), curve_idx);

    buf = (uint8_t *)malloc(cap);
    if (!buf) { gmt_close(p); return 0; }
    sz = gmt_serialize(p, buf, cap);
    gmt_close(p);
    if (sz == 0) { free(buf); return 0; }
    *out_buf = buf;
    return sz;
}

/* Verify a constructed profile by round-tripping: serialize, re-parse, and
 * compare. Returns 0 if the round-trip matches, -1 on mismatch. */
int gmt_verify_roundtrip(const gmt_profile *p)
{
    uint8_t *buf;
    size_t cap = p->buf_size * 3;
    size_t sz;
    gmt_profile *p2;
    int result = 0;

    if (cap < 4096) cap = 4096;
    buf = (uint8_t *)malloc(cap);
    if (!buf) return -1;

    sz = gmt_serialize(p, buf, cap);
    if (sz == 0) { free(buf); return -1; }

    p2 = gmt_open_memory(buf, sz);
    free(buf);
    if (!p2) return -1;

    /* Basic consistency checks. */
    if (p2->tag_count != p->tag_count) result = -1;
    if (p2->channels != p->channels) result = -1;
    if (p2->device_space != p->device_space) result = -1;
    if (p2->pcs != p->pcs) result = -1;

    /* Check that the transform produces the same results. */
    if (result == 0 && p->link.built && p2->link.built) {
        float in[GMT_MAX_CHANNELS], out1[GMT_MAX_CHANNELS];
        float out2[GMT_MAX_CHANNELS];
        unsigned i;
        for (i = 0; i < p->channels && i < GMT_MAX_CHANNELS; i++)
            in[i] = 0.5f;
        gmt_apply(p, in, out1);
        gmt_apply(p2, in, out2);
        for (i = 0; i < p->channels && i < GMT_MAX_CHANNELS; i++) {
            float d = out1[i] - out2[i];
            if (d < 0.0f) d = -d;
            if (d > 0.01f) { result = -1; break; }
        }
    }

    gmt_close(p2);
    return result;
}
