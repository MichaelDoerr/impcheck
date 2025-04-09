
#pragma once

#include "trusted_utils.h"


struct siphash {
    const unsigned char* kk;
    u8* out;
    int outlen;
    u64 v0, v1, v2, v3;
    u64 k0, k1;
    u64 m;
    int i;
    u64 inlen;
    u8* buf;
    unsigned char buflen;
};

struct siphash* siphash_cls_init(const unsigned char* key_128bit);
void siphash_cls_reset(struct siphash* hash);
void siphash_cls_update(struct siphash* hash, const unsigned char* data, u64 nb_bytes);
void siphash_cls_pad(struct siphash* hash, u64 nb_bytes);
u8* siphash_cls_digest(struct siphash* hash);
void siphash_cls_free(struct siphash* hash);
