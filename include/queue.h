#include <stdlib.h>
#include <stdio.h>
#include <threads.h>
#include <stddef.h>

#include "mr.h"

typedef struct queue queue_t;

queue_t *queue_create(mr_t mr, size_t capacity);

void queue_destroy(mr_t mr, queue_t *q);

void queue_push(mr_t mr, queue_t *q, void *item);

void *queue_pop(mr_t mr, queue_t *q);

void queue_close(mr_t mr, queue_t *q);