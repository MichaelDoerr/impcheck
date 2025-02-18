
#pragma once

#include <stdbool.h>

void tc_init(const char* fifo_in, const char* fifo_out, unsigned long num_solvers, unsigned long global_solver_id);
void tc_end();
int tc_run(bool check_model, bool lenient);
