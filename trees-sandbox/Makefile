# itree - unsupervised binary partition of a feature space

VERSION = 0.1

CC = cc
CFLAGS = -std=c99 -pedantic -Wall -Wextra -Os
LDFLAGS = -lm

BIN = itree
SRC = src/csv.c src/itree.c src/tree.c src/util.c
HDR = src/csv.h src/tree.h src/util.h
OBJ = $(SRC:.c=.o)

# The default target compiles only.  Run `make $(BIN)` to link, which
# leaves the binary in this directory.
all: $(OBJ)

$(OBJ): $(HDR)

$(BIN): $(OBJ)
	$(CC) -o $@ $(OBJ) $(LDFLAGS)

fmt:
	clang-format -i --style=file $(SRC) $(HDR)

clean:
	rm -f $(BIN) $(OBJ)

.PHONY: all fmt clean
