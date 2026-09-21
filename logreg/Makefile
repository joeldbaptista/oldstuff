# logreg  - fit a weighted, regularised logistic regression
# predict - classify a problem with a fitted model

VERSION = 0.1

# Point these at whichever SQLite you want to link against.
SQLITEINC =
SQLITELIB = -lsqlite3

CC = cc
CFLAGS = -std=c99 -pedantic -Wall -Wextra -Os $(SQLITEINC)
LDFLAGS = $(SQLITELIB) -lm

COMMON = src/db.c src/train.c src/util.c
SRC = src/logreg.c src/predict.c $(COMMON)
HDR = src/db.h src/prob.h src/train.h src/util.h
OBJ = $(SRC:.c=.o)
COMMONOBJ = $(COMMON:.c=.o)

# The default target compiles only.  Run `make bin` to link both
# programs, which leaves them in this directory, where the scripts look.
all: $(OBJ)

$(OBJ): $(HDR)

bin: logreg predict

logreg: src/logreg.o $(COMMONOBJ)
	$(CC) -o $@ src/logreg.o $(COMMONOBJ) $(LDFLAGS)

predict: src/predict.o $(COMMONOBJ)
	$(CC) -o $@ src/predict.o $(COMMONOBJ) $(LDFLAGS)

fmt:
	clang-format -i --style=file $(SRC) $(HDR)

clean:
	rm -f logreg predict $(OBJ)

.PHONY: all bin fmt clean
