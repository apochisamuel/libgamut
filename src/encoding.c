/* encoding.c - color value encoding and normalization
 *
 * Handle the encoding conventions for color values in different contexts:
 * ICC PCS encoding (16-bit Lab, 16-bit XYZ), device value normalization
 * for different bit depths, legacy v2 profile encoding, and conversion
 * between encoded integer values and float working space.
 */
#include "internal.h"
#include <string.h>

/* Encode a float Lab value to 16-bit ICC PCS encoding.
 * L: 0..100 maps to 0..0xFF00
 * a,b: -128..127 maps to 0..0xFFFF with 128 at 0x8080 */
void gmt_encode_lab16(const float lab[3], uint16_t out[3])
{
    float L = lab[0], a = lab[1], b = lab[2];

    /* Clamp to valid range. */
    if (L < 0.0f) L = 0.0f;
    if (L > 100.0f) L = 100.0f;
    if (a < -128.0f) a = -128.0f;
    if (a > 127.0f) a = 127.0f;
    if (b < -128.0f) b = -128.0f;
    if (b > 127.0f) b = 127.0f;

    out[0] = (uint16_t)(L / 100.0f * 65280.0f + 0.5f);
    out[1] = (uint16_t)((a + 128.0f) / 255.0f * 65535.0f + 0.5f);
    out[2] = (uint16_t)((b + 128.0f) / 255.0f * 65535.0f + 0.5f);
}

/* Decode 16-bit ICC PCS Lab encoding to float. */
void gmt_decode_lab16(const uint16_t in[3], float lab[3])
{
    lab[0] = (float)in[0] / 65280.0f * 100.0f;
    lab[1] = (float)in[1] / 65535.0f * 255.0f - 128.0f;
    lab[2] = (float)in[2] / 65535.0f * 255.0f - 128.0f;
}

/* Encode float XYZ to 16-bit ICC PCS encoding.
 * Each component: value / 1.99997 * 65535 */
void gmt_encode_xyz16(const float xyz[3], uint16_t out[3])
{
    int i;
    for (i = 0; i < 3; i++) {
        float v = xyz[i];
        if (v < 0.0f) v = 0.0f;
        if (v > 1.99997f) v = 1.99997f;
        out[i] = (uint16_t)(v / 1.99997f * 65535.0f + 0.5f);
    }
}

/* Decode 16-bit ICC PCS XYZ encoding to float. */
void gmt_decode_xyz16(const uint16_t in[3], float xyz[3])
{
    int i;
    for (i = 0; i < 3; i++)
        xyz[i] = (float)in[i] / 65535.0f * 1.99997f;
}

/* Encode a float Lab value to 8-bit legacy encoding (v2 profiles).
 * L: 0..100 maps to 0..255
 * a,b: -128..127 maps to 0..255 with 128 at 128 */
void gmt_encode_lab8(const float lab[3], uint8_t out[3])
{
    float L = lab[0], a = lab[1], b = lab[2];
    if (L < 0.0f) L = 0.0f; if (L > 100.0f) L = 100.0f;
    if (a < -128.0f) a = -128.0f; if (a > 127.0f) a = 127.0f;
    if (b < -128.0f) b = -128.0f; if (b > 127.0f) b = 127.0f;

    out[0] = (uint8_t)(L / 100.0f * 255.0f + 0.5f);
    out[1] = (uint8_t)(a + 128.0f + 0.5f);
    out[2] = (uint8_t)(b + 128.0f + 0.5f);
}

/* Decode 8-bit legacy Lab encoding. */
void gmt_decode_lab8(const uint8_t in[3], float lab[3])
{
    lab[0] = (float)in[0] / 255.0f * 100.0f;
    lab[1] = (float)in[1] - 128.0f;
    lab[2] = (float)in[2] - 128.0f;
}

/* Normalize device values from integer bit depth to [0, 1] float. */
void gmt_normalize_device(const uint16_t *raw, unsigned nch,
                          unsigned bits, float *out)
{
    float scale;
    unsigned i;
    if (bits == 0 || bits > 16)
        return;
    scale = 1.0f / (float)((1u << bits) - 1);
    for (i = 0; i < nch; i++)
        out[i] = (float)raw[i] * scale;
}

/* Denormalize float [0, 1] device values to integer bit depth. */
void gmt_denormalize_device(const float *flt, unsigned nch,
                            unsigned bits, uint16_t *out)
{
    float scale;
    unsigned i;
    if (bits == 0 || bits > 16)
        return;
    scale = (float)((1u << bits) - 1);
    for (i = 0; i < nch; i++) {
        float v = flt[i];
        if (v < 0.0f) v = 0.0f;
        if (v > 1.0f) v = 1.0f;
        out[i] = (uint16_t)(v * scale + 0.5f);
    }
}

/* Convert between v2 and v4 Lab encoding. In v2, L is 0..100 in u8Fixed8
 * (0x0000..0xFF00); in v4 it's the same but the a/b range is full 16-bit.
 * This adjusts the a/b scaling. */
void gmt_lab_v2_to_v4(const float v2[3], float v4[3])
{
    v4[0] = v2[0];
    v4[1] = v2[1] * (65535.0f / 65280.0f);
    v4[2] = v2[2] * (65535.0f / 65280.0f);
}

void gmt_lab_v4_to_v2(const float v4[3], float v2[3])
{
    v2[0] = v4[0];
    v2[1] = v4[1] * (65280.0f / 65535.0f);
    v2[2] = v4[2] * (65280.0f / 65535.0f);
}

/* Round-trip test for Lab 16-bit encoding. Returns max error component. */
float gmt_lab16_roundtrip_error(const float lab[3])
{
    uint16_t enc[3];
    float dec[3];
    float max_err = 0.0f;
    int i;
    gmt_encode_lab16(lab, enc);
    gmt_decode_lab16(enc, dec);
    for (i = 0; i < 3; i++) {
        float d = lab[i] - dec[i];
        if (d < 0.0f) d = -d;
        if (d > max_err) max_err = d;
    }
    return max_err;
}

/* Round-trip test for XYZ 16-bit encoding. */
float gmt_xyz16_roundtrip_error(const float xyz[3])
{
    uint16_t enc[3];
    float dec[3];
    float max_err = 0.0f;
    int i;
    gmt_encode_xyz16(xyz, enc);
    gmt_decode_xyz16(enc, dec);
    for (i = 0; i < 3; i++) {
        float d = xyz[i] - dec[i];
        if (d < 0.0f) d = -d;
        if (d > max_err) max_err = d;
    }
    return max_err;
}

/* Batch encode an array of float Lab values to 16-bit. */
void gmt_encode_lab16_batch(const float *lab, uint16_t *out, size_t count)
{
    size_t i;
    for (i = 0; i < count; i++)
        gmt_encode_lab16(lab + i * 3, out + i * 3);
}

/* Batch decode an array of 16-bit Lab values to float. */
void gmt_decode_lab16_batch(const uint16_t *in, float *lab, size_t count)
{
    size_t i;
    for (i = 0; i < count; i++)
        gmt_decode_lab16(in + i * 3, lab + i * 3);
}

/* Quantization error analysis: compute the maximum encoding error across
 * a uniform grid of Lab values. */
float gmt_lab16_max_quant_error(unsigned grid_size)
{
    float max_err = 0.0f;
    unsigned li, ai, bi;
    for (li = 0; li < grid_size; li++)
        for (ai = 0; ai < grid_size; ai++)
            for (bi = 0; bi < grid_size; bi++) {
                float lab[3];
                float err;
                lab[0] = (float)li / (float)(grid_size - 1) * 100.0f;
                lab[1] = (float)ai / (float)(grid_size - 1) * 255.0f - 128.0f;
                lab[2] = (float)bi / (float)(grid_size - 1) * 255.0f - 128.0f;
                err = gmt_lab16_roundtrip_error(lab);
                if (err > max_err) max_err = err;
            }
    return max_err;
}

/* Compute the effective bit depth of a device value encoding by measuring
 * the smallest distinguishable step. */
float gmt_effective_bit_depth(unsigned bits)
{
    float step;
    float ln2 = 0.6931f;
    float y;
    int k;
    if (bits == 0 || bits > 16) return 0.0f;
    step = 1.0f / (float)((1u << bits) - 1);
    /* log2(1/step) = -log2(step) = log(1/step)/log(2) */
    /* Approximate log(1/step) using ln(x) series for x near 1 */
    {
        float x = 1.0f / step;
        float u;
        if (x <= 0.0f) return 0.0f;
        /* Reduce x to [1, 2) by counting doublings. */
        y = 0.0f;
        while (x >= 2.0f) { x *= 0.5f; y += 1.0f; }
        u = x - 1.0f;
        y += u - u*u*0.5f + u*u*u/3.0f - u*u*u*u*0.25f;
    }
    /* Convert from ln to log2. */
    (void)k;
    return y / ln2;
}
