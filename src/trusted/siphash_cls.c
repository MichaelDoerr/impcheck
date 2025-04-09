
#include "siphash_cls.h"

#include <assert.h>   // for assert
#include <stdbool.h>  // for true
#include <stdlib.h>   // for free, abort

#include "trusted_utils.h"

#define cROUNDS 2
#define dROUNDS 4

#define SH_UINT64_C(c) c##UL

#define ROTL(x, b) (u64)(((x) << (b)) | ((x) >> (64 - (b))))

#define U32TO8_LE(p, v)       \
    (p)[0] = (u8)((v));       \
    (p)[1] = (u8)((v) >> 8);  \
    (p)[2] = (u8)((v) >> 16); \
    (p)[3] = (u8)((v) >> 24);

#define U64TO8_LE(p, v)                  \
    U32TO8_LE((p), (unsigned int)((v))); \
    U32TO8_LE((p) + 4, (unsigned int)((v) >> 32));

#define U8TO64_LE(p)                                 \
    (((u64)((p)[0])) | ((u64)((p)[1]) << 8) |        \
     ((u64)((p)[2]) << 16) | ((u64)((p)[3]) << 24) | \
     ((u64)((p)[4]) << 32) | ((u64)((p)[5]) << 40) | \
     ((u64)((p)[6]) << 48) | ((u64)((p)[7]) << 56))

#define SIPROUND                       \
    do {                               \
        hash->v0 += hash->v1;          \
        hash->v1 = ROTL(hash->v1, 13); \
        hash->v1 ^= hash->v0;          \
        hash->v0 = ROTL(hash->v0, 32); \
        hash->v2 += hash->v3;          \
        hash->v3 = ROTL(hash->v3, 16); \
        hash->v3 ^= hash->v2;          \
        hash->v0 += hash->v3;          \
        hash->v3 = ROTL(hash->v3, 21); \
        hash->v3 ^= hash->v0;          \
        hash->v2 += hash->v1;          \
        hash->v1 = ROTL(hash->v1, 17); \
        hash->v1 ^= hash->v2;          \
        hash->v2 = ROTL(hash->v2, 32); \
    } while (0)

void process_cls_next_block(struct siphash* hash) {
    hash->m = U8TO64_LE(hash->buf);
    hash->v3 ^= hash->m;
    for (hash->i = 0; hash->i < cROUNDS; ++hash->i)
        SIPROUND;
    hash->v0 ^= hash->m;
}

void process_cls_final_block(struct siphash* hash) {
    const int left = hash->inlen & 7;
    assert(left == hash->buflen);
    u64 b = ((u64)hash->inlen) << 56;
    u8* ni = hash->buf;

    switch (left) {
        case 7:
            b |= ((u64)ni[6]) << 48;
            /* FALLTHRU */
        case 6:
            b |= ((u64)ni[5]) << 40;
            /* FALLTHRU */
        case 5:
            b |= ((u64)ni[4]) << 32;
            /* FALLTHRU */
        case 4:
            b |= ((u64)ni[3]) << 24;
            /* FALLTHRU */
        case 3:
            b |= ((u64)ni[2]) << 16;
            /* FALLTHRU */
        case 2:
            b |= ((u64)ni[1]) << 8;
            /* FALLTHRU */
        case 1:
            b |= ((u64)ni[0]);
            break;
        case 0:
            break;
    }

    hash->v3 ^= b;

    for (hash->i = 0; hash->i < cROUNDS; ++hash->i)
        SIPROUND;

    hash->v0 ^= b;

    if (hash->outlen == 16)
        hash->v2 ^= 0xee;
    else
        hash->v2 ^= 0xff;

    for (hash->i = 0; hash->i < dROUNDS; ++hash->i)
        SIPROUND;

    b = hash->v0 ^ hash->v1 ^ hash->v2 ^ hash->v3;
    U64TO8_LE(hash->out, b);

    hash->v1 ^= 0xdd;

    for (hash->i = 0; hash->i < dROUNDS; ++hash->i)
        SIPROUND;

    b = hash->v0 ^ hash->v1 ^ hash->v2 ^ hash->v3;
    U64TO8_LE(hash->out + 8, b);
}

struct siphash* siphash_cls_init(const unsigned char* key_128bit) {
    struct siphash* hash = (struct siphash*)trusted_utils_calloc(1, sizeof(struct siphash));
    hash->outlen = 128 / 8;
    hash->buflen = 0;
    hash->kk = key_128bit;
    hash->out = trusted_utils_malloc(128 / 8);
    hash->buf = trusted_utils_malloc(8);
    if (hash->kk) siphash_cls_reset(hash);

    return hash;
}
void siphash_cls_reset(struct siphash* hash) {
    hash->v0 = SH_UINT64_C(0x736f6d6570736575);
    hash->v1 = SH_UINT64_C(0x646f72616e646f6d);
    hash->v2 = SH_UINT64_C(0x6c7967656e657261);
    hash->v3 = SH_UINT64_C(0x7465646279746573);
    hash->k0 = U8TO64_LE(hash->kk);
    hash->k1 = U8TO64_LE(hash->kk + 8);
    hash->v3 ^= hash->k1;
    hash->v2 ^= hash->k0;
    hash->v1 ^= hash->k1;
    hash->v0 ^= hash->k0;
    hash->inlen = 0;
    hash->buflen = 0;
    if (hash->outlen == 16)
        hash->v1 ^= 0xee;
}
void siphash_cls_update(struct siphash* hash, const unsigned char* data, u64 nb_bytes) {
    u32 datapos = 0;
    while (true) {
        while (hash->buflen < 8u && datapos < nb_bytes) {
            hash->buf[hash->buflen++] = data[datapos++];
        }
        if (hash->buflen < 8u) {
            break;
        }
        process_cls_next_block(hash);
        hash->buflen = 0;
    }
    hash->inlen += nb_bytes;
}
void siphash_cls_pad(struct siphash* hash, u64 nb_bytes) {
    const unsigned char c = 0;
    for (u64 i = 0; i < nb_bytes; i++) siphash_cls_update(hash, &c, 1);
}
u8* siphash_cls_digest(struct siphash* hash) {
    process_cls_final_block(hash);
    return hash->out;
}
void siphash_cls_free(struct siphash* hash) {
    free(hash->buf);
    free(hash->out);
}

#undef SH_UINT64_C
