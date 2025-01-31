
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

// Buffering.
struct int_vec** all_lits;
u64* clause_ids;
int* left_clauses;
FILE** importfiles;

void read_literals_index(int index, int nb_lits) {
    FILE* file = importfiles[index];
    struct int_vec* buf_lits = all_lits[index];
    int_vec_reserve(buf_lits, nb_lits);
    trusted_utils_read_ints(buf_lits->data, nb_lits, file);
}

void load_clause(int index) {
    FILE* file = importfiles[index];
    clause_ids[index] = trusted_utils_read_ul(file);
    int nb_lits = trusted_utils_read_int(file);
    read_literals_index(index, nb_lits);
}

void skip_proof_header(){
    char c = '\0';

    c = trusted_utils_read_char(my_proof);
    if(c == TRUSTED_CHK_INIT) {
        trusted_utils_skip_bytes(sizeof(int), my_proof);
    } else {
        trusted_utils_log_err("Invalid INIT");
    }

    c = trusted_utils_read_char(my_proof);
    if (c == TRUSTED_CHK_LOAD) {    
            const int nb_lits = trusted_utils_read_int(my_proof);
            trusted_utils_skip_bytes(nb_lits * sizeof(int), my_proof);
    } else {
        trusted_utils_log_err("Invalid LOAD");
    }

    c = trusted_utils_read_char(my_proof);
    if(c == TRUSTED_CHK_END_LOAD) {
        plrat_utils_log("Header Skipped");
    } else {
        plrat_utils_log_err("Invalid END_LOAD");
    }
}

void plrat_finder_init(const char* main_path, unsigned long solver_id, unsigned long num_solvers, unsigned long redistribution_strategy) {
    redist_strat = redistribution_strategy;
    n_solvers = num_solvers;
    out_path = main_path;
    local_id = solver_id;
    all_lits = trusted_utils_malloc(sizeof(struct int_vec*) * num_solvers);
    clause_ids = trusted_utils_malloc(sizeof(u64) * num_solvers);
    importfiles = trusted_utils_malloc(sizeof(FILE*) * num_solvers);
    left_clauses = trusted_utils_malloc(sizeof(int) * num_solvers);
    char msgg[512];
    char proof_path[512];
    snprintf(proof_path, 512, "%s/%lu/out.plrat", out_path, local_id);
    my_proof = fopen(proof_path, "r");

    skip_proof_header();

    for (size_t i = 0; i < n_solvers; i++) {
        char import_path[512];
        snprintf(import_path, 512, "%s/%lu/%lu.plrat_import", out_path, i, local_id);
        plrat_utils_log(import_path);
        importfiles[i] = fopen(import_path, "r");
        if (!(importfiles[i])) trusted_utils_exit_eof();
        all_lits[i] = int_vec_init(1);
        left_clauses[i] = trusted_utils_read_int(importfiles[i]);
        if (left_clauses[i] > 0) {
            load_clause(i);
        } else {
            clause_ids[i] = -1;
        }
        snprintf(msgg, 512, "i=%lu left_clauses[i]=%i clause_ids[i]=%lu", i, left_clauses[i], clause_ids[i]);
        plrat_utils_log(msgg);
    }
}

int compare_clause(const void* a, const void* b) {
    u64 id_a = ((struct clause*)a)->id;
    u64 id_b = ((struct clause*)b)->id;
    return (id_a - id_b);
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
    // struct clause _clause;
    // int file_id = id % n_solvers;
    //_clause.id = id;
    //_clause.nb_lits = nb_literals;
    //_clause.start = all_lits[file_id]->size;
    // clause_vec_push(clauses[file_id], _clause);
    //
    // for (int i = 0; i < nb_literals; i++) {
    //    int_vec_push(all_lits[file_id], literals[i]);
    //}
}