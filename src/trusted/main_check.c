
#include <stdbool.h>          // for bool, false
#include <stdio.h>            // for fflush, stdout
#include "trusted_checker.h"  // for tc_init, tc_run
#include "trusted_utils.h"    // for trusted_utils_try_match_arg, trusted_ut...
#if IMPCHECK_WRITE_DIRECTIVES
#include <unistd.h>
#include "../writer.h"
#endif

int main(int argc, char *argv[]) {

    const char *fifo_directives = "", *fifo_feedback = "", *dir_path = ".";
    bool check_model = false, lenient = false;
    u64 num_solvers = 0, solver_id = 0;
    for (int i = 1; i < argc; i++) {
        trusted_utils_try_match_arg(argv[i], "-fifo-directives=", &fifo_directives);
        trusted_utils_try_match_arg(argv[i], "-fifo-feedback=", &fifo_feedback);
        trusted_utils_try_match_arg(argv[i], "-output-path=", &dir_path);
        trusted_utils_try_match_num(argv[i], "-num-solvers=", &num_solvers);
        trusted_utils_try_match_num(argv[i], "-solver-id=", &solver_id);
        trusted_utils_try_match_flag(argv[i], "-check-model", &check_model);
        trusted_utils_try_match_flag(argv[i], "-lenient", &lenient);
    }

#if IMPCHECK_WRITE_DIRECTIVES
    char output_path[512];
    #if IMPCHECK_PLRAT
        snprintf(output_path, 512, "%s/%lu/out.plrat", dir_path, solver_id);
        trusted_utils_log(output_path);
    #else
        snprintf(output_path, 512, "directives.%i.impcheck", getpid());
    #endif
    writer_init(output_path);
#endif

    tc_init(fifo_directives, fifo_feedback, num_solvers, solver_id);
    int res = tc_run(check_model, lenient);
    fflush(stdout);
    return res;
}
