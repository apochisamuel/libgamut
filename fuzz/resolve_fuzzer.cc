// resolve_fuzzer.cc - exercises the assembled transform: open a profile and
// run the forward and inverse device/connection-space conversions across a
// few sample points, then release it.
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
    if (nch > 0 && nch <= 15) {
        float in[15];
        float out[15];
        const float pts[3] = { 0.0f, 0.5f, 1.0f };
        for (int s = 0; s < 3; s++) {
            for (unsigned i = 0; i < 15; i++) { in[i] = pts[s]; out[i] = 0.0f; }
            gmt_apply(p, in, out);
            gmt_apply_inverse(p, in, out);
        }
    }

    gmt_close(p);
    return 0;
}
