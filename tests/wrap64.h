#ifndef _WRAP64_H
#define _WRAP64_H 1
#include <syscall.h>
#include <linux/types.h>
#include "util.h"

/*
 * Make cmocka work with _FILE_OFFSET_BITS == 64
 *
 * If -D_FILE_OFFSET_BITS=64 is set, glibc headers replace some functions with
 * their 64bit equivalents. "open()" is replaced by "open64()", etc. cmocka
 * wrappers for these functions must have correct name __wrap_open() doesn't
 * work if the function called is not open() but open64(). Consequently, unit
 * tests using such wrappers will fail.
 * Use some CPP trickery to insert the correct name. The Makefile rule that
 * creates the .wrap file must parse the C preprocessor output to generate the
 * correct -Wl,wrap= option.
 */

/* Without this indirection, WRAP_FUNC(x) would be expanded to __wrap_WRAP_NAME(x) */
#define CONCAT2_(x, y) x ## y
#define CONCAT2(x, y) CONCAT2_(x, y)

#if defined(__GLIBC__) && _FILE_OFFSET_BITS == 64
#define WRAP_NAME(x) x ## 64
#else
#define WRAP_NAME(x) x
#endif
#define WRAP_FUNC(x) CONCAT2(__wrap_, WRAP_NAME(x))
#define REAL_FUNC(x) CONCAT2(__real_, WRAP_NAME(x))

/*
 * With clang, glibc 2.39, and _FILE_OFFSET_BITS==64,
 * open() resolves to __open64_2().
 */
#if defined(__GLIBC__) && __GLIBC_PREREQ(2, 39) && \
	defined(__clang__) && __clang__ == 1 && \
	defined(__fortify_use_clang) && __fortify_use_clang == 1
#define WRAP_OPEN_NAME __open64_2
#else
#define WRAP_OPEN_NAME WRAP_NAME(open)
#endif
#define WRAP_OPEN CONCAT2(__wrap_, WRAP_OPEN_NAME)
#define REAL_OPEN CONCAT2(__real_, WRAP_OPEN_NAME)

/*
 * fcntl() needs special treatment; fcntl64() has been introduced in 2.28.
 * https://savannah.gnu.org/forum/forum.php?forum_id=9205
 */
#if defined(__GLIBC__) && __GLIBC_PREREQ(2, 37) && defined(__arm__) && __ARM_ARCH == 7
#define WRAP_FCNTL_NAME __fcntl_time64
#elif defined(__GLIBC__) && __GLIBC_PREREQ(2, 28)
#define WRAP_FCNTL_NAME WRAP_NAME(fcntl)
#else
#define WRAP_FCNTL_NAME fcntl
#endif
#define WRAP_FCNTL CONCAT2(__wrap_, WRAP_FCNTL_NAME)
#define REAL_FCNTL CONCAT2(__real_, WRAP_FCNTL_NAME)

/*
 * glibc 2.37 uses __ioctl_time64 for ioctl
 */
#if defined(__GLIBC__) && __GLIBC_PREREQ(2, 37) && defined(__arm__) && __ARM_ARCH == 7
#define WRAP_IOCTL_NAME __ioctl_time64
#else
#define WRAP_IOCTL_NAME ioctl
#endif
#define WRAP_IOCTL CONCAT2(__wrap_, WRAP_IOCTL_NAME)
#define REAL_IOCTL CONCAT2(__real_, WRAP_IOCTL_NAME)

#if defined(__NR_io_pgetevents) && __BITS_PER_LONG == 32 && defined(_TIME_BITS) && _TIME_BITS == 64
#define WRAP_IO_GETEVENTS_NAME io_getevents_time64
#else
#define WRAP_IO_GETEVENTS_NAME io_getevents
#endif
#define WRAP_IO_GETEVENTS CONCAT2(__wrap_, WRAP_IO_GETEVENTS_NAME)
#define REAL_IO_GETEVENTS CONCAT2(__real_, WRAP_IO_GETEVENTS_NAME)

/*
 * will_return() is itself a macro that uses CPP "stringizing". We need a
 * macro indirection to make sure the *value* of WRAP_FUNC() is stringized
 * (see https://gcc.gnu.org/onlinedocs/cpp/Stringizing.html).
 */
#define wrap_will_return(x, y) will_return(x, y)
#endif
