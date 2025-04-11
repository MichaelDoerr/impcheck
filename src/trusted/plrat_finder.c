
#include "plrat_finder.h"

#include <assert.h>
#include <math.h>     // for sqrt
#include <stdbool.h>  // for bool, true, false
#include <stdio.h>    // for fclose, fflush_unlocked, fopen, snprintf
#include <stdlib.h>   // for free
#include <time.h>     // for clock, CLOCKS_PER_SEC, clock_t

#include "checker_interface.h"
#include "hash.h"
#include "import_merger.h"
#include "plrat_checker.h"
#include "plrat_file_reader.h"
#include "plrat_utils.h"
#include "secret.h"
#include "siphash_cls.h"

const char* out_path;  // named pipe
u64 n_solvers;         // number of solvers
double root_n;         // square root of number of solvers
size_t comm_size;
u64 redist_strat;  // redistribution_strategy
u64 local_rank;    // solver id
FILE* my_proof;
const u64 empty_ID = -1;
struct siphash* clause_hash;

// Buffering.
struct int_vec** all_lits;
int* left_clauses;
FILE** importfiles;
u64 current_ID = empty_ID;
int* current_literals_data;
u64 current_literals_size;
struct plrat_reader* proof_reader;

struct int_vec* proof_lits;

void read_literals(int nb_lits) {
    int_vec_reserve(proof_lits, nb_lits);
    plrat_reader_read_ints(proof_lits->data, nb_lits, proof_reader);
}

void skip_proof_header() {
    char c = '\0';

    c = plrat_reader_read_char(proof_reader);
    // if (local_rank == 0) {
    //     printf("char: %c\n", c);
    // }
    if (c == TRUSTED_CHK_INIT) {
        plrat_reader_skip_bytes(sizeof(int), proof_reader);
    } else {
        trusted_utils_log_err("Invalid INIT");
    }

    c = plrat_reader_read_char(proof_reader);
    // if (local_rank == 0) {
    //     printf("c: %c\n", c);
    // }
    while (c == TRUSTED_CHK_LOAD) {
        const int nb_lits = plrat_reader_read_int(proof_reader);
        plrat_reader_skip_bytes(nb_lits * sizeof(int), proof_reader);
        c = plrat_reader_read_char(proof_reader);
    }

    if (c == TRUSTED_CHK_END_LOAD) {
        if (local_rank == 0) {
            plrat_utils_log("Header Skipped");
        }
    } else {
        char err_str[512];
        snprintf(err_str, 512, "Invalid END_LOAD c:%c", c);
        plrat_utils_log_err(err_str);
    }
}

void plrat_finder_init(const char* main_path, unsigned long solver_id, unsigned long num_solvers, unsigned long redistribution_strategy, unsigned long read_buffer_size) {
    redist_strat = redistribution_strategy;
    n_solvers = num_solvers;
    root_n = sqrt((double)num_solvers);
    comm_size = (size_t)ceil(root_n);  // round to nearest integer
    if (redist_strat == 1) {
        comm_size = n_solvers;
    }
    out_path = main_path;
    local_rank = solver_id;
    proof_lits = int_vec_init(1);
    char proof_path[512];
    snprintf(proof_path, 512, "%s/%lu/out.plrat", out_path, local_rank);
    my_proof = fopen(proof_path, "rb");

    clause_hash = siphash_cls_init(SECRET_KEY);
    proof_reader = plrat_reader_init(read_buffer_size, my_proof, local_rank);

    skip_proof_header();

    char** file_paths = trusted_utils_malloc(sizeof(char*) * comm_size);

    for (size_t i = 0; i < comm_size; i++) {
        file_paths[i] = trusted_utils_malloc(512);
        snprintf(file_paths[i], 512, "%s/%lu/%lu.plrat_import", out_path, local_rank, i);
    }

    import_merger_init(comm_size, file_paths, &current_ID, &current_literals_data, &current_literals_size, read_buffer_size);

    // free
    for (size_t i = 0; i < comm_size; i++) {
        free(file_paths[i]);
    }
    free(file_paths);
}

// int compare_clause(const void* a, const void* b) {
//     u64 id_a = ((struct clause*)a)->id;
//     u64 id_b = ((struct clause*)b)->id;
//     if (id_a <= id_b) return -1;
//     return 1;  // Dont care about a == b
// }

void plrat_finder_end() {
    plrat_reader_end(proof_reader);
    import_merger_end();
    int_vec_free(proof_lits);
}

void plrat_finder_run() {
    bool found_T = false;
    while (!found_T) {
        import_merger_next();

        // if (current_ID == empty_ID) break;

        while (true) {
            int c = plrat_reader_read_char(proof_reader);
            if (c == TRUSTED_CHK_CLS_PRODUCE) {
                // parse
                u64 id = plrat_reader_read_ul(proof_reader);
                // if (local_rank == 0) {
                //     printf("id: %lu\n", id);
                // }

                siphash_cls_update(clause_hash, (u8*)&id, sizeof(u64));
                const int nb_lits = plrat_reader_read_int(proof_reader);
                // TODO: ALWAYS read lits for siphash_cls_update
                read_literals(nb_lits);
                siphash_cls_update(clause_hash, (u8*)proof_lits->data, nb_lits * sizeof(int));
                int nb_hints;
                // skip line
                if (id < current_ID) {
                    nb_hints = plrat_reader_read_int(proof_reader);
                    plrat_reader_skip_bytes(nb_hints * sizeof(u64), proof_reader);
                    continue;
                }
                // check if the clause is the same
                if (id == current_ID) {
                    nb_hints = plrat_reader_read_int(proof_reader);
                    plrat_reader_skip_bytes(nb_hints * sizeof(u64), proof_reader);
                    if (plrat_utils_compare_lits(current_literals_data, proof_lits->data, current_literals_size, nb_lits)) {
                        // plrat_utils_log("found clause, nice");
                        break;
                    } else {
                        char err_str[512];
                        snprintf(err_str, 512, "literals do not match in proof my rank:%lu ID:%lu", local_rank, current_ID);
                        if (local_rank == 0) {
                            printf("current_literals_data %lu: ", current_literals_size);
                            for (u64 i = 0; i < current_literals_size; i++) {
                                printf("%d ", current_literals_data[i]);
                            }
                            printf("\n");
                            printf("proof_lits %i: ", nb_lits);
                            for (int i = 0; i < nb_lits; i++) {
                                printf("%d ", proof_lits->data[i]);
                            }
                            printf("\n");
                        }

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
                plrat_reader_skip_bytes(sizeof(u64), proof_reader);
                const int nb_lits = plrat_reader_read_int(proof_reader);
                plrat_reader_skip_bytes(nb_lits * sizeof(int), proof_reader);

            } else if (c == TRUSTED_CHK_CLS_DELETE) {
                // parse
                const int nb_hints = plrat_reader_read_int(proof_reader);
                plrat_reader_skip_bytes(nb_hints * sizeof(u64), proof_reader);

            } else if (c == TRUSTED_CHK_TERMINATE) {
                const u8* sig_res_computed = siphash_cls_digest(clause_hash);
                const u8 sig_res_reported[16];
                plrat_reader_read_ints((int*)sig_res_reported, 4, proof_reader);
                if (!trusted_utils_equal_signatures(sig_res_computed, sig_res_reported)) {
                    trusted_utils_log_err("Signature does not match!");
                } else {
                    char msg[512];
                    snprintf(msg, 512, "Signature matches in local rank: %lu", local_rank);
                    trusted_utils_log(msg);
                }
                siphash_cls_free(clause_hash);
                found_T = true;
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