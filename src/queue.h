#include <stdlib.h>
#include <stdio.h>
#include <threads.h>
#include <stddef.h>

typedef struct queue queue_t;

queue_t *queue_create(size_t capacity);

void queue_destroy(queue_t *q);

void queue_push(queue_t *q, void *item);

void *queue_pop(queue_t *q);

void queue_close(queue_t *q);