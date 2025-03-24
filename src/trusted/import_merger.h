#pragma once

#include <stdbool.h>
#include <stdlib.h>
#include "checker_interface.h"
#include "plrat_utils.h"
#include "int_vec.h"


void import_merger_init(int count_input_files, char** file_paths, u64* current_id, int** current_literals_data, u64* current_literals_size, u64 read_buffer_size);
void import_merger_next();
void import_merger_end();
