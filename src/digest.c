/* digest.c - hash and digest utilities
 *
 * Several digest algorithms used for profile identification and data
 * integrity checking. FNV-1a is the primary hash; CRC32 is used for
 * compatibility checks; and a combined digest provides a stronger
 * fingerprint for profile matching.
 */
#include "internal.h"
#include <string.h>

/* FNV-1a 32-bit hash. */
uint32_t gmt_fnv1a_32(const uint8_t *data, size_t size)
{
    uint32_t h = 2166136261u;
    size_t i;
    for (i = 0; i < size; i++) {
        h ^= data[i];
        h *= 16777619u;
    }
    return h;
}

/* FNV-1a 32-bit hash with a range excluded (used for profile ID where the
 * ID field itself is zeroed during computation). */
uint32_t gmt_fnv1a_32_exclude(const uint8_t *data, size_t size,
                               size_t exc_off, size_t exc_len)
{
    uint32_t h = 2166136261u;
    size_t i;
    for (i = 0; i < size; i++) {
        uint8_t b = data[i];
        if (i >= exc_off && i < exc_off + exc_len)
            b = 0;
        h ^= b;
        h *= 16777619u;
    }
    return h;
}

/* CRC32 (ISO 3309). */
static uint32_t crc32_table[256];
static int crc32_init = 0;

static void init_crc32(void)
{
    uint32_t i, j;
    for (i = 0; i < 256; i++) {
        uint32_t c = i;
        for (j = 0; j < 8; j++) {
            if (c & 1)
                c = 0xEDB88320u ^ (c >> 1);
            else
                c >>= 1;
        }
        crc32_table[i] = c;
    }
    crc32_init = 1;
}

uint32_t gmt_crc32(const uint8_t *data, size_t size)
{
    uint32_t crc = 0xFFFFFFFFu;
    size_t i;
    if (!crc32_init)
        init_crc32();
    for (i = 0; i < size; i++)
        crc = crc32_table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    return crc ^ 0xFFFFFFFFu;
}

/* Update a running CRC32. */
uint32_t gmt_crc32_update(uint32_t crc, const uint8_t *data, size_t size)
{
    size_t i;
    if (!crc32_init)
        init_crc32();
    crc ^= 0xFFFFFFFFu;
    for (i = 0; i < size; i++)
        crc = crc32_table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    return crc ^ 0xFFFFFFFFu;
}

/* Combined 64-bit fingerprint: FNV-1a of the header || CRC32 of the body. */
uint64_t gmt_profile_fingerprint(const uint8_t *data, size_t size)
{
    uint32_t head_hash, body_crc;
    uint64_t fp;
    if (size < GMT_HEADER_SIZE)
        return 0;
    head_hash = gmt_fnv1a_32_exclude(data, GMT_HEADER_SIZE, 24, 4);
    body_crc = gmt_crc32(data + GMT_HEADER_SIZE, size - GMT_HEADER_SIZE);
    fp = ((uint64_t)head_hash << 32) | (uint64_t)body_crc;
    return fp;
}

/* Compute the profile ID as Fenrir's gamut format defines it:
 * FNV-1a over the entire buffer with bytes [24..28) zeroed. */
uint32_t gmt_compute_profile_id(const uint8_t *data, size_t size)
{
    return gmt_fnv1a_32_exclude(data, size, 24, 4);
}

/* Verify the stored profile ID against the computed one. Returns 1 if
 * they match, 0 if they differ, or -1 if no ID is stored. */
int gmt_verify_profile_id(const uint8_t *data, size_t size)
{
    uint32_t stored, computed;
    if (size < 28)
        return -1;
    stored = ((uint32_t)data[24] << 24) | ((uint32_t)data[25] << 16) |
             ((uint32_t)data[26] << 8)  | (uint32_t)data[27];
    if (stored == 0)
        return -1;
    computed = gmt_compute_profile_id(data, size);
    return (stored == computed) ? 1 : 0;
}

/* Hash a single tag body for deduplication. */
uint32_t gmt_hash_tag_body(const uint8_t *data, size_t size,
                            uint32_t offset, uint32_t tag_size)
{
    if ((size_t)offset + tag_size > size)
        return 0;
    return gmt_fnv1a_32(data + offset, tag_size);
}

/* Build a hash table of tag body hashes for all tags. Caller provides the
 * output array sized to tag_count. */
void gmt_hash_all_tags(const gmt_profile *p, uint32_t *hashes)
{
    size_t i;
    for (i = 0; i < p->tag_count; i++)
        hashes[i] = gmt_hash_tag_body(p->buf, p->buf_size,
                                       p->tags[i].offset,
                                       p->tags[i].size);
}

/* Find tags with identical body hashes (potential sharing candidates). */
unsigned gmt_find_hashdup_tags(const gmt_profile *p, const uint32_t *hashes,
                                uint32_t *dup_sigs, size_t cap)
{
    unsigned count = 0;
    size_t i, j;
    for (i = 0; i < p->tag_count && count < cap; i++) {
        if (hashes[i] == 0) continue;
        for (j = i + 1; j < p->tag_count; j++) {
            if (hashes[j] == hashes[i] &&
                p->tags[i].offset != p->tags[j].offset) {
                dup_sigs[count++] = p->tags[i].sig;
                if (count < cap)
                    dup_sigs[count++] = p->tags[j].sig;
                break;
            }
        }
    }
    return count;
}
