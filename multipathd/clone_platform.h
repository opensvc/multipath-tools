/*
 * This file is copied from the LTP project. Thanks.
 * It is covered by the GPLv2, see the LICENSE file
 */
#define CHILD_STACK_SIZE 16384

#if defined (__s390__) || (__s390x__)
#define clone __clone
extern int __clone(int(void*),void*,int,void*);
#elif defined(__ia64__)
#define clone2 __clone2
extern int  __clone2(int (*fn) (void *arg), void *child_stack_base, 
		     size_t child_stack_size, int flags, void *arg, 
		     pid_t *parent_tid, void *tls, pid_t *child_tid); 
#endif
