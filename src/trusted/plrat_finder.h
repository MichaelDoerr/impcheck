
#pragma once

#include <stdbool.h>

void plrat_finder_init(const char* main_path, unsigned long solver_id, unsigned long num_solvers, unsigned long redistribution_strategy);
void plrat_finder_run();
void plrat_finder_end();
