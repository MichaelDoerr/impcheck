#include "plrat_utils.h"

#include <stdbool.h>  // for bool
#include <assert.h>

#include "hash.h"
#include "trusted_utils.h"

u64 plrat_utils_get_next_valid_id(const u64 old_id, u64* offset, struct hash_table* id_offsets, u64* hints, int nb_hints, u64 nb_solvers) {
    u64 local_offset = *offset;
    u64 new_id = old_id + local_offset;
    u64 max_hint_id = plrat_utils_add_offset(id_offsets, hints, nb_hints);

    if (new_id > max_hint_id) {
        hash_table_insert(id_offsets, old_id, (void*)local_offset);
        return new_id;  // no new offset needed
    }

    u64 new_offset = 1 + max_hint_id - old_id;
    u64 temp_rank = new_offset % nb_solvers;
    new_offset += nb_solvers - temp_rank;
    
    assert((new_offset % nb_solvers) == 0);

    hash_table_insert(id_offsets, old_id, (void*)new_offset);
    *offset = new_offset;
    new_id = old_id + new_offset;

    char msgstr2[512] = "";
    snprintf(msgstr2, 512, "bigger offset! new_id:%lu jump_offset:%lu new_offset:%lu htsize:%lu", old_id + new_offset, new_offset - local_offset, new_offset, id_offsets->capacity);
    trusted_utils_log(msgstr2);

    return old_id + new_offset;
}

u64 plrat_utils_add_offset(struct hash_table* id_offsets, u64* hints, int nb_hints) {
    u64 max_hint_id = 1;

    // look at hints and translate their id to id + offset
    for (int i = 0; i < nb_hints; ++i) {
        u64 hint = hints[i];
        void* current_offset = hash_table_find(id_offsets, hint);
        if (current_offset != NULL) {
            hint = hint + (u64)current_offset;
            hints[i] = hint;
        }
        max_hint_id = (max_hint_id < hint) ? hint : max_hint_id;  // find largest hint id
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
        if (current_offset != NULL) {
            hints[i] = original_id + (u64)current_offset;
            hash_table_delete(id_offsets, original_id);  // delete id
        }
    }
}