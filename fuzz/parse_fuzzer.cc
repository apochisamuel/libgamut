// parse_fuzzer.cc - exercises the full parse pipeline and the assembled
// transform: open a profile from the input, read basic metadata, run the
// device-to-connection-space transform on one sample, then release it.
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

    unsigned nch = gmt_channel_count(p);
    (void)gmt_device_space(p);
    (void)gmt_connection_space(p);
    (void)gmt_tag_count(p);

    if (nch > 0 && nch <= 15) {
        float in[15];
        float out[15];
        for (unsigned i = 0; i < 15; i++) { in[i] = 0.5f; out[i] = 0.0f; }
        gmt_apply(p, in, out);
    }

    gmt_close(p);
    return 0;
}
