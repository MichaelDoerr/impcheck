
#include "plrat_checker.h"  // for trusted_utils_read_int, trusted_utils_log...

#include <assert.h>
#include <stdbool.h>  // for bool, true, false
#include <stdio.h>    // for fclose, fflush_unlocked, fopen, snprintf
#include <stdlib.h>   // for free
#include <time.h>     // for clock, CLOCKS_PER_SEC, clock_t

#include "checker_interface.h"
#include "hash.h"
#include "plrat_importer.h"
#include "plrat_utils.h"
#include "top_check.h"  // for top_check_commit_formula_sig, top_check_d...

#define UNUSED(x) (void)(x)

// Instantiate int_vec
#define TYPE int
#define TYPED(THING) int_##THING
#include "vec.h"
#undef TYPED
#undef TYPE

// Instantiate u64_vec
#define TYPE u64
#define TYPED(THING) u64_##THING
#include "vec.h"
#undef TYPED
#undef TYPE

FILE* proof;            // named pipe
FILE* formular;         // named pipe
int nb_vars;            // # variables in formula
signature formula_sig;  // formula signature
u64 nb_solvers;         // number of solvers
u64 redist;             // redistribution_strategy
u64 pc_nb_loaded_clauses;  // number of loaded clauses

bool do_logging = true;

// Buffering.
signature buf_sig;
struct int_vec* buf_lits;
struct u64_vec* buf_hints;

void read_literals(int nb_lits) {
    int_vec_reserve(buf_lits, nb_lits);
    trusted_utils_read_ints(buf_lits->data, nb_lits, proof);
}

void read_hints(int nb_hints) {
    u64_vec_reserve(buf_hints, nb_hints);
    trusted_utils_read_uls(buf_hints->data, nb_hints, proof);
}

void pc_init(const char* formula_path, const char* proofs_path, unsigned long solver_id, unsigned long num_solvers, unsigned long redistribution_strategy) {
    char proof_path[512];
    snprintf(proof_path, 512, "%s/%lu/out.plrat", proofs_path, solver_id);

    proof = fopen(proof_path, "r");
    if (!proof) trusted_utils_exit_eof();
    formular = fopen(formula_path, "r");
    if (!formular) trusted_utils_exit_eof();
    UNUSED(formula_path);
    buf_lits = int_vec_init(1 << 14);
    buf_hints = u64_vec_init(1 << 14);
    nb_solvers = num_solvers;
    plrat_importer_init(proofs_path, solver_id, num_solvers, redistribution_strategy);
}

void pc_end() {
    plrat_importer_end();
    top_check_end();
    int_vec_free(buf_lits);
    u64_vec_free(buf_hints);
    fclose(formular);
    fclose(proof);
}

int pc_run() {
    clock_t start = clock();
    u64 nb_produced = 0, nb_imported = 0, nb_deleted = 0;
    bool reported_error = false;
    char c = '\0';

    c = trusted_utils_read_char(proof);
    if (c == TRUSTED_CHK_INIT) {
        nb_vars = trusted_utils_read_int(proof);
        top_check_init(nb_vars, false, false);
    } else {
        trusted_utils_log_err("Invalid INIT");
        reported_error = true;
    }

    c = trusted_utils_read_char(proof);
    while (c == TRUSTED_CHK_LOAD) {
        if (c == TRUSTED_CHK_LOAD) {
            const int nb_lits = trusted_utils_read_int(proof);
            read_literals(nb_lits);
            for (int i = 0; i < nb_lits; i++) top_check_load(buf_lits->data[i]);
        } else {
            trusted_utils_log_err("Invalid LOAD");
            reported_error = true;
        }
        c = trusted_utils_read_char(proof);
    }

    if (c == TRUSTED_CHK_END_LOAD) {
        char log_str[512];
        snprintf(log_str, 512, "Formular Loaded");
        plrat_utils_log(log_str);
    } else {
        char err_str[512];
        snprintf(err_str, 512, "Invalid END_LOAD c:%c", c);
        plrat_utils_log_err(err_str);
        reported_error = true;
    }

    while (true) {
        int c = trusted_utils_read_char(proof);
        if (c == TRUSTED_CHK_CLS_PRODUCE) {
            // parse
            u64 id = trusted_utils_read_ul(proof);
            const int nb_lits = trusted_utils_read_int(proof);
            read_literals(nb_lits);
            const int nb_hints = trusted_utils_read_int(proof);
            read_hints(nb_hints);
            // forward to checker
            top_check_produce(id, buf_lits->data, nb_lits,
                              buf_hints->data, nb_hints);
            nb_produced++;

        } else if (c == TRUSTED_CHK_CLS_IMPORT) {
            // parse
            const u64 id = trusted_utils_read_ul(proof);
            const int nb_lits = trusted_utils_read_int(proof);
            read_literals(nb_lits);
            // forward to checker
            plrat_utils_import_unchecked(id, buf_lits->data, nb_lits);
            nb_imported++;

            // write in file for stage 2
            plrat_importer_log(id, buf_lits->data, nb_lits);

        } else if (c == TRUSTED_CHK_CLS_DELETE) {
            // parse
            const int nb_hints = trusted_utils_read_int(proof);
            read_hints(nb_hints);
            // forward to checker
            top_check_delete(buf_hints->data, nb_hints);
            nb_deleted += nb_hints;

        } else if (c == TRUSTED_CHK_TERMINATE) {
            break;

        } else {
            trusted_utils_log_err("Invalid directive!");
            break;
        }

        if (MALLOB_UNLIKELY(!top_check_valid())) {
            if (!reported_error) {
                trusted_utils_log_err(trusted_utils_msgstr);
                reported_error = true;
            }
        }
    }
    float elapsed = (float)(clock() - start) / CLOCKS_PER_SEC;
    snprintf(trusted_utils_msgstr, 512, "cpu:%.3f prod:%lu imp:%lu del:%lu n_s:%lu", elapsed, nb_produced, nb_imported, nb_deleted, nb_solvers);
    trusted_utils_log(trusted_utils_msgstr);

    return 0;
}