#ifndef __MSORT_H
#define __MSORT_H
typedef int(*__compar_fn_t)(const void *, const void *);
void msort (void *b, size_t n, size_t s, __compar_fn_t cmp);

#endif
