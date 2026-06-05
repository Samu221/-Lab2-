#ifndef REDUCER_H
#define REDUCER_H

#include "mr.h"
#include "queue.h"
#include "config.h"
#include "io.h"
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>

typedef struct {
    queue_t *q;
    mr_t mr;
    mtx_t *out_mtx;
} reducer_worker_ctx_t;

typedef struct {
    queue_t *q;
    mr_t mr;
} reducer_reader_ctx_t;

typedef struct {

    char *token;

    mr_value_t *values;

    size_t count;
    size_t capacity;

} reduce_group_t;

int reducer_process_main(mr_t mr);

#endif