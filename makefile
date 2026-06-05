# ====================================================================================
# COMPILATORE
# ====================================================================================

# Variabile che contiene il nome del compilatore da utilizzare.
CC = gcc


# Variabile contenente tutti i flag passati al compilatore.
#
# -Iinclude
#   Aggiunge la cartella "include/" tra i path in cui cercare gli header file (.h).
#
# -Wall -Wextra
#   Abilitano warning aggiuntivi più severi.
#
# -std=c11
#   Usa lo standard C11.
#
# -D_GNU_SOURCE
#   Definisce la macro GNU_SOURCE.
#   Abilita estensioni GNU/Linux.
#
# -D_POSIX_C_SOURCE=200809L
#   Abilita funzionalità POSIX moderne.
#   Necessario per syscall/funzioni come:
#   - localtime_r
#   - getline
#   - clock_gettime

CFLAGS = -Iinclude -Wall -Wextra -std=c11 -D_GNU_SOURCE -D_POSIX_C_SOURCE=200809L


# ====================================================================================
# DIRECTORY DEL PROGETTO
# ====================================================================================

# Directory contenente i sorgenti principali del framework.

SRC_DIR = src


# Directory contenente gli esempi/test.

EX_DIR = examples



# ====================================================================================
# FILE SORGENTE DEL FRAMEWORK
# ====================================================================================

# Lista completa di tutti i file sorgente (.c)
# che fanno parte della libreria MapReduce.

SRC = $(SRC_DIR)/mr.c \
      $(SRC_DIR)/mapper.c \
      $(SRC_DIR)/reducer.c \
      $(SRC_DIR)/queue.c \
      $(SRC_DIR)/io.c



# Variabile che trasforma automaticamente tutti i file ".c" presenti in SRC nei corrispondenti file ".o".

OBJ = $(SRC:.c=.o)


# ====================================================================================
# LIBRERIA STATICA
# ====================================================================================

# Nome della libreria statica finale prodotta dal progetto.

LIB = libmr.a



# ====================================================================================
# TEST / ESEMPI
# ====================================================================================

# Lista dei programmi esempio/test da compilare.

TESTS = inverted_index word_count line_count


# Costruisce automaticamente la lista completa dei file sorgente dei test.
# aggiunge ".c" ad ogni elemento
# aggiunge "examples/" davanti

TEST_SRCS = $(addprefix $(EX_DIR)/, $(addsuffix .c, $(TESTS)))


# Lista dei nomi finali degli eseguibili.

TEST_BINS = $(TESTS)



# ====================================================================================
# TARGET DI DEFAULT
# ====================================================================================

# make compilera automaticamente:
# 1) la libreria
# 2) tutti i file di test

all: $(LIB) $(TEST_BINS)



# ====================================================================================
# CREAZIONE DELLA LIBRERIA STATICA
# ====================================================================================

# Regola che crea la libreria statica usando tutti gli object file contenuti in $(OBJ).
#
# "ar" serve per creare librerie statiche.
#
# r = sostituisce i file già presenti
#
# c = crea l'archivio se non esiste
#
# s = crea indice dei simboli (aiuta il linker a trovare velocemente i simboli della libreria.)
#
# $@ = target corrente -> libmr.a
#
# $^ = tutte le dipendenze -> lista di tutti i .o

$(LIB): $(OBJ)
	ar rcs $@ $^



# ====================================================================================
# COMPILAZIONE FILE OBJECT (.o)
# ====================================================================================

# Pattern rule generica.
#
# $(CC) -> gcc
#
# $(CFLAGS) -> tutti i flag definiti sopra
#
# -c compila senza effettuare linking
#
# $< -> file .c sorgente
#
# -o $@ -> file .o finale

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@



# ====================================================================================
# CREAZIONE ESEGUIBILI DI TEST
# ====================================================================================

# Regola generica per creare gli eseguibili finali.
#
# $< -> file sorgente del test
#
# -L. aggiunge la directory corrente ai path delle librerie
#
# -lmr linka la libreria "libmr.a"
#
# -o $@ nome eseguibile finale

%: $(EX_DIR)/%.c $(LIB)
	$(CC) $(CFLAGS) $< -L. -lmr -o $@



# ====================================================================================
# ESECUZIONE AUTOMATICA TEST
# ====================================================================================

# Target che esegue automaticamente tutti i test.
#
# 1.Dipende da "all" quindi prima compila tutto il progetto.
# 2.Stampa il messaggio.
# 3.Ciclo shell che esegue tutti gli eseguibili.
# 	uso doppio "$" perché make usa "$" per le sue variabili "$$" viene trasformato in "$" nella shell.
# 4.Stampa solo se tutti i test terminano.
test: all

	@echo "Test di $(TESTS)"

	@for t in $(TEST_BINS); do \
		echo "-> $$t in esecuzione"; \
		./$$t || exit 1; \
	done

	@echo "Tutti i test sono terminati"



# ====================================================================================
# PULIZIA FILE GENERATI
# ====================================================================================

# Target usato per eliminare tutti i file generati automaticamente.
#
# rm -f rimuove i file senza chiedere conferma.
#
# Vengono eliminati:
# $(SRC_DIR)/*.o -> object file dentro src/
#
# *.o -> eventuali object file nella root
#
# $(LIB) -> libreria statica libmr.a
#
# $(TEST_BINS) -> tutti gli eseguibili di test
#
# logs/*.log -> file di log presenti in logs
#
# results/*.mro -> file output presenti in results

clean:
	rm -f $(SRC_DIR)/*.o *.o $(LIB) $(TEST_BINS) logs/*.log results/*.mro