
#pragma once

#include <stdbool.h>

void plrat_importer_init(const char* main_path, unsigned long num_solvers, unsigned long redistribution_strategy);
int plrat_importer_log(unsigned long id, const int* literals, int nb_literals);
void plrat_importer_end();
