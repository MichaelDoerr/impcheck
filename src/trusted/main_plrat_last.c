#include <stdbool.h>          // for bool, false
#include <stdio.h>            // for fflush, stdout
#include "trusted_utils.h"    // for trusted_utils_try_match_arg, trusted_ut...
#include "plrat_utils.h"
#include "plrat_finder.h"

int main(int argc, char *argv[]) {
    // path/to/formula.cnf path/to/proofs/ <num-solvers> <solver-id> <redistribution-strategy>
    const char *formula_path = "", *proofs_path = "";
    u64 num_solvers = 0, solver_id = 0, redistribution_strategy = 0, read_buffer_KB = 1024;
    for (int i = 1; i < argc; i++) {
        trusted_utils_try_match_arg(argv[i], "-formula-path=", &formula_path);
        trusted_utils_try_match_arg(argv[i], "-proofs-path=", &proofs_path);
        trusted_utils_try_match_num(argv[i], "-num-solvers=", &num_solvers);
        trusted_utils_try_match_num(argv[i], "-solver-id=", &solver_id);
        trusted_utils_try_match_num(argv[i], "-read-buffer-KB=", &read_buffer_KB);
        trusted_utils_try_match_num(argv[i], "-redistribution-strategy=", &redistribution_strategy);
    }

    char output_path[512];
    snprintf(output_path, 512, "-formula-path=%s -proofs-path=%s num-solvers=%lu -solver-id=%lu -redistribution-strategy=%lu",
    formula_path, proofs_path, num_solvers, solver_id, redistribution_strategy);
    //plrat_utils_log(output_path);
    u64 read_buffer_size = read_buffer_KB * 1024; // convert to bytes
    plrat_finder_init(proofs_path, solver_id, num_solvers, redistribution_strategy, read_buffer_size);
    plrat_finder_run();
    plrat_finder_end();

    return 0;
}
