#ifndef PARSE_PATH_H
#define PARSE_PATH_H

#define MAXDBNAME 200
#define MAXCOLLNAME 200

typedef struct {
	char dbname[MAXDBNAME];
	char collname[MAXCOLLNAME];
} path_t;

int parse_path(const char *paths, path_t *newpath, int *dbstart,
    int *collstart);

#endif
