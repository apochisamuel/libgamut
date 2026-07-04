/* inspect.c - command-line profile inspector
 *
 * Reads a profile from a file and prints a structured summary: header
 * metadata, tag directory, element pool contents, transform assembly, and
 * cross-tag consistency notes.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gamut.h"

static void print_header(const gmt_profile *p)
{
    printf("=== Header ===\n");
    printf("Version:          %u.%u\n",
           (gmt_version(p) >> 24) & 0xFF,
           (gmt_version(p) >> 16) & 0xFF);
    printf("Device space:     %u\n", (unsigned)gmt_device_space(p));
    printf("PCS:              %u\n", (unsigned)gmt_connection_space(p));
    printf("Channels:         %u\n", gmt_channel_count(p));
    printf("Status:           %d\n", (int)gmt_error(p));
    printf("\n");
}

static void print_tags(const gmt_profile *p)
{
    size_t count = gmt_tag_count(p);
    size_t i;
    printf("=== Tags (%zu) ===\n", count);
    for (i = 0; i < count; i++) {
        uint32_t sig = gmt_tag_signature(p, i);
        char s[5];
        s[0] = (char)((sig >> 24) & 0xFF);
        s[1] = (char)((sig >> 16) & 0xFF);
        s[2] = (char)((sig >> 8) & 0xFF);
        s[3] = (char)(sig & 0xFF);
        s[4] = '\0';
        printf("  [%zu] '%s' (0x%08X)\n", i, s, sig);
    }
    printf("\n");
}

static void print_colorants(const gmt_profile *p)
{
    unsigned nc = gmt_colorant_count(p);
    unsigned i;
    if (nc == 0)
        return;
    printf("=== Colorants (%u) ===\n", nc);
    for (i = 0; i < nc; i++) {
        const char *name = gmt_colorant_name(p, i);
        printf("  [%u] %s\n", i, name ? name : "(none)");
    }
    printf("\n");
}

static void print_named_colors(const gmt_profile *p)
{
    unsigned nn = gmt_named_color_count(p);
    unsigned i;
    if (nn == 0)
        return;
    printf("=== Named Colors (%u) ===\n", nn);
    for (i = 0; i < nn && i < 32; i++) {
        const char *name = gmt_named_color_name(p, i);
        printf("  [%u] %s\n", i, name ? name : "(none)");
    }
    if (nn > 32)
        printf("  ... and %u more\n", nn - 32);
    printf("\n");
}

static void print_transform_test(const gmt_profile *p)
{
    unsigned nch = gmt_channel_count(p);
    float in[15], out[15];
    unsigned i;
    gmt_status st;

    if (nch == 0 || nch > 15)
        return;

    printf("=== Transform Test ===\n");
    for (i = 0; i < nch; i++)
        in[i] = 0.5f;

    st = gmt_apply(p, in, out);
    printf("  Status: %d\n", (int)st);
    if (st == GMT_OK) {
        printf("  Input: ");
        for (i = 0; i < nch; i++)
            printf(" %.4f", (double)in[i]);
        printf("\n  Output:");
        for (i = 0; i < nch; i++)
            printf(" %.4f", (double)out[i]);
        printf("\n");
    }
    printf("\n");
}

static uint8_t *read_file(const char *path, size_t *out_size)
{
    FILE *f;
    uint8_t *buf;
    long len;

    f = fopen(path, "rb");
    if (!f) { perror(path); return NULL; }
    fseek(f, 0, SEEK_END);
    len = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (len <= 0 || len > 64 * 1024 * 1024) {
        fprintf(stderr, "file too large or empty\n");
        fclose(f);
        return NULL;
    }
    buf = (uint8_t *)malloc((size_t)len);
    if (!buf) { fclose(f); return NULL; }
    if (fread(buf, 1, (size_t)len, f) != (size_t)len) {
        free(buf);
        fclose(f);
        return NULL;
    }
    fclose(f);
    *out_size = (size_t)len;
    return buf;
}

int main(int argc, char **argv)
{
    uint8_t *data;
    size_t size;
    gmt_profile *p;
    int i;

    if (argc < 2) {
        fprintf(stderr, "usage: %s <profile.bin> [...]\n", argv[0]);
        return 1;
    }

    for (i = 1; i < argc; i++) {
        printf("--- %s ---\n", argv[i]);
        data = read_file(argv[i], &size);
        if (!data)
            continue;
        p = gmt_open_memory(data, size);
        if (!p) {
            fprintf(stderr, "  failed to open\n");
            free(data);
            continue;
        }
        print_header(p);
        print_tags(p);
        print_colorants(p);
        print_named_colors(p);
        print_transform_test(p);
        printf("Library: %s\n\n", gmt_library_version());
        gmt_close(p);
        free(data);
    }
    return 0;
}
