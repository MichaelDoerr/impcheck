
#include "plrat_finder.h"

#include <assert.h>
#include <math.h>     // for sqrt
#include <stdbool.h>  // for bool, true, false
#include <stdio.h>    // for fclose, fflush_unlocked, fopen, snprintf
#include <stdlib.h>   // for free
#include <time.h>     // for clock, CLOCKS_PER_SEC, clock_t

#include "checker_interface.h"
#include "import_merger.h"
#include "hash.h"
#include "plrat_checker.h"
#include "plrat_utils.h"



const char* out_path;  // named pipe
u64 n_solvers;         // number of solvers
double root_n;         // square root of number of solvers
size_t comm_size;
u64 redist_strat;      // redistribution_strategy
u64 local_rank;          // solver id
FILE* my_proof;
const u64 empty_ID = -1;

// Buffering.
struct int_vec** all_lits;
int* left_clauses;
FILE** importfiles;
u64 current_ID = empty_ID;
struct int_vec* current_literals;

struct int_vec* proof_lits;

void read_literals(int nb_lits) {
    int_vec_reserve(proof_lits, nb_lits);
    trusted_utils_read_ints(proof_lits->data, nb_lits, my_proof);
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
    root_n = sqrt((double)num_solvers);
    comm_size = (size_t)ceil(root_n);  // round to nearest integer
    if(redist_strat == 1) {
        comm_size = n_solvers;
    }
    out_path = main_path;
    local_rank = solver_id;
    proof_lits = int_vec_init(1);
    char proof_path[512];
    snprintf(proof_path, 512, "%s/%lu/out.plrat", out_path, local_rank);
    my_proof = fopen(proof_path, "r");

    skip_proof_header();

    char** file_paths = trusted_utils_malloc(sizeof(char*) * comm_size);

    for (size_t i = 0; i < comm_size; i++) {
        file_paths[i] = trusted_utils_malloc(512);
        snprintf(file_paths[i], 512, "%s/%lu/%lu.plrat_import", out_path, local_rank, i);
    }
    current_literals = int_vec_init(1);
    import_merger_init(comm_size, file_paths, &current_ID, current_literals);

    // free
    for (size_t i = 0; i < comm_size; i++) {
        free(file_paths[i]);
    }
    free(file_paths);
}

//int compare_clause(const void* a, const void* b) {
//    u64 id_a = ((struct clause*)a)->id;
//    u64 id_b = ((struct clause*)b)->id;
//    if (id_a <= id_b) return -1;
//    return 1;  // Dont care about a == b
//}

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
    while (true) {
        import_merger_next();
        
        if (current_ID == empty_ID) break;

        while (true) {
            int c = trusted_utils_read_char(my_proof);
            if (c == TRUSTED_CHK_CLS_PRODUCE) {
                // parse
                u64 id = trusted_utils_read_ul(my_proof);
                const int nb_lits = trusted_utils_read_int(my_proof);
                int nb_hints;
                // skip line
                if (id < current_ID) {
                    trusted_utils_skip_bytes(nb_lits * sizeof(int), my_proof);
                    nb_hints = trusted_utils_read_int(my_proof);
                    trusted_utils_skip_bytes(nb_hints * sizeof(u64), my_proof);
                    continue;
                }
                // check if the clause is the same
                if (id == current_ID) {
                    read_literals(nb_lits);
                    nb_hints = trusted_utils_read_int(my_proof);
                    trusted_utils_skip_bytes(nb_hints * sizeof(u64), my_proof);
                    if (plrat_utils_compare_lits(current_literals->data, proof_lits->data, current_literals->size, nb_lits)) {
                        // plrat_utils_log("found clause, nice");
                        break;
                    } else {
                        char err_str[512];
                        snprintf(err_str, 512, "literals do not match in proof my rank:%lu ID:%lu", local_rank, current_ID);
                        plrat_utils_log_err(err_str);
                        exit(1);
                    }
                }
                if (id > current_ID) {
                    char err_str[512];
                    snprintf(err_str, 512, "clause not found in proof my rank:%lu ID:%lu", local_rank, current_ID);
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
    }
    char msg[512];
    snprintf(msg, 512, "Done local_rank=%lu", local_rank);
    plrat_utils_log(msg);
}