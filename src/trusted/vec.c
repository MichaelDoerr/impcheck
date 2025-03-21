
#include <stdlib.h>
#include "trusted_utils.h"

#include "vec.h"

struct TYPED(vec)* TYPED(vec_init)(u64 capacity) {
    struct TYPED(vec)* vector = trusted_utils_malloc(sizeof(struct TYPED(vec)));
    vector->size = 0;
    vector->capacity = capacity;
    vector->data = trusted_utils_calloc(capacity, sizeof(TYPE));
    return vector;
}

void TYPED(vec_free)(struct TYPED(vec)* vec) {
    free(vec->data);
    free(vec);
}

void TYPED(vec_reserve)(struct TYPED(vec)* vec, u64 new_cap) {
    if (new_cap > vec->capacity) {
        vec->data = (TYPE*) trusted_utils_realloc(vec->data, new_cap * sizeof(TYPE));
        vec->capacity = new_cap;
    }
    //if (vec->size > new_cap) vec->size = new_cap; // shrink // <- this can only cause to trigger unnecessary reallocs later
}

void TYPED(vec_resize)(struct TYPED(vec)* vec, u64 new_size) {
    TYPED(vec_reserve)(vec, new_size);
    vec->size = new_size; // grow
}

void TYPED(vec_push)(struct TYPED(vec)* vec, TYPE elem) {
    u64 current_cap = vec->capacity;
    if (vec->size == current_cap) {
        u64 new_cap = current_cap*1.3;
        if (new_cap < current_cap+1) new_cap = current_cap+1;
        TYPED(vec_reserve)(vec, new_cap);
    }
    vec->data[vec->size++] = elem;
}

void TYPED(vec_clear)(struct TYPED(vec)* vec) {
    vec->size = 0;
    vec->capacity = 1;
    vec->data = trusted_utils_realloc(vec->data, sizeof(TYPE));
}
