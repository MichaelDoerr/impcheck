
#pragma once

#include "trusted_utils.h"
#include "secret.h"
#include <stdlib.h>
#include <string.h> //for memcpy

struct comm_sig {
    u64 first_object_state;
    u64 second_object_state; 
    u64 key; 
    u64 first_signature;
    u64 second_signature; 
};

struct comm_sig* comm_sig_init(const u64 key);
void comm_sig_update_clause(struct comm_sig* hash, u64 clause_id, const int* lits, u64 nb_literals);
void comm_sig_update(struct comm_sig* hash, const unsigned char* data, u64 nb_bytes);
void finish_object(struct comm_sig* hash);
u8* comm_sig_digest(struct comm_sig* hash);
void comm_sig_free(struct comm_sig* hash);