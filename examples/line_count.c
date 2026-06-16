#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "mr.h"

//conta righe

int my_mapper(const mr_file_line_t *line, mr_emit_pair_t emit, void *emit_arg, void *user_arg) {
    (void)user_arg; // Ignora user_arg se non usato

    int one=1;
    
    // Emette la coppia <nome_file, 1> serializzata
    if (emit(line->file_name, &one, sizeof(int), emit_arg) == -1) {
        return -1;
    }
            
    return 0;
}

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

    mr_attr_set_log_file(&attr, "logs/line_count.log");

    //printf("Starting MapReduce with %zu mapper threads and %zu reducer threads\n", attr.mapper_threads, attr.reducer_threads);

    // Creazione istanza framework [11]
    if (mr_create(&mr, &attr, my_mapper, my_reducer, NULL) == -1) {
        mr_attr_destroy(&attr);
        return 1;
    }

    // Avvio elaborazione su una directory o file [5, 12]
    if (mr_start(mr, "examples/input_dir", "results/line_count.mro") == -1) {
        perror("Errore durante mr_start");
    }

    // Pulizia risorse [11, 13]
    mr_destroy(mr);
    mr_attr_destroy(&attr);

    return 0;
}