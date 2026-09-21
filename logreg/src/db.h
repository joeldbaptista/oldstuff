/*
** Reading the training problem and writing the result back.
*/

#ifndef DB_H
#define DB_H

#include <sqlite3.h>

#include "prob.h"
#include "train.h"

sqlite3 *dbopen(const char *path);
void dbexec(sqlite3 *db, const char *sql);
void dbload(sqlite3 *db, int dsid, int pid, Prob *p);
void dbsave(sqlite3 *db, int dsid, const Prob *p, const Fit *f, int *cid,
            int *bid);
void dbloadmodel(sqlite3 *db, int mid, Model *mo);
void dbloadproblem(sqlite3 *db, int prid, int n, Points *q);
void dbsaveyhat(sqlite3 *db, int prid, int mid, const Points *q,
                const double *pos, int replace);

#endif /* DB_H */
