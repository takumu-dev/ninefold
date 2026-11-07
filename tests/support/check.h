/* SPDX-License-Identifier: Zlib */
#ifndef CHECK_H
#define CHECK_H

/*
 * Assertion macros for the tests. A failed check prints where it failed and
 * the values it compared, then ends the test process with a failure code.
 *
 * These are not assert(): NDEBUG does not remove them, so they also run in
 * release builds. Each argument is evaluated exactly once.
 *
 *	CHECK(cond)
 *	CHECK_INT(a, op, b)	signed integers, compared as long long
 *	CHECK_UINT(a, op, b)	unsigned integers, compared as unsigned long long
 *	CHECK_PTR(a, op, b)	pointers, compared as const void *
 *	CHECK_STR(a, b)		NUL-terminated strings are equal
 *	CHECK_MEM(a, b, len)	len bytes at a and b are equal
 *
 * op is a comparison operator, such as == or <. The checks can run on any
 * thread: a failure ends the whole process at once.
 */

#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static inline void check_fail(const char *file, int line, const char *expr,
			      const char *fmt, ...)
{
	va_list ap;

	fprintf(stderr, "%s:%d: check failed: %s\n", file, line, expr);
	if (fmt) {
		fputs("  ", stderr);
		va_start(ap, fmt);
		vfprintf(stderr, fmt, ap);
		va_end(ap);
		fputc('\n', stderr);
	}
	fflush(stderr);
	/* Not exit(): another thread may be running, or failing too. */
	_Exit(EXIT_FAILURE);
}

#define CHECK(cond)                                                  \
	do {                                                         \
		if (!(cond))                                         \
			check_fail(__FILE__, __LINE__, #cond, NULL); \
	} while (0)

#define CHECK_INT(a, op, b)                                               \
	do {                                                              \
		long long check_a = (a);                                  \
		long long check_b = (b);                                  \
		if (!(check_a op check_b))                                \
			check_fail(__FILE__, __LINE__, #a " " #op " " #b, \
				   "%lld vs %lld", check_a, check_b);     \
	} while (0)

#define CHECK_UINT(a, op, b)                                              \
	do {                                                              \
		unsigned long long check_a = (a);                         \
		unsigned long long check_b = (b);                         \
		if (!(check_a op check_b))                                \
			check_fail(__FILE__, __LINE__, #a " " #op " " #b, \
				   "%llu vs %llu", check_a, check_b);     \
	} while (0)

#define CHECK_PTR(a, op, b)                                               \
	do {                                                              \
		const void *check_a = (a);                                \
		const void *check_b = (b);                                \
		if (!(check_a op check_b))                                \
			check_fail(__FILE__, __LINE__, #a " " #op " " #b, \
				   "%p vs %p", check_a, check_b);         \
	} while (0)

#define CHECK_STR(a, b)                                                   \
	do {                                                              \
		const char *check_a = (a);                                \
		const char *check_b = (b);                                \
		if (strcmp(check_a, check_b) != 0)                        \
			check_fail(__FILE__, __LINE__, #a " == " #b,      \
				   "\"%s\" vs \"%s\"", check_a, check_b); \
	} while (0)

static inline void check_mem(const char *file, int line, const char *expr,
			     const void *a, const void *b, size_t len)
{
	const unsigned char *pa = a;
	const unsigned char *pb = b;
	size_t i;

	for (i = 0; i < len; i++) {
		if (pa[i] != pb[i])
			check_fail(file, line, expr,
				   "byte %zu of %zu: 0x%02x vs 0x%02x", i, len,
				   pa[i], pb[i]);
	}
}

#define CHECK_MEM(a, b, len) \
	check_mem(__FILE__, __LINE__, #a " == " #b, (a), (b), (len))

#endif /* CHECK_H */
