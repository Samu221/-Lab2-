# ===== Compiler =====
CC = gcc
CFLAGS = -Iinclude -Wall -Wextra -std=c11 -g

# ===== Directories =====
SRC_DIR = src
EX_DIR = examples

# ===== Sources =====
SRC = $(SRC_DIR)/mr.c \
      $(SRC_DIR)/mapper.c \
      $(SRC_DIR)/reducer.c \
      $(SRC_DIR)/queue.c \
      $(SRC_DIR)/io.c

OBJ = $(SRC:.c=.o)

# ===== Library =====
LIB = libmr.a

# ===== Tests =====
TESTS = inverted_index word_count line_count

TEST_SRCS = $(addprefix $(EX_DIR)/, $(addsuffix .c, $(TESTS)))
TEST_BINS = $(TESTS)

# ===== Default target =====
all: $(LIB) $(TEST_BINS)

# ===== Build static library =====
$(LIB): $(OBJ)
	ar rcs $@ $^

# ===== Compile object files =====
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# ===== Build test executables =====
%: $(EX_DIR)/%.c $(LIB)
	$(CC) $(CFLAGS) $< -L. -lmr -o $@

# ===== Run test suite =====
test: all
	@echo "Running MapReduce test suite..."
	@for t in $(TEST_BINS); do \
		echo "==> Running $$t"; \
		./$$t || exit 1; \
	done
	@echo "All tests passed!"

# ===== Clean =====
clean:
	rm -f $(SRC_DIR)/*.o *.o $(LIB) $(TEST_BINS) mr.log output*.mro