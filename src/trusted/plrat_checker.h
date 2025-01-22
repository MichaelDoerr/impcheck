
#pragma once

#include <stdbool.h>

void pc_init(const char* formula_path, const char* proof_path, unsigned long num_solvers);
void pc_end();
int pc_run();
