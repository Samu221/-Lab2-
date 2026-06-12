#include "reducer.h"
#include "len_limits.h"

static int emit_result(const char *token, const void *result, size_t result_size, void *emit_arg ){

    if (token == NULL || emit_arg == NULL) {
        errno = EINVAL;
        return -1;
    }

    
    reducer_worker_ctx_t *ctx = emit_arg;

    size_t token_len = strlen(token);
    size_t result_len = result_size;

    mtx_lock(ctx->out_mtx);
    //scrivo sulla pipe lunghezza del token, lunghezza del risultato, token e risultato
    if (writen(STDOUT_FILENO, &token_len, sizeof(size_t)) == -1) {
        mtx_unlock(ctx->out_mtx);
        return -1;
    }

    if (writen(STDOUT_FILENO, &result_len, sizeof(size_t)) == -1) {
        mtx_unlock(ctx->out_mtx);
        return -1;
    }

    if (writen(STDOUT_FILENO, token, token_len) == -1) {
        mtx_unlock(ctx->out_mtx);
        return -1;
    }

    if (result_len > 0) {
        if (writen(STDOUT_FILENO, result, result_len) == -1) {
            mtx_unlock(ctx->out_mtx);
            return -1;
        }
    }
    mtx_unlock(ctx->out_mtx);

    return 0;
}

static int reader_thread(void *arg) {
    reducer_reader_ctx_t *ctx=arg;
    mr_t mr=ctx->mr;
    log_event(mr, "reducer.c", "reader_thread", "reducer reader thread iniziato");

    queue_t *q = ctx->q;
    size_t groups_capacity = 16;
    size_t groups_count = 0;

    //array di dimensione 16 di tipo reduce_group_t che uso per raggruppare insieme risultati con stesso token
    reduce_group_t **groups = malloc(sizeof(reduce_group_t*) * groups_capacity);
    if (!groups) return -1;

    unsigned long token_unici=0;
    while (1) {

        size_t token_len;
        size_t value_len;

        if (readn(STDIN_FILENO, &token_len, sizeof(size_t)) <= 0){
            break;
        }

        if(token_len == 0 || token_len > MR_MAX_TOKEN_LEN){
            errno = EINVAL;
            return -1;
        }

        if (readn(STDIN_FILENO, &value_len, sizeof(size_t)) <= 0){
            break;
        }

        if(value_len == 0 || value_len > MR_MAX_VALUE_LEN){
            errno = EINVAL;
            return -1;
        }

        char *token = malloc(token_len + 1);

        if (!token)
            return -1;

        if (readn(STDIN_FILENO, token, token_len) <= 0) {
            free(token);
            break;
        }

        token[token_len] = '\0';

        
        void *value = malloc(value_len);

        if (!value) {
            free(token);
            return -1;
        }

        if (readn(STDIN_FILENO, value, value_len) <= 0) {
            free(token);
            free(value);
            break;
        }

        int trovato=0;

        
        //dopo aver letto <token, processed_token> raggruppo le coppie con lo stesso token
        for(size_t i=0; i<groups_count; i++){ 
            if (strcmp(groups[i]->token, token) == 0) {
                trovato=1;
                //se lo trovo aggiorno i valori
                if (groups[i]->count == groups[i]->capacity) {
                    size_t new_capacity = groups[i]->capacity * 2;
                    mr_value_t *new_values = realloc(groups[i]->values, sizeof(mr_value_t) * new_capacity);
                    if (!new_values) {
                        free(token);
                        free(value);
                        return -1;
                    }
                    groups[i]->values = new_values;
                    groups[i]->capacity = new_capacity;
                }
                groups[i]->values[groups[i]->count].data = value;
                groups[i]->values[groups[i]->count].size = value_len;
                groups[i]->count++;
                free(token);
                break;
            }
        }

        //se non lo trovo, lo aggiungo all'array
        if(!trovato){
            if (groups_count == groups_capacity) {
                size_t new_capacity = groups_capacity * 2;
                reduce_group_t **new_groups = realloc(groups, sizeof(reduce_group_t*) * new_capacity);
                if (!new_groups) {
                    free(token);
                    free(value);
                    return -1;
                }
                groups = new_groups;
                groups_capacity = new_capacity;
            }

            reduce_group_t *group = malloc(sizeof(reduce_group_t));
            if (!group) {
                free(token);
                free(value);
                return -1;
            }

            group->token = token;
            group->capacity = 16;
            group->values = malloc(sizeof(mr_value_t) * group->capacity);
            if (!group->values) {
                free(token);
                free(value);
                free(group);
                return -1;
            }
            group->count = 1;
            group->values[0].data = value;
            group->values[0].size = value_len;

            groups[groups_count++] = group;
        }
        token_unici++;

        
        /*char msg[128];
        snprintf(msg, sizeof(msg),
                "reducer reader thread ha letto il %luesimo token", token_unici);
        log_event(mr, "reducer.c", "reader_thread", msg);*/

    }

    for(size_t i=0; i<groups_count; i++){ 
        queue_push(mr, q, groups[i]);
    }

    free(groups);
    // segnala ai worker che non arriveranno più gruppi
    queue_close(mr, q);

    char msg[128];
    snprintf(msg, sizeof(msg),
            "reducer reader thread finito, ha letto: %lu token totali, raggruppati in %zu token distinti",
            token_unici, groups_count);
    log_event(mr, "reducer.c", "reader_thread", msg);

    return 0;
}

static int worker_thread(void *arg) {
    
    reducer_worker_ctx_t *ctx = arg;

    mr_t mr= ctx->mr;
    log_event(mr, "reducer.c", "worker_thread", "reducer worker thread creato");


    unsigned long gruppi_elaborati=0;
    while (1) {

        //estraggo dalla coda un gruppo elaborato
        reduce_group_t *group = queue_pop(mr, ctx->q);

        if (!group)
            break;

        //chiamo la funzione reducer sul gruppo
        mr_get_reducer(mr)(
            group->token,
            group->values,
            group->count,
            emit_result,
            ctx,
            mr_get_user_arg(mr)
        );

        free(group->token);

        for (size_t i = 0; i < group->count; i++) {
            free((void *)group->values[i].data);
        }

        free(group->values);
        free(group);

        gruppi_elaborati++;
    }

    char msg[128];
    snprintf(msg, sizeof(msg),
            "reducer worker thread finito, ha elaborato: %lu gruppi",
            gruppi_elaborati);
    log_event(mr, "reducer.c", "worker_thread", msg);

    return 0;
}

int reducer_process_main(mr_t mr){
    //mutex per la l'emit
    mtx_t out_mutex;
    mtx_init(&out_mutex, mtx_plain);

    mr_attr_t attr = mr_get_attr(mr);

    queue_t *q = queue_create(mr, attr.queue_size);
    if (!q)
        return -1;

    thrd_t reader_tid;
    reducer_reader_ctx_t reader_ctx;
    reader_ctx.mr=mr;
    reader_ctx.q=q;

    //creo il thread reader
    if (thrd_create(&reader_tid, reader_thread, &reader_ctx) != thrd_success) {
        queue_destroy(mr, q);
        return -1;
    }


    thrd_t *worker_tids = malloc(sizeof(thrd_t) * attr.reducer_threads);

    if (!worker_tids) {
        queue_close(mr, q);
        queue_destroy(mr, q);
        return -1;
    }

    for (size_t i = 0; i < attr.reducer_threads; i++) {

        reducer_worker_ctx_t *ctx = malloc(sizeof(reducer_worker_ctx_t));

        if (!ctx) {

            queue_close(mr, q);
            thrd_join(reader_tid, NULL);

            for (size_t j = 0; j < i; j++) {
                thrd_join(worker_tids[j], NULL);
            }

            free(worker_tids);
            queue_destroy(mr, q);

            return -1;
        }

        ctx->q = q;
        ctx->mr = mr;
        ctx->out_mtx = &out_mutex;

        //creo i worker 
        if (thrd_create(&worker_tids[i], worker_thread, ctx ) != thrd_success) {

            free(ctx);

            queue_close(mr, q);
            thrd_join(reader_tid, NULL);

            for (size_t j = 0; j < i; j++) {
                thrd_join(worker_tids[j], NULL);
            }

            free(worker_tids);
            queue_destroy(mr, q);

            return -1;
        }
    }


    thrd_join(reader_tid, NULL);

    for (size_t i = 0; i < attr.reducer_threads; i++) {
        thrd_join(worker_tids[i], NULL);
    }

    free(worker_tids);

    queue_destroy(mr, q);

    mtx_destroy(&out_mutex);

    return 0;
}