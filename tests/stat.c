/* SPDX-License-Identifier: Zlib */
#include <errno.h>
#include <stdint.h>
#include <string.h>

#include "ninefold.h"

#include "check.h"
#include "demo.h"
#include "recorder.h"

#define POS_SIZE (3 * sizeof(float))

struct world {
	struct nine_tree *t;
	struct demo *d;
	struct nine_ns *ns;
};

/* A tree with a demo instance at /world, holding @n entities. */
static void world_init(struct world *w, int n)
{
	uint32_t id;
	int i;

	w->t = nine_tree_new();
	w->d = demo_new();
	CHECK_PTR(w->t, !=, NULL);
	CHECK_PTR(w->d, !=, NULL);
	for (i = 0; i < n; i++)
		CHECK_INT(demo_create(w->d, &id), ==, 0);
	CHECK_INT(nine_mount(w->t, "/world", demo_ops(), w->d), ==, 0);
	w->ns = nine_tree_ns(w->t);
}

static void world_free(struct world *w)
{
	nine_tree_free(w->t);
	demo_free(w->d);
}

static void check_stat(struct world *w, const char *path, enum nine_kind kind,
		       uint64_t size)
{
	struct nine_handle h;
	struct nine_stat st;

	CHECK_INT(nine_open(w->ns, path, NINE_RDONLY, &h), ==, 0);
	CHECK_INT(nine_stat(&h, &st), ==, 0);
	CHECK_INT(st.kind, ==, kind);
	CHECK_UINT(st.size, ==, size);
}

static void test_demo_kinds_and_sizes(void)
{
	struct world w;

	world_init(&w, 3);
	check_stat(&w, "/world", NINE_DIR, 0);
	check_stat(&w, "/world/components", NINE_DIR, 0);
	check_stat(&w, "/world/components/transform", NINE_DIR, 0);
	check_stat(&w, "/world/components/transform/pos", NINE_FILE,
		   3 * POS_SIZE);
	check_stat(&w, "/world/components/transform/ids", NINE_FILE,
		   3 * sizeof(uint32_t));
	check_stat(&w, "/world/entities", NINE_DIR, 0);
	check_stat(&w, "/world/entities/2", NINE_DIR, 0);
	check_stat(&w, "/world/entities/2/components", NINE_DIR, 0);
	check_stat(&w, "/world/entities/2/components/transform", NINE_DIR, 0);
	check_stat(&w, "/world/entities/2/components/transform/pos", NINE_FILE,
		   POS_SIZE);
	world_free(&w);
}

/* The bulk files of an empty column are empty. */
static void test_demo_empty_column(void)
{
	struct world w;

	world_init(&w, 0);
	check_stat(&w, "/world/components/transform/pos", NINE_FILE, 0);
	check_stat(&w, "/world/components/transform/ids", NINE_FILE, 0);
	world_free(&w);
}

static void test_demo_stale(void)
{
	struct world w;
	struct nine_handle h;
	struct nine_stat st;

	world_init(&w, 2);
	CHECK_INT(nine_open(w.ns, "/world/entities/1", NINE_RDONLY, &h), ==, 0);
	CHECK_INT(demo_destroy(w.d, 1), ==, 0);
	CHECK_INT(nine_stat(&h, &st), ==, -ESTALE);
	world_free(&w);
}

/* Stat asks for no access, so a handle in any mode can make it. */
static void test_any_mode(void)
{
	struct world w;
	struct nine_handle h;
	struct nine_stat st;

	world_init(&w, 1);
	CHECK_INT(nine_open(w.ns, "/world/entities/0", NINE_WRONLY, &h), ==, 0);
	CHECK_INT(nine_stat(&h, &st), ==, 0);
	CHECK_INT(st.kind, ==, NINE_DIR);
	world_free(&w);
}

static void test_zero_handle(void)
{
	struct nine_handle h;
	struct nine_stat st;

	memset(&h, 0, sizeof(h));
	CHECK_INT(nine_stat(&h, &st), ==, -EBADF);
}

/* The recorder's stat writes nothing, so what it leaves is the framework's. */
static void test_zeroed_before_call(void)
{
	struct nine_tree *t;
	struct nine_handle h;
	struct nine_stat st;
	struct recorder r;

	recorder_init(&r);
	t = nine_tree_new();
	CHECK_PTR(t, !=, NULL);
	CHECK_INT(nine_mount(t, "/rec", &r.ops, &r), ==, 0);
	CHECK_INT(nine_open(nine_tree_ns(t), "/rec/f", NINE_RDONLY, &h), ==, 0);

	memset(&st, 0xff, sizeof(st));
	CHECK_INT(nine_stat(&h, &st), ==, 0);
	CHECK_INT(st.kind, ==, 0);
	CHECK_UINT(st.size, ==, 0);

	CHECK_UINT(r.ncalls, ==, 2);
	CHECK_INT(r.calls[1].op, ==, RECORDER_STAT);
	CHECK_UINT(r.calls[1].node.w[0], ==, 1);
	nine_tree_free(t);
}

int main(void)
{
	test_demo_kinds_and_sizes();
	test_demo_empty_column();
	test_demo_stale();
	test_any_mode();
	test_zero_handle();
	test_zeroed_before_call();
	return 0;
}
