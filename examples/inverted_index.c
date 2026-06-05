#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "../include/mr.h"

int my_mapper(const mr_file_line_t *line, mr_emit_pair_t emit, void *emit_arg, void *user_arg) {
    (void)user_arg; // Ignora user_arg se non usato

    char *token = malloc(256);
    size_t i = 0, j = 0;

    while (i < line->line_len) {
        while (i < line->line_len && !isalnum((unsigned char)line->line[i])) i++;
        
        j = 0;
        while (i < line->line_len && isalnum((unsigned char)line->line[i]) && j < 255) {
            token[j++] = line->line[i++];
        }
        
        if (j > 0) {
            token[j] = '\0';
            if (emit(token, line->file_name, line->file_name_len, emit_arg) == -1) {
                free(token);
                return -1;
            }
        }
    }
    free(token);
    return 0;
}

int my_reducer(const char *token, const mr_value_t *values, size_t values_count, mr_emit_result_t emit, void *emit_arg, void *user_arg){
    (void)user_arg;

    char *files[values_count];
    size_t unique_count = 0;

    for (size_t i = 0; i < values_count; i++) {
        
        // copia e elimina cartelle
        char *tmpname = malloc(values[i].size + 1);
        memcpy(tmpname, values[i].data, values[i].size);
        tmpname[values[i].size] = '\0';

        char *fname = malloc(values[i].size + 1);
        size_t k = 0;

        for (size_t j = 0; j < values[i].size; j++) {
            if (tmpname[j] == '/') {
                k = 0;
            } else {
                fname[k++] = tmpname[j];
            }
        }

        fname[k] = '\0';

        int found = 0;
        for (size_t j = 0; j < unique_count; j++) {
            if (strcmp(files[j], fname) == 0) {
                found = 1;
                break;
            }
        }

        if (!found) {
            files[unique_count++] = fname;
        } else {
            free(fname);
        }
    }

    //costruisci output
    char buffer[1024];
    buffer[0] = '\0';

    for (size_t i = 0; i < unique_count; i++) {
        strcat(buffer, files[i]);
        if (i + 1 < unique_count)
            strcat(buffer, "->");
        free(files[i]);
    }

    return emit(token, buffer, strlen(buffer), emit_arg);
}

int main(int argc, char **argv) {

    (void)argc; // Ignora argc se non usato
    (void)argv; // Ignora argv se non usato

    mr_t mr;
    mr_attr_t attr;

    if (mr_attr_init(&attr) == -1) return 1;

    if(mr_attr_set_mapper_threads(&attr, 4)) return 1;

    if(mr_attr_set_reducer_threads(&attr, 4)) return 1;

    if(mr_attr_set_queue_size(&attr, 1)) return 1;

    if(mr_attr_set_log_file(&attr, "logs/inverted_index.log")) return 1;

    //printf("Starting MapReduce with %zu mapper threads and %zu reducer threads\n", attr.mapper_threads, attr.reducer_threads);

    // Creazione istanza framework
    if (mr_create(&mr, &attr, my_mapper, my_reducer, NULL) == -1) {
        mr_attr_destroy(&attr);
        return 1;
    }

    // Avvio elaborazione
    if (mr_start(mr, "examples/input_dir", "results/inverted_index.mro") == -1) {
        perror("Errore durante mr_start");
    }

    // Pulizia risorse
    mr_destroy(mr);
    mr_attr_destroy(&attr);

    return 0;
}