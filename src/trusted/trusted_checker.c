
#include <stdbool.h>        // for bool, true, false
#include <stdio.h>          // for fclose, fflush_unlocked, fopen, snprintf
#include <stdlib.h>         // for free
#include <time.h>           // for clock, CLOCKS_PER_SEC, clock_t
#include "top_check.h"      // for top_check_commit_formula_sig, top_check_d...
#include "trusted_utils.h"  // for trusted_utils_read_int, trusted_utils_log...
#include "checker_interface.h"
#include "hash.h"
#include "plrat_utils.h"
#include <assert.h>

// Instantiate int_vec
#define TYPE int
#define TYPED(THING) int_ ## THING
#include "vec.h"
#undef TYPED
#undef TYPE

// Instantiate u64_vec
#define TYPE u64
#define TYPED(THING) u64_ ## THING
#include "vec.h"
#undef TYPED
#undef TYPE

FILE* input; // named pipe
FILE* output; // named pipe
int nb_vars; // # variables in formula
signature formula_sig; // formula signature
u64 nb_solvers; // number of solvers
u64 solver_id; 
u64 solver_modulo_remainder = 0;

bool do_logging = true;

// Buffering.
signature buf_sig;
struct int_vec* buf_lits;
struct u64_vec* buf_hints;


void say(bool ok) {
    trusted_utils_write_char(ok ? TRUSTED_CHK_RES_ACCEPT : TRUSTED_CHK_RES_ERROR, output);
#if IMPCHECK_FLUSH_ALWAYS
    UNLOCKED_IO(fflush)(output);
#endif
}
void say_with_flush(bool ok) {
    say(ok);
    UNLOCKED_IO(fflush)(output);
}

void read_literals(int nb_lits) {
    int_vec_reserve(buf_lits, nb_lits);
    trusted_utils_read_ints(buf_lits->data, nb_lits, input);
}

void read_hints(int nb_hints) {
    u64_vec_reserve(buf_hints, nb_hints);
    trusted_utils_read_uls(buf_hints->data, nb_hints, input);
}

void tc_init(const char* fifo_in, const char* fifo_out, u64 num_solvers, u64 global_solver_id) {
    input = fopen(fifo_in, "r");
    if (!input) trusted_utils_exit_eof();
    output = fopen(fifo_out, "w");
    if (!output) trusted_utils_exit_eof();
    buf_lits = int_vec_init(1 << 14);
    buf_hints = u64_vec_init(1 << 14);
    nb_solvers = num_solvers;
    solver_id = global_solver_id;
    
    plrat_utils_init_debug(global_solver_id,nb_solvers, "./");
}

void tc_end() {
    free(buf_hints);
    free(buf_lits);
    fclose(output);
    fclose(input);
    plrat_utils_end_debug();
    top_check_end();
}

int tc_run(bool check_model, bool lenient) {
    clock_t start = clock();

    u64 nb_produced = 0, nb_imported = 0, nb_deleted = 0;
#if IMPCHECK_PLRAT
    u64 last_id = 1;
    u64 offset = 0;
    struct hash_table* id_offsets = hash_table_init(15);
#endif

    bool reported_error = false;

    while (true) {
        int c = trusted_utils_read_char(input);
        if (c == TRUSTED_CHK_CLS_PRODUCE) {
            // parse
            u64 id = trusted_utils_read_ul(input);
            const int nb_lits = trusted_utils_read_int(input);
            read_literals(nb_lits);
            const int nb_hints = trusted_utils_read_int(input);
            read_hints(nb_hints);
            const bool share = trusted_utils_read_bool(input);
            // forward to checker
            bool res = top_check_produce(id, buf_lits->data, nb_lits,
                buf_hints->data, nb_hints);
            // respond
            say(res);
#if IMPCHECK_PLRAT
            id = plrat_utils_get_next_valid_id(id, &offset, 
            id_offsets, buf_hints->data, nb_hints, nb_solvers, solver_modulo_remainder);

            assert(last_id < id);
            last_id = id;
            trusted_utils_write_lrat_add(id, 
                buf_lits->data, nb_lits,
                buf_hints->data, nb_hints);
            id -= solver_modulo_remainder;
#endif
            if (share) {
                // compute signature if desired

                top_check_compute_clause_signature(id, buf_lits->data, nb_lits, buf_sig);
#if IMPCHECK_PLRAT
                trusted_utils_write_ul(id, output);
#endif
                trusted_utils_write_sig(buf_sig, output);
            }
#if IMPCHECK_FLUSH_ALWAYS
            UNLOCKED_IO(fflush)(output);
#endif
            nb_produced++;

        } else if (c == TRUSTED_CHK_CLS_IMPORT) {

            // parse
            u64 id = trusted_utils_read_ul(input);
            const int nb_lits = trusted_utils_read_int(input);
            read_literals(nb_lits);
            trusted_utils_read_sig(buf_sig, input);

            // forward to checker
            bool res = top_check_import(id, buf_lits->data, nb_lits, buf_sig);
            // respond
            say(res);
            nb_imported++;
#if IMPCHECK_PLRAT
            hash_table_insert(id_offsets, id, (void*)solver_modulo_remainder);
            id += solver_modulo_remainder;
            trusted_utils_write_lrat_import(last_id, id, buf_lits->data, nb_lits);
#endif

        } else if (c == TRUSTED_CHK_CLS_DELETE) {
            
            // parse
            const int nb_hints = trusted_utils_read_int(input);
            read_hints(nb_hints);
            // forward to checker
            bool res = top_check_delete(buf_hints->data, nb_hints);
            // respond
            say(res);
            nb_deleted += nb_hints;
#if IMPCHECK_PLRAT
            plrat_utils_translate_and_delete(id_offsets, buf_hints->data, nb_hints);
            trusted_utils_write_lrat_delete(last_id, buf_hints->data, nb_hints);
#endif

        } else if (c == TRUSTED_CHK_LOAD) {

            const int nb_lits = trusted_utils_read_int(input);
            read_literals(nb_lits);
            for (int i = 0; i < nb_lits; i++) top_check_load(buf_lits->data[i]);
#if IMPCHECK_PLRAT
            trusted_utils_write_lrat_load(TRUSTED_CHK_LOAD, buf_lits->data, nb_lits);
            
            //char o[512];
            //snprintf(o, 512, "LOAD nb_lits:%i", nb_lits);
            //trusted_utils_log(o);
#endif

        } else if (c == TRUSTED_CHK_INIT) {

            nb_vars = trusted_utils_read_int(input);
            top_check_init(nb_vars, check_model, lenient);
            trusted_utils_read_sig(formula_sig, input);
            top_check_commit_formula_sig(formula_sig);
            say_with_flush(true);
#if IMPCHECK_PLRAT
            trusted_utils_write_init(TRUSTED_CHK_INIT, nb_vars);
#endif
        } else if (c == TRUSTED_CHK_END_LOAD) {

            say_with_flush(top_check_end_load());
#if IMPCHECK_PLRAT
            
            u64 num_original_clauses = top_check_get_nb_loaded_clauses();

            char log_str[512];
            snprintf(log_str, 512, "Formular Loaded nb_clauses:%lu", num_original_clauses);
            plrat_utils_log(log_str);

            
            solver_modulo_remainder = (nb_solvers - (num_original_clauses % nb_solvers)) % nb_solvers;
            
            snprintf(log_str, 512, "solver_modulo_remainder:%lu", solver_modulo_remainder);
            plrat_utils_log(log_str);
            trusted_utils_write_end_load(TRUSTED_CHK_END_LOAD);
#endif
        } else if (c == TRUSTED_CHK_VALIDATE_UNSAT) {

            bool res = top_check_validate_unsat(buf_sig);
            say(res);
            trusted_utils_write_sig(buf_sig, output);
            UNLOCKED_IO(fflush)(output);
            if (res) trusted_utils_log("UNSAT validated");

        } else if (c == TRUSTED_CHK_VALIDATE_SAT) {

            const int model_size = trusted_utils_read_int(input);
            int* model = trusted_utils_malloc(sizeof(int) * model_size); // exits if error
            trusted_utils_read_ints(model, model_size, input);
            bool res = top_check_validate_sat(model, model_size, buf_sig);
            say(res);
            trusted_utils_write_sig(buf_sig, output);
            UNLOCKED_IO(fflush)(output);
            if (res) trusted_utils_log("SAT validated");
            free(model);

        } else if (c == TRUSTED_CHK_TERMINATE) {
#if IMPCHECK_PLRAT
            trusted_utils_write_terminate(TRUSTED_CHK_TERMINATE);
#endif
            say_with_flush(true);
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

    float elapsed = (float) (clock() - start) / CLOCKS_PER_SEC;
    snprintf(trusted_utils_msgstr, 512, "cpu:%.3f prod:%lu imp:%lu del:%lu n_s:%lu", elapsed, nb_produced, nb_imported, nb_deleted, nb_solvers);
    trusted_utils_log(trusted_utils_msgstr);

    return 0;
}
