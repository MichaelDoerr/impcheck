#pragma once

#include "hash.h"
#include "trusted_utils.h"

// return largest hint id + offset.  Mutate hints
u64 plrat_utils_add_offset(struct hash_table* id_offsets, u64* hints, int nb_hints); 

// return the next valid id. Mutate offset, id_offset and hints
u64 plrat_utils_get_next_valid_id(const u64 id, u64* offset, struct hash_table* id_offsets, u64* hints, int nb_hints, u64 nb_solvers); 

// Mutate hints and delete hints from hash_table
void plrat_utils_translate_and_delete(struct hash_table* id_offsets, u64* hints, int nb_hints);

bool plrat_utils_import_unchecked(unsigned long id, const int* literals, int nb_literals);

void plrat_utils_log(const char* msg);
void plrat_utils_log_err(const char* msg);

bool plrat_utils_compare_lits(int* lits1, int* lits2, int nb_lits1, int nb_lits2);