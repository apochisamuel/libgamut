# libgamut

A C library for parsing color profile binary files. libgamut decodes profile
headers, tag directories, and element bodies (tone curves, multi-dimensional
lookup tables, matrices, XYZ values, and text), assembles a device-to-PCS
transform pipeline, and evaluates it on color samples.

## Building

libgamut has no external dependencies beyond a C99 compiler and the standard
library.

```
make            # build static library + tools
make clean      # remove build artifacts
```

For fuzzing with libFuzzer and AddressSanitizer:

```
export CC=clang CXX=clang++
export CFLAGS="-g -O1 -fsanitize=address,fuzzer-no-link"
export CXXFLAGS="$CFLAGS"
export LIB_FUZZING_ENGINE="-fsanitize=fuzzer"
./build.sh
```

## API

```c
#include "gamut.h"

gmt_profile *p = gmt_open_memory(data, size);
if (p) {
    float in[3] = {0.5f, 0.5f, 0.5f};
    float out[3];
    gmt_apply(p, in, out);
    gmt_close(p);
}
```

See `gamut.h` for the full public API.

## License

MIT
