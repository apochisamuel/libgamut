/* gen_seeds.c - generate seed corpus files for fuzzer harnesses
 *
 * Produces a set of minimal structurally-valid profiles covering the major
 * tag combinations: RGB with curves, CMYK with colorants, profiles with
 * LUT and multi-process elements, profiles with white points and adaptation
 * matrices, and gray profiles with a single tone curve.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

static void u32(uint8_t *b, size_t o, uint32_t v)
{ b[o]=(uint8_t)(v>>24); b[o+1]=(uint8_t)(v>>16);
  b[o+2]=(uint8_t)(v>>8); b[o+3]=(uint8_t)v; }
static void u16(uint8_t *b, size_t o, uint16_t v)
{ b[o]=(uint8_t)(v>>8); b[o+1]=(uint8_t)v; }
static void u8b(uint8_t *b, size_t o, uint8_t v) { b[o]=v; }

#define SIG(a,b,c,d) \
  (((uint32_t)(a)<<24)|((uint32_t)(b)<<16)|((uint32_t)(c)<<8)|(uint32_t)(d))
#define FIX(f) ((uint32_t)(int32_t)((f) * 65536.0f))
#define MAGIC 0x676D7401u

static size_t write_header(uint8_t *b, size_t total, uint32_t dev,
                           uint32_t pcs, uint32_t nch)
{
    memset(b, 0, 128);
    u32(b, 0, (uint32_t)total);
    u32(b, 4, MAGIC);
    u32(b, 8, 0x00040000u);
    u32(b, 12, dev);
    u32(b, 16, pcs);
    u32(b, 20, nch);
    return 128;
}

static void write_file(const char *dir, const char *name,
                       const uint8_t *data, size_t size)
{
    char path[512];
    FILE *f;
    snprintf(path, sizeof(path), "%s/%s", dir, name);
    f = fopen(path, "wb");
    if (!f) { perror(path); return; }
    fwrite(data, 1, size, f);
    fclose(f);
    fprintf(stderr, "  %s (%zu bytes)\n", path, size);
}

/* Seed 1: minimal RGB profile with three shared tone curves. */
static void gen_rgb_shared(const char *dir)
{
    uint8_t b[512];
    size_t td = 128, curve = td + 4 + 3*12;
    uint32_t nsamp = 8;
    size_t csz = 4 + 2 + 2 + 4 + nsamp*2;
    size_t total = curve + csz;
    unsigned i;

    write_header(b, total, 2, 5, 3);
    u32(b, td, 3);
    u32(b, td+4,  SIG('r','T','R','C')); u32(b, td+8,  (uint32_t)curve); u32(b, td+12, (uint32_t)csz);
    u32(b, td+16, SIG('g','T','R','C')); u32(b, td+20, (uint32_t)curve); u32(b, td+24, (uint32_t)csz);
    u32(b, td+28, SIG('b','T','R','C')); u32(b, td+32, (uint32_t)curve); u32(b, td+36, (uint32_t)csz);
    u32(b, curve, SIG('c','u','r','v'));
    u16(b, curve+4, 0); u16(b, curve+6, 0);
    u32(b, curve+8, nsamp);
    for (i = 0; i < nsamp; i++)
        u16(b, curve+12+i*2, (uint16_t)(i * (65535/(nsamp-1))));
    write_file(dir, "seed_rgb_shared.bin", b, total);
}

/* Seed 2: RGB profile with distinct tone curves. */
static void gen_rgb_distinct(const char *dir)
{
    uint8_t b[1024];
    size_t td = 128;
    uint32_t nsamp = 4;
    size_t csz = 4 + 2 + 2 + 4 + nsamp*2;
    size_t c0 = td + 4 + 3*12;
    size_t c1 = c0 + csz;
    size_t c2 = c1 + csz;
    size_t total = c2 + csz;
    unsigned i, ch;

    write_header(b, total, 2, 5, 3);
    u32(b, td, 3);
    u32(b, td+4,  SIG('r','T','R','C')); u32(b, td+8,  (uint32_t)c0); u32(b, td+12, (uint32_t)csz);
    u32(b, td+16, SIG('g','T','R','C')); u32(b, td+20, (uint32_t)c1); u32(b, td+24, (uint32_t)csz);
    u32(b, td+28, SIG('b','T','R','C')); u32(b, td+32, (uint32_t)c2); u32(b, td+36, (uint32_t)csz);

    for (ch = 0; ch < 3; ch++) {
        size_t co = (ch == 0) ? c0 : (ch == 1) ? c1 : c2;
        u32(b, co, SIG('c','u','r','v'));
        u16(b, co+4, 0); u16(b, co+6, 0);
        u32(b, co+8, nsamp);
        for (i = 0; i < nsamp; i++)
            u16(b, co+12+i*2, (uint16_t)(i * (65535/(nsamp-1)) + ch*100));
    }
    write_file(dir, "seed_rgb_distinct.bin", b, total);
}

/* Seed 3: Gray profile with a single curve. */
static void gen_gray(const char *dir)
{
    uint8_t b[256];
    size_t td = 128, curve = td + 4 + 12;
    uint32_t nsamp = 4;
    size_t csz = 4 + 2 + 2 + 4 + nsamp*2;
    size_t total = curve + csz;
    unsigned i;

    write_header(b, total, 1, 5, 1);
    u32(b, td, 1);
    u32(b, td+4, SIG('k','T','R','C')); u32(b, td+8, (uint32_t)curve); u32(b, td+12, (uint32_t)csz);
    u32(b, curve, SIG('c','u','r','v'));
    u16(b, curve+4, 0); u16(b, curve+6, 0);
    u32(b, curve+8, nsamp);
    for (i = 0; i < nsamp; i++)
        u16(b, curve+12+i*2, (uint16_t)(i * (65535/(nsamp-1))));
    write_file(dir, "seed_gray.bin", b, total);
}

/* Seed 4: profile with white point. */
static void gen_wtpt(const char *dir)
{
    uint8_t b[256];
    size_t td = 128, wt = td + 4 + 12;
    size_t wsz = 16;
    size_t total = wt + wsz;

    write_header(b, total, 5, 5, 3);
    u32(b, td, 1);
    u32(b, td+4, SIG('w','t','p','t')); u32(b, td+8, (uint32_t)wt); u32(b, td+12, (uint32_t)wsz);
    u32(b, wt, SIG('X','Y','Z',' '));
    u32(b, wt+4, FIX(0.9505f));
    u32(b, wt+8, FIX(1.0f));
    u32(b, wt+12, FIX(1.0890f));
    write_file(dir, "seed_wtpt.bin", b, total);
}

/* Seed 5: profile with CLUT via D2B0 mpet. */
static void gen_mpet_clut(const char *dir)
{
    uint8_t b[512];
    size_t td = 128, mpet = td + 4 + 12;
    size_t msz = 20;
    size_t clut = mpet + msz;
    size_t csz = 4 + 1 + 1 + 2 + 3 + 24*2;
    size_t total = clut + csz;
    unsigned i;

    write_header(b, total, 2, 5, 3);
    u32(b, td, 1);
    u32(b, td+4, SIG('D','2','B','0')); u32(b, td+8, (uint32_t)mpet); u32(b, td+12, (uint32_t)msz);
    u32(b, mpet, SIG('m','p','e','t'));
    u8b(b, mpet+4, 3); u8b(b, mpet+5, 3); u16(b, mpet+6, 0);
    u32(b, mpet+8, 1);
    u32(b, mpet+12, SIG('c','l','u','t'));
    u32(b, mpet+16, (uint32_t)clut);
    u32(b, clut, SIG('c','l','u','t'));
    u8b(b, clut+4, 3); u8b(b, clut+5, 3); u16(b, clut+6, 0);
    u8b(b, clut+8, 2); u8b(b, clut+9, 2); u8b(b, clut+10, 2);
    for (i = 0; i < 24; i++)
        u16(b, clut+11+i*2, (uint16_t)(i * 2000));
    write_file(dir, "seed_mpet_clut.bin", b, total);
}

/* Seed 6: CMYK profile with colorants. */
static void gen_cmyk_colorants(const char *dir)
{
    uint8_t b[512];
    size_t td = 128;
    size_t clrt = td + 4 + 12;
    size_t csz = 4 + 4;
    size_t total = clrt + csz;

    write_header(b, total, 3, 5, 4);
    u32(b, td, 1);
    u32(b, td+4, SIG('c','l','r','t')); u32(b, td+8, (uint32_t)clrt); u32(b, td+12, (uint32_t)csz);
    u32(b, clrt, SIG('c','l','r','t'));
    u32(b, clrt+4, 4);
    write_file(dir, "seed_cmyk.bin", b, total);
}

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : "fuzz/corpus/parse_fuzzer";
    fprintf(stderr, "generating seeds into %s\n", dir);
    gen_rgb_shared(dir);
    gen_rgb_distinct(dir);
    gen_gray(dir);
    gen_wtpt(dir);
    gen_mpet_clut(dir);
    gen_cmyk_colorants(dir);
    fprintf(stderr, "done\n");
    return 0;
}
