#include "mapper.h"

static int emit_pair(const char *token, const void *value, size_t value_size, void *emit_arg) {

    if (token == NULL || emit_arg == NULL) {
        errno = EINVAL;
        return -1;
    }

    worker_ctx_t *ctx = emit_arg;

    size_t token_len = (size_t)strlen(token);
    size_t value_len = (size_t)value_size;

    
    mtx_lock(ctx->out_mtx);

    if (writen(STDOUT_FILENO, &token_len, sizeof(size_t)) == -1) {
        mtx_unlock(ctx->out_mtx);
        return -1;
    }

    if (writen(STDOUT_FILENO, &value_len, sizeof(size_t)) == -1) {
        mtx_unlock(ctx->out_mtx);
        return -1;
    }

    if (writen(STDOUT_FILENO, token, token_len) == -1) {
        mtx_unlock(ctx->out_mtx);
        return -1;
    }

    if (value_len > 0) {
        if (writen(STDOUT_FILENO, value, value_len) == -1) {
            mtx_unlock(ctx->out_mtx);
            return -1;
        }
    }

    mtx_unlock(ctx->out_mtx);

    return 0;
}


static int reader_thread(void *arg) {

    reader_ctx_t *ctx = arg;
    mr_t mr = ctx->mr;
    queue_t *q = ctx->q;

    log_event(mr, "mapper.c", "reader_thread", "mapper reader thread iniziato");

    unsigned long linee_lette=0;
    while (1) {


        mr_file_line_t *file_line = malloc(sizeof(mr_file_line_t));
        if (!file_line) return -1;

        // 1. leggi header (DA PIPE)
        size_t file_name_len;
        unsigned long line_number;
        size_t line_len;

        if (readn(STDIN_FILENO, &file_name_len, sizeof(file_name_len)) <= 0)
            break;

        char *file_name = malloc(file_name_len + 1);
        readn(STDIN_FILENO, file_name, file_name_len);
        file_name[file_name_len] = '\0';

        readn(STDIN_FILENO, &line_number, sizeof(line_number));
        readn(STDIN_FILENO, &line_len, sizeof(line_len));

        char *line = malloc(line_len + 1);
        readn(STDIN_FILENO, line, line_len);
        line[line_len] = '\0';

        file_line->file_name = file_name;
        file_line->file_name_len = file_name_len;
        file_line->line_number = line_number;
        file_line->line = line;
        file_line->line_len = line_len;

        queue_push(mr, q, file_line);

        linee_lette++;
        
        /*char msg[128];
        snprintf(msg, sizeof(msg),
                "mapper reader thread ha letto la %luesima linea", linee_lette);
        log_event(mr, "mapper", "mapper_thread", msg);*/
    }

    char msg[64];
    snprintf(msg, sizeof(msg),
            "mapper reader thread finito, ha letto: %lu linee",
            linee_lette);
    log_event(mr, "mapper.c", "reader_thread", msg);

    return 0;
}


static int worker_thread(void *arg) {
    
    worker_ctx_t *ctx = arg;
    mr_t mr = ctx->mr;

    log_event(mr, "mapper.c", "worker_thread", "mapper worker thread iniziato");

    unsigned long coppie_inviate=0;
    while (1) {

        mr_file_line_t *file_line = queue_pop(mr, ctx->q);
        if (!file_line) break;

        mr_get_mapper(ctx->mr)(
            file_line,
            emit_pair,
            ctx,
            mr_get_user_arg(ctx->mr)
        );

        coppie_inviate++;

        free((void *)file_line->file_name);
        free((void *)file_line->line);
        free(file_line);
    }

    char msg[64];

    snprintf(msg, sizeof(msg),
            "mapper worker thread finito, coppie prodotte: %lu",
            coppie_inviate);

    log_event(mr, "mapper.c", "worker_thread", msg);

    return 0;
}

int mapper_process_main(mr_t mr) {


    mtx_t out_mutex;
    mtx_init(&out_mutex, mtx_plain);

    mr_attr_t attr = mr_get_attr(mr);
    queue_t *q = queue_create(mr, attr.queue_size);

    if (!q) return -1;

    reader_ctx_t *reader_ctx = malloc(sizeof(reader_ctx_t));
    reader_ctx->q = q;
    reader_ctx->mr = mr;


    thrd_t reader_tid;
    if (thrd_create(&reader_tid, reader_thread, reader_ctx) != thrd_success) {
        queue_destroy(mr, q);
        return -1;
    }

    thrd_t *worker_tids = malloc(sizeof(thrd_t) * attr.mapper_threads);
    if (!worker_tids) {
        queue_close(mr, q);
        thrd_join(reader_tid, NULL);
        queue_destroy(mr, q);
        return -1;
    }

    for (size_t i = 0; i < attr.mapper_threads; i++) {
        worker_ctx_t *ctx = malloc(sizeof(worker_ctx_t));
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

        if (thrd_create(&worker_tids[i], worker_thread, ctx) != thrd_success) {
            free(ctx);
            queue_close(mr,q);
            thrd_join(reader_tid, NULL);

            for (size_t j = 0; j < i; j++) {
                thrd_join(worker_tids[j], NULL);
            }

            free(worker_tids);
            queue_destroy(mr,q);
            return -1;
        }
    }

    thrd_join(reader_tid, NULL);

    queue_close(mr, q);

    for (size_t i = 0; i < attr.mapper_threads; i++) {
        thrd_join(worker_tids[i], NULL);
    }

    free(worker_tids);

    queue_destroy(mr, q);

    mtx_destroy(&out_mutex);

    return 0;
}

