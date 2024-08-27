#ifndef MSORT_H_INCLUDED
#define MSORT_H_INCLUDED
typedef int(*__compar_fn_t)(const void *, const void *);
void msort (void *b, size_t n, size_t s, __compar_fn_t cmp);

#endif
