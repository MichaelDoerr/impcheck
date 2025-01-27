
#pragma once

#include <stdbool.h>

void pc_init(const char* formula_path, const char* proof_path, unsigned long solver_id, unsigned long num_solvers, unsigned long redistribution_strategy);
void pc_end();
int pc_run();
