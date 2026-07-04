/* libgamut - internal declarations
 *
 * Shared data structures used across the parsing phases. Public consumers
 * see only gamut.h; everything here is private to the library.
 */
#ifndef GAMUT_INTERNAL_H
#define GAMUT_INTERNAL_H

#include "gamut.h"
#include <stdint.h>
#include <stddef.h>

#define GMT_MAGIC          0x676D7401u  /* 'gmt\x01' file signature        */
#define GMT_HEADER_SIZE    128u
#define GMT_MAX_CHANNELS   15
#define GMT_MIN_VERSION    0x00020000u
#define GMT_MAX_VERSION    0x00040300u

/* ---- context / error state ------------------------------------------- */

typedef struct gmt_ctx {
    gmt_status status;      /* most recent status code                     */
    int        fatal;       /* set when parsing cannot continue            */
    size_t     alloc_bytes; /* running total of live allocation, diagnostic*/
    unsigned   parse_depth; /* nesting of element decode, bounds recursion */
} gmt_ctx;

void  gmt_ctx_init(gmt_ctx *c);
void  gmt_fail(gmt_ctx *c, gmt_status s);      /* record non-fatal status  */
void  gmt_fail_fatal(gmt_ctx *c, gmt_status s);/* record + set fatal flag   */
void *gmt_alloc(gmt_ctx *c, size_t n);         /* calloc with accounting   */
void *gmt_realloc(gmt_ctx *c, void *p, size_t n);
void  gmt_release(gmt_ctx *c, void *p);        /* free with accounting     */

/* ---- byte reader ----------------------------------------------------- */

typedef struct gmt_reader {
    const uint8_t *base;
    size_t         size;
    size_t         pos;
    int            overrun;  /* set once a read passed the end             */
} gmt_reader;

void     gmt_rd_init(gmt_reader *r, const uint8_t *base, size_t size);
void     gmt_rd_seek(gmt_reader *r, size_t off);
int      gmt_rd_ok(const gmt_reader *r, size_t need);
uint8_t  gmt_rd_u8(gmt_reader *r);
uint16_t gmt_rd_u16(gmt_reader *r);
uint32_t gmt_rd_u32(gmt_reader *r);
int32_t  gmt_rd_s15f16(gmt_reader *r);   /* s15Fixed16 -> raw int          */
float    gmt_rd_fixed(gmt_reader *r);    /* s15Fixed16 -> float            */
void     gmt_rd_bytes(gmt_reader *r, uint8_t *dst, size_t n);

/* ---- parsed element bodies ------------------------------------------- */

typedef struct gmt_curve {
    uint16_t fn_type;    /* 0 = sampled table, 1..4 = parametric family    */
    uint32_t count;      /* sample count in table form                     */
    float   *samples;    /* owned; `count` entries                         */
    float    params[7];  /* parametric coefficients                        */
} gmt_curve;

typedef struct gmt_matrix {
    float   e[9];        /* row-major 3x3                                  */
    float   off[3];
    uint8_t rows, cols;
} gmt_matrix;

typedef struct gmt_clut {
    uint8_t  in_ch;
    uint8_t  out_ch;
    uint8_t  grid[GMT_MAX_CHANNELS];  /* points per input dimension        */
    uint32_t point_count;             /* product of grid[]                 */
    float   *table;                   /* owned; point_count * out_ch       */
} gmt_clut;

typedef struct gmt_lut {
    uint8_t in_ch, out_ch;
    int32_t a_curve[GMT_MAX_CHANNELS]; /* pool indices, -1 if absent       */
    int32_t m_curve[GMT_MAX_CHANNELS];
    int32_t b_curve[GMT_MAX_CHANNELS];
    int32_t matrix;                    /* pool index, -1 if absent          */
    int32_t clut;                      /* pool index, -1 if absent          */
} gmt_lut;

typedef enum { GMT_MP_CURVES = 1, GMT_MP_MATRIX, GMT_MP_CLUT } gmt_mp_op;

typedef struct gmt_mp_stage {
    gmt_mp_op op;
    uint8_t   in_ch, out_ch;
    int32_t   elem;      /* pool index of the element this stage runs      */
} gmt_mp_stage;

typedef struct gmt_mpet {
    uint8_t       in_ch, out_ch;
    uint32_t      stage_count;
    gmt_mp_stage *stages;    /* owned                                      */
    int           collapsed; /* single-stage sequence folded into one op   */
    int32_t       fold_elem; /* pool index folded when collapsed, else -1  */
    void         *fold_owned;/* element adopted by a collapsed sequence    */
} gmt_mpet;

typedef struct gmt_xyz { float x, y, z; } gmt_xyz;

typedef struct gmt_namedcolor {
    uint32_t      count;
    char         *prefix;   /* owned                                       */
    char         *suffix;   /* owned                                       */
    const char  **names;    /* owned array; entries point into the         */
                            /* colorant name pool                          */
    float        *pcs;      /* owned; count * 3                            */
} gmt_namedcolor;

/* ---- element wrapper + interning pool --------------------------------- */

typedef enum {
    GMT_EL_NONE = 0, GMT_EL_CURVE, GMT_EL_MATRIX, GMT_EL_CLUT,
    GMT_EL_LUT, GMT_EL_MPET, GMT_EL_NAMEDCOLOR, GMT_EL_XYZ, GMT_EL_TEXT
} gmt_el_kind;

typedef struct gmt_element {
    gmt_el_kind kind;
    uint32_t    src_offset;  /* file offset this element was decoded from  */
    uint32_t    hold;        /* retention counter maintained during        */
                             /* interning                                  */
    union {
        gmt_curve      *curve;
        gmt_matrix     *matrix;
        gmt_clut       *clut;
        gmt_lut        *lut;
        gmt_mpet       *mpet;
        gmt_namedcolor *nc;
        gmt_xyz        *xyz;
        char           *text;
    } u;
} gmt_element;

/* Elements are keyed by the file offset they were decoded from. A request
 * for an offset already present returns the existing element, so tags that
 * point at the same offset resolve to one element. */
typedef struct gmt_elem_pool {
    gmt_element **items;   /* owned wrapper pointers, stable across growth  */
    size_t        count;
    size_t        cap;
} gmt_elem_pool;

void         gmt_pool_init(gmt_elem_pool *pool);
gmt_element *gmt_pool_get(gmt_elem_pool *pool, uint32_t src_offset);
int32_t      gmt_pool_find(gmt_elem_pool *pool, uint32_t src_offset);
gmt_element *gmt_pool_add(gmt_ctx *c, gmt_elem_pool *pool, gmt_element *el);
gmt_element *gmt_pool_at(gmt_elem_pool *pool, int32_t index);
void         gmt_pool_free(gmt_ctx *c, gmt_elem_pool *pool);

int32_t         gmt_parse_element_at(gmt_profile *p, uint32_t offset);
gmt_element    *gmt_curve_decode(gmt_ctx *c, gmt_reader *r, uint32_t offset);
void            gmt_curve_free(gmt_ctx *c, gmt_element *el);
gmt_element    *gmt_clut_decode(gmt_ctx *c, gmt_reader *r, uint32_t offset);
void            gmt_clut_free(gmt_ctx *c, gmt_element *el);
gmt_element    *gmt_lut_decode(gmt_profile *p, gmt_reader *r, uint32_t offset);
void            gmt_lut_free(gmt_ctx *c, gmt_element *el);
void            gmt_lut_release_clut(gmt_profile *p, gmt_element *lut_el);
gmt_element    *gmt_mpet_decode(gmt_profile *p, gmt_reader *r, uint32_t offset);
void            gmt_mpet_free(gmt_ctx *c, gmt_element *el);
gmt_element    *gmt_xyz_decode(gmt_ctx *c, gmt_reader *r, uint32_t offset);
void            gmt_xyz_free(gmt_ctx *c, gmt_element *el);
gmt_element    *gmt_matrix_decode(gmt_ctx *c, gmt_reader *r, uint32_t offset);
void            gmt_matrix_free(gmt_ctx *c, gmt_element *el);
void            gmt_matrix_release_chad(gmt_profile *p);
gmt_element    *gmt_text_decode(gmt_ctx *c, gmt_reader *r, uint32_t offset);
void            gmt_text_free(gmt_ctx *c, gmt_element *el);
int             gmt_parse_colorants(gmt_profile *p);

/* ---- tag directory ---------------------------------------------------- */

typedef struct gmt_tag {
    uint32_t sig;
    uint32_t offset;
    uint32_t size;
    int32_t  pool_idx;   /* element pool index once resolved, -1 otherwise */
} gmt_tag;

const gmt_tag *gmt_find_tag(const gmt_profile *p, uint32_t sig);

/* ---- colorant table --------------------------------------------------- */

typedef struct gmt_colorants {
    unsigned     count;
    char        *pool;    /* owned; contiguous name bytes                  */
    size_t       pool_len;
    const char **names;   /* owned array; entries point into `pool`        */
} gmt_colorants;

/* ---- link cache (assembled transform) --------------------------------- */

typedef enum {
    GMT_ST_CURVE = 1, GMT_ST_MATRIX, GMT_ST_CLUT, GMT_ST_CHAD
} gmt_stage_kind;

typedef struct gmt_linkstage {
    gmt_stage_kind kind;
    uint8_t        in_ch, out_ch;
    /* pointers resolved from element bodies while the link is assembled */
    const float   *curve_samples;  uint32_t curve_count;
    const gmt_matrix *matrix;
    const float   *clut_table;     uint8_t  grid[GMT_MAX_CHANNELS];
    const gmt_xyz *white;
} gmt_linkstage;

typedef struct gmt_linkcache {
    gmt_linkstage  *stages;      /* value array; reallocated as it grows   */
    size_t          count;
    size_t          cap;
    gmt_linkstage **order;       /* application order over `stages`        */
    size_t          order_count;
    size_t          order_cap;
    uint8_t         in_ch, out_ch;
    int             built;
} gmt_linkcache;

void gmt_link_init(gmt_linkcache *lc);
void gmt_link_free(gmt_ctx *c, gmt_linkcache *lc);

/* ---- profile ---------------------------------------------------------- */

struct gmt_profile {
    gmt_ctx        ctx;

    uint32_t       version;
    gmt_colorspace device_space;
    gmt_colorspace pcs;
    unsigned       channels;
    unsigned       pcs_channels;
    uint32_t       stored_id;  /* profile identifier recorded in the header */

    uint8_t       *buf;        /* owned copy of the source buffer          */
    size_t         buf_size;

    gmt_tag       *tags;
    size_t         tag_count;

    gmt_elem_pool  pool;
    gmt_colorants  colorants;
    gmt_namedcolor named;
    int            has_named;
    gmt_element   *white;      /* white-point element                      */

    gmt_linkcache  link;
    gmt_linkcache  link_inv;

    gmt_status     status;
};

/* ---- phase entry points ---------------------------------------------- */

int gmt_parse_header(gmt_profile *p);
int gmt_parse_tagtable(gmt_profile *p);
int gmt_parse_elements(gmt_profile *p);
int gmt_parse_curves(gmt_profile *p);
int gmt_parse_matrix(gmt_profile *p);
int gmt_parse_luts(gmt_profile *p);
int gmt_parse_mpet(gmt_profile *p);
int gmt_parse_colorants(gmt_profile *p);
int gmt_parse_namedcolor(gmt_profile *p);
int gmt_build_link(gmt_profile *p);
int gmt_verify_checksum(gmt_profile *p);
int gmt_validate(gmt_profile *p);
int gmt_resolve_apply(const gmt_profile *p, const gmt_linkcache *lc,
                      const float *in, float *out);
void gmt_mpet_apply_clut(const gmt_linkstage *s, float *work);
size_t gmt_serialize(const gmt_profile *p, uint8_t *buf, size_t cap);
int32_t gmt_edit_add_curve(gmt_profile *p, const float *samples, uint32_t count);
int32_t gmt_edit_add_xyz(gmt_profile *p, float x, float y, float z);
int32_t gmt_edit_add_matrix(gmt_profile *p, const float e[9], const float off[3]);
int     gmt_edit_add_tag(gmt_profile *p, uint32_t sig, int32_t pool_idx);

/* signature helpers */
#define GMT_SIG(a,b,c,d) \
    (((uint32_t)(a)<<24)|((uint32_t)(b)<<16)|((uint32_t)(c)<<8)|(uint32_t)(d))

#endif /* GAMUT_INTERNAL_H */
