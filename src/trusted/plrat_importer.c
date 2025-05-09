
#include "plrat_importer.h"

#include <assert.h>
#include <math.h>      // for sqrt
#include <stdbool.h>   // for bool, true, false
#include <stdio.h>     // for fclose, fflush_unlocked, fopen, snprintf
#include <stdlib.h>    // for free
#include <sys/stat.h>  // for mkdir
#include <time.h>      // for clock, CLOCKS_PER_SEC, clock_t

#include "checker_interface.h"
#include "clause.h"
#include "hash.h"
#include "plrat_checker.h"  // for trusted_utils_read_int, trusted_utils_log...
#include "plrat_utils.h"
#include "secret.h"
#include "commutative_sig.h"
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
double root_n;         // square root of number of solvers
long* written_lits;     // number of lits written to the file
struct comm_sig** signatures;
size_t comm_size;
u64 redist_strat;  // redistribution_strategy
u64 local_rank;    // solver id

// Buffering.
struct int_vec** all_lits;
struct clause_vec** clauses;
FILE** id_reference_files;
FILE** lits_array_files;

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

void plrat_importer_write_int(int value, FILE* current_out) {
    if (redist_strat == 0) {
        fprintf(current_out, "%i", value);
        fprintf(current_out, "\n");
    } else {
        trusted_utils_write_int(value, current_out);
    }
}

void plrat_importer_write_id_ref(struct clause* clause, FILE* current_out) {
    if (redist_strat == 0) {
        fprintf(current_out, "%lu %lu %i\n", clause->id, clause->start, clause->nb_lits);
    } else {
        // const int last_index = sizeof(u64) - 1; // index of the last byte
        // char* first_byte = ((char*)&clause->id);
        // for (char* current_byte = first_byte + last_index; first_byte <= current_byte; --current_byte) { // write with big endian for binary sorting
        //     trusted_utils_write_char(*current_byte, current_out);
        // }
        trusted_utils_write_ul(plrat_swap_endianess(clause->id), current_out);  // write with big endian for binary sorting
        // trusted_utils_write_ul(clause->id, current_out);
        trusted_utils_write_ul(clause->start, current_out);
        trusted_utils_write_int(clause->nb_lits, current_out);
    }
}

void plrat_importer_write_ints(int* literals, size_t nb_literals, FILE* current_out) {
    if (redist_strat == 0) {
        for (size_t i = 0; i < nb_literals; ++i) {
            fprintf(current_out, "%i ", literals[i]);
        }
    } else {
        trusted_utils_write_ints(literals, nb_literals, current_out);
    }
}

void plrat_importer_write_hash(u8* hash, FILE* current_out) {
    trusted_utils_write_sig(hash, current_out);
}

u64 plrat_importer_get_proxy_rank(size_t id) {
    u64 x = plrat_utils_rank_to_x(id % n_solvers, comm_size);
    u64 y = plrat_utils_rank_to_y(local_rank, comm_size);
    return plrat_utils_2d_to_rank(x, y, comm_size);
}

FILE* plrat_importer_get_proxy_file(size_t id) {
    return id_reference_files[plrat_importer_get_proxy_rank(id)];
}

void plrat_importer_init(const char* main_path, unsigned long solver_id, unsigned long num_solvers, unsigned long redistribution_strategy, unsigned long write_buffer_size) {
    redist_strat = redistribution_strategy;
    n_solvers = num_solvers;
    root_n = sqrt((double)num_solvers);
    comm_size = (size_t)ceil(root_n);  // round to nearest integer
    if (redist_strat == 1) {
        comm_size = n_solvers;
    }
    out_path = main_path;
    local_rank = solver_id;
    written_lits = trusted_utils_calloc(comm_size, sizeof(long));
    all_lits = trusted_utils_malloc(sizeof(struct int_vec*) * num_solvers);
    clauses = trusted_utils_malloc(sizeof(struct clause_vec*) * num_solvers);
    id_reference_files = trusted_utils_malloc(sizeof(FILE*) * comm_size);
    lits_array_files = trusted_utils_malloc(sizeof(FILE*) * comm_size);
    signatures = trusted_utils_malloc(sizeof(struct comm_sig*) * comm_size);

    if (local_rank == 0) {
        char msg[512];
        snprintf(msg, 512, "comm_size: %ld\n", comm_size);
        plrat_utils_log(msg);
    }

    for (size_t i = 0; i < comm_size; i++) {
        char ids_path[512];
        char clauses_path[512];
        char proof_folder[512];
        u64 proxy_rank = plrat_importer_get_proxy_rank(i);
        if (redist_strat == 2) {
            snprintf(proof_folder, 512, "%s/%lu", out_path, proxy_rank);
        } else {
            snprintf(proof_folder, 512, "%s/%lu", out_path, i);
        }
        if (proxy_rank > num_solvers - 1) {
            mkdir(proof_folder, 0755);
        }

        if (redist_strat == 2) {
            snprintf(ids_path, 512, "%s/%lu.plrat_ids", proof_folder, plrat_utils_rank_to_x(local_rank, comm_size));
            snprintf(clauses_path, 512, "%s/%lu.plrat_clauses", proof_folder, plrat_utils_rank_to_x(local_rank, comm_size));
        } else {
            snprintf(ids_path, 512, "%s/%lu.plrat_import", proof_folder, local_rank);
        }

        // plrat_utils_log(ids_path);
        id_reference_files[i] = fopen(ids_path, "wb");
        if (!(id_reference_files[i])) trusted_utils_exit_eof();
        lits_array_files[i] = fopen(clauses_path, "wb");
        if (!(lits_array_files[i])) trusted_utils_exit_eof();

        if (i != local_rank) {
            all_lits[i] = int_vec_init(write_buffer_size / sizeof(int));
            clauses[i] = clause_vec_init(write_buffer_size / sizeof(struct clause));
        } else {
            // Use a small placeholder for less edgecases
            all_lits[i] = int_vec_init(1);
            clauses[i] = clause_vec_init(1);
        }
        signatures[i] = comm_sig_init(SECRET_KEY_2);
    }
}

int compare_clause(const void* a, const void* b) {
    u64 id_a = ((struct clause*)a)->id;
    u64 id_b = ((struct clause*)b)->id;
    return (id_a - id_b);
}

void plrat_importer_end() {
    FILE* id_out;
    FILE* lits_out;

    for (size_t i = 0; i < comm_size; i++) {
        
        id_out = id_reference_files[i];
        lits_out = lits_array_files[i];
        struct int_vec current_lits = *all_lits[i];
        struct clause* end = clauses[i]->data + clauses[i]->size;  // Get the end of the clause array
        for (struct clause* c = clauses[i]->data; c < end; c++) {
            comm_sig_update_clause(signatures[i], c->id, current_lits.data + c->start, c->nb_lits);  // Update the signature with the clause id and literals
            plrat_importer_write_id_ref(c, id_out);
        }
        plrat_importer_write_ints(current_lits.data, current_lits.size, lits_out);  // Write the number of clauses
        u8* sig = comm_sig_digest(signatures[i]);
        plrat_importer_write_hash(sig, id_out);
        comm_sig_free(signatures[i]);
    }

    for (size_t i = 0; i < comm_size; i++) {
        int_vec_free(all_lits[i]);
        clause_vec_free(clauses[i]);
        fclose(id_reference_files[i]);
    }
    free(id_reference_files);
    free(lits_array_files);
    free(all_lits);
    free(clauses);
}

void plrat_importer_end_old() {
    struct clause current_clause;
    u64 current_clause_id;
    FILE* current_out;

    for (size_t i = 0; i < comm_size; i++) {
        //struct siphash* hash = siphash_cls_init(SECRET_KEY);  // Initialize the hash with SECRET_KEY
        // qsort(clauses[i]->data, clauses[i]->size, sizeof(struct clause), compare_clause);

        current_out = id_reference_files[i];
        plrat_importer_write_int(clauses[i]->size, current_out);
        //siphash_cls_update(hash, (u8*)&(clauses[i]->size), sizeof(int));

        for (size_t c = 0; c < clauses[i]->size; c++) {
            current_clause = clauses[i]->data[c];
            current_clause_id = current_clause.id;
            //siphash_cls_update(hash, (u8*)&current_clause_id, sizeof(u64));
            //siphash_cls_update(hash, (u8*)&(all_lits[i]->data[current_clause.start]), current_clause.nb_lits * sizeof(int));
            plrat_importer_write_lrat_import_file(
                current_clause_id,
                &(all_lits[i]->data[current_clause.start]),
                current_clause.nb_lits,
                current_out);
        }
        //u8* sig = siphash_cls_digest(hash);
        //plrat_importer_write_hash(sig, current_out);
        //siphash_cls_free(hash);
    }

    for (size_t i = 0; i < comm_size; i++) {
        int_vec_free(all_lits[i]);
        clause_vec_free(clauses[i]);
        fclose(id_reference_files[i]);
    }
    free(id_reference_files);
    free(all_lits);
    free(clauses);
}

void plrat_importer_log(unsigned long id, const int* literals, int nb_literals) {
    struct clause _clause;
    int file_id = plrat_utils_rank_to_x(id % n_solvers, comm_size);
    _clause.id = id;
    _clause.nb_lits = nb_literals;
    _clause.start = all_lits[file_id]->size + written_lits[file_id];
    struct clause_vec* clauses_vec = clauses[file_id];

    if (clauses_vec->size == clauses_vec->capacity) { // write to file if capacity is reached
        FILE* id_out = id_reference_files[file_id];
        struct clause* end = clauses_vec->data + clauses_vec->size;  // Get the end of the clause array
        for (struct clause* c = clauses_vec->data; c < end; c++) {
            plrat_importer_write_id_ref(c, id_out);
        }
        clauses_vec->size = 0; // Reset used size to 0 after writing to file
    }
    clauses_vec->data[clauses_vec->size++] = _clause;

    struct int_vec* lits_vec = all_lits[file_id];
    FILE* lits_out = lits_array_files[file_id];
    if ((long)lits_vec->size + (long)nb_literals > (long)lits_vec->capacity) {  // write to file if capacity is reached
        plrat_importer_write_ints(lits_vec->data, lits_vec->size, lits_out);
        written_lits[file_id] += lits_vec->size; // Update the total number of written literals
        lits_vec->size = 0;  // Reset used size to 0 after writing to file
    }
    int_vec_reserve(lits_vec, nb_literals);  // capacity is defined to only grow and never shrink

    for (int i = 0; i < nb_literals; i++) {
        lits_vec->data[lits_vec->size++] = literals[i];
    }
}