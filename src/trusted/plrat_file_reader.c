#include "plrat_file_reader.h"

void fill_buffer(struct plrat_reader* reader) {
    u64 read_size;
    
    //printf("FILL BUFFER reader->remaining_bytes: %lu\n", reader->remaining_bytes);
    if (MALLOB_UNLIKELY(reader->remaining_bytes < reader->buffer_size)) {
        read_size = reader->remaining_bytes;
    } else {
        read_size = reader->buffer_size;
    }
    if (MALLOB_UNLIKELY(read_size == 0)) {
        //printf("nothing to read\n");
        //printf("buffer_size: %lu\n", reader->buffer_size);
        //printf("remaining bytes: %lu\n", reader->remaining_bytes);
        //printf("read size: %lu\n", read_size);
        return;
    }
    u64 objs_read = UNLOCKED_IO(fread)(reader->read_buffer, read_size, 1, reader->buffered_file);
    if (MALLOB_UNLIKELY(objs_read == 0)) {
        printf("ERROR\n");
        printf("buffer_size: %lu\n", reader->buffer_size);
        printf("remaining bytes: %lu\n", reader->remaining_bytes);
        printf("read size: %lu\n", read_size);
    }
    reader->pos = reader->read_buffer;
    reader->end = reader->pos + read_size;
    reader->remaining_bytes -= read_size;
    reader->actual_buffer_size = read_size;
    //printf("read size: %lu\n", read_size);
    //printf("Left in File After read: %lu\n", reader->remaining_bytes);

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
    reader->total_bytes = st.st_size;
    reader->remaining_bytes = st.st_size;

    reader->local_rank = local_rank;
    reader->buffered_file = file;
    reader->buffer_size = buffer_size_bytes;
    reader->read_buffer = (char*)trusted_utils_malloc(buffer_size_bytes);
    reader->actual_buffer_size = 0;
    reader->pos = reader->read_buffer;
    reader->end = reader->pos;
    reader->fragment_buffer.size = 0;
    reader->fragment_buffer.capacity = 1;
    reader->fragment_buffer.data = trusted_utils_calloc(1, sizeof(u8));

    //printf("buffer size: %lu\n", reader->buffer_size);
    //printf("remaining bytes: %lu\n", reader->remaining_bytes);
    //printf("read buffer: %p\n", reader->read_buffer);
    //printf("pos: %p\n", reader->pos);
    //printf("fragment buffer: %p\n", reader->fragment_buffer.data);


    fill_buffer(reader);
    //long bytes_till_end = reader->end - reader->pos;
    //printf("bytes till end: %ld\n", bytes_till_end);

    return reader;
}

void plrat_reader_seek(u64 byte_pos, struct plrat_reader* reader) {
    long searched_byte = (long)byte_pos;
    if (searched_byte > reader->total_bytes){
        printf("Error: Seeking beyond file size.\n");
        return;
    }
    long loaded_end = reader->total_bytes - reader->remaining_bytes;
    long loaded_start = loaded_end - reader->actual_buffer_size;

    if (loaded_start <= searched_byte && searched_byte < loaded_end){ // seeked position is loaded in buffer
        reader->pos = reader->read_buffer + (searched_byte - loaded_start);
    }
    else if (loaded_end <= searched_byte){ // seeked position comes later
        long current_pos = (reader->pos - reader->read_buffer) + loaded_start;
        plrat_reader_skip_bytes(searched_byte - current_pos, reader);
    }
    else{ // seeked position comes earlier
        long offset_front = reader->buffer_size/10L;
        long set_position = max(searched_byte - offset_front, 0L);
        reader->remaining_bytes = reader->total_bytes - set_position;
        fseek(reader->buffered_file, set_position, SEEK_SET);
        fill_buffer(reader);
        reader->pos += searched_byte - set_position;
    }
}

void plrat_reader_end(struct plrat_reader* reader){
    fclose(reader->buffered_file);
    free(reader->fragment_buffer.data);
    free(reader->read_buffer);
    free(reader);
}

bool plrat_reader_check_bounds(u64 nb_bytes, struct plrat_reader* reader){
    
    long bytes_till_end = reader->end - reader->pos;
    //printf("nb bytes: %lu\n", nb_bytes);
    if (MALLOB_UNLIKELY(bytes_till_end == 0)){
        fill_buffer(reader);
    }
    bytes_till_end = reader->end - reader->pos;
    if(((long)nb_bytes) > bytes_till_end){
        u8_vec_resize(&reader->fragment_buffer, nb_bytes);
        memcpy(reader->fragment_buffer.data, reader->pos, bytes_till_end); // put first part of the data in the fragment buffer
        nb_bytes -= bytes_till_end;
        u8* fragment_pos = reader->fragment_buffer.data + bytes_till_end;
        while ((long)nb_bytes > reader->buffer_size){
            fill_buffer(reader);
            memcpy(fragment_pos, reader->pos, reader->buffer_size);
            fragment_pos += reader->buffer_size;
            nb_bytes -= reader->buffer_size;
        }
        fill_buffer(reader);
        memcpy(fragment_pos, reader->pos, nb_bytes); // put the rest of the data in the fragment buffer
        
        reader->pos += nb_bytes;
        

        return false;
    }
    return true;
}


int plrat_reader_read_int(struct plrat_reader* reader){
    int res;
    //printf("reading int\n");
    if (plrat_reader_check_bounds(sizeof(int), reader)){
        res = *((int*)(reader->pos));
        reader->pos += sizeof(int);
    } else {
        res = *((int*)(reader->fragment_buffer.data));
    }
    return res;
}

void plrat_reader_read_ints(int* data, u64 nb_ints, struct plrat_reader* reader){
    //printf("reading ints\n");
    //printf("%ld ints to read\n", nb_ints);
    if (plrat_reader_check_bounds(nb_ints * sizeof(int), reader)){
        memcpy(data, reader->pos, nb_ints * sizeof(int));  
        reader->pos += nb_ints * sizeof(int);
    } else {
        memcpy(data, reader->fragment_buffer.data, nb_ints * sizeof(int));
    }
}


u64  plrat_reader_read_ul(struct plrat_reader* reader){
    u64 res;
    //printf("reading ul\n");
    if (plrat_reader_check_bounds(sizeof(u64), reader)){
        res = *((u64*)(reader->pos));
        reader->pos += sizeof(u64);
    } else {
        res = *((u64*)(reader->fragment_buffer.data));
    }
    return res;
}
void plrat_reader_read_uls(u64* data, u64 nb_uls, struct plrat_reader* reader){
    //printf("reading uls\n");
    //printf("nb_uls: %lu\n", nb_uls);
    //printf("reader->pos: %p\n", reader->pos);
    if (plrat_reader_check_bounds(nb_uls * sizeof(u64), reader)){
        //printf("data: %p\n", (void*)data);
        //printf("reader->pos: %p\n", (void*)reader->pos);

        memcpy(data, reader->pos, nb_uls * sizeof(u64));
        reader->pos += nb_uls * sizeof(u64);
    } else {
        memcpy(data, reader->fragment_buffer.data, nb_uls * sizeof(u64));
    }
}

char plrat_reader_read_char(struct plrat_reader* reader){
    char res;
    //printf("reading char\n");
    //printf("reader->pos: %p\n", reader->pos);
    plrat_reader_check_bounds(sizeof(char), reader); // makes shure that the char is in the buffer
    res = *(reader->pos);
    reader->pos += 1;
    
    return res;
}


void plrat_reader_skip_bytes(u64 nb_bytes, struct plrat_reader* reader){
    
    long bytes_till_end = reader->end - reader->pos;
    
    //printf("bytes till end: %lu\n", bytes_till_end);
    //printf("skipped %lu bytes\n", nb_bytes);
    
    while (MALLOB_UNLIKELY(bytes_till_end <= (long)nb_bytes)){ // reader has reached the end of the buffer
        //printf("skipping %lu bytes\n", bytes_till_end);
        fill_buffer(reader);
        nb_bytes -= bytes_till_end;
        bytes_till_end = reader->buffer_size;
    }
    reader->pos += nb_bytes; // move the reader to the right position
    
}