#include "reducer.h"

static int emit_result(const char *token, const void *result, size_t result_size, void *emit_arg ){

    if (token == NULL || emit_arg == NULL) {
        errno = EINVAL;
        return -1;
    }

    reducer_worker_ctx_t *ctx = emit_arg;

    size_t token_len = strlen(token);
    size_t result_len = result_size;

    mtx_lock(ctx->out_mtx);
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
    //fprintf(stderr,"reducer.c: Reducer reader process started with PID %d\n", getpid());
    reducer_reader_ctx_t *ctx=arg;
    mr_t mr=ctx->mr;
    log_event(mr, "reducer.c", "reader_thread", "reducer reader thread iniziato");

    queue_t *q = ctx->q;
    size_t groups_capacity = 16;
    size_t groups_count = 0;

    reduce_group_t **groups = malloc(sizeof(reduce_group_t*) * groups_capacity);
    if (!groups) return -1;

    unsigned long token_unici=0;
    while (1) {

        size_t token_len;
        size_t value_len;

        if (readn(STDIN_FILENO, &token_len, sizeof(size_t)) <= 0)
            break;

        //fprintf(stderr, "reducer.c: Reducer reader read token_len:%zu\n", token_len);

        if (readn(STDIN_FILENO, &value_len, sizeof(size_t)) <= 0)
            break;

        //fprintf(stderr, "reducer.c: Reducer reader read value_len:%zu\n", value_len);

        char *token = malloc(token_len + 1);

        if (!token)
            return -1;

        if (readn(STDIN_FILENO, token, token_len) <= 0) {

            free(token);
            break;
        }

        token[token_len] = '\0';

        
        //fprintf(stderr, "reducer.c: Reducer reader read token:%s\n", token);

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

        for(size_t i=0; i<groups_count; i++){ 
            if (strcmp(groups[i]->token, token) == 0) {
                trovato=1;
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

    }

    for(size_t i=0; i<groups_count; i++){ 
        queue_push(q, groups[i]);
    }

    free(groups);
    queue_close(q);

    char msg[128];
    snprintf(msg, sizeof(msg),
            "reducer reader thread finito, ha letto: %lu token totali, raggruppati in %zu token distinti",
            token_unici, groups_count);
    log_event(mr, "reducer.c", "reader_thread", msg);

    //fprintf(stderr,"reducer.c: Reducer reader process with PID %d finished successfully\n", getpid());
    return 0;
}

static int worker_thread(void *arg) {
    
    reducer_worker_ctx_t *ctx = arg;

    mr_t mr= ctx->mr;
    //fprintf(stderr,"reducer.c: Reducer worker threads starting with PID %d\n", getpid());
    log_event(mr, "reducer.c", "worker_thread", "reducer worker thread creato");


    unsigned long gruppi_elaborati=0;
    while (1) {

        reduce_group_t *group = queue_pop(ctx->q);

        if (!group)
            break;

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

    //fprintf(stderr,"reducer.c: Reducer worker threads with PID %d finished successfully\n", getpid());

    return 0;
}

int reducer_process_main(mr_t mr)
{
    //fprintf(stderr,"reducer.c: Reducer process started with PID %d\n", getpid());

    mtx_t out_mutex;
    mtx_init(&out_mutex, mtx_plain);

    mr_attr_t attr = mr_get_attr(mr);

    queue_t *q = queue_create(attr.queue_size);
    if (!q)
        return -1;

    thrd_t reader_tid;
    reducer_reader_ctx_t reader_ctx;
    reader_ctx.mr=mr;
    reader_ctx.q=q;

    if (thrd_create(&reader_tid, reader_thread, &reader_ctx) != thrd_success) {
        queue_destroy(q);
        return -1;
    }

    thrd_join(reader_tid, NULL);

    thrd_t *worker_tids = malloc(sizeof(thrd_t) * attr.reducer_threads);

    if (!worker_tids) {
        queue_close(q);
        queue_destroy(q);
        return -1;
    }

    for (size_t i = 0; i < attr.reducer_threads; i++) {

        reducer_worker_ctx_t *ctx = malloc(sizeof(reducer_worker_ctx_t));

        if (!ctx) {

            queue_close(q);

            for (size_t j = 0; j < i; j++) {
                thrd_join(worker_tids[j], NULL);
            }

            free(worker_tids);
            queue_destroy(q);

            return -1;
        }

        ctx->q = q;
        ctx->mr = mr;
        ctx->out_mtx = &out_mutex;

        if (thrd_create(
                &worker_tids[i],
                worker_thread,
                ctx
            ) != thrd_success) {

            free(ctx);

            queue_close(q);

            for (size_t j = 0; j < i; j++) {
                thrd_join(worker_tids[j], NULL);
            }

            free(worker_tids);
            queue_destroy(q);

            return -1;
        }
    }


    for (size_t i = 0; i < attr.reducer_threads; i++) {
        thrd_join(worker_tids[i], NULL);
    }

    free(worker_tids);

    queue_destroy(q);

    mtx_destroy(&out_mutex);

    //fprintf(stderr,"reducer.c: Reducer process with PID %d finished successfully\n", getpid());
    return 0;
}