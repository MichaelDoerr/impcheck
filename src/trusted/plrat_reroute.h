
#pragma once

#include <stdbool.h>

void plrat_reroute_init(const char* main_path, unsigned long solver_id, unsigned long num_solvers, unsigned long redistribution_strategy);
void plrat_reroute_log(unsigned long id, const int* literals, int nb_literals);
void plrat_reroute_end();
