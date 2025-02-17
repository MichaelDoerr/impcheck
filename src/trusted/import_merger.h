#pragma once

#include <stdbool.h>
#include <stdlib.h>
#include "checker_interface.h"
#include "plrat_utils.h"
#include "int_vec.h"


void import_merger_init(unsigned long solver_id, int count_input_files, char** file_paths, u64* current_id, struct int_vec* current_literals);
void import_merger_next();
void import_merger_end();
