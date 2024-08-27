#ifndef INIT_UNWINDER_H_INCLUDED
#define INIT_UNWINDER_H_INCLUDED

/*
 * init_unwinder(): make sure unwinder symbols are loaded
 *
 * libc's implementation of pthread_cancel() loads symbols from
 * libgcc_s.so using dlopen() when pthread_cancel() is called
 * for the first time. This happens even with LD_BIND_NOW=1.
 * This may imply the need for file system access when a thread is
 * cancelled, which in the case of multipath-tools might be in a
 * dangerous situation where multipathd must avoid blocking.
 *
 * Call load_unwinder() during startup to make sure the dynamic
 * linker has all necessary symbols resolved early on.
 *
 * Return: 0 if successful, an error number otherwise.
 */
int init_unwinder(void);

#endif
