#ifndef MR_H
#define MR_H

#include <stddef.h>

/* Handle opaco di una elaborazione . */
typedef struct mr *mr_t;

/* Attributi del framework */
typedef struct {
    size_t mapper_threads;  //numero di thread mapper
    size_t reducer_threads; //numero di thread reducer
    size_t queue_size;  //dimensione massima della coda di comunicazione tra mapper e reducer

    const char *log_file;   //percorso del file di log
} mr_attr_t;

/* Riga logica di un file visto dal mapper */
typedef struct {
    const char *file_name;  //nome del file
    size_t file_name_len;   //lunghezza del nome del file

    unsigned long line_number;  //numero di riga (a partire da 1)

    const char *line;   //contenuto della riga
    size_t line_len;    //lunghezza della riga
} mr_file_line_t;

/* Valore opaco associato a un token*/
typedef struct {
    const void *data;   //dato associato al token
    size_t size;    //dimensione del dato
} mr_value_t;

/* Record finale prodotto dal framework MapReduce */
typedef struct {
    char *token;  //token associato al risultato
    void *result;   //risultato prodotto dal reducer per quel token
    size_t token_len;
    size_t result_len;
} mr_out_record_t;

/*
Funzione usata dal mapper per emettere una coppia <token , valore > 

prende in input un token (stringa) e un valore (buffer di dimensione value_size)
emette una coppia che sarà poi processata dal reducer.
*/
typedef int (*mr_emit_pair_t)(
    const char *token,  
    const void *value,  
    size_t value_size, 
    void *emit_arg  
);

/* 
Funzione usata dal reducer per emettere un risultato finale 

prende in input un token (stringa), un risultato (buffer di dimensione result_size) 
emette un risultato finale che sarà poi scritto su file dal processo principale.
*/
typedef int (*mr_emit_result_t)(
    const char *token,      
    const void *result,     
    size_t result_size,
    void *emit_arg
);

/* Funzione mapper fornita dal programma utente  */
typedef int (*mr_mapper_t)(
    const mr_file_line_t *line,
    mr_emit_pair_t emit,
    void *emit_arg,
    void *user_arg
);

/* Funzione reducer fornita dal programma utente*/
typedef int (*mr_reducer_t)(
    const char *token,
    const mr_value_t *values,
    size_t values_count,
    mr_emit_result_t emit,
    void *emit_arg,
    void *user_arg
);

/* Gestione attributi */
int mr_attr_init(mr_attr_t *attr);
int mr_attr_destroy(mr_attr_t *attr);

int mr_attr_set_mapper_threads(mr_attr_t *attr, size_t n);
int mr_attr_set_reducer_threads(mr_attr_t *attr, size_t n);
int mr_attr_set_queue_size(mr_attr_t *attr, size_t n);
int mr_attr_set_log_file(mr_attr_t *attr, const char *path);

/* Inizializzazione del framework */
int mr_create(
    mr_t *mr,
    const mr_attr_t *attr,
    mr_mapper_t mapper,
    mr_reducer_t reducer,
    void *user_arg
);

/* Esecuzione del framework */
int mr_start(
    mr_t mr,
    const char *input_path,
    const char *output_path
);

/* Distruzione del framework */
int mr_destroy(mr_t mr);

/* Funzioni di accesso agli attributi del framework */
mr_mapper_t mr_get_mapper(mr_t mr);
void *mr_get_user_arg(mr_t mr);
mr_attr_t mr_get_attr(mr_t mr);
mr_reducer_t mr_get_reducer(mr_t mr);

/* Funzione per scrivere eventi sul log_file */
int log_event(mr_t mr, const char *process, const char *event, const char *message);
#endif