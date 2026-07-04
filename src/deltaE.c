/* deltaE.c - color difference metrics
 *
 * Implementations of the CIE and CMC color-difference formulas. All
 * functions take pairs of L*a*b* triplets. The Delta E 2000 formula is
 * the most perceptually uniform; the 1976 version is computationally
 * simplest.
 */
#include "internal.h"

/* sqrt approximation without <math.h>: Newton iterations. */
static float fsqrt(float x)
{
    float y;
    int k;
    if (x <= 0.0f) return 0.0f;
    y = x;
    for (k = 0; k < 10; k++)
        y = (y + x / y) * 0.5f;
    return y;
}

/* atan2 approximation. Only the ratio matters for hue; we use a polynomial
 * approximation of atan over [-1, 1] and extend via symmetry. */
static float fatan2(float y, float x)
{
    float ax = (x < 0.0f) ? -x : x;
    float ay = (y < 0.0f) ? -y : y;
    float a, r, t;

    if (ax < 1e-10f && ay < 1e-10f)
        return 0.0f;

    if (ax >= ay) {
        a = ay / ax;
    } else {
        a = ax / ay;
    }
    t = a * a;
    r = ((-0.0464964749f * t + 0.15931422f) * t - 0.327622764f) * t * a + a;

    if (ax < ay)
        r = 1.5707963f - r;
    if (x < 0.0f)
        r = 3.14159265f - r;
    if (y < 0.0f)
        r = -r;
    return r;
}

/* cos approximation via Taylor series around 0. */
static float fcos(float x)
{
    float x2 = x * x;
    return 1.0f - x2 * 0.5f + x2 * x2 / 24.0f
         - x2 * x2 * x2 / 720.0f;
}

/* sin approximation. */
static float fsin(float x)
{
    float x2 = x * x;
    return x - x * x2 / 6.0f + x * x2 * x2 / 120.0f
         - x * x2 * x2 * x2 / 5040.0f;
}

/* exp approximation. */
static float fexp(float x)
{
    float sum = 1.0f, term = 1.0f;
    int k;
    for (k = 1; k <= 16; k++) {
        term *= x / (float)k;
        sum += term;
    }
    return sum;
}

/* CIE Delta E 1976. */
float gmt_delta_e_76(const float a[3], const float b[3])
{
    float dL = a[0] - b[0];
    float da = a[1] - b[1];
    float db = a[2] - b[2];
    return fsqrt(dL*dL + da*da + db*db);
}

/* CIE Delta E 1994 (textiles weighting). */
float gmt_delta_e_94(const float lab1[3], const float lab2[3])
{
    float dL = lab1[0] - lab2[0];
    float da = lab1[1] - lab2[1];
    float db = lab1[2] - lab2[2];
    float C1 = fsqrt(lab1[1]*lab1[1] + lab1[2]*lab1[2]);
    float C2 = fsqrt(lab2[1]*lab2[1] + lab2[2]*lab2[2]);
    float dC = C1 - C2;
    float dH2 = da*da + db*db - dC*dC;
    float dH = (dH2 > 0.0f) ? fsqrt(dH2) : 0.0f;
    float SL = 1.0f;
    float SC = 1.0f + 0.048f * C1;
    float SH = 1.0f + 0.014f * C1;
    float tL = dL / SL;
    float tC = dC / SC;
    float tH = dH / SH;
    return fsqrt(tL*tL + tC*tC + tH*tH);
}

/* CMC(l:c) color difference (l=2, c=1 for acceptability). */
float gmt_delta_e_cmc(const float lab1[3], const float lab2[3],
                      float l_weight, float c_weight)
{
    float L1 = lab1[0], a1 = lab1[1], b1 = lab1[2];
    float dL = lab1[0] - lab2[0];
    float da = lab1[1] - lab2[1];
    float db = lab1[2] - lab2[2];
    float C1 = fsqrt(a1*a1 + b1*b1);
    float C2 = fsqrt(lab2[1]*lab2[1] + lab2[2]*lab2[2]);
    float dC = C1 - C2;
    float dH2 = da*da + db*db - dC*dC;
    float dH = (dH2 > 0.0f) ? fsqrt(dH2) : 0.0f;
    float H1 = fatan2(b1, a1);
    float T, F, SL, SC, SH;

    if (H1 < 0.0f) H1 += 6.2831853f;
    T = (H1 >= 2.8798f && H1 <= 6.0214f) ?
        0.56f + 0.2f * fcos(H1 + 2.8274f) * 0.7f :
        0.36f + 0.4f * fcos(H1 + 0.6109f) * 0.7f;

    {
        float C14 = C1 * C1 * C1 * C1;
        F = fsqrt(C14 / (C14 + 1900.0f));
    }

    SL = (L1 < 16.0f) ? 0.511f : (0.040975f * L1) / (1.0f + 0.01765f * L1);
    SC = (0.0638f * C1) / (1.0f + 0.0131f * C1) + 0.638f;
    SH = SC * (F * T + 1.0f - F);

    {
        float tL = dL / (l_weight * SL);
        float tC = dC / (c_weight * SC);
        float tH = dH / SH;
        return fsqrt(tL*tL + tC*tC + tH*tH);
    }
}

/* CIE Delta E 2000 (CIEDE2000). */
float gmt_delta_e_2000(const float lab1[3], const float lab2[3])
{
    float L1 = lab1[0], a1 = lab1[1], b1 = lab1[2];
    float L2 = lab2[0], a2 = lab2[1], b2 = lab2[2];
    float C1 = fsqrt(a1*a1 + b1*b1);
    float C2 = fsqrt(a2*a2 + b2*b2);
    float Cab = (C1 + C2) * 0.5f;
    float Cab7, G, a1p, a2p, C1p, C2p;
    float h1p, h2p, dLp, dCp, dhp, dHp;
    float Lbp, Cbp, hbp, T, SL, SC, SH;
    float Cbp7, RC, dtheta, Rterm;

    Cab7 = Cab * Cab * Cab * Cab * Cab * Cab * Cab;
    G = 0.5f * (1.0f - fsqrt(Cab7 / (Cab7 + 6103515625.0f)));  /* 25^7 */
    a1p = a1 * (1.0f + G);
    a2p = a2 * (1.0f + G);
    C1p = fsqrt(a1p*a1p + b1*b1);
    C2p = fsqrt(a2p*a2p + b2*b2);

    h1p = fatan2(b1, a1p);
    if (h1p < 0.0f) h1p += 6.2831853f;
    h2p = fatan2(b2, a2p);
    if (h2p < 0.0f) h2p += 6.2831853f;

    dLp = L2 - L1;
    dCp = C2p - C1p;

    if (C1p * C2p < 1e-10f) {
        dhp = 0.0f;
    } else {
        dhp = h2p - h1p;
        if (dhp > 3.14159265f) dhp -= 6.2831853f;
        if (dhp < -3.14159265f) dhp += 6.2831853f;
    }
    dHp = 2.0f * fsqrt(C1p * C2p) * fsin(dhp * 0.5f);

    Lbp = (L1 + L2) * 0.5f;
    Cbp = (C1p + C2p) * 0.5f;

    if (C1p * C2p < 1e-10f) {
        hbp = h1p + h2p;
    } else {
        float dh = h1p - h2p;
        if (dh < 0.0f) dh = -dh;
        if (dh <= 3.14159265f)
            hbp = (h1p + h2p) * 0.5f;
        else if (h1p + h2p < 6.2831853f)
            hbp = (h1p + h2p + 6.2831853f) * 0.5f;
        else
            hbp = (h1p + h2p - 6.2831853f) * 0.5f;
    }

    T = 1.0f - 0.17f * fcos(hbp - 0.5236f)
             + 0.24f * fcos(2.0f * hbp)
             + 0.32f * fcos(3.0f * hbp + 0.1047f)
             - 0.20f * fcos(4.0f * hbp - 1.0996f);

    {
        float Lbp50 = (Lbp - 50.0f) * (Lbp - 50.0f);
        SL = 1.0f + 0.015f * Lbp50 / fsqrt(20.0f + Lbp50);
    }
    SC = 1.0f + 0.045f * Cbp;
    SH = 1.0f + 0.015f * Cbp * T;

    Cbp7 = Cbp * Cbp * Cbp * Cbp * Cbp * Cbp * Cbp;
    RC = 2.0f * fsqrt(Cbp7 / (Cbp7 + 6103515625.0f));
    dtheta = 0.5236f * fexp(-((hbp - 4.7997f) * (hbp - 4.7997f))
                            / (0.6981f * 0.6981f));
    Rterm = -fsin(2.0f * dtheta) * RC;

    {
        float tL = dLp / SL;
        float tC = dCp / SC;
        float tH = dHp / SH;
        return fsqrt(tL*tL + tC*tC + tH*tH + Rterm * tC * tH);
    }
}

/* Batch Delta E computation: compute pairwise differences between two
 * arrays of Lab colors. */
void gmt_delta_e_batch(const float *lab_a, const float *lab_b,
                       float *results, size_t count, int method)
{
    size_t i;
    for (i = 0; i < count; i++) {
        const float *a = lab_a + i * 3;
        const float *b = lab_b + i * 3;
        switch (method) {
        case 76:   results[i] = gmt_delta_e_76(a, b);  break;
        case 94:   results[i] = gmt_delta_e_94(a, b);  break;
        case 2000: results[i] = gmt_delta_e_2000(a, b); break;
        default:   results[i] = gmt_delta_e_76(a, b);  break;
        }
    }
}

/* Compute statistics over a Delta E array. */
void gmt_delta_e_stats(const float *de, size_t count,
                       float *out_min, float *out_max, float *out_mean)
{
    float sum = 0.0f, mn = 1e30f, mx = 0.0f;
    size_t i;
    for (i = 0; i < count; i++) {
        if (de[i] < mn) mn = de[i];
        if (de[i] > mx) mx = de[i];
        sum += de[i];
    }
    *out_min = mn;
    *out_max = mx;
    *out_mean = (count > 0) ? sum / (float)count : 0.0f;
}
