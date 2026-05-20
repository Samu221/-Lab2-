#include "queue.h"

struct queue {
    void **buffer;

    size_t head;
    size_t tail;
    size_t count;
    size_t capacity;

    int closed;

    mtx_t mtx;
    cnd_t not_empty;
    cnd_t not_full;
};

queue_t *queue_create(size_t capacity) {
    queue_t *q = malloc(sizeof(queue_t));
    if (q == NULL) {
        return NULL;
    }

    q->buffer = malloc(capacity * sizeof(void *));
    if (q->buffer == NULL) {
        free(q);
        return NULL;
    }

    q->head = 0;
    q->tail = 0;
    q->count = 0;
    q->capacity = capacity;
    q->closed = 0;

    mtx_init(&q->mtx, mtx_plain);
    cnd_init(&q->not_empty);
    cnd_init(&q->not_full);

    return q;
};

void queue_destroy(queue_t *q) {
    if (q == NULL) {
        return;
    }

    free(q->buffer);
    mtx_destroy(&q->mtx);
    cnd_destroy(&q->not_empty);
    cnd_destroy(&q->not_full);
    free(q);
};

void queue_push(queue_t *q, void *item) {
    mtx_lock(&q->mtx);
    while (q->count == q->capacity && !q->closed) {
        cnd_wait(&q->not_full, &q->mtx);
    }

    if (q->closed) {
        mtx_unlock(&q->mtx);
        return;
    }
    
    q->buffer[q->tail] = item;
    q->tail = (q->tail + 1) % q->capacity;
    q->count++;

    cnd_signal(&q->not_empty);
    mtx_unlock(&q->mtx);
};

void *queue_pop(queue_t *q) {
    mtx_lock(&q->mtx);

    while (q->count == 0 && !q->closed) {
        cnd_wait(&q->not_empty, &q->mtx);
    }

    if (q->count == 0 && q->closed) {
        mtx_unlock(&q->mtx);
        return NULL;
    }

    void *item = q->buffer[q->head];
    q->head = (q->head + 1) % q->capacity;
    q->count--;

    cnd_signal(&q->not_full);

    mtx_unlock(&q->mtx);
    return item;
};

void queue_close(queue_t *q) {
    mtx_lock(&q->mtx);
    q->closed = 1;

    cnd_broadcast(&q->not_empty);
    
    mtx_unlock(&q->mtx);
};