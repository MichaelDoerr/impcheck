
#include <assert.h>
#include <stdbool.h>  // for bool, true, false
#include <stdio.h>    // for fclose, fflush_unlocked, fopen, snprintf
#include <stdlib.h>   // for free
#include <time.h>     // for clock, CLOCKS_PER_SEC, clock_t

#include "checker_interface.h"
#include "clause.h"
#include "hash.h"
#include "plrat_checker.h"  // for trusted_utils_read_int, trusted_utils_log...
#include "plrat_utils.h"
#include "top_check.h"  // for top_check_commit_formula_sig, top_check_d...

// Instantiate clause_vec
#define TYPE struct clause
#define TYPED(THING) clause_##THING
#include "vec.h"
#undef TYPED
#undef TYPE

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

// Buffering.
struct int_vec** all_lits;
struct clause_vec** clauses;
FILE** importfiles;

void plrat_importer_write_lrat_import_file(u64 clause_id, int* literals, int nb_literals, FILE* current_out) {
    if (redist_strat == 0) {
        fprintf(current_out, "%lu ", clause_id);
        fprintf(current_out, " ");
        fprintf(current_out, "n:%i", nb_literals);
        fprintf(current_out, " ");
        for (int i = 0; i < nb_literals; i++) {
            fprintf(current_out, "%i ", literals[i]);
        }
        fprintf(current_out, "%i", 0);
        fprintf(current_out, "\n");
    } else {
        trusted_utils_write_ul(clause_id, current_out);
        trusted_utils_write_int(nb_literals, current_out);
        trusted_utils_write_ints(literals, nb_literals, current_out);
    }
}

void plrat_utils_write_int(int value, u64 redist_strat, FILE* current_out) {
    if (redist_strat == 0) {
        fprintf(current_out, "%i", value);
        fprintf(current_out, "\n");
    } else {
        trusted_utils_write_int(value, current_out);
    }
}

void plrat_importer_init(const char* main_path, unsigned long solver_id, unsigned long num_solvers, unsigned long redistribution_strategy) {
    redist_strat = redistribution_strategy;
    n_solvers = num_solvers;
    out_path = main_path;
    local_id = solver_id;
    all_lits = trusted_utils_malloc(sizeof(struct int_vec*) * (num_solvers));
    clauses = trusted_utils_malloc(sizeof(struct clause_vec*) * (num_solvers));
    importfiles = trusted_utils_malloc(sizeof(FILE*) * num_solvers);

    for (size_t i = 0; i < n_solvers; i++) {
        char proof_path[512];
        snprintf(proof_path, 512, "%s/%lu/%lu.plrat_import", out_path, i, local_id);
        plrat_utils_log(proof_path);
        importfiles[i] = fopen(proof_path, "w");
        if (i != local_id) {
            all_lits[i] = int_vec_init(1024);
            clauses[i] = clause_vec_init(1024);
        } else {
            all_lits[i] = int_vec_init(1);
            clauses[i] = clause_vec_init(1);
        }
        if (!(importfiles[i])) trusted_utils_exit_eof();
    }
}

int compare_clause(const void* a, const void* b) {
    u64 id_a = ((struct clause*)a)->id;
    u64 id_b = ((struct clause*)b)->id;
    return (id_a - id_b);
}

void plrat_importer_end() {
    struct clause current_clause;
    u64 current_clause_id;
    FILE* current_out;

    for (size_t i = 0; i < n_solvers; i++) {
        qsort(clauses[i]->data, clauses[i]->size, sizeof(struct clause), compare_clause);

        current_out = importfiles[i];
        plrat_utils_write_int(clauses[i]->size, redist_strat, current_out);

        for (size_t c = 0; c < clauses[i]->size; c++) {
            current_clause = clauses[i]->data[c];
            current_clause_id = current_clause.id;
            plrat_importer_write_lrat_import_file(
                current_clause_id,
                &(all_lits[i]->data[current_clause.start]),
                current_clause.nb_lits,
                current_out);
        }
    }

    for (size_t i = 0; i < n_solvers; i++) {
        int_vec_free(all_lits[i]);
        clause_vec_free(clauses[i]);

        free(all_lits[i]);
        free(clauses[i]);
        fclose(importfiles[i]);
    }
    free(importfiles);
    free(all_lits);
    free(clauses);
}

void plrat_importer_log(unsigned long id, const int* literals, int nb_literals) {
    struct clause _clause;
    int file_id = id % n_solvers;
    _clause.id = id;
    _clause.nb_lits = nb_literals;
    _clause.start = all_lits[file_id]->size;
    clause_vec_push(clauses[file_id], _clause);

    for (int i = 0; i < nb_literals; i++) {
        int_vec_push(all_lits[file_id], literals[i]);
    }
}