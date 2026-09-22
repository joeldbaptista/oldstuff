# build-ct - grow a classification tree from an SQLite training database

VERSION = 0.1

# Point these at whichever SQLite you want to link against.
SQLITEINC =
SQLITELIB = -lsqlite3

CC = cc
CFLAGS = -std=c99 -pedantic -Wall -Wextra -Os $(SQLITEINC)
LDFLAGS = $(SQLITELIB) -lm

SRC = src/build-ct.c src/db.c src/tree.c src/util.c
HDR = src/data.h src/db.h src/tree.h src/util.h
OBJ = $(SRC:.c=.o)

# The default target compiles only.  Run `make bin` to link the
# program, which leaves it in this directory, where the scripts look.
all: $(OBJ)

$(OBJ): $(HDR)

bin: build-ct

build-ct: $(OBJ)
	$(CC) -o $@ $(OBJ) $(LDFLAGS)

fmt:
	clang-format -i --style=file $(SRC) $(HDR)

clean:
	rm -f build-ct $(OBJ)

.PHONY: all bin fmt clean
