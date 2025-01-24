
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
u64 redist; // redistribution_strategy

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

void plrat_importer_init(const char* main_path, unsigned long num_solvers, unsigned long redistribution_strategy) {
    // For each i in num_solvers: proof = fopen(proof_path + "(i)", "w");
    //      if (!proof) trusted_utils_exit_eof();
    // init vec of clause ids
    // init ht of imported clauses with (clause id) -> (literals pointer)

    nb_solvers = num_solvers;
}

void plrat_importer_end() {
    free(buf_hints);
    free(buf_lits);
    fclose(formular);
    fclose(proof);
}

int plrat_importer_log(unsigned long id, const int* literals, int nb_literals) {
    
}