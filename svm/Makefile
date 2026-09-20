# smo   - train a weighted binary SVM from an SQLite training database
# score - classify a problem with a trained model

VERSION = 0.1

# Point these at whichever SQLite you want to link against.
SQLITEINC =
SQLITELIB = -lsqlite3

CC = cc
CFLAGS = -std=c99 -pedantic -Wall -Wextra -Os $(SQLITEINC)
LDFLAGS = $(SQLITELIB) -lm

COMMON = src/db.c src/kernel.c src/platt.c src/solver.c src/util.c
SRC = src/score.c src/smo.c $(COMMON)
HDR = src/db.h src/kernel.h src/platt.h src/prob.h src/solver.h src/util.h
OBJ = $(SRC:.c=.o)
COMMONOBJ = $(COMMON:.c=.o)

# The default target compiles only.  Run `make bin` to link both
# programs, which leaves them in this directory, where the scripts look.
all: $(OBJ)

$(OBJ): $(HDR)

bin: smo score

smo: src/smo.o $(COMMONOBJ)
	$(CC) -o $@ src/smo.o $(COMMONOBJ) $(LDFLAGS)

score: src/score.o $(COMMONOBJ)
	$(CC) -o $@ src/score.o $(COMMONOBJ) $(LDFLAGS)

fmt:
	clang-format -i --style=file $(SRC) $(HDR)

clean:
	rm -f smo score $(OBJ)

.PHONY: all bin fmt clean
