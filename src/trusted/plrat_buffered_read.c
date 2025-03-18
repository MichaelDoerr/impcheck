#include <string.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/mman.h>
#include "plrat_buffered_read.h"
#include "plrat_utils.h"

char* read_buffer;
u64 buffer_size;
u64 buffer_pos;
u64 remaining_bytes; // starts as filesize
FILE* buffered_file;



void fill_buffer() {
    u64 read_size;
    if (MALLOB_UNLIKELY(remaining_bytes < buffer_size)) {
        read_size = remaining_bytes;
    } else {
        read_size = buffer_size;
    }
    u64 bytes_read = UNLOCKED_IO(fread)(read_buffer, read_size, 1, buffered_file);
    if (bytes_read == 0) {
        printf("ERROR\n");
    }
    buffer_pos = 0;
    remaining_bytes -= bytes_read;
}

void plrat_reader_init(u64 buffer_size_bytes, FILE* file){
    struct stat st;
    int fd = fileno(file);
    fstat(fd, &st);
    remaining_bytes = st.st_size;
    buffered_file = file;
    //read_buffer = plrat_utils_malloc(buffer_size_bytes);
    read_buffer = mmap(NULL, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (read_buffer == MAP_FAILED) {
        printf("Mapping Failed %lu %i\n", remaining_bytes, fd);
      }
    buffer_size = buffer_size_bytes;
    buffer_pos = 0;
}

//void plrat_read_to_buf(void* data, size_t size, size_t nb_objs, FILE* file) {
//    u64 buffer_end = UNLOCKED_IO(fread)(data, size, nb_objs, file);
//}


int plrat_reader_read_int(){
    int res;
    res = *((int*)(read_buffer + buffer_pos));
    buffer_pos += sizeof(int);
    return res;
}

void plrat_reader_read_ints(int* data, u64 nb_ints){
    memcpy(data, read_buffer + buffer_pos, nb_ints * sizeof(int));
    buffer_pos += nb_ints * sizeof(int);
}


u64  plrat_reader_read_ul(){
    u64 res;
    res = *((u64*)(read_buffer + buffer_pos));
    buffer_pos += sizeof(u64);
    return res;
}
void plrat_reader_read_uls(u64* data, u64 nb_uls){
    memcpy(data, read_buffer + buffer_pos, nb_uls * sizeof(u64));
    buffer_pos += nb_uls * sizeof(u64);
}

char plrat_reader_read_char(){
    char res;
    res = *(read_buffer + buffer_pos);
    buffer_pos += 1;
    return res;
}


void plrat_reader_skip_bytes(u64 nb_bytes){
    buffer_pos += nb_bytes;
}

void plrat_utils_exit_eof() {
    plrat_utils_log("end-of-file - terminating");
    exit(0);
}


void plrat_reader_end(){
    //plrat_utils_free(read_buffer);
    munmap(read_buffer, remaining_bytes);
    fclose(buffered_file);
}