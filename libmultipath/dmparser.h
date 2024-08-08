#ifndef DMPARSER_H_INCLUDED
#define DMPARSER_H_INCLUDED

int assemble_map (struct multipath *, char **);
int disassemble_map (const struct vector_s *, const char *, struct multipath *);
int disassemble_status (const char *, struct multipath *);

#endif
