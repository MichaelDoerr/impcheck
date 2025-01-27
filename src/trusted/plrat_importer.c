
#include <stdbool.h>        // for bool, true, false
#include <stdio.h>          // for fclose, fflush_unlocked, fopen, snprintf
#include <stdlib.h>         // for free
#include <time.h>           // for clock, CLOCKS_PER_SEC, clock_t
#include "top_check.h"      // for top_check_commit_formula_sig, top_check_d...
#include "plrat_checker.h"  // for trusted_utils_read_int, trusted_utils_log...
#include "checker_interface.h"
#include "hash.h"
#include "plrat_utils.h"
#include <assert.h>
#include "clause.h"

// Instantiate clause_vec
#define TYPE struct clause
#define TYPED(THING) clause_ ## THING
#include "vec.h"
#undef TYPED
#undef TYPE

// Instantiate int_vec
#define TYPE int
#define TYPED(THING) int_ ## THING
#include "vec.h"
#undef TYPED
#undef TYPE

const char* out_path; // named pipe
u64 n_solvers; // number of solvers
u64 redist_strat; // redistribution_strategy


// Buffering.
struct int_vec* all_lits;
struct clause_vec* clauses;
FILE** importfiles;



void plrat_importer_init(const char* main_path, unsigned long solver_id, unsigned long num_solvers, unsigned long redistribution_strategy) {
    redist_strat = redistribution_strategy;
    n_solvers = num_solvers;
    out_path = main_path;
    all_lits = int_vec_init(1024);
    clauses = clause_vec_init(128);
    importfiles = trusted_utils_malloc(sizeof(FILE*) * num_solvers);

    for (size_t i = 0; i < n_solvers; i++) {
        char proof_path[512];
        snprintf(proof_path, 512, "%s/%lu/%lu.plrat_import", out_path, i, solver_id);
        plrat_utils_log(proof_path);
        importfiles[i] = fopen(proof_path, "w");
        if (!(importfiles[i])) trusted_utils_exit_eof();
    }
}

void plrat_importer_end() {
    u32 origin_index;
    struct clause current_clause;
    u64 current_clause_id;
    FILE* current_out;
    for (size_t c = 0; c < clauses->size; c++){
        current_clause = clauses->data[c];
        current_clause_id = current_clause.id;
        origin_index = current_clause_id % n_solvers;
        current_out = importfiles[origin_index];
        trusted_utils_write_ul(current_clause_id, current_out);
        trusted_utils_write_int(current_clause.nb_lits, current_out);
        trusted_utils_write_ints(
            &(all_lits->data[current_clause.start]),
            current_clause.nb_lits,
            current_out);
    }

    for (size_t i = 0; i < n_solvers; i++) {
        fclose(importfiles[i]);
    }
    free(importfiles);   
    free(all_lits->data);
    free(clauses->data);
    free(all_lits);
    free(clauses);
}

void plrat_importer_log(unsigned long id, const int* literals, int nb_literals) {
    struct clause _clause;
    _clause.id = id;
    _clause.nb_lits = nb_literals;
    _clause.start = all_lits->size;
    clause_vec_push(clauses, _clause);

    for (int i = 0; i < nb_literals; i++) {
        int_vec_push(all_lits,literals[i]);
    }
}