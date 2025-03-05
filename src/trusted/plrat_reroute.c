
#include <assert.h>
#include <stdbool.h>  // for bool, true, false
#include <stdio.h>    // for fclose, fflush_unlocked, fopen, snprintf
#include <stdlib.h>   // for free
#include <time.h>     // for clock, CLOCKS_PER_SEC, clock_t
#include <math.h>     // for sqrt

#include "checker_interface.h"
#include "clause.h"
#include "hash.h"
#include "plrat_checker.h"  // for trusted_utils_read_int, trusted_utils_log...
#include "plrat_utils.h"
#include "top_check.h"  // for top_check_commit_formula_sig, top_check_d...
#include "plrat_reroute.h"

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
double root_n;          // square root of number of solvers
size_t comm_size;
u64 redist_strat;      // redistribution_strategy
u64 local_id;          // solver id

// Buffering.
struct int_vec** _re_all_lits;
struct int_vec* _re_current_literals;
struct clause_vec** _re_clauses;
FILE** _re_proxy_files;
FILE** _re_output_files;

void plrat_reroute_write_lrat_import_file(u64 clause_id, int* literals, int nb_literals, FILE* current_out) {
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

void plrat_reroute_write_int(int value, FILE* current_out) {
    if (redist_strat == 0) {
        fprintf(current_out, "%i", value);
        fprintf(current_out, "\n");
    } else {
        trusted_utils_write_int(value, current_out);
    }
}

u64 plrat_reroute_get_destination_rank(size_t id) {
    u64 x = plrat_utils_rank_to_x(local_id, comm_size);
    u64 y = plrat_utils_rank_to_y(id*comm_size, comm_size);
    return plrat_utils_2d_to_rank(x, y, comm_size);
}

FILE* plrat_reroute_get_proxy_file(size_t id) {
    return _re_proxy_files[plrat_reroute_get_proxy_rank(id)];
}

void plrat_reroute_init(const char* main_path, unsigned long solver_id, unsigned long num_solvers, unsigned long redistribution_strategy) {
    redist_strat = redistribution_strategy;
    n_solvers = num_solvers;
    root_n = sqrt((double)num_solvers);
    comm_size = (size_t)ceil(root_n); // round to nearest integer
    if(redist_strat == 1) {
        comm_size = n_solvers;
    }
    out_path = main_path;
    local_id = solver_id;
    _re_all_lits = trusted_utils_malloc(sizeof(struct int_vec*) * (num_solvers));
    _re_clauses = trusted_utils_malloc(sizeof(struct clause_vec*) * (num_solvers));
    _re_proxy_files = trusted_utils_malloc(sizeof(FILE*) * comm_size);
    _re_output_files = trusted_utils_malloc(sizeof(FILE*) * comm_size);
    char msg[512];
    snprintf(msg, 512, "root_n:%f", root_n);
    plrat_utils_log(msg);
    for (size_t i = 0; i < comm_size; i++) {
        char tmp_path[512];
        snprintf(tmp_path, 512, "%s/%lu/%lu.plrat_proxy", out_path, local_id, i);
        plrat_utils_log(tmp_path);
        _re_proxy_files[i] = fopen(tmp_path, "r");

        snprintf(tmp_path, 512, "%s/%lu/%lu.plrat_import", out_path, plrat_reroute_get_destination_rank(i), plrat_utils_rank_to_y(local_id, comm_size));
        plrat_utils_log(tmp_path);
        _re_proxy_files[i] = fopen(tmp_path, "w");

        if (!(_re_proxy_files[i])) trusted_utils_exit_eof();
        if (!(_re_output_files[i])) trusted_utils_exit_eof();
        
        //setup für den merger, oben ist das mit plrat_proxy wohl redundant. wahscheinlich müssen die plrat_proxy files erst hier so gelesen werden.

        char** file_paths = trusted_utils_malloc(sizeof(char*) * n_solvers);

        for (size_t i = 0; i < n_solvers; i++) {
            file_paths[i] = trusted_utils_malloc(512);
            snprintf(file_paths[i], 512, "%s/%lu/%lu.plrat_import", out_path, local_id, i);
        }
        _re_current_literals = int_vec_init(1);
        import_merger_init(solver_id, n_solvers, file_paths, &current_ID, _re_current_literals);

        // free
        for (size_t i = 0; i < n_solvers; i++) {
            free(file_paths[i]);
        }
        free(file_paths);
    }
}

int compare_clause(const void* a, const void* b) {
    u64 id_a = ((struct clause*)a)->id;
    u64 id_b = ((struct clause*)b)->id;
    return (id_a - id_b);
}

void plrat_reroute_end() {
    struct clause current_clause;
    u64 current_clause_id;
    FILE* current_out;

    for (size_t i = 0; i < comm_size; i++) {
        qsort(_re_clauses[i]->data, _re_clauses[i]->size, sizeof(struct clause), compare_clause);

        current_out = _re_proxy_files[i];
        plrat_reroute_write_int(_re_clauses[i]->size, current_out);

        for (size_t c = 0; c < _re_clauses[i]->size; c++) {
            current_clause = _re_clauses[i]->data[c];
            current_clause_id = current_clause.id;
            plrat_reroute_write_lrat_import_file(
                current_clause_id,
                &(_re_all_lits[i]->data[current_clause.start]),
                current_clause.nb_lits,
                current_out);
        }
    }

    for (size_t i = 0; i < comm_size; i++) {
        int_vec_free(_re_all_lits[i]);
        clause_vec_free(_re_clauses[i]);
        fclose(_re_proxy_files[i]);
    }
    free(_re_proxy_files);
    free(_re_all_lits);
    free(_re_clauses);
}

void plrat_reroute_run() {

}