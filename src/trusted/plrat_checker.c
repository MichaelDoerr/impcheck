
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

FILE* proof; // named pipe
FILE* formular; // named pipe
int nb_vars; // # variables in formula
signature formula_sig; // formula signature
u64 nb_solvers; // number of solvers

bool do_logging = true;

// Buffering.
signature buf_sig;
struct int_vec* buf_lits;
struct u64_vec* buf_hints;


int _sign = 1;
bool _comment = false;
bool _began_num = false;
bool _assumption = false;
int _num = 0;
int _max_var = 0;
int _num_read_clauses = 0;
bool _last_added_lit_was_zero = true;
bool _contains_empty_clause = false;


bool _input_invalid = false;
bool _input_finished = false;


void read_literals(int nb_lits) {
    int_vec_reserve(buf_lits, nb_lits);
    trusted_utils_read_ints(buf_lits->data, nb_lits, proof);
}

void read_hints(int nb_hints) {
    u64_vec_reserve(buf_hints, nb_hints);
    trusted_utils_read_uls(buf_hints->data, nb_hints, proof);
}

void pc_init(const char* formula_path, const char* proof_path, unsigned long num_solvers) {
    proof = fopen(proof_path, "r");
    if (!proof) trusted_utils_exit_eof();
    formular = fopen(formula_path, "r");
    if (!formular) trusted_utils_exit_eof();
    buf_lits = int_vec_init(1 << 14);
    buf_hints = u64_vec_init(1 << 14);
    nb_solvers = num_solvers;
}

void pc_end() {
    free(buf_hints);
    free(buf_lits);
    fclose(formular);
    fclose(proof);
}

int pc_run() {
    clock_t start = clock();
    u64 nb_produced = 0, nb_imported = 0, nb_deleted = 0;
    bool reported_error = false;
    char c = '\0';

    c = trusted_utils_read_char(proof);
    if(c == TRUSTED_CHK_INIT) {
        nb_vars = trusted_utils_read_int(proof);
        top_check_init(nb_vars, false, false);
    } else {
        trusted_utils_log_err("Invalid INIT");
        reported_error = true;
    }

    c = trusted_utils_read_char(proof);
    if (c == TRUSTED_CHK_LOAD) {    
            const int nb_lits = trusted_utils_read_int(proof);
            read_literals(nb_lits);
            for (int i = 0; i < nb_lits; i++) top_check_load(buf_lits->data[i]);
    } else {
        trusted_utils_log_err("Invalid LOAD");
        reported_error = true;
    }

    c = trusted_utils_read_char(proof);
    if(c == TRUSTED_CHK_END_LOAD) {
        trusted_utils_log("Formular Loaded");
    } else {
        trusted_utils_log_err("Invalid END_LOAD");
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
    float elapsed = (float) (clock() - start) / CLOCKS_PER_SEC;
    snprintf(trusted_utils_msgstr, 512, "cpu:%.3f prod:%lu imp:%lu del:%lu n_s:%lu", elapsed, nb_produced, nb_imported, nb_deleted, nb_solvers);
    trusted_utils_log(trusted_utils_msgstr);

    return 0;
}

inline void process(char c) {

    if (_comment && c != '\n') return;

    signed char uc = *((signed char*) &c);
    switch (uc) {
    case EOF:
        _input_finished = true;
    case '\n':
    case '\r':
        _comment = false;
        if (_began_num) {
            if (_num != 0) {
                _input_invalid = true;
                return;
            }
            if (!_assumption) {
                //desc.addPermanentData(0);
                if (_last_added_lit_was_zero) _contains_empty_clause = true;
                _last_added_lit_was_zero = true;
                _num_read_clauses++;
            }
            _began_num = false;
        }
        _assumption = false;
        break;
    case 'p':
    case 'c':
        _comment = true;
        break;
    case 'a':
        _assumption = true;
        break;
    case ' ':
        if (_began_num) {
            //_max_var = std::max(_max_var, _num);
            if (!_assumption) {
                int lit = _sign * _num;
                //desc.addPermanentData(lit);
                if (lit == 0) {
                    if (_last_added_lit_was_zero) _contains_empty_clause = true;
                    _num_read_clauses++;
                }
                _last_added_lit_was_zero = lit == 0;
            } else if (_num != 0) {
                //desc.addTransientData(_sign * _num);
            }
            _num = 0;
            _began_num = false;
        }
        _sign = 1;
        break;
    case '-':
        _sign = -1;
        _began_num = true;
        break;
    default:
        // Add digit to current number
        _num = _num*10 + (c-'0');
        _began_num = true;
        break;
    }
}
