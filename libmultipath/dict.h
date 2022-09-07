#ifndef _DICT_H
#define _DICT_H

#ifndef _VECTOR_H
#include "vector.h"
#endif

#include "byteorder.h"
struct strbuf;

void init_keywords(vector keywords);
int get_sys_max_fds(int *);
int print_rr_weight(struct strbuf *buff, long v);
int print_pgfailback(struct strbuf *buff, long v);
int print_pgpolicy(struct strbuf *buff, long v);
int print_no_path_retry(struct strbuf *buff, long v);
int print_undef_off_zero(struct strbuf *buff, long v);
int print_dev_loss(struct strbuf *buff, unsigned long v);
int print_off_int_undef(struct strbuf *buff, long v);
#endif /* _DICT_H */
