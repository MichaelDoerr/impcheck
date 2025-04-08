
#include "plrat_reroute.h"

#include <assert.h>
#include <math.h>     // for sqrt
#include <stdbool.h>  // for bool, true, false
#include <stdio.h>    // for fclose, fflush_unlocked, fopen, snprintf
#include <stdlib.h>   // for free
#include <time.h>     // for clock, CLOCKS_PER_SEC, clock_t
#include <unistd.h>   // for access

#include "checker_interface.h"
#include "clause.h"
#include "hash.h"
#include "plrat_checker.h"  // for trusted_utils_read_int, trusted_utils_log...
#include "plrat_utils.h"
#include "import_merger.h"
#include "top_check.h"  // for top_check_commit_formula_sig, top_check_d...

const char* out_path;  // named pipe
u64 n_solvers;         // number of solvers
double root_n;         // square root of number of solvers
size_t comm_size;
u64 redist_strat;  // redistribution_strategy
u64 local_rank;      // solver id
const u64 empty_ID = -1;

// Buffering.
int* _re_current_literals_data;
u64 _re_current_literals_size;
u64 _re_current_ID = empty_ID;
u64* _re_count_clauses;
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
    u64 x = plrat_utils_rank_to_x(local_rank, comm_size);
    u64 y = plrat_utils_rank_to_y(id * comm_size, comm_size);
    return plrat_utils_2d_to_rank(x, y, comm_size);
}

void plrat_reroute_init(const char* main_path, unsigned long solver_rank, unsigned long num_solvers, unsigned long redistribution_strategy, unsigned long read_buffer_size) {
    redist_strat = redistribution_strategy;
    n_solvers = num_solvers;
    root_n = sqrt((double)num_solvers);
    comm_size = (size_t)ceil(root_n);  // round to nearest integer
    if (redist_strat == 1) {
        comm_size = n_solvers;
    }
    out_path = main_path;
    local_rank = solver_rank;
    _re_output_files = trusted_utils_malloc(sizeof(FILE*) * comm_size);
    _re_count_clauses = trusted_utils_calloc(comm_size, sizeof(u64));
    char msg[512];
    printf("local rank: %lu, num solvers: %lu\n", local_rank, n_solvers);
    snprintf(msg, 512, "root_n:%f", root_n);
    if (local_rank == 0) plrat_utils_log(msg);
    for (size_t i = 0; i < comm_size; i++) {
        char tmp_path[512];

        snprintf(tmp_path, 512, "%s/%lu/%lu.plrat_import", out_path, plrat_reroute_get_destination_rank(i), plrat_utils_rank_to_y(local_rank, comm_size));
        if (local_rank == 6) plrat_utils_log(tmp_path);
        _re_output_files[i] = fopen(tmp_path, "w");

        if (!(_re_output_files[i])) trusted_utils_exit_eof();
        plrat_reroute_write_int(0, _re_output_files[i]); // write placeholder 0 for count of clauses
    }
    char** file_paths = trusted_utils_malloc(sizeof(char*) * comm_size);

    for (size_t i = 0; i < comm_size; i++) {
        file_paths[i] = trusted_utils_malloc(512);
        snprintf(file_paths[i], 512, "%s/%lu/%lu.plrat_proxy", out_path, local_rank, i);
        if (access(file_paths[i], F_OK) != 0) {
            // file doesn't exist
            // create placeholder file containing only 0
            FILE* f = fopen(file_paths[i], "w");
            trusted_utils_write_int(0, f); // write placeholder 0 for count of clauses
            fclose(f);
        }
        if (local_rank == 6) plrat_utils_log(file_paths[i]);
        
    }
    import_merger_init(comm_size, file_paths, &_re_current_ID, &_re_current_literals_data, &_re_current_literals_size, read_buffer_size);

    // free
    for (size_t i = 0; i < comm_size; i++) {
        free(file_paths[i]);
    }
    free(file_paths);
}

int compare_clause(const void* a, const void* b) {
    u64 id_a = ((struct clause*)a)->id;
    u64 id_b = ((struct clause*)b)->id;
    return (id_a - id_b);
}

void plrat_reroute_end() {

    for (size_t i = 0; i < comm_size; i++) {
        fseek(_re_output_files[i], 0, SEEK_SET);
        plrat_reroute_write_int(_re_count_clauses[i], _re_output_files[i]);
        fclose(_re_output_files[i]);
    }
    free(_re_count_clauses);
    free(_re_output_files);
}

void plrat_reroute_run() {
    char msg[512];
    while (true) {
        import_merger_next();
        int destination_index = plrat_utils_rank_to_y(_re_current_ID % n_solvers, comm_size);
        if (2241 == _re_current_ID) {
            snprintf(msg, 512, "myID:%li current_ID:%lu destination_index:%i x:%li", local_rank, _re_current_ID, destination_index, plrat_utils_rank_to_x(_re_current_ID % n_solvers, comm_size));
            plrat_utils_log(msg);
        }
        if (MALLOB_UNLIKELY(_re_current_ID == empty_ID)) break;
        
        plrat_reroute_write_lrat_import_file(
            _re_current_ID,
            _re_current_literals_data,
            _re_current_literals_size,
            _re_output_files[destination_index]);

        _re_count_clauses[destination_index] += 1;


    }
    snprintf(msg, 512, "Done local_rank=%lu", local_rank);
    plrat_utils_log(msg);
}