/* SPDX-License-Identifier: Zlib */
#include "ninefold.h"

#include "check.h"
#include "thread.h"

#define NTHREADS 4
#define NROUNDS 100

/* Each thread makes and frees its own trees, at the same time as others. */
static void make_trees(void *arg)
{
	int *made = arg;
	struct nine_tree *t;
	int i;

	for (i = 0; i < NROUNDS; i++) {
		t = nine_tree_new();
		if (!t)
			continue;
		if (nine_tree_ns(t))
			(*made)++;
		nine_tree_free(t);
	}
}

int main(void)
{
	struct test_thread threads[NTHREADS];
	int made[NTHREADS] = { 0 };
	int i;

	for (i = 0; i < NTHREADS; i++)
		CHECK_INT(test_thread_start(&threads[i], make_trees, &made[i]),
			  ==, 0);
	for (i = 0; i < NTHREADS; i++)
		CHECK_INT(test_thread_join(&threads[i]), ==, 0);
	for (i = 0; i < NTHREADS; i++)
		CHECK_INT(made[i], ==, NROUNDS);
	return 0;
}
