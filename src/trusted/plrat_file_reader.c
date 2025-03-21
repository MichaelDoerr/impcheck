#include "plrat_file_reader.h"

void fill_buffer(struct plrat_reader* reader) {
    u64 read_size;
    if (MALLOB_UNLIKELY(reader->remaining_bytes < reader->buffer_size)) {
        read_size = reader->remaining_bytes;
    } else {
        read_size = reader->buffer_size;
    }
    u64 objs_read = UNLOCKED_IO(fread)(reader->read_buffer, read_size, 1, reader->buffered_file);
    if (MALLOB_UNLIKELY(objs_read == 0)) {
        printf("ERROR\n");
    }
    reader->pos = reader->read_buffer;
    reader->end = reader->pos + read_size;
    reader->remaining_bytes -= read_size;
    //if (reader->local_rank == 0) {
    //    printf("read %lu bytes\n", read_size);
    //    printf("remaining bytes: %lu\n", reader->remaining_bytes);
    //    printf("buffer size: %lu\n", reader->buffer_size);
    //    printf("pos: %p\n", reader->pos);
    //    printf("end: %p\n", reader->end);
    //    printf("read buffer: %p\n", reader->read_buffer);
    //}
}

struct plrat_reader* plrat_reader_init(u64 buffer_size_bytes, FILE* file, int local_rank) {
    struct plrat_reader* reader = (struct plrat_reader*)trusted_utils_malloc(sizeof(struct plrat_reader));

    struct stat st;
    int fd = fileno(file);
    fstat(fd, &st);
    reader->remaining_bytes = st.st_size;

    reader->local_rank = local_rank;
    reader->buffered_file = file;
    reader->buffer_size = buffer_size_bytes;
    reader->read_buffer = (char*)trusted_utils_malloc(buffer_size_bytes);
    reader->pos = reader->read_buffer;
    reader->end = reader->pos;
    reader->fragment_buffer = *u8_vec_init(1);

    fill_buffer(reader);

    return reader;
}

//void plrat_read_to_buf(void* data, size_t size, size_t nb_objs, FILE* file) {
//    u64 buffer_end = UNLOCKED_IO(fread)(data, size, nb_objs, file);
//}

bool plrat_reader_check_bounds(u64 nb_bytes, struct plrat_reader* reader){
    long bytes_till_end = reader->end - reader->pos;
    if (MALLOB_UNLIKELY(bytes_till_end == 0)){
        fill_buffer(reader);
        return true;
    }
    if((long)nb_bytes > bytes_till_end){
        u8_vec_resize(&reader->fragment_buffer, nb_bytes);
        memcpy(reader->fragment_buffer.data, reader->pos, bytes_till_end); // put first part of the data in the fragment buffer
        
        fill_buffer(reader);
        memcpy(reader->fragment_buffer.data + bytes_till_end, reader->pos, nb_bytes - bytes_till_end); // put the rest of the data in the fragment buffer
        reader->pos += nb_bytes - bytes_till_end;

        return false;
    }
    return true;
}


int plrat_reader_read_int(struct plrat_reader* reader){
    int res;
    if (plrat_reader_check_bounds(sizeof(int), reader)){
        res = *((int*)(reader->pos));
        reader->pos += sizeof(int);
    } else {
        res = *((int*)(reader->fragment_buffer.data));
    }
    return res;
}

void plrat_reader_read_ints(int* data, u64 nb_ints, struct plrat_reader* reader){
    if (plrat_reader_check_bounds(nb_ints * sizeof(int), reader)){
        memcpy(data, reader->pos, nb_ints * sizeof(int));
        reader->pos += nb_ints * sizeof(int);
    } else {
        memcpy(data, reader->fragment_buffer.data, nb_ints * sizeof(int));
    }
}


u64  plrat_reader_read_ul(struct plrat_reader* reader){
    u64 res;
    if (plrat_reader_check_bounds(sizeof(u64), reader)){
        res = *((u64*)(reader->pos));
        reader->pos += sizeof(u64);
    } else {
        res = *((u64*)(reader->fragment_buffer.data));
    }
    return res;
}
void plrat_reader_read_uls(u64* data, u64 nb_uls, struct plrat_reader* reader){
    if (plrat_reader_check_bounds(nb_uls * sizeof(u64), reader)){
        memcpy(data, reader->pos, nb_uls * sizeof(u64));
        reader->pos += nb_uls * sizeof(u64);
    } else {
        memcpy(data, reader->fragment_buffer.data, nb_uls * sizeof(u64));
    }
}

char plrat_reader_read_char(struct plrat_reader* reader){
    char res;
    plrat_reader_check_bounds(sizeof(char), reader); // makes shure that the char is in the buffer
    res = *(reader->pos);
    reader->pos += 1;
    
    return res;
}


void plrat_reader_skip_bytes(u64 nb_bytes, struct plrat_reader* reader){
    reader->pos += nb_bytes;
    long bytes_till_end = reader->end - reader->pos;
    //if(reader->local_rank == 0){
    //    printf("pos: %p\n", reader->pos);
    //    printf("end: %p\n", reader->end);
    //    printf("bytes till end: %lu\n", bytes_till_end);
    //    printf("skipped %lu bytes\n", nb_bytes);
    //}
    if (MALLOB_UNLIKELY(bytes_till_end <= (long)0)){ // reader has reached the end of the buffer
        fill_buffer(reader);
        reader->pos += -bytes_till_end; // move the reader to the right position
        
        //if(reader->local_rank == 0){
        //    printf("new pos: %p\n", reader->pos);
        //}
    }
}

void plrat_reader_end(struct plrat_reader* reader){
    //plrat_utils_free(read_buffer);
    fclose(reader->buffered_file);
    free(reader->fragment_buffer.data);
    free(reader->read_buffer);
    free(reader);
}