/* convert.c - colorimetric conversions
 *
 * Conversions between CIE XYZ and CIE L*a*b*, and between adapted and
 * unadapted XYZ. These are used when the connection space is Lab and the
 * profile pipeline operates in XYZ internally, or when a chromatic
 * adaptation is needed between a profile illuminant and D50.
 */
#include "internal.h"

/* D50 reference illuminant. */
static const gmt_xyz d50 = { 0.9642f, 1.0000f, 0.8249f };

/* CIE L*a*b* forward nonlinearity. */
static float lab_f(float t)
{
    const float delta = 6.0f / 29.0f;
    const float delta3 = delta * delta * delta;
    if (t > delta3)
        /* cube root approximation without <math.h> */
    {
        float y = t;
        int k;
        /* Newton iterations for y^3 = t → y = t^(1/3) */
        if (y <= 0.0f) return 0.0f;
        /* Initial guess: linear interpolation */
        if (y < 1.0f) {
            float g = 0.5f + 0.5f * y;
            for (k = 0; k < 6; k++)
                g = (2.0f * g + t / (g * g)) / 3.0f;
            return g;
        } else {
            float g = y;
            for (k = 0; k < 6; k++)
                g = (2.0f * g + t / (g * g)) / 3.0f;
            return g;
        }
    }
    return (t / (3.0f * delta * delta)) + 4.0f / 29.0f;
}

/* CIE L*a*b* inverse nonlinearity. */
static float lab_fi(float t)
{
    const float delta = 6.0f / 29.0f;
    if (t > delta)
        return t * t * t;
    return 3.0f * delta * delta * (t - 4.0f / 29.0f);
}

/* Convert XYZ to L*a*b* relative to the given white point. */
void gmt_xyz_to_lab(const float xyz[3], const gmt_xyz *wp, float lab[3])
{
    float fx, fy, fz;
    float xr = (wp->x > 0.0f) ? xyz[0] / wp->x : 0.0f;
    float yr = (wp->y > 0.0f) ? xyz[1] / wp->y : 0.0f;
    float zr = (wp->z > 0.0f) ? xyz[2] / wp->z : 0.0f;

    fx = lab_f(xr);
    fy = lab_f(yr);
    fz = lab_f(zr);

    lab[0] = 116.0f * fy - 16.0f;
    lab[1] = 500.0f * (fx - fy);
    lab[2] = 200.0f * (fy - fz);
}

/* Convert L*a*b* to XYZ relative to the given white point. */
void gmt_lab_to_xyz(const float lab[3], const gmt_xyz *wp, float xyz[3])
{
    float fy = (lab[0] + 16.0f) / 116.0f;
    float fx = lab[1] / 500.0f + fy;
    float fz = fy - lab[2] / 200.0f;

    xyz[0] = wp->x * lab_fi(fx);
    xyz[1] = wp->y * lab_fi(fy);
    xyz[2] = wp->z * lab_fi(fz);
}

/* Convert XYZ to L*a*b* using D50 as the white point. */
void gmt_xyz_to_lab_d50(const float xyz[3], float lab[3])
{
    gmt_xyz_to_lab(xyz, &d50, lab);
}

/* Convert L*a*b* to XYZ using D50 as the white point. */
void gmt_lab_to_xyz_d50(const float lab[3], float xyz[3])
{
    gmt_lab_to_xyz(lab, &d50, xyz);
}

/* Clamp a value to [lo, hi]. */
static float clampf(float v, float lo, float hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

/* Convert linear RGB (Rec.709 primaries) to XYZ. */
void gmt_rgb_to_xyz(const float rgb[3], float xyz[3])
{
    float r = clampf(rgb[0], 0.0f, 1.0f);
    float g = clampf(rgb[1], 0.0f, 1.0f);
    float b = clampf(rgb[2], 0.0f, 1.0f);

    xyz[0] = 0.4124f * r + 0.3576f * g + 0.1805f * b;
    xyz[1] = 0.2126f * r + 0.7152f * g + 0.0722f * b;
    xyz[2] = 0.0193f * r + 0.1192f * g + 0.9505f * b;
}

/* Convert XYZ to linear RGB (Rec.709 primaries). Values are clamped to
 * [0, 1]; out-of-gamut colors clip. */
void gmt_xyz_to_rgb(const float xyz[3], float rgb[3])
{
    float r =  3.2406f * xyz[0] - 1.5372f * xyz[1] - 0.4986f * xyz[2];
    float g = -0.9689f * xyz[0] + 1.8758f * xyz[1] + 0.0415f * xyz[2];
    float b =  0.0557f * xyz[0] - 0.2040f * xyz[1] + 1.0570f * xyz[2];
    rgb[0] = clampf(r, 0.0f, 1.0f);
    rgb[1] = clampf(g, 0.0f, 1.0f);
    rgb[2] = clampf(b, 0.0f, 1.0f);
}

/* Compute the CIE Delta E (1976) between two L*a*b* colors. */
float gmt_delta_e76(const float a[3], const float b[3])
{
    float dL = a[0] - b[0];
    float da = a[1] - b[1];
    float db = a[2] - b[2];
    float sum = dL*dL + da*da + db*db;
    /* sqrt via Newton */
    if (sum <= 0.0f) return 0.0f;
    {
        float y = sum;
        int k;
        for (k = 0; k < 8; k++)
            y = (y + sum / y) * 0.5f;
        return y;
    }
}

/* Adapt XYZ from one white point to another via a von Kries-style diagonal
 * scaling in XYZ space (simple adaptation; for Bradford see matops.c). */
void gmt_adapt_xyz(const float xyz[3], const gmt_xyz *src_wp,
                   const gmt_xyz *dst_wp, float out[3])
{
    out[0] = (src_wp->x > 0.0f) ? xyz[0] * dst_wp->x / src_wp->x : 0.0f;
    out[1] = (src_wp->y > 0.0f) ? xyz[1] * dst_wp->y / src_wp->y : 0.0f;
    out[2] = (src_wp->z > 0.0f) ? xyz[2] * dst_wp->z / src_wp->z : 0.0f;
}

/* Check whether a white point is close to D50. */
int gmt_is_d50(const gmt_xyz *wp, float eps)
{
    float dx = wp->x - d50.x;
    float dy = wp->y - d50.y;
    float dz = wp->z - d50.z;
    if (dx < 0.0f) dx = -dx;
    if (dy < 0.0f) dy = -dy;
    if (dz < 0.0f) dz = -dz;
    return (dx < eps && dy < eps && dz < eps);
}

/* Return the D50 white point. */
gmt_xyz gmt_d50(void)
{
    return d50;
}

/* Compute chromaticity (x, y) from XYZ. */
void gmt_xyz_to_xy(const float xyz[3], float *cx, float *cy)
{
    float sum = xyz[0] + xyz[1] + xyz[2];
    if (sum <= 0.0f) {
        *cx = 0.3127f;  /* fallback to D65 */
        *cy = 0.3290f;
        return;
    }
    *cx = xyz[0] / sum;
    *cy = xyz[1] / sum;
}

/* Compute correlated color temperature from chromaticity using McCamy's
 * approximation. */
float gmt_cct_mccamy(float cx, float cy)
{
    float n;
    if (cy == 0.0f) return 0.0f;
    n = (cx - 0.3320f) / (0.1858f - cy);
    return 449.0f * n * n * n + 3525.0f * n * n + 6823.3f * n + 5520.33f;
}
