/* SPDX-License-Identifier: Zlib */
#include "thread.h"

#ifdef _WIN32

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <errno.h>
#include <process.h>
#include <stdint.h>

static unsigned __stdcall run(void *p)
{
	struct test_thread *t = p;

	t->fn(t->arg);
	return 0;
}

int test_thread_start(struct test_thread *t, void (*fn)(void *arg), void *arg)
{
	uintptr_t handle;

	t->fn = fn;
	t->arg = arg;
	handle = _beginthreadex(NULL, 0, run, t, 0, NULL);
	if (!handle)
		return -errno;

	t->handle = (void *)handle;
	return 0;
}

int test_thread_join(struct test_thread *t)
{
	DWORD ret;

	ret = WaitForSingleObject(t->handle, INFINITE);
	CloseHandle(t->handle);
	return ret == WAIT_OBJECT_0 ? 0 : -EINVAL;
}

#else

#include <stddef.h>

static void *run(void *p)
{
	struct test_thread *t = p;

	t->fn(t->arg);
	return NULL;
}

int test_thread_start(struct test_thread *t, void (*fn)(void *arg), void *arg)
{
	t->fn = fn;
	t->arg = arg;
	return -pthread_create(&t->id, NULL, run, t);
}

int test_thread_join(struct test_thread *t)
{
	return -pthread_join(t->id, NULL);
}

#endif
