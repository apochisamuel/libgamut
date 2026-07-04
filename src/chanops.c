/* chanops.c - per-channel operations on color data arrays
 *
 * Manipulate multi-channel color data at the sample level: extract or
 * insert individual channels, reorder channel layouts, compute per-channel
 * statistics, and convert between interleaved and planar layouts. These
 * are used by the sampling, gamut mapping, and comparison modules.
 */
#include "internal.h"
#include <string.h>

/* Extract one channel from an interleaved array into a planar buffer. */
void gmt_chan_extract(const float *interleaved, unsigned nch,
                     unsigned channel, float *planar, size_t count)
{
    size_t i;
    if (channel >= nch) {
        memset(planar, 0, count * sizeof(float));
        return;
    }
    for (i = 0; i < count; i++)
        planar[i] = interleaved[i * nch + channel];
}

/* Insert a planar channel into an interleaved array. */
void gmt_chan_insert(float *interleaved, unsigned nch,
                    unsigned channel, const float *planar, size_t count)
{
    size_t i;
    if (channel >= nch)
        return;
    for (i = 0; i < count; i++)
        interleaved[i * nch + channel] = planar[i];
}

/* Swap two channels in an interleaved array. */
void gmt_chan_swap(float *data, unsigned nch,
                  unsigned ch_a, unsigned ch_b, size_t count)
{
    size_t i;
    if (ch_a >= nch || ch_b >= nch || ch_a == ch_b)
        return;
    for (i = 0; i < count; i++) {
        float tmp = data[i * nch + ch_a];
        data[i * nch + ch_a] = data[i * nch + ch_b];
        data[i * nch + ch_b] = tmp;
    }
}

/* Reorder channels according to a permutation map. map[new_pos] = old_pos. */
void gmt_chan_reorder(const float *src, unsigned src_nch,
                     float *dst, unsigned dst_nch,
                     const unsigned *map, size_t count)
{
    size_t i;
    unsigned c;
    for (i = 0; i < count; i++) {
        for (c = 0; c < dst_nch; c++) {
            unsigned src_ch = map[c];
            dst[i * dst_nch + c] =
                (src_ch < src_nch) ? src[i * src_nch + src_ch] : 0.0f;
        }
    }
}

/* Add a constant to one channel. */
void gmt_chan_add(float *data, unsigned nch, unsigned channel,
                 float offset, size_t count)
{
    size_t i;
    if (channel >= nch) return;
    for (i = 0; i < count; i++)
        data[i * nch + channel] += offset;
}

/* Multiply one channel by a constant. */
void gmt_chan_scale(float *data, unsigned nch, unsigned channel,
                   float factor, size_t count)
{
    size_t i;
    if (channel >= nch) return;
    for (i = 0; i < count; i++)
        data[i * nch + channel] *= factor;
}

/* Clamp one channel to [lo, hi]. */
void gmt_chan_clamp(float *data, unsigned nch, unsigned channel,
                   float lo, float hi, size_t count)
{
    size_t i;
    if (channel >= nch) return;
    for (i = 0; i < count; i++) {
        float *v = &data[i * nch + channel];
        if (*v < lo) *v = lo;
        if (*v > hi) *v = hi;
    }
}

/* Compute the minimum, maximum, and mean of one channel. */
void gmt_chan_stats(const float *data, unsigned nch, unsigned channel,
                   size_t count, float *out_min, float *out_max,
                   float *out_mean)
{
    size_t i;
    float mn = 1e30f, mx = -1e30f, sum = 0.0f;
    if (channel >= nch || count == 0) {
        *out_min = *out_max = *out_mean = 0.0f;
        return;
    }
    for (i = 0; i < count; i++) {
        float v = data[i * nch + channel];
        if (v < mn) mn = v;
        if (v > mx) mx = v;
        sum += v;
    }
    *out_min = mn;
    *out_max = mx;
    *out_mean = sum / (float)count;
}

/* Compute the variance of one channel. */
float gmt_chan_variance(const float *data, unsigned nch, unsigned channel,
                       size_t count)
{
    float mean, sum = 0.0f;
    size_t i;
    if (channel >= nch || count < 2) return 0.0f;
    {
        float s = 0.0f;
        for (i = 0; i < count; i++)
            s += data[i * nch + channel];
        mean = s / (float)count;
    }
    for (i = 0; i < count; i++) {
        float d = data[i * nch + channel] - mean;
        sum += d * d;
    }
    return sum / (float)(count - 1);
}

/* Convert interleaved to planar layout. */
void gmt_interleaved_to_planar(const float *inter, unsigned nch,
                               float *planar, size_t count)
{
    unsigned c;
    size_t i;
    for (c = 0; c < nch; c++)
        for (i = 0; i < count; i++)
            planar[(size_t)c * count + i] = inter[i * nch + c];
}

/* Convert planar to interleaved layout. */
void gmt_planar_to_interleaved(const float *planar, unsigned nch,
                               float *inter, size_t count)
{
    unsigned c;
    size_t i;
    for (c = 0; c < nch; c++)
        for (i = 0; i < count; i++)
            inter[i * nch + c] = planar[(size_t)c * count + i];
}

/* Pad channel count: copy src with src_nch channels to dst with dst_nch
 * channels, zero-filling extra channels. */
void gmt_chan_pad(const float *src, unsigned src_nch,
                 float *dst, unsigned dst_nch, size_t count)
{
    size_t i;
    unsigned c;
    for (i = 0; i < count; i++) {
        for (c = 0; c < dst_nch; c++)
            dst[i * dst_nch + c] = (c < src_nch)
                ? src[i * src_nch + c] : 0.0f;
    }
}

/* Truncate channel count: copy only the first dst_nch channels. */
void gmt_chan_truncate(const float *src, unsigned src_nch,
                      float *dst, unsigned dst_nch, size_t count)
{
    size_t i;
    unsigned c;
    unsigned n = (dst_nch < src_nch) ? dst_nch : src_nch;
    for (i = 0; i < count; i++)
        for (c = 0; c < n; c++)
            dst[i * dst_nch + c] = src[i * src_nch + c];
}

/* Compute the Euclidean distance between two interleaved color samples. */
float gmt_chan_distance(const float *a, const float *b, unsigned nch)
{
    float sum = 0.0f;
    unsigned c;
    float y;
    int k;
    for (c = 0; c < nch; c++) {
        float d = a[c] - b[c];
        sum += d * d;
    }
    if (sum <= 0.0f) return 0.0f;
    y = sum;
    for (k = 0; k < 8; k++)
        y = (y + sum / y) * 0.5f;
    return y;
}

/* Find the index of the sample closest to a given target color. */
size_t gmt_chan_nearest(const float *data, unsigned nch,
                       size_t count, const float *target)
{
    size_t best = 0;
    float best_d = 1e30f;
    size_t i;
    for (i = 0; i < count; i++) {
        float d = gmt_chan_distance(data + i * nch, target, nch);
        if (d < best_d) {
            best_d = d;
            best = i;
        }
    }
    return best;
}

/* Compute per-channel histogram with `bins` buckets over [0, 1]. */
void gmt_chan_histogram(const float *data, unsigned nch, unsigned channel,
                       size_t count, uint32_t *bins, unsigned nbins)
{
    size_t i;
    memset(bins, 0, (size_t)nbins * sizeof(uint32_t));
    if (channel >= nch || nbins == 0) return;
    for (i = 0; i < count; i++) {
        float v = data[i * nch + channel];
        unsigned b;
        if (v < 0.0f) v = 0.0f;
        if (v > 1.0f) v = 1.0f;
        b = (unsigned)(v * (float)(nbins - 1) + 0.5f);
        if (b >= nbins) b = nbins - 1;
        bins[b]++;
    }
}
