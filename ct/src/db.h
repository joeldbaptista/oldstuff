/*
** Reading the training set and writing the tree back.
*/

#ifndef DB_H
#define DB_H

#include <sqlite3.h>

#include "data.h"
#include "tree.h"

sqlite3 *dbopen(const char *path);
void dbexec(sqlite3 *db, const char *sql);
int dbonlydsid(sqlite3 *db);
void dbload(sqlite3 *db, int dsid, Data *d);
void dbsavetree(const char *path, const Tree *t, int force);

#endif /* DB_H */
