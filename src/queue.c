#include "queue.h"

struct queue {
    void **buffer;    // Buffer che contiene puntatori generici agli elementi della coda

    size_t head;    // Indice del primo elemento da leggere

    size_t tail;    // Indice della prossima posizione libera dove inserire

    size_t count;    // Numero attuale di elementi presenti nella coda

    size_t capacity;    // Capacità massima della coda

    int closed;    // Flag di chiusura

    mtx_t mtx;    // Mutex per proteggere accessi concorrenti alla coda

    cnd_t not_empty;    // Variabile di condizione usata da chi consuma per aspettare dati

    cnd_t not_full;    // Variabile di condizione usata da chi produce per aspettare spazio libero
};

queue_t *queue_create(mr_t mr, size_t capacity) {

    // Alloca la struttura queue
    queue_t *q = malloc(sizeof(queue_t));
    if (q == NULL) {
        return NULL;
    }

    // Alloca il buffer della coda
    q->buffer = malloc(capacity * sizeof(void *));
    if (q->buffer == NULL) {
        free(q);
        return NULL;
    }

    // Inizializzazione indici e stato iniziale
    q->head = 0;
    q->tail = 0;
    q->count = 0;
    q->capacity = capacity;

    // La coda inizialmente è aperta
    q->closed = 0;

    // Inizializza mutex
    mtx_init(&q->mtx, mtx_plain);

    // Inizializza condition variable
    cnd_init(&q->not_empty);
    cnd_init(&q->not_full);
    
    (void)mr;
    //log_event(mr, "queue.c", "queue_create", "coda creata");

    return q;
};

void queue_destroy(mr_t mr, queue_t *q) {

    // Se la coda è NULL non faccio nulla
    if (q == NULL) {
        return;
    }

    // Libera buffer
    free(q->buffer);

    // Distrugge mutex
    mtx_destroy(&q->mtx);

    // Distrugge condition variables
    cnd_destroy(&q->not_empty);
    cnd_destroy(&q->not_full);

    // Libera struttura queue
    free(q);

    (void)mr;
    //log_event(mr, "queue.c", "queue_destroy", "coda distrutta");
};

void queue_push(mr_t mr, queue_t *q, void *item) {

    // Acquisisce il lock sulla coda
    mtx_lock(&q->mtx);

    // Se la coda è piena e non è chiusa,
    // il thread produttore aspetta spazio libero
    while (q->count == q->capacity && !q->closed) {

        (void)mr;
        //log_event(mr, "queue.c", "queue_push", "coda piena, attesa per spazio disponibile");

        cnd_wait(&q->not_full, &q->mtx);
    }

    // Se la coda è stata chiusa,
    // esce senza inserire nulla
    if (q->closed) {
        mtx_unlock(&q->mtx);
        return;
    }

    // Inserisce elemento nella posizione tail
    q->buffer[q->tail] = item;

    // Aggiorna tail in modo circolare
    q->tail = (q->tail + 1) % q->capacity;

    // Incrementa numero elementi
    q->count++;

    (void)mr;
    //log_event(mr, "queue.c", "queue_push", "elemento inserito nella coda");

    // Sveglia eventuali thread consumatori
    cnd_signal(&q->not_empty);

    // Rilascia lock
    mtx_unlock(&q->mtx);
};

void *queue_pop(mr_t mr, queue_t *q) {

    // Acquisisce lock
    mtx_lock(&q->mtx);

    // Se la coda è vuota ma non chiusa,
    // il consumatore aspetta nuovi elementi
    while (q->count == 0 && !q->closed) {

        (void)mr;
        //log_event(mr, "queue.c", "queue_pop", "coda vuota, attesa per elementi disponibili");

        cnd_wait(&q->not_empty, &q->mtx);
    }

    // Se la coda è vuota e chiusa,
    // non arriveranno più elementi
    if (q->count == 0 && q->closed) {
        mtx_unlock(&q->mtx);
        return NULL;
    }

    // Estrae elemento dalla testa della coda
    void *item = q->buffer[q->head];

    // Aggiorna head in modo circolare
    q->head = (q->head + 1) % q->capacity;

    // Decrementa numero elementi
    q->count--;

    (void)mr;
    //log_event(mr, "queue.c", "queue_pop", "elemento estratto dalla coda");

    // Sveglia eventuali produttori in attesa di spazio
    cnd_signal(&q->not_full);

    // Rilascia lock
    mtx_unlock(&q->mtx);

    return item;
};

void queue_close(mr_t mr, queue_t *q) {

    // Acquisisce lock
    mtx_lock(&q->mtx);

    // Segna la coda come chiusa
    q->closed = 1;

    (void)mr;
    //log_event(mr, "queue.c", "queue_close", "chiusura coda");

    // Sveglia tutti i thread bloccati su not_empty
    // così possono uscire
    cnd_broadcast(&q->not_empty);

    // Rilascia lock
    mtx_unlock(&q->mtx);
};