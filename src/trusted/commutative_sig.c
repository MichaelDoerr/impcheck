#include "commutative_sig.h"

const u64 first_init = 0xC4D715A715861702;
const u64 second_init = 0xB767977356C49687;

inline u64 murmurhash3(u64 x) {
    x ^= x >> 33U;
    x *= 0xff51afd7ed558ccd;
    x ^= x >> 33U;
    x *= 0xc4ceb9fe1a85ec53;
    x ^= x >> 33U;

    return x;
}


inline void hash_combine(u64* state, u64 value){
    u64 s = *state; 
    *state ^= (murmurhash3(value) + 0x9e3779b9 + (s<< 6) + (s>> 2));
}

struct comm_sig* comm_sig_init(const u64 key){
    struct comm_sig *hash = trusted_utils_malloc(sizeof(struct comm_sig));
    if(hash == NULL){
        return NULL;
    }
    hash->first_object_state = first_init * key;
    hash->second_object_state = second_init * key;
    hash->key = key;
    hash->first_signature = hash->first_object_state;
    hash->second_signature = hash->second_object_state;
    return hash;
}


void comm_sig_update_clause(struct comm_sig* hash, u64 clause_id, const int* lits, u64 nb_literals){
    comm_sig_update(hash, (const unsigned char*)&clause_id, sizeof(u64));
    comm_sig_update(hash, (const unsigned char*)lits, nb_literals * sizeof(int));
    finish_object(hash);
}

void comm_sig_update(struct comm_sig* hash, const unsigned char* data, u64 nb_bytes){
    if (nb_bytes == 8){
        hash_combine(&(hash->first_object_state), *(u64*)data);
        hash_combine(&(hash->second_object_state), *(u64*)data);
        return;
    }
    if (nb_bytes == 4){
        hash_combine(&(hash->first_object_state), (u64) (*(u32*)data));
        hash_combine(&(hash->second_object_state), (u64) (*(u32*)data));
        return;
    }
    if (nb_bytes % 8 == 0){
        for (u64 i = 0; i < nb_bytes / 8; i++){
            hash_combine(&(hash->first_object_state), *(u64*)(data + i * 8));
            hash_combine(&(hash->second_object_state), *(u64*)(data + i * 8));
        }
        return;
    }
    if (nb_bytes % 4 == 0){
        for (u64 i = 0; i < nb_bytes / 4; i++){
            hash_combine(&(hash->first_object_state), (u64) (*(u32*)(data + i * 4)));
            hash_combine(&(hash->second_object_state), (u64) (*(u32*)(data + i * 4)));
        }
        return;
    }
    for(u64 i=0; i<nb_bytes; i++){
        hash_combine(&(hash->first_object_state), data[i]);
        hash_combine(&(hash->second_object_state), data[i]);
    }
}
u8* comm_sig_digest(struct comm_sig* hash){
    u8* digest = (u8*)malloc(16);
    memcpy(digest, &hash->first_signature, 8);
    memcpy(digest + 8, &hash->second_signature, 8);
    return digest;
}

void finish_object(struct comm_sig* hash){
    hash->first_signature += hash->first_object_state * hash->first_object_state;
    hash->second_signature += hash->second_object_state * hash->second_object_state;

    hash->first_object_state = first_init * hash->key;
    hash->second_object_state = second_init * hash->key;
}

void comm_sig_free(struct comm_sig* hash){
    free(hash);
}