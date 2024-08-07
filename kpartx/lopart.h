#ifndef LOPART_H_INCLUDED
#define LOPART_H_INCLUDED
extern int verbose;
extern int set_loop (char **, const char *, int, int *);
extern int del_loop (const char *);
extern char * find_loop_by_file (const char *);
#endif
