
#pragma once

#include <stdbool.h>

void plrat_reroute_init(const char* main_path, unsigned long solver_id, unsigned long num_solvers, unsigned long redistribution_strategy, unsigned long read_buffer_size);
void plrat_reroute_run();
void plrat_reroute_end();
