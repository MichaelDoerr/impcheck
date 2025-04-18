#include "import_merger.h"
#include "plrat_file_reader.h"

const u64 _im_empty_ID = -1;

size_t _im_n_files;
int* _im_left_clauses;
u64* _im_clause_ids;
struct int_vec** _im_all_lits;
struct plrat_reader** _im_import_files;
struct siphash** _im_check_hash;
int last_index_to_load = 0;
// Buffering.

u64* _im_current_id;  // is -1 if no clause is available (end of files)
int** _im_current_literals_data;
u64* _im_current_literals_size;

void read_literals_index(int index, int nb_lits) {
    struct int_vec* buf_lits = _im_all_lits[index];
    int_vec_resize(buf_lits, nb_lits);
    plrat_reader_read_ints(buf_lits->data, nb_lits, _im_import_files[index]);
}

void load_clause_if_available(int index) {
    if (MALLOB_LIKELY(_im_left_clauses[index] > 0)) {
        struct plrat_reader* file = _im_import_files[index];
        _im_clause_ids[index] = plrat_reader_read_ul(file);
        int nb_lits = plrat_reader_read_int(file);
        read_literals_index(index, nb_lits);
        _im_left_clauses[index] -= 1;
        if (_im_check_hash != NULL) {
            siphash_cls_update(_im_check_hash[index], (const u8*)&_im_clause_ids[index], sizeof(u64));
            siphash_cls_update(_im_check_hash[index], (const u8*)_im_all_lits[index]->data, _im_all_lits[index]->size * sizeof(int));
        }
    } else {
        _im_clause_ids[index] = -1;
    }
}

void copy_lits(int* dest, int* src, int nb_lits) {
    for (int i = 0; i < nb_lits; i++) {
        dest[i] = src[i];
    }
}

void import_merger_init(int count_input_files, char** file_paths, u64* current_id, int** current_literals_data, u64* current_literals_size, u64 read_buffer_size, struct siphash** import_check_hash) {
    _im_n_files = count_input_files;
    _im_check_hash = import_check_hash;
    _im_current_id = current_id;              // output location
    _im_current_literals_data = current_literals_data;  // output location
    _im_current_literals_size = current_literals_size;  // output location
    _im_clause_ids = trusted_utils_malloc(sizeof(u64) * _im_n_files);
    _im_all_lits = trusted_utils_malloc(sizeof(struct int_vec*) * _im_n_files);
    _im_import_files = trusted_utils_malloc(sizeof(struct plrat_reader*) * _im_n_files);
    _im_left_clauses = trusted_utils_malloc(sizeof(int) * _im_n_files);
    for (size_t i = 0; i < _im_n_files; i++) {
        // plrat_utils_log(file_paths[i]);
        
        _im_import_files[i] = plrat_reader_init(read_buffer_size, fopen(file_paths[i], "rb"), -1);
        if (!(_im_import_files[i])) trusted_utils_exit_eof();
        _im_all_lits[i] = int_vec_init(1);
        _im_left_clauses[i] = plrat_reader_read_int(_im_import_files[i]);
        
    }
    // load the first clause of each file exept for 0
    // that way import_merger_next does not need an if statement at start (if "last_index_to_load is invalid")
    for (size_t i = 1; i < _im_n_files; i++) {
        load_clause_if_available(i);
    }
}

void import_merger_end() {
    for (size_t i = 0; i < _im_n_files; i++) {
        plrat_reader_end(_im_import_files[i]);
        int_vec_free(_im_all_lits[i]);
    }
    free(_im_import_files);
    free(_im_all_lits);
    free(_im_clause_ids);
    free(_im_left_clauses);
}

void import_merger_next() {
    load_clause_if_available(last_index_to_load);  
    bool imports_left = false;
    u64 current_id = _im_empty_ID;
    *_im_current_id = _im_empty_ID;
    size_t index_to_load = -1;
    struct int_vec candidate_lits;
    // find the smallest clause id
    for (size_t i = 0; i < _im_n_files; i++) {
        u64 temp_id = _im_clause_ids[i];
        

        if (temp_id < current_id && temp_id != _im_empty_ID) {
            current_id = temp_id;
            index_to_load = i;
            candidate_lits = *_im_all_lits[index_to_load];
            imports_left = true;
        } else if (temp_id == current_id && current_id != _im_empty_ID) { // check and skip duplicates
            candidate_lits = *_im_all_lits[index_to_load];
            const struct int_vec* temp_lits = _im_all_lits[i];
            if (MALLOB_UNLIKELY(!plrat_utils_compare_lits(candidate_lits.data, temp_lits->data, candidate_lits.size, temp_lits->size))) {
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
    *_im_current_literals_size = candidate_lits.size;
    *_im_current_literals_data = candidate_lits.data;
    *_im_current_id = current_id;
    last_index_to_load = index_to_load;
    
}

void import_merger_read_sig(int* sig_res_reported, int index) {
    plrat_reader_read_ints(sig_res_reported, 4, _im_import_files[index]);
}