#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <threads.h> 
#include <sys/stat.h>
#include <dirent.h>
#include <string.h>
#include <limits.h>
#include <semaphore.h>
#include <time.h>


#include "mr.h"
#include "mapper.h"
#include "reducer.h"
#include "config.h"
#include "len_limits.h"


/* Definizione di mr_t */
struct mr {
    mr_attr_t attr;     //attributi del framework

    mr_mapper_t mapper;     //funzione mapper
    mr_reducer_t reducer;   //funzione reducer

    void *user_arg;     // Puntatore generico a dati forniti dall'utente del framework.
                        // Viene passato automaticamente alle funzioni mapper e reducer

    sem_t *log_sem; //semaforo per l'accesso al file di log
};

//scrittura di eventi sul file di log
int log_event(mr_t mr, const char *process, const char *event, const char *message) {

    if (!mr || !process || !event || !message) {
        errno = EINVAL;
        return -1;
    }
    
    //salvataggio della data e dell'orario
    char timestamp[64];

    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);

    //calcolo millisecondi per maggiore precisione
    time_t sec = ts.tv_sec;
    long ms = ts.tv_nsec / 1000000;

    struct tm tm_info;
    localtime_r(&sec, &tm_info);

    strftime(timestamp, sizeof(timestamp),
            "%d-%m-%Y %H:%M:%S",
            &tm_info);

    //attesa sul semaforo
    if (sem_wait(mr->log_sem) == -1) {
        return -1;
    }

    //apertura file di log
    FILE *log_file = fopen(mr->attr.log_file, "a");

    if (!log_file) {
        sem_post(mr->log_sem);
        return -1;
    }
    
    //scrittura sul file
    fprintf(log_file,
        "[%s.%03ld] [processo:%-10s] [PID:%6d] [TID:%12lu] [funzione:%-14s] %s\n",
        timestamp,
        ms,
        process,
        getpid(),
        (unsigned long) thrd_current(),
        event,
        message
    );

    fflush(log_file);

    fclose(log_file);

    sem_post(mr->log_sem);
    //chiusura file e post sul semaforo

    return 0;
}

//ordinamento e scrittura dell'output
int sort_write(mr_t mr, const char *output_path, mr_out_record_t *output, ssize_t output_len) {

    if (!output || !output_path) {
        errno = EINVAL;
        return -1;
    }

    //funzione per comparare due token per ordinarli
    int comp(const void *a, const void *b) {
        const mr_out_record_t *ra = a;
        const mr_out_record_t *rb = b;

        return strcmp(ra->token, rb->token);
    }

    //ordinamento dell'array contenente gli output
    qsort(output, output_len, sizeof(mr_out_record_t), comp);

    //apertura file output
    FILE *output_file = fopen(output_path, "w");
    if (!output_file) {
        perror("fopen");
        return -1;
    }
    log_event(mr, "mr.c", "sort_write", "file di output aperto");

    //scrittura dell'output sul file
    for (ssize_t i = 0; i < output_len; i++) {

        fwrite(&output[i].token_len, sizeof(size_t), 1, output_file);
        fwrite(output[i].token, 1, output[i].token_len, output_file);

        fwrite(&output[i].result_len, sizeof(size_t), 1, output_file);
        fwrite(output[i].result, 1, output[i].result_len, output_file);
  
    }

    fclose(output_file);
    log_event(mr, "mr.c", "sort_write", "file di output chiuso");

    return 0;
}

//funzione che scrive riga x riga dal main al mapper
int writer(mr_t mr, const char *input_path, int main_to_mapper_write_fd ) {
    log_event(mr, "mr.c", "writer", "file di input aperto");

    //apertura del file di input
    FILE *input_file = fopen(input_path, "r");

    if (!input_file)
        return -1;

    char line[BUF_SIZE];

    unsigned long line_number = 1;

    //leggo una linea alla volta finchè non sono finite 
    while (fgets(line, sizeof(line), input_file) != NULL) {

        size_t file_name_len = strlen(input_path);
        size_t line_len = strlen(line);

        // Rimuoviamo il newline se presente
        if (line_len > 0 && line[line_len - 1] == '\n') {
            line[line_len - 1] = '\0';  // Sostituisci '\n' con terminatore
            line_len--;                   // Aggiorna la lunghezza
        }

        //scrittura della lunghezza del nome del file
        if (writen(main_to_mapper_write_fd, &file_name_len, sizeof(size_t)) == -1) {

            fclose(input_file);
            return -1;
        }

        //scrittura del nome del file
        if (writen(main_to_mapper_write_fd, input_path, file_name_len ) == -1) {

            fclose(input_file);
            return -1;
        }

        //scrittura del numero della linea
        if (writen(main_to_mapper_write_fd, &line_number, sizeof(unsigned long) ) == -1) {

            fclose(input_file);
            return -1;
        }

        //scrittura della lunghezza del testo della linea
        if (writen(main_to_mapper_write_fd, &line_len, sizeof(size_t)) == -1) {

            fclose(input_file);
            return -1;
        }

        //scrittura del testo contenuto nella linea
        if (writen(main_to_mapper_write_fd, line, line_len) == -1) {

            fclose(input_file);
            return -1;
        }

        line_number++;
    }

    fclose(input_file);

    char msg[64];

    snprintf(msg, sizeof(msg),
            "file di input chiuso, inviate linee: %lu",
            line_number-1);

    log_event(mr, "mr.c", "writer", msg);

    return 0;
}

int cmp_string(const void *a, const void *b) {
    const char * const *sa = a;
    const char * const *sb = b;

    return strcmp(*sa, *sb);
}
int main_function(mr_t mr, const char *input_path,const char *output_path, int main_to_mapper, int reducer_to_main){

    //Controlla il tipo del percorso di input usando stat():
    struct stat st;

    if (stat(input_path, &st) == -1) {
        return -1;
    }

    if (S_ISREG(st.st_mode)) {
        //Se input è un file regolare invio il file al mapper

        if(writer(mr, input_path, main_to_mapper) == -1) {
            return -1;
        }

    } else if (S_ISDIR(st.st_mode)) {
        //Se input è una directory la apro

        DIR *dir = opendir(input_path);

        if (!dir) {
            return -1;
        }

        struct dirent *entry;

        // Memorizza i nomi dei file per poterli ordinare
        char **names = NULL;
        size_t count = 0;

        //Scorre tutte le entry della directory.

        while ((entry = readdir(dir)) != NULL) {

            /* Ignora le entry speciali:
            "."  directory corrente
            ".." directory padre */

            if (strcmp(entry->d_name, ".") == 0 ||
                strcmp(entry->d_name, "..") == 0) {
                continue;
            }

            char **tmp = realloc(names, (count + 1) * sizeof(char *));
            if (!tmp) {
                closedir(dir);

                for (size_t i = 0; i < count; i++) {
                    free(names[i]);
                }

                free(names);

                return -1;
            }

            names = tmp;
            names[count] = strdup(entry->d_name);

            if (!names[count]) {
                closedir(dir);

                for (size_t i = 0; i < count; i++) {
                    free(names[i]);
                }

                free(names);

                return -1;
            }

            count++;
        }

        closedir(dir);

        // Ordina i nomi in ordine lessicografico
        qsort(names, count, sizeof(char *), cmp_string);

        // Processa i file nell'ordine richiesto
        for (size_t i = 0; i < count; i++) {

            //percorso completo (PATH_MAX è una costante definita dal sistema operativo inclusa con limits.h)
            char fullpath[PATH_MAX];

            //Costruzione del percorso completo.
            // esempio: input_dir/file.txt
            snprintf(
                fullpath,
                PATH_MAX,
                "%s/%s",
                input_path,
                names[i]
            );

            //Recupera informazioni sul file corrente.

            struct stat fst;

            if (stat(fullpath, &fst) == -1) {
                free(names[i]);
                continue;
            }

            //Processa solamente file regolari.
            if (S_ISREG(fst.st_mode)) {

                // invio del file al mapper
                if (writer(mr, fullpath, main_to_mapper) == -1) {

                    free(names[i]);

                    for (size_t j = i + 1; j < count; j++) {
                        free(names[j]);
                    }

                    free(names);

                    return -1;
                }
            }

            free(names[i]);
        }

        free(names);

    } else {
        // Caso non supportato: non è né file né directory.

        return -1;
    }



    close(main_to_mapper);

    //file inviati al mapper

    //lettura da reducer_to_main

    //alloco un array di 16 elementi per salvare il risultato
    size_t out_count=0;
    size_t out_capacity=16;
    mr_out_record_t *output = malloc(sizeof(mr_out_record_t) * out_capacity);
    if (!output) return -1;

    while (1) {

        size_t token_len;
        size_t result_len;

        //leggo lunghezza del token
        if (readn(reducer_to_main, &token_len, sizeof(token_len)) <= 0)
            break;

        if(token_len == 0 || token_len> MR_MAX_TOKEN_LEN)
            return -1;

        //leggo lunghezza del risultato
        if (readn(reducer_to_main, &result_len, sizeof(result_len)) <= 0)
            break;

        if(result_len == 0 || result_len> MR_MAX_VALUE_LEN)
            return -1;

        //alloco spazio per il token
        char *token = malloc(token_len + 1);

        if (!token)
            return -1;

        //leggo il token
        if (readn(reducer_to_main, token, token_len) <= 0) {

            free(token);
            break;
        }

        //aggiungo terminatore di stringa
        token[token_len] = '\0';

        //alloco spazio per il risultato
        char *result = malloc(result_len);

        if (!result) {
            free(token);
            return -1;
        }

        //leggo il risultato
        if (readn(reducer_to_main, result, result_len) <= 0) {
            free(token);
            free(result);
            break;
        }

        //se ho esaurito lo spazio raddoppio la capacita e alloco nuova memoria
        if(out_count==out_capacity){
            size_t new_capacity = out_capacity * 2;
            mr_out_record_t *new_output = realloc(output, sizeof(mr_out_record_t) * new_capacity);
            if (!new_output) {
                free(token);
                free(result);
                return -1;
            }
            output = new_output;
            out_capacity = new_capacity;
        }

        //salvo gli output nel vettore
        output[out_count].token = malloc(token_len);
        memcpy(output[out_count].token, token, token_len);

        output[out_count].result = malloc(result_len);
        memcpy(output[out_count].result, result, result_len);

        output[out_count].token_len = token_len;
        output[out_count].result_len = result_len;

        out_count++;

        free(token);
        free(result);

    }

    close(reducer_to_main);
    //letture del risultato finita

    //chiamo la funzione che ordina e scrive l'output
    if(sort_write(mr,output_path, output, out_count)==-1){
        return -1;
    }

    char msg[128];
    snprintf(msg, sizeof(msg),
            "elaborazione terminata, ha elaborato: %zu coppie",
            out_count);
    log_event(mr, "mr.c", "main_function", msg);

    for (size_t i = 0; i < out_count; i++) {
        free(output[i].token);
        free(output[i].result);
    }

    free(output);

    return 0;
}

//inizializza il framework
int mr_create(mr_t *mr, const mr_attr_t *attr, mr_mapper_t mapper, mr_reducer_t reducer, void *user_arg) {
    if (mr == NULL || attr == NULL || mapper == NULL || reducer == NULL) {
        errno = EINVAL;
        return -1;
    }


    struct mr *obj = calloc(1, sizeof(struct mr));
    if (obj == NULL) {
        return -1;
    }

    obj->attr = *attr;

    obj->mapper = mapper;
    obj->reducer = reducer;

    obj->user_arg = user_arg;


    *mr = obj;

    return 0;
};

//distruggi framework
int mr_destroy(mr_t mr) {
    if (mr == NULL) {
        errno = EINVAL;
        return -1;
    }

    free(mr);

    return 0;
};

//esecuzione del framework
int mr_start(mr_t mr, const char *input_path, const char *output_path) {

    if (mr == NULL ||
        input_path == NULL ||
        output_path == NULL) {
        errno = EINVAL;
        return -1;
    }

    //svuoto il file di log se era gia scritto
    FILE *f = fopen(mr->attr.log_file, "w");
    if (!f) return -1;
    fclose(f);

    //creo semaforo per logfile
    sem_unlink("/mr_log_sem");
    sem_t *log_sem = sem_open("/mr_log_sem", O_CREAT, 0644, 1);
    if (log_sem == SEM_FAILED) {
        return -1;
    }
    mr->log_sem = log_sem;
    
    log_event(mr, "mr.c", "mr_start", "inizio esecuzione di mr_start");

    //creazione delle pipe
    int main_to_mapper[2];
    int mapper_to_reducer[2];
    int reducer_to_main[2];

    if (pipe(main_to_mapper) == -1) {
        return -1;
        sem_close(log_sem);
        sem_unlink("/mr_log_sem");
    }
    log_event(mr, "mr.c", "mr_start", "pipe main_to_mapper creata");


    if (pipe(mapper_to_reducer) == -1) {
        close(main_to_mapper[0]);
        close(main_to_mapper[1]);
        sem_close(log_sem);
        sem_unlink("/mr_log_sem");
        return -1;
    }
    log_event(mr, "mr.c", "mr_start", "pipe mapper_to_reducer creata");

    if (pipe(reducer_to_main) == -1) {
        close(main_to_mapper[0]);
        close(main_to_mapper[1]);

        close(mapper_to_reducer[0]);
        close(mapper_to_reducer[1]);

        sem_close(log_sem);
        sem_unlink("/mr_log_sem");
        return -1;
    }
    log_event(mr, "mr.c", "mr_start", "pipe reducer_to_main creata");

    //fork per creare processo mapper
    pid_t mapper_pid = fork();
    if (mapper_pid == -1) {
        close(main_to_mapper[0]);
        close(main_to_mapper[1]);

        close(mapper_to_reducer[0]);
        close(mapper_to_reducer[1]);

        close(reducer_to_main[0]);
        close(reducer_to_main[1]);

        sem_close(log_sem);
        sem_unlink("/mr_log_sem");
        return -1;
    }
    if(mapper_pid == 0) {
        // Processo mapper
        log_event(mr, "mr.c", "mr_start", "processo mapper iniziato con fork");

        // input=main_to_mapper                           output=mapper_to_reducer
        if(dup2(main_to_mapper[0], STDIN_FILENO) == -1 || dup2(mapper_to_reducer[1], STDOUT_FILENO) == -1) {
            sem_close(log_sem);
            sem_unlink("/mr_log_sem");
            close(main_to_mapper[0]);
            close(main_to_mapper[1]);
            close(mapper_to_reducer[0]);
            close(mapper_to_reducer[1]);
            close(reducer_to_main[0]);
            close(reducer_to_main[1]);
            _exit(1);
        }

        //chiudo pipe inutili
        close(main_to_mapper[0]);
        close(main_to_mapper[1]);

        close(mapper_to_reducer[0]);
        close(mapper_to_reducer[1]);

        close(reducer_to_main[0]);
        close(reducer_to_main[1]);

        if (mapper_process_main(mr) == -1){
            sem_close(log_sem);
            sem_unlink("/mr_log_sem");
            _exit(1);
        }

        log_event(mr, "mr.c", "mr_start", "processo mapper finito");

        _exit(0);
    }

    //fork per creare il processo reducer
    pid_t reducer_pid = fork();
    if (reducer_pid == -1) {
        close(main_to_mapper[0]);
        close(main_to_mapper[1]);

        close(mapper_to_reducer[0]);
        close(mapper_to_reducer[1]);

        close(reducer_to_main[0]);
        close(reducer_to_main[1]);

        sem_close(log_sem);
        sem_unlink("/mr_log_sem");

        return -1;
    }
    if(reducer_pid == 0) {
        // Processo reducer

        log_event(mr, "mr.c", "mr_start", "processo reducer iniziato con fork");

        // input=mapper_to_reducer                           output=reducer_to_main
        if (dup2(mapper_to_reducer[0], STDIN_FILENO) == -1 || dup2(reducer_to_main[1], STDOUT_FILENO) == -1) {
            sem_close(log_sem);
            sem_unlink("/mr_log_sem");
            close(main_to_mapper[0]);
            close(main_to_mapper[1]);
            close(mapper_to_reducer[0]);
            close(mapper_to_reducer[1]);
            close(reducer_to_main[0]);
            close(reducer_to_main[1]);
            _exit(1);
        }

        //chiudo pipe inutili
        close(main_to_mapper[0]);
        close(main_to_mapper[1]);

        close(mapper_to_reducer[0]);
        close(mapper_to_reducer[1]);

        close(reducer_to_main[0]);
        close(reducer_to_main[1]);

        if (reducer_process_main(mr) == -1){
            sem_close(log_sem); 
            sem_unlink("/mr_log_sem");
            _exit(1);
        }

        log_event(mr, "mr.c", "mr_start", "processo reducer finito");
        _exit(0);
    }

    //PADRE

    //chiude pipe inutilizzate
    close(main_to_mapper[0]);

    close(mapper_to_reducer[0]);
    close(mapper_to_reducer[1]);

    close(reducer_to_main[1]);

    //chiamo la funzione main_function che manda il file al mapper e legge il risultato dal reducer
    if(main_function(mr, input_path, output_path, main_to_mapper[1], reducer_to_main[0]) == -1) {
        sem_close(log_sem);
        sem_unlink("/mr_log_sem");
        close(main_to_mapper[1]);
        close(reducer_to_main[0]);
        return -1;
    }

    close(main_to_mapper[1]);
    

    int statusMap;
    int statusReduce;

    //Blocca il processo padre finché il mapper termina e salva lo stato di uscita in statusMap
    if (waitpid(mapper_pid, &statusMap, 0) == -1) {
        return -1;
    }

    //Blocca il processo padre finché il reducer termina e salva lo stato di uscita in statusReduce
    if (waitpid(reducer_pid, &statusReduce, 0) == -1) {
        return -1;
    }

    //Controlla che il mapper sia terminato normalmente con una exit e che abbia restituito codice di uscita 0
    if (!WIFEXITED(statusMap) || WEXITSTATUS(statusMap) != 0) {
        return -1;
    }

    //Controlla che il reducer sia terminato normalmente con una exit e che abbia restituito codice di uscita 0
    if (!WIFEXITED(statusReduce) || WEXITSTATUS(statusReduce) != 0) {
        return -1;
    }
    
    log_event(mr, "mr.c", "mr_start", "fine esecuzione di mr_start");

    //elimino il semaforo
    sem_close(log_sem);
    sem_unlink("/mr_log_sem");

    return 0;

};

//inizializzo a un valore default
int mr_attr_init(mr_attr_t *attr) {
    if (attr == NULL) {
        errno = EINVAL;
        return -1;
    }

    attr->mapper_threads = 4;
    attr->reducer_threads = 4;
    attr->queue_size = 64;
    attr->log_file = "logs/mr.log";
    return 0;
}

//elimino gli attributi
int mr_attr_destroy(mr_attr_t *attr) {
    if (attr == NULL) {
        errno = EINVAL;
        return -1;
    }


    return 0;
}

//getters
mr_mapper_t mr_get_mapper(mr_t mr) {
    return mr->mapper;
}

void *mr_get_user_arg(mr_t mr) {
    return mr->user_arg;
}

mr_attr_t mr_get_attr(mr_t mr) {
    return mr->attr;
}

mr_reducer_t mr_get_reducer(mr_t mr) {
    return mr->reducer;
}

//setters
int mr_attr_set_mapper_threads(mr_attr_t *attr, size_t n){
    if (attr == NULL || n == 0) {
        errno = EINVAL;
        return -1;
    }

    attr->mapper_threads = n;
    return 0;
}
int mr_attr_set_reducer_threads(mr_attr_t *attr, size_t n){
    if (attr == NULL || n == 0) {
        errno = EINVAL;
        return -1;
    }

    attr->reducer_threads = n;

    return 0;
}
int mr_attr_set_queue_size(mr_attr_t *attr, size_t n){
    if (attr == NULL || n == 0) {
        errno = EINVAL;
        return -1;
    }

    attr->queue_size = n;

    return 0;
}
int mr_attr_set_log_file(mr_attr_t *attr, const char *path){
    if (attr == NULL || path == NULL) {
        errno = EINVAL;
        return -1;
    }

    attr->log_file = path;

    return 0;
}