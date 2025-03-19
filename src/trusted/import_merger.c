#include "import_merger.h"

const u64 _im_empty_ID = -1;

size_t _im_n_files;
int* _im_left_clauses;
u64* _im_clause_ids;
struct int_vec** _im_all_lits;
FILE** _im_import_files;
// Buffering.

u64* _im_current_id;  // is -1 if no clause is available (end of files)
struct int_vec* _im_current_literals;

void read_literals_index(int index, int nb_lits) {
    FILE* file = _im_import_files[index];
    struct int_vec* buf_lits = _im_all_lits[index];
    int_vec_resize(buf_lits, nb_lits);
    trusted_utils_read_ints(buf_lits->data, nb_lits, file);
}

void load_clause_if_available(int index) {
    if (_im_left_clauses[index] > 0) {
        FILE* file = _im_import_files[index];
        _im_clause_ids[index] = trusted_utils_read_ul(file);
        int nb_lits = trusted_utils_read_int(file);
        read_literals_index(index, nb_lits);
        _im_left_clauses[index] -= 1;
    } else {
        _im_clause_ids[index] = -1;
    }
}

void copy_lits(int* dest, int* src, int nb_lits) {
    for (int i = 0; i < nb_lits; i++) {
        dest[i] = src[i];
    }
}

void import_merger_init(int count_input_files, char** file_paths, u64* current_id, struct int_vec* current_literals) {
    _im_n_files = count_input_files;
    _im_current_id = current_id;              // output location
    _im_current_literals = current_literals;  // output location
    _im_clause_ids = trusted_utils_malloc(sizeof(u64) * _im_n_files);
    _im_all_lits = trusted_utils_malloc(sizeof(struct int_vec*) * _im_n_files);
    _im_import_files = trusted_utils_malloc(sizeof(FILE*) * _im_n_files);
    _im_left_clauses = trusted_utils_malloc(sizeof(int) * _im_n_files);
    for (size_t i = 0; i < _im_n_files; i++) {
        // plrat_utils_log(file_paths[i]);
        _im_import_files[i] = fopen(file_paths[i], "r");
        if (!(_im_import_files[i])) trusted_utils_exit_eof();
        _im_all_lits[i] = int_vec_init(1);
        _im_left_clauses[i] = trusted_utils_read_int(_im_import_files[i]);
        load_clause_if_available(i);
    }
}

void import_merger_next() {
    bool imports_left = false;
    u64 current_id = _im_empty_ID;
    *_im_current_id = _im_empty_ID;
    size_t index_to_load = -1;
    const struct int_vec* candidate_lits;
    // find the smallest clause id
    for (size_t i = 0; i < _im_n_files; i++) {
        u64 temp_id = _im_clause_ids[i];
        

        if (temp_id < current_id && temp_id != _im_empty_ID) {
            current_id = temp_id;
            index_to_load = i;
            candidate_lits = _im_all_lits[index_to_load];
            imports_left = true;
        } else if (temp_id == current_id && current_id != _im_empty_ID) { // check and skip duplicates
            candidate_lits = _im_all_lits[index_to_load];
            const struct int_vec* temp_lits = _im_all_lits[i];
            if (MALLOB_UNLIKELY(!plrat_utils_compare_lits(candidate_lits->data, temp_lits->data, candidate_lits->size, temp_lits->size))) {
                char err_str[512];
                snprintf(err_str, 512, "literals do not match \nID:%lu index_to_load:%lu i:%lu", current_id, index_to_load, i);
                plrat_utils_log_err(err_str);
                exit(1);
            }
            load_clause_if_available(i);
        }
    }

    //char msg[512];
    //snprintf(msg, 512, "current_ID:%lu index_to_load:%lu imports_left:%d", *_im_current_id, index_to_load, imports_left);
    //plrat_utils_log(msg);
    if (!imports_left) {
        return;
    }
    // load the lits
    // char err_str1[512];
    // snprintf(err_str1, 512, "HERE WE HAVE:%lu", index_to_load);
    // plrat_utils_log_err(err_str1);
    int_vec_resize(_im_current_literals, candidate_lits->size);
    copy_lits(_im_current_literals->data, candidate_lits->data, candidate_lits->size);
    *_im_current_id = current_id;

    load_clause_if_available(index_to_load);
    
}