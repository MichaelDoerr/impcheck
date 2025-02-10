
#include "plrat_finder.h"

#include <assert.h>
#include <stdbool.h>  // for bool, true, false
#include <stdio.h>    // for fclose, fflush_unlocked, fopen, snprintf
#include <stdlib.h>   // for free
#include <time.h>     // for clock, CLOCKS_PER_SEC, clock_t

#include "checker_interface.h"
#include "clause.h"
#include "hash.h"
#include "plrat_checker.h"
#include "plrat_utils.h"

// Instantiate int_vec
#define TYPE int
#define TYPED(THING) int_##THING
#include "vec.h"
#undef TYPED
#undef TYPE

const char* out_path;  // named pipe
u64 n_solvers;         // number of solvers
u64 redist_strat;      // redistribution_strategy
u64 local_id;          // solver id
FILE* my_proof;
const u64 empty_ID = -1;

// Buffering.
struct int_vec** all_lits;
u64* clause_ids;
int* left_clauses;
FILE** importfiles;

struct int_vec* proof_lits;

void read_literals(int nb_lits) {
    int_vec_reserve(proof_lits, nb_lits);
    trusted_utils_read_ints(proof_lits->data, nb_lits, my_proof);
}

void read_literals_index(int index, int nb_lits) {
    FILE* file = importfiles[index];
    struct int_vec* buf_lits = all_lits[index];
    int_vec_resize(buf_lits, nb_lits);
    trusted_utils_read_ints(buf_lits->data, nb_lits, file);
}

void load_clause_if_available(int index) {
    if (left_clauses[index] > 0) {
        FILE* file = importfiles[index];
        clause_ids[index] = trusted_utils_read_ul(file);
        int nb_lits = trusted_utils_read_int(file);
        read_literals_index(index, nb_lits);
        left_clauses[index] -= 1;
    } else {
        clause_ids[index] = -1;
    }
}

void copy_lits(int* dest, int* src, int nb_lits) {
    for (int i = 0; i < nb_lits; i++) {
        dest[i] = src[i];
    }
}

bool compare_lits(int* lits1, int* lits2, int nb_lits1, int nb_lits2) {
    if (nb_lits1 != nb_lits2) return false;
    for (int i = 0; i < nb_lits1; i++) {
        if (lits1[i] != lits2[i]) return false;
    }
    return true;
}

void skip_proof_header() {
    char c = '\0';

    c = trusted_utils_read_char(my_proof);
    if (c == TRUSTED_CHK_INIT) {
        trusted_utils_skip_bytes(sizeof(int), my_proof);
    } else {
        trusted_utils_log_err("Invalid INIT");
    }

    c = trusted_utils_read_char(my_proof);
    while (c == TRUSTED_CHK_LOAD) {
        if (c == TRUSTED_CHK_LOAD) {
            const int nb_lits = trusted_utils_read_int(my_proof);
            trusted_utils_skip_bytes(nb_lits * sizeof(int), my_proof);
        } else {
            trusted_utils_log_err("Invalid LOAD");
        }
        c = trusted_utils_read_char(my_proof);
    }

    if (c == TRUSTED_CHK_END_LOAD) {
        plrat_utils_log("Header Skipped");
    } else {
        char err_str[512];
        snprintf(err_str, 512, "Invalid END_LOAD c:%c", c);
        plrat_utils_log_err(err_str);
    }
}

void plrat_finder_init(const char* main_path, unsigned long solver_id, unsigned long num_solvers, unsigned long redistribution_strategy) {
    redist_strat = redistribution_strategy;
    n_solvers = num_solvers;
    out_path = main_path;
    local_id = solver_id;
    proof_lits = int_vec_init(1);
    all_lits = trusted_utils_malloc(sizeof(struct int_vec*) * num_solvers);
    clause_ids = trusted_utils_malloc(sizeof(u64) * num_solvers);
    importfiles = trusted_utils_malloc(sizeof(FILE*) * num_solvers);
    left_clauses = trusted_utils_malloc(sizeof(int) * num_solvers);
    char proof_path[512];
    snprintf(proof_path, 512, "%s/%lu/out.plrat", out_path, local_id);
    my_proof = fopen(proof_path, "r");

    skip_proof_header();

    for (size_t i = 0; i < n_solvers; i++) {
        char import_path[512];
        snprintf(import_path, 512, "%s/%lu/%lu.plrat_import", out_path, local_id, i);
        // plrat_utils_log(import_path);
        importfiles[i] = fopen(import_path, "r");
        if (!(importfiles[i])) trusted_utils_exit_eof();
        all_lits[i] = int_vec_init(1);
        left_clauses[i] = trusted_utils_read_int(importfiles[i]);
        load_clause_if_available(i);
    }
}

int compare_clause(const void* a, const void* b) {
    u64 id_a = ((struct clause*)a)->id;
    u64 id_b = ((struct clause*)b)->id;
    if (id_a <= id_b) return -1;
    return 1;  // Dont care about a == b
}

void plrat_finder_end() {
    // struct clause current_clause;
    // u64 current_clause_id;
    // FILE* current_out;
    //
    // for (size_t i = 0; i < n_solvers; i++) {
    //    qsort(clauses[i]->data, clauses[i]->size, sizeof(struct clause), compare_clause);
    //
    //    current_out = importfiles[i];
    //    plrat_utils_write_int(clauses[i]->size, redist_strat, current_out);
    //
    //    for (size_t c = 0; c < clauses[i]->size; c++) {
    //        current_clause = clauses[i]->data[c];
    //        current_clause_id = current_clause.id;
    //        plrat_importer_write_lrat_import_file(
    //            current_clause_id,
    //            &(all_lits[i]->data[current_clause.start]),
    //            current_clause.nb_lits,
    //            current_out);
    //    }
    //}
    //
    // for (size_t i = 0; i < n_solvers; i++) {
    //    int_vec_free(all_lits[i]);
    //    clause_vec_free(clauses[i]);
    //    free(all_lits[i]);
    //    free(clauses[i]);
    //    fclose(importfiles[i]);
    //}
    // free(importfiles);
    // free(all_lits);
    // free(clauses);
    // free(left_clauses);
}

void plrat_finder_run() {
    bool imports_left = true;
    while (imports_left) {
        u64 current_ID = -1;
        struct int_vec* current_literals = int_vec_init(1);
        size_t index_to_load = -1;
        // find the smallest clause id
        for (size_t i = 0; i < n_solvers; i++) {
            if (clause_ids[i] != empty_ID && clause_ids[i] < current_ID) {
                current_ID = clause_ids[i];
                index_to_load = i;
            }
        }
        // load the lits
        int_vec_resize(current_literals, all_lits[index_to_load]->size);
        copy_lits(current_literals->data, all_lits[index_to_load]->data, all_lits[index_to_load]->size);

        load_clause_if_available(index_to_load);
        // check and skip duplicates
        for (size_t i = 0; i < n_solvers; i++) {
            if (index_to_load != i && clause_ids[i] == current_ID) {
                current_ID = clause_ids[i];
                if (!compare_lits(current_literals->data, all_lits[i]->data, current_literals->size, all_lits[i]->size)) {
                    char err_str[512];
                    snprintf(err_str, 512, "literals do not match \nID:%lu index_to_load:%lu i:%lu", current_ID, index_to_load, i);
                    plrat_utils_log_err(err_str);
                    exit(1);
                }
                load_clause_if_available(i);
            }
        }
        // find clause id in proof
        // if (local_id == 0) plrat_utils_log("search");
        while (true) {
            int c = trusted_utils_read_char(my_proof);
            // if (local_id == 0) {
            //     char msg[512];
            //     snprintf(msg, 512, "current_ID=%lu processIndex=%lu c=%c", current_ID, local_id, c);
            //     plrat_utils_log(msg);
            // }
            if (c == TRUSTED_CHK_CLS_PRODUCE) {
                // parse
                u64 id = trusted_utils_read_ul(my_proof);
                const int nb_lits = trusted_utils_read_int(my_proof);
                int nb_hints;
                // skip line
                // if (local_id == 0) {
                //    char msgr[512];
                //    snprintf(msgr, 512, "A %lu n:%i", id, nb_hints);
                //    plrat_utils_log(msgr);
                //}
                if (id < current_ID) {
                    trusted_utils_skip_bytes(nb_lits * sizeof(int), my_proof);
                    nb_hints = trusted_utils_read_int(my_proof);
                    trusted_utils_skip_bytes(nb_hints * sizeof(u64), my_proof);
                }
                // check if the clause is the same
                if (id == current_ID) {
                    read_literals(nb_lits);
                    nb_hints = trusted_utils_read_int(my_proof);
                    trusted_utils_skip_bytes(nb_hints * sizeof(u64), my_proof);
                    if (compare_lits(current_literals->data, proof_lits->data, current_literals->size, nb_lits)) {
                        // plrat_utils_log("found clause, nice");
                        break;
                    } else {
                        char err_str[512];
                        snprintf(err_str, 512, "literals do not match in proof my rank:%lu ID:%lu importer_index:%lu", local_id, current_ID, index_to_load);
                        plrat_utils_log_err(err_str);
                        exit(1);
                    }
                }
                if (id > current_ID) {
                    char err_str[512];
                    snprintf(err_str, 512, "clause not found in proof my rank:%lu ID:%lu importer_index:%lu", local_id, current_ID, index_to_load);
                    plrat_utils_log_err(err_str);
                    exit(1);
                }

            } else if (c == TRUSTED_CHK_CLS_IMPORT) {
                trusted_utils_skip_bytes(sizeof(u64), my_proof);
                const int nb_lits = trusted_utils_read_int(my_proof);
                trusted_utils_skip_bytes(nb_lits * sizeof(int), my_proof);

            } else if (c == TRUSTED_CHK_CLS_DELETE) {
                // parse
                const int nb_hints = trusted_utils_read_int(my_proof);
                trusted_utils_skip_bytes(nb_hints * sizeof(u64), my_proof);

            } else if (c == TRUSTED_CHK_TERMINATE) {
                break;

            } else {
                trusted_utils_log_err("Invalid directive!");
                exit(1);
                break;
            }
        }

        imports_left = false;
        for (size_t i = 0; i < n_solvers; i++) {
            if (clause_ids[i] != empty_ID) {
                imports_left = true;
            }
        }
    }
    char msg[512];
    snprintf(msg, 512, "Done local_id=%lu", local_id);
    plrat_utils_log(msg);
}