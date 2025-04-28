
#include "plrat_rebuild.h"

#include <assert.h>
#include <math.h>      // for sqrt
#include <stdbool.h>   // for bool, true, false
#include <stdio.h>     // for fclose, fflush_unlocked, fopen, snprintf
#include <stdlib.h>    // for free
#include <sys/stat.h>  // for mkdir
#include <time.h>      // for clock, CLOCKS_PER_SEC, clock_t
#include <unistd.h>    // for access

#include "checker_interface.h"
#include "clause.h"
#include "hash.h"
#include "import_merger.h"
#include "plrat_checker.h"  // for trusted_utils_read_int, trusted_utils_log...
#include "plrat_utils.h"
#include "secret.h"
#include "siphash_cls.h"
#include "top_check.h"  // for top_check_commit_formula_sig, top_check_d...
#include "plrat_file_reader.h"


const char* out_path;  // named pipe
u64 n_solvers;         // number of solvers
double root_n;         // square root of number of solvers
size_t comm_size;
u64 redist_strat;  // redistribution_strategy
u64 local_rank;    // solver id
const u64 empty_ID = -1;

// Buffering.
u64* _bu_count_clauses;
struct plrat_reader** _bu_id_files;
FILE** _bu_clause_files;
FILE** _bu_output_files;
struct int_vec* _bu_clause_buffer;

void plrat_rebuild_write_lrat_import_file(u64 clause_id, int* literals, int nb_literals, FILE* current_out) {
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

void plrat_rebuild_write_int(int value, FILE* current_out) {
    if (redist_strat == 0) {
        fprintf(current_out, "%i", value);
        fprintf(current_out, "\n");
    } else {
        trusted_utils_write_int(value, current_out);
    }
}

u64 plrat_rebuild_get_destination_rank(size_t id) {
    u64 x = plrat_utils_rank_to_x(local_rank, comm_size);
    u64 y = plrat_utils_rank_to_y(id * comm_size, comm_size);
    return plrat_utils_2d_to_rank(x, y, comm_size);
}

void plrat_rebuild_init(const char* main_path, unsigned long solver_rank, unsigned long num_solvers, unsigned long redistribution_strategy, unsigned long read_buffer_size) {
    redist_strat = redistribution_strategy;
    n_solvers = num_solvers;
    root_n = sqrt((double)num_solvers);
    comm_size = (size_t)ceil(root_n);  // round to nearest integer
    if (redist_strat == 1) {
        comm_size = n_solvers;
    }
    out_path = main_path;
    local_rank = solver_rank;
    _bu_count_clauses = trusted_utils_malloc(sizeof(u64) * comm_size);
    _bu_output_files = trusted_utils_malloc(sizeof(FILE*) * comm_size);
    _bu_clause_files = trusted_utils_malloc(sizeof(FILE*) * comm_size);
    _bu_id_files = trusted_utils_malloc(sizeof(struct plrat_reader*) * comm_size);

    // printf("local rank: %lu, num solvers: %lu\n", local_rank, n_solvers);
    char msg[512];
    snprintf(msg, 512, "root_n:%f", root_n);
    if (local_rank == 0) plrat_utils_log(msg);
    _bu_clause_buffer = int_vec_init(1024);

    for (size_t i = 0; i < comm_size; i++) {
        char id_file_path[512];
        char cls_file_path[512];
        char out_file_path[512];
        snprintf(id_file_path, 512, "%s/%lu/%lu.plrat_ids_sorted", out_path, local_rank, i);
        snprintf(cls_file_path, 512, "%s/%lu/%lu.plrat_clauses", out_path, local_rank, i);
        snprintf(out_file_path, 512, "%s/%lu/%lu.plrat_proxy", out_path, local_rank, i);

        if (access(cls_file_path, F_OK) != 0) {
            // file doesn't exist
            // create placeholder file containing only 0
            FILE* f = fopen(out_file_path, "w");
            trusted_utils_write_int(0, f);  // write placeholder 0 for count of clauses
            fclose(f);
            exit(0);
        }
        else {
            FILE *id_file = fopen(id_file_path, "rb");
            struct stat st;
            int fd = fileno(id_file);
            fstat(fd, &st);
            _bu_count_clauses[i] = st.st_size / 20;

            _bu_id_files[i] = plrat_reader_init(read_buffer_size, id_file, local_rank);
            _bu_clause_files[i] = fopen(cls_file_path, "rb");
            _bu_output_files[i] = fopen(out_file_path, "wb");
        }
    }
}

int compare_clause(const void* a, const void* b) {
    u64 id_a = ((struct clause*)a)->id;
    u64 id_b = ((struct clause*)b)->id;
    return (id_a - id_b);
}

void plrat_rebuild_end() {
    for (size_t i = 0; i < comm_size; i++) {
        fclose(_bu_clause_files[i]);
        fclose(_bu_output_files[i]);
        plrat_reader_end(_bu_id_files[i]);
    }
    free(_bu_clause_files);
    free(_bu_output_files);
    free(_bu_id_files);
    free(_bu_count_clauses);
}

void plrat_rebuild_run() {
    char msg[512];
    for (size_t i = 0; i < comm_size; i++) {
        trusted_utils_write_int(_bu_count_clauses[i], _bu_output_files[i]);
        for (size_t cls_nr = 0; cls_nr < _bu_count_clauses[i] ; cls_nr++) {
            u64 clause_id = plrat_swap_endianess(plrat_reader_read_ul(_bu_id_files[i]));
            u64 start_index = plrat_reader_read_ul(_bu_id_files[i]);
            u64 nb_lits = plrat_reader_read_int(_bu_id_files[i]);
            fseek(_bu_clause_files[i], start_index * sizeof(int), SEEK_SET);
            int_vec_resize(_bu_clause_buffer, nb_lits);
            trusted_utils_read_ints(_bu_clause_buffer->data, nb_lits, _bu_clause_files[i]);

            plrat_rebuild_write_lrat_import_file(clause_id, _bu_clause_buffer->data, nb_lits, _bu_output_files[i]);
        }
    }
    
    snprintf(msg, 512, "Done local_rank=%lu", local_rank);
    plrat_utils_log(msg);
}