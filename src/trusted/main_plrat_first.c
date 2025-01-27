
#include <stdbool.h>          // for bool, false
#include <stdio.h>            // for fflush, stdout
//#include "trusted_checker.h"  // for tc_init, tc_run
#include "trusted_utils.h"    // for trusted_utils_try_match_arg, trusted_ut...
#include "plrat_utils.h"
#include "plrat_checker.h"
//#if IMPCHECK_WRITE_DIRECTIVES
//#include <unistd.h>
//#include "../writer.h"
//#endif

int main(int argc, char *argv[]) {
    // path/to/formula.cnf path/to/proofs/ <num-solvers> <solver-id> <redistribution-strategy>
    const char *formula_path = "", *proofs_path = "";
    u64 num_solvers = 0, solver_id = 0, redistribution_strategy = 0;
    for (int i = 1; i < argc; i++) {
        trusted_utils_try_match_arg(argv[i], "-formula-path=", &formula_path);
        trusted_utils_try_match_arg(argv[i], "-proofs-path=", &proofs_path);
        trusted_utils_try_match_num(argv[i], "-num-solvers=", &num_solvers);
        trusted_utils_try_match_num(argv[i], "-solver-id=", &solver_id);
        trusted_utils_try_match_num(argv[i], "-redistribution-strategy=", &redistribution_strategy);
    }


    char output_path[512];
    snprintf(output_path, 512, "-formula-path=%s -proofs-path=%s num-solvers=%lu -solver-id=%lu -redistribution-strategy=%lu",
    formula_path, proofs_path, num_solvers, solver_id, redistribution_strategy);
    plrat_utils_log(output_path);

    char proof_path[512];
    snprintf(proof_path, 512, "%s/%lu/out.plrat", proofs_path, solver_id);

    pc_init(formula_path, proofs_path, solver_id, num_solvers, redistribution_strategy);
    int res = pc_run();
    fflush(stdout);
    return res;
}
