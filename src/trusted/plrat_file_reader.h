#include <string.h>
#include <stdio.h>
#include <sys/stat.h>
#include <stdlib.h>
#include "trusted_utils.h"

#define TYPE u8
#define TYPED(THING) u8_ ## THING
#include "vec.h"
#undef TYPED
#undef TYPE

struct plrat_reader{
    /* data */
    int local_rank;
    char* read_buffer;
    struct u8_vec fragment_buffer;
    u64 buffer_size;
    char* pos;
    char* end;
    u64 remaining_bytes; // starts as filesize
    FILE* buffered_file;
};

void fill_buffer(struct plrat_reader* reader);

struct plrat_reader* plrat_reader_init(u64 buffer_size_bytes, FILE* file, int local_rank);
bool plrat_reader_check_bounds(u64 nb_bytes, struct plrat_reader* reader);

int plrat_reader_read_int(struct plrat_reader* reader);
void plrat_reader_read_ints(int* data, u64 nb_ints, struct plrat_reader* reader);
u64  plrat_reader_read_ul(struct plrat_reader* reader);
void plrat_reader_read_uls(u64* data, u64 nb_uls, struct plrat_reader* reader);
char plrat_reader_read_char(struct plrat_reader* reader);
void plrat_reader_skip_bytes(u64 nb_bytes, struct plrat_reader* reader);
void plrat_reader_end(struct plrat_reader* reader);