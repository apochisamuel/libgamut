# libgamut Architecture

libgamut is a color profile parsing library. It reads a binary profile from
memory, decodes the tag directory and element bodies, assembles a
device-to-connection-space transform, and exposes query and evaluation APIs.

## File Format

A profile begins with a 128-byte header carrying the format signature,
version, device color space, profile connection space, and channel count.
Immediately after the header, a tag directory lists (signature, offset, size)
triples. Tag bodies may share an offset; two tags naming the same offset
resolve to one decoded element.

## Parsing Phases

The library processes a profile in the following order:

1. **Header** (`header.c`): Reads the fixed header and establishes the
   device and connection space metadata.

2. **Tag Directory** (`tagtable.c`): Reads the tag count and entry triples.
   Entries whose body falls outside the buffer are noted and skipped.

3. **Element Decode** (`elements.c`): Iterates over the tag directory and
   decodes each body through a type-dispatch switch. Elements are stored in
   an interning pool keyed by file offset, so tags that share an offset
   resolve to one element.

4. **Curves** (`curves.c`): Sampled-table and parametric tone curves.

5. **CLUTs** (`clut.c`): Multi-dimensional color lookup tables with
   per-dimension grid counts.

6. **Matrices** (`matrix.c`): 3×3 affine transforms with offset.

7. **Lookups** (`luts.c`): AtoB/BtoA lookup elements that reference
   sub-elements (curves, CLUT, matrix) by offset through the pool.

8. **Multi-Process Elements** (`mpet.c`): Ordered sequences of processing
   stages, each naming an operation and the offset of the element it runs.

9. **XYZ Elements** (`xyz.c`): Tristimulus values (white point, colorants).

10. **Text Elements** (`text.c`): Description, copyright, and manufacturer
    strings.

11. **Colorant Table** (`colorants.c`): Device colorant names.

12. **Named Colors** (`namedcolor.c`): Spot color labels and device recipes.

## Transform Assembly

`linkcache.c` builds the device-to-PCS transform by walking the resolved
tags and appending stages to a growable array. Stage kinds include:

- **Curve**: per-channel tone mapping via a resolved sample table.
- **CLUT**: multi-dimensional table lookup.
- **Matrix**: 3×3 affine transform.
- **Chad**: chromatic adaptation via the profile white point.

The application-order array records the sequence in which stages are
evaluated.

## Validation

After assembly, `validate.c` runs cross-tag consistency checks:

- TRC sharing: three per-channel tone tags that resolve to one element
  indicate the profile cannot provide independent per-channel reproduction.
- Colorant/CLUT agreement: the colorant count must match the CLUT input
  dimension.
- Matrix chain: a multi-process matrix stage must match the sequence's
  declared dimensions.

Checks that find a mismatch perform a selective cleanup of the inconsistent
element so the profile no longer advertises data it cannot honor.

## Identifier Verification

`checksum.c` compares the stored profile identifier against a recomputed
digest. A mismatch is non-fatal; the white-point element is adjusted so
downstream math uses a consistent value.

## Evaluation

`resolve.c` walks the link cache in application order and runs each stage
against a working sample vector. Curve stages interpolate the resolved
sample table; CLUT stages sample the resolved grid; matrix stages apply the
resolved 3×3 transform; chad stages scale by the resolved white point.

## Query API

`profile.c` and `query.c` expose metadata: tag signatures, colorant names,
named-color labels, and the transform evaluation entry points `gmt_apply`
and `gmt_apply_inverse`.

## Cleanup

`gmt_close` releases the link cache, then frees the element pool (each
element is released through its kind-specific destructor), then releases the
tag directory and the profile buffer.
