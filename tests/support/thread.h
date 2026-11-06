/* SPDX-License-Identifier: Zlib */
#ifndef THREAD_H
#define THREAD_H

/*
 * A small thread helper for the tests, over POSIX threads, or Win32 threads
 * on Windows. It does not use <threads.h>, which not every C library has.
 */

#ifndef _WIN32
#include <pthread.h>
#endif

/*
 * struct test_thread - A running thread.
 *
 * The caller owns it, and it must stay in place from test_thread_start()
 * until test_thread_join() returns.
 */
struct test_thread {
#ifdef _WIN32
	void *handle;
#else
	pthread_t id;
#endif
	void (*fn)(void *arg);
	void *arg;
};

/*
 * test_thread_start() - Start a thread that calls fn(arg).
 *
 * Return: 0, or a negative error code if the thread could not start.
 */
int test_thread_start(struct test_thread *t, void (*fn)(void *arg), void *arg);

/*
 * test_thread_join() - Wait for a thread to return from its function.
 *
 * Return: 0, or a negative error code if the wait failed.
 */
int test_thread_join(struct test_thread *t);

#endif /* THREAD_H */
