#include "plrat_utils.h"

#include <assert.h>
#include <unistd.h>  // getpid

#include "hash.h"
#include "lrat_check.h"
#include "trusted_utils.h"

FILE* debug_file;
int checker_id;
int ns;

void plrat_utils_init_debug(int checker_id, int num_s,  const char* location) {
    checker_id = checker_id;
    ns = num_s;
    char debug_file_name[512];
    snprintf(debug_file_name, 512, "%s/debug_%i.log", location, checker_id);
    debug_file = fopen(debug_file_name, "w");
    assert(debug_file != NULL);
}

void plrat_utils_end_debug() {
    fclose(debug_file);
}

// log old id and new id
void plrat_utils_debug(char type, const u64 old_id, const u64 new_id) {
    fprintf(debug_file, "%c %lu %lu %lu %lu\n", type, old_id, old_id%ns, new_id, new_id%ns);
}

u64 plrat_utils_get_next_valid_id(const u64 old_id, u64* offset, struct hash_table* id_offsets, u64* hints, int nb_hints, u64 nb_solvers, u64 solver_modulo_remainder) {
    u64 local_offset = *offset + solver_modulo_remainder;
    u64 new_id = old_id + local_offset;
    u64 max_hint_id = plrat_utils_add_offset(id_offsets, hints, nb_hints);

    if (new_id > max_hint_id) {
        
        //plrat_utils_debug('a', old_id, new_id);
        hash_table_insert(id_offsets, old_id, (void*)local_offset);
        return new_id;  // no new offset needed
    }

    u64 new_offset = 1 + max_hint_id - old_id;
    assert(new_offset + old_id > max_hint_id);

    // offsets have to be a multiple of nb_solvers
    u64 temp_rank = (new_offset % nb_solvers);
    // (correct the rank) and do not add nb_solvers if temp_rank is 0
    new_offset += (nb_solvers - temp_rank) % nb_solvers;
    
    
    assert((new_offset % nb_solvers) ==  0);
    hash_table_insert(id_offsets, old_id, (void*)(new_offset + solver_modulo_remainder));

    new_id = old_id + new_offset + solver_modulo_remainder;
    //plrat_utils_debug('b', old_id, new_id);

    //char msgstr2[512] = "";
    //snprintf(msgstr2, 512, "bigger offset! new_id:%lu jump_offset:%lu new_offset:%lu htsize:%lu", new_id, new_offset - *offset, new_offset, id_offsets->capacity);
    //trusted_utils_log(msgstr2);

    assert(new_offset > *offset);
    *offset = new_offset;
    
    assert(new_id > max_hint_id);
    assert((new_id % nb_solvers) ==  ((old_id + solver_modulo_remainder) % nb_solvers));
    assert(*offset % nb_solvers == 0);

    return new_id;
}



u64 plrat_utils_add_offset(struct hash_table* id_offsets, u64* hints, int nb_hints) {
    u64 max_hint_id = 1;

    // look at hints and translate their id to id + offset
    for (int i = 0; i < nb_hints; ++i) {
        u64 hint = hints[i];
        void* current_offset = hash_table_find(id_offsets, hint);
        //if (current_offset != NULL) {
            hint += (u64)current_offset;
            assert(hint > 0);
            assert((long)current_offset >= 0);
            assert(hint >= hints[i]);
            //plrat_utils_debug('h', hints[i], hint);
            hints[i] = hint;
        //}
        max_hint_id = (max_hint_id < hint) ? hint : max_hint_id;  // find largest hint id

    }

    for (int i = 0; i < nb_hints; ++i) {
        assert(max_hint_id >= hints[i]);
    }

    return max_hint_id;
}

// trusted_utils_write_lrat_delete needs hints which are translated.
// clauses that are deleted from the clause table, can be deleted from the offset table too.
// to avoid temporary memory allocation, translating and deleting is done element wise in the same loop.
void plrat_utils_translate_and_delete(struct hash_table* id_offsets, u64* hints, int nb_hints) {
    for (int i = 0; i < nb_hints; ++i) {
        u64 original_id = hints[i];                                       // temp save id
        void* current_offset = hash_table_find(id_offsets, original_id);  // translate id
        //plrat_utils_debug('d', original_id, (u64)current_offset);
        if (current_offset != NULL) {
            hints[i] = original_id + (u64)current_offset;
            
            //plrat_utils_debug('D', original_id, hints[i]);
            hash_table_delete(id_offsets, original_id);  // delete id
        }
    }
}

bool plrat_utils_import_unchecked(unsigned long id, const int* literals, int nb_literals) {
    // signature is veryfied in later stages - forward clause to checker as an axiom
    return lrat_check_add_axiomatic_clause(id, literals, nb_literals);
}

void plrat_utils_log(const char* msg) {
    printf("p [PLRAT_CHECKER %i] %s\n", getpid(), msg);
}

void plrat_utils_log_err(const char* msg) {
    printf("p [PLRAT_CHECKER %i] [ERROR] %s\n", getpid(), msg);
}

bool plrat_utils_compare_lits(int* lits1, int* lits2, int nb_lits1, int nb_lits2) {
    if (MALLOB_UNLIKELY(nb_lits1 != nb_lits2)) return false;
    for (int i = 0; i < nb_lits1; i++) {
        if (MALLOB_UNLIKELY(lits1[i] != lits2[i])) return false;
    }
    return true;
}

void plrat_utils_rank_to_2d(u64 rank, u64 n, u64* x, u64* y) {
    *x = rank % n;
    *y = rank / n;
}

u64 plrat_utils_2d_to_rank(u64 x, u64 y, u64 n) {
    return y * n + x;
}

u64 plrat_utils_rank_to_x(u64 rank, u64 n) {
    return rank % n;
}

u64 plrat_utils_rank_to_y(u64 rank, u64 n) {
    return rank / n;
}