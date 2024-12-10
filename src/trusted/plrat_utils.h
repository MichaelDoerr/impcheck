#pragma once

#include "hash.h"
#include "trusted_utils.h"

u64 plrat_utils_add_offset(struct hash_table* id_offsets, u64* hints, int nb_hints); // return largest hint id + offset
u64 plrat_utils_get_next_valid_id(const u64 id, u64* offset, struct hash_table* id_offsets, u64* hints, int nb_hints); //return the next valid id. Modify 