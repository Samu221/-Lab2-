#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "../include/mr.h"

// Funzione Mapper: estrae parole alfanumeriche e invia <token, 1>
int my_mapper(const mr_file_line_t *line, mr_emit_pair_t emit, void *emit_arg, void *user_arg) {
    (void)user_arg; // Ignora user_arg se non usato

    char *token = malloc(256);
    size_t i = 0, j = 0;
    int one = 1;

    while (i < line->line_len) {
        // Salta caratteri non alfanumerici [6]
        while (i < line->line_len && !isalnum((unsigned char)line->line[i])) i++;
        
        j = 0;
        // Estrae il token [7]
        while (i < line->line_len && isalnum((unsigned char)line->line[i]) && j < 255) {
            token[j++] = line->line[i++];
        }
        
        if (j > 0) {
            token[j] = '\0';
            // Emette la coppia <token, 1> serializzata [1, 3]
            if (emit(token, &one, sizeof(int), emit_arg) == -1) {
                free(token);
                return -1;
            }
        }
    }
    free(token);
    return 0;
}

// Funzione Reducer: somma tutti gli '1' ricevuti per un token [1]
int my_reducer(const char *token, const mr_value_t *values, size_t values_count, mr_emit_result_t emit, void *emit_arg, void *user_arg) {

    (void)user_arg; // Ignora user_arg se non usato

    int sum = 0;
    for (size_t i = 0; i < values_count; i++) {
        if (values[i].size == sizeof(int)) {
            sum += *(int *)(values[i].data);
        }
    }
    // Emette il risultato finale [8]
    return emit(token, &sum, sizeof(int), emit_arg);
}

int main(int argc, char **argv) {

    (void)argc; // Ignora argc se non usato
    (void)argv; // Ignora argv se non usato

    mr_t mr;
    mr_attr_t attr;

    // Inizializzazione parametri [2, 9]
    if (mr_attr_init(&attr) == -1) return 1;

    mr_attr_set_mapper_threads(&attr, 4);

    mr_attr_set_reducer_threads(&attr, 4);

    mr_attr_set_queue_size(&attr, 100); 

    mr_attr_set_log_file(&attr, "logs/word_count.log");

    //printf("Starting MapReduce with %zu mapper threads and %zu reducer threads\n", attr.mapper_threads, attr.reducer_threads);

    // Creazione istanza framework [11]
    if (mr_create(&mr, &attr, my_mapper, my_reducer, NULL) == -1) {
        mr_attr_destroy(&attr);
        return 1;
    }

    // Avvio elaborazione su una directory o file [5, 12]
    if (mr_start(mr, "examples/input_dir", "results/word_count.mro") == -1) {
        perror("Errore durante mr_start");
    }

    // Pulizia risorse [11, 13]
    mr_destroy(mr);
    mr_attr_destroy(&attr);

    return 0;
}