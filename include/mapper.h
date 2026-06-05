#ifndef MAPPER_H
#define MAPPER_H

#include "../include/mr.h"
#include "queue.h"
#include "config.h"
#include "io.h"
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include "mr.h"

int mapper_process_main(mr_t mr);

typedef struct {
    queue_t *q;
    mr_t mr;
    mtx_t *out_mtx;
} worker_ctx_t;

typedef struct {
    queue_t *q;
    mr_t mr;
} reader_ctx_t;

#endif