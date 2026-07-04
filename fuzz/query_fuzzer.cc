// query_fuzzer.cc - exercises the name-lookup and metadata query paths: open
// a profile, walk its colorant and named-color tables, and resolve names to
// indices, then release it.
#include <stdint.h>
#include <stddef.h>
#include "gamut.h"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    if (size < 64 || size > 16 * 1024 * 1024)
        return 0;

    gmt_profile *p = gmt_open_memory(data, size);
    if (!p)
        return 0;

    unsigned nc = gmt_colorant_count(p);
    for (unsigned i = 0; i < nc; i++)
        (void)gmt_colorant_name(p, i);

    (void)gmt_colorant_index(p, "cyan");
    (void)gmt_colorant_index(p, "K");

    unsigned nn = gmt_named_color_count(p);
    for (unsigned i = 0; i < nn; i++)
        (void)gmt_named_color_name(p, i);

    gmt_close(p);
    return 0;
}
