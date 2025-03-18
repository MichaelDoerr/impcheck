#pragma once

#include <stdbool.h>   // for bool
#include <stdio.h>     // for FILE
#include <features.h>  // for _DEFAULT_SOURCE
#include <stdbool.h>   // for bool
#include <stdio.h>     // for FILE
#include "trusted_utils.h"

void plrat_reader_init(u64 buffer_size_bytes, FILE* file);
void plrat_reader_read(u64 id, const int* literals, int nb_literals);
char plrat_reader_read_char();
int  plrat_reader_read_int();
void plrat_reader_read_ints(int* data, u64 nb_ints);
u64  plrat_reader_read_ul();
void plrat_reader_read_uls(u64* data, u64 nb_uls);
void plrat_reader_read_sig(u8* out_sig);
void plrat_reader_skip_bytes(u64 nb_bytes);

void plrat_reader_end();