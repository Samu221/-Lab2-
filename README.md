# MapReduce Framework in C

## Descrizione

Questo progetto implementa un **framework MapReduce** in linguaggio C per sistemi Linux (Ubuntu 24.04).

Il framework permette di eseguire elaborazioni parallele su file di input tramite due fasi principali:

- **Mapper**: elabora i dati in ingresso e produce coppie intermedie `(key, value)`
- **Reducer**: aggrega i risultati intermedi e produce l’output finale

La comunicazione tra i processi avviene tramite **pipe UNIX**, mentre la sincronizzazione del logging viene gestita tramite **semafori POSIX**.

---

## Architettura

Il sistema è composto da tre processi principali:
      +----------------+
      |   **MAIN**     |
      | (framework)    |
      +--------+-------+
               |
            pipe (input)
               |
      +--------v-------+
      |  **MAPPER**    |
      +--------+-------+
               |
            pipe (key/value)
               |
      +--------v-------+
      |  **REDUCER**   |
      +--------+-------+
               |
            pipe (output)
               |
      +--------v-------+
      |   **MAIN**     |
      +----------------+

---

## Struttura del progetto

.
├── include/
│ ├── mr.h
│ ├── mapper.h
│ ├── reducer.h
│ ├── config.h
│ ├── queue.h
│ ├── io.h
│
├── src/
│ ├── mr.c
│ ├── mapper.c
│ ├── reducer.c
│ ├── queue.c
│ ├── io.c
│
├── examples/
│ ├── input_test.txt
│ ├── input_dir/
│ │ ├── a.txt
│ │ ├── b.txt
│ ├── inverted_index.c
│ ├── word_count.c
│ ├── line_count.c
│
├── logs/
│
├── results/
│ ├── reader_int
│ ├── reader_string
│ ├── result_reader_int.c
│ ├── result_reader_string.c
│
├── Makefile
└── README.md


---

## Compilazione

### Per compilare il progetto:

```bash
make
```

Questo comando genera:

- la libreria statica libmr.a
- gli eseguibili di test


### Per eseguire la suite di test:

```bash
make test
```

Oppure eseguire singolarmente:

./word_count
./inverted_index
./line_count

Word Count

Conta le occorrenze delle parole nei file di input.

Line Count

Conta il numero di righe elaborate.

Inverted Index

Costruisce un indice inverso parola → file contenenti la parola.


### Per rimuovere file compilati e output:

```bash
make clean
```

## API del framework

### Creazione del contesto
int mr_create(mr_t *mr,
              const mr_attr_t *attr,
              mr_mapper_t mapper,
              mr_reducer_t reducer,
              void *user_arg);

### Avvio esecuzione
int mr_start(mr_t mr,
             const char *input_path,
             const char *output_path);

### Distruzione del framework
int mr_destroy(mr_t mr);

### Configurazione attributi

Il framework supporta la configurazione tramite mr_attr_t:

numero thread mapper
numero thread reducer
dimensione coda
file di log

Esempio:

mr_attr_t attr;
mr_attr_init(&attr);

mr_attr_set_mapper_threads(&attr, 4);
mr_attr_set_reducer_threads(&attr, 4);
mr_attr_set_queue_size(&attr, 64);
mr_attr_set_log_file(&attr, "mr.log");

## Logging

Il framework genera un file di log con informazioni su:

creazione processi
invio dati tra pipe
eventi mapper/reducer
fine esecuzione

Esempio:

[21-05-2026 12:30:01.123] [processo:main] [PID:12345] ...

## Requisiti
GCC
Linux Ubuntu 24.04
POSIX compliant system

Compilazione con:

-std=c11
-Wall -Wextra
-D_POSIX_C_SOURCE=200809L
-D_GNU_SOURCE