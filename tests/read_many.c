/* SPDX-License-Identifier: Zlib */
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ninefold.h"

#include "check.h"
#include "demo.h"
#include "recorder.h"

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

#define NENTITIES 6

#define POS_SIZE (3 * sizeof(float))

struct world {
	struct nine_tree *t;
	struct demo *d;
	struct nine_ns *ns;
	uint32_t ids[NENTITIES];
	struct nine_handle hs[NENTITIES];
};

/* The position entity number @i of a test starts at. */
static void start_pos(int i, float pos[3])
{
	pos[0] = (float)i + 0.5f;
	pos[1] = (float)i * -10.0f;
	pos[2] = (float)i * 100.0f + 0.25f;
}

static void open_entity_pos(struct nine_ns *ns, uint32_t id,
			    struct nine_handle *h)
{
	char path[64];

	snprintf(path, sizeof(path),
		 "/world/entities/%lu/components/transform/pos",
		 (unsigned long)id);
	CHECK_INT(nine_open(ns, path, NINE_RDONLY, h), ==, 0);
}

/*
 * A tree with a demo instance at /world, mounted with @ops, holding
 * NENTITIES entities in the order of ids[], entity i at start_pos(i), and
 * hs[i] a handle to the pos file of entity i.
 */
static void world_init(struct world *w, const struct nine_file_server_ops *ops)
{
	float pos[3];
	int i;

	w->t = nine_tree_new();
	w->d = demo_new();
	CHECK_PTR(w->t, !=, NULL);
	CHECK_PTR(w->d, !=, NULL);

	for (i = 0; i < NENTITIES; i++) {
		CHECK_INT(demo_create(w->d, &w->ids[i]), ==, 0);
		start_pos(i, pos);
		CHECK_INT(demo_set_pos(w->d, w->ids[i], pos), ==, 0);
	}
	CHECK_INT(nine_mount(w->t, "/world", ops, w->d), ==, 0);
	w->ns = nine_tree_ns(w->t);

	for (i = 0; i < NENTITIES; i++)
		open_entity_pos(w->ns, w->ids[i], &w->hs[i]);
}

static void world_free(struct world *w)
{
	nine_tree_free(w->t);
	demo_free(w->d);
}

/* A batch of entity pos handles reads what a read through each one reads. */
static void
test_same_bytes_as_single_reads(const struct nine_file_server_ops *ops)
{
	struct world w;
	unsigned char got[NENTITIES][POS_SIZE], want[NENTITIES][POS_SIZE];
	int errs[NENTITIES];
	int i;

	world_init(&w, ops);
	for (i = 0; i < NENTITIES; i++)
		CHECK_INT(nine_read(&w.hs[i], want[i], POS_SIZE, 0), ==,
			  (ptrdiff_t)POS_SIZE);

	memset(errs, 0x5a, sizeof(errs));
	CHECK_INT(nine_read_many(w.hs, NENTITIES, got, POS_SIZE, errs), ==, 0);
	CHECK_MEM(got, want, sizeof(got));
	for (i = 0; i < NENTITIES; i++)
		CHECK_INT(errs[i], ==, 0);

	world_free(&w);
}

/*
 * One destroyed entity fails only its own item, which keeps its part of the
 * buffer, and the call returns its error.
 */
static void test_destroyed_entity(const struct nine_file_server_ops *ops)
{
	struct world w;
	float got[NENTITIES][3], want[3];
	unsigned char untouched[POS_SIZE];
	int errs[NENTITIES];
	int i;

	world_init(&w, ops);
	CHECK_INT(demo_destroy(w.d, w.ids[2]), ==, 0);

	memset(got, 0xa5, sizeof(got));
	memset(untouched, 0xa5, sizeof(untouched));
	CHECK_INT(nine_read_many(w.hs, NENTITIES, got, POS_SIZE, errs), ==,
		  -ESTALE);

	for (i = 0; i < NENTITIES; i++) {
		if (i == 2) {
			CHECK_INT(errs[i], ==, -ESTALE);
			CHECK_MEM(got[i], untouched, POS_SIZE);
			continue;
		}
		CHECK_INT(errs[i], ==, 0);
		start_pos(i, want);
		CHECK_MEM(got[i], want, POS_SIZE);
	}

	world_free(&w);
}

/*
 * An item through a write-only or zeroed handle fails with -EBADF, and the
 * call returns the error of the first item that failed.
 */
static void test_first_error_returned(const struct nine_file_server_ops *ops)
{
	static const int ok[] = { 0, 2, 5 };
	struct world w;
	float got[NENTITIES][3], want[3];
	int errs[NENTITIES];
	size_t i;

	world_init(&w, ops);
	CHECK_INT(demo_destroy(w.d, w.ids[4]), ==, 0);
	memset(&w.hs[3], 0, sizeof(w.hs[3]));
	CHECK_INT(nine_open(w.ns, "/world/components/transform/pos",
			    NINE_WRONLY, &w.hs[1]),
		  ==, 0);

	CHECK_INT(nine_read_many(w.hs, NENTITIES, got, POS_SIZE, errs), ==,
		  -EBADF);
	CHECK_INT(errs[1], ==, -EBADF);
	CHECK_INT(errs[3], ==, -EBADF);
	CHECK_INT(errs[4], ==, -ESTALE);
	for (i = 0; i < ARRAY_SIZE(ok); i++) {
		CHECK_INT(errs[ok[i]], ==, 0);
		start_pos(ok[i], want);
		CHECK_MEM(got[ok[i]], want, POS_SIZE);
	}

	CHECK_INT(nine_read_many(w.hs + 2, NENTITIES - 2, got, POS_SIZE, errs),
		  ==, -EBADF);
	CHECK_INT(nine_read_many(w.hs + 4, NENTITIES - 4, got, POS_SIZE, errs),
		  ==, -ESTALE);

	world_free(&w);
}

/* Two recorders, a at /a and b at /b, with every operation. */
struct two_recs {
	struct nine_tree *t;
	struct recorder a, b;
};

static void two_recs_init(struct two_recs *tr)
{
	recorder_init(&tr->a);
	recorder_init(&tr->b);
	tr->t = nine_tree_new();
	CHECK_PTR(tr->t, !=, NULL);
}

static void two_recs_mount(struct two_recs *tr)
{
	CHECK_INT(nine_mount(tr->t, "/a", &tr->a.ops, &tr->a), ==, 0);
	CHECK_INT(nine_mount(tr->t, "/b", &tr->b.ops, &tr->b), ==, 0);
}

/*
 * Open the batch A A B B A, where the handle of index i is i + 1 names deep,
 * so a recorded node tells which handle it came from. Then forget the
 * lookups.
 */
static void open_mixed_batch(struct two_recs *tr, struct nine_handle hs[5])
{
	static const char *const paths[] = {
		"/a/1", "/a/1/2", "/b/1/2/3", "/b/1/2/3/4", "/a/1/2/3/4/5",
	};
	size_t i;

	for (i = 0; i < ARRAY_SIZE(paths); i++)
		CHECK_INT(nine_open(nine_tree_ns(tr->t), paths[i], NINE_RDONLY,
				    &hs[i]),
			  ==, 0);
	tr->a.ncalls = 0;
	tr->b.ncalls = 0;
}

static void check_call(const struct recorder_call *c, enum recorder_op op,
		       uint64_t depth)
{
	CHECK_INT(c->op, ==, op);
	CHECK_UINT(c->node.w[0], ==, depth);
}

/* Check a read_many call of @len handles, the first @depth names deep. */
static void check_run(const struct recorder_call *c, uint64_t depth, size_t len)
{
	check_call(c, RECORDER_READ_MANY, depth);
	CHECK_UINT(c->len, ==, len);
}

/*
 * A batch A A B B A makes one read_many call for each run of one mount in a
 * row, given the first handle of the run. The recorder reads no bytes, so
 * the values are 0 bytes long.
 */
static void test_one_call_per_run(void)
{
	struct two_recs tr;
	struct nine_handle hs[5];
	char buf[1];
	int errs[5];

	two_recs_init(&tr);
	two_recs_mount(&tr);
	open_mixed_batch(&tr, hs);

	CHECK_INT(nine_read_many(hs, 5, buf, 0, errs), ==, 0);
	CHECK_UINT(tr.a.ncalls, ==, 2);
	CHECK_UINT(tr.b.ncalls, ==, 1);
	check_run(&tr.a.calls[0], 1, 2);
	check_run(&tr.b.calls[0], 3, 2);
	check_run(&tr.a.calls[1], 5, 1);

	nine_tree_free(tr.t);
}

/* A file server with no read_many gets a read for each handle instead. */
static void test_read_many_fallback(void)
{
	struct two_recs tr;
	struct nine_handle hs[5];
	char buf[1];
	int errs[5];

	two_recs_init(&tr);
	tr.a.ops.read_many = NULL;
	two_recs_mount(&tr);
	open_mixed_batch(&tr, hs);

	CHECK_INT(nine_read_many(hs, 5, buf, 0, errs), ==, 0);
	CHECK_UINT(tr.a.ncalls, ==, 3);
	CHECK_UINT(tr.b.ncalls, ==, 1);
	check_call(&tr.a.calls[0], RECORDER_READ, 1);
	check_call(&tr.a.calls[1], RECORDER_READ, 2);
	check_call(&tr.a.calls[2], RECORDER_READ, 5);
	check_run(&tr.b.calls[0], 3, 2);

	nine_tree_free(tr.t);
}

/*
 * A handle that is not open for reading never reaches the file server, so
 * it ends a run.
 */
static void test_bad_handle_ends_run(void)
{
	struct two_recs tr;
	struct nine_handle hs[5];
	char buf[1];
	int errs[5];

	two_recs_init(&tr);
	two_recs_mount(&tr);
	open_mixed_batch(&tr, hs);
	hs[1] = hs[0];
	CHECK_INT(nine_open(nine_tree_ns(tr.t), "/a/1/2", NINE_WRONLY, &hs[0]),
		  ==, 0);
	memset(&hs[3], 0, sizeof(hs[3]));
	tr.a.ncalls = 0;

	CHECK_INT(nine_read_many(hs, 5, buf, 0, errs), ==, -EBADF);
	CHECK_INT(errs[0], ==, -EBADF);
	CHECK_INT(errs[1], ==, 0);
	CHECK_INT(errs[2], ==, 0);
	CHECK_INT(errs[3], ==, -EBADF);
	CHECK_INT(errs[4], ==, 0);
	CHECK_UINT(tr.a.ncalls, ==, 2);
	CHECK_UINT(tr.b.ncalls, ==, 1);
	check_run(&tr.a.calls[0], 1, 1);
	check_run(&tr.b.calls[0], 3, 1);
	check_run(&tr.a.calls[1], 5, 1);

	nine_tree_free(tr.t);
}

/* An empty batch calls no file server and succeeds. */
static void test_empty_batch(void)
{
	struct two_recs tr;

	two_recs_init(&tr);
	two_recs_mount(&tr);

	CHECK_INT(nine_read_many(NULL, 0, NULL, 4, NULL), ==, 0);
	CHECK_UINT(tr.a.ncalls, ==, 0);
	CHECK_UINT(tr.b.ncalls, ==, 0);

	nine_tree_free(tr.t);
}

/* An item through a handle to a directory fails with the demo's -EISDIR. */
static void test_directory_item(const struct nine_file_server_ops *ops)
{
	struct world w;
	float got[NENTITIES][3];
	int errs[NENTITIES];

	world_init(&w, ops);
	CHECK_INT(nine_open(w.ns, "/world/entities", NINE_RDONLY, &w.hs[3]), ==,
		  0);

	CHECK_INT(nine_read_many(w.hs, NENTITIES, got, POS_SIZE, errs), ==,
		  -EISDIR);
	CHECK_INT(errs[2], ==, 0);
	CHECK_INT(errs[3], ==, -EISDIR);
	CHECK_INT(errs[4], ==, 0);

	world_free(&w);
}

/*
 * The demo's own read_many refuses a size other than the size of a file
 * with -EINVAL, and leaves every part of the buffer as it was.
 */
static void test_wrong_size(void)
{
	static const size_t sizes[] = { POS_SIZE - 4, POS_SIZE + 4 };
	struct world w;
	unsigned char got[NENTITIES][POS_SIZE + 4];
	unsigned char untouched[sizeof(got)];
	int errs[NENTITIES];
	size_t i;
	int j;

	world_init(&w, demo_ops());
	memset(untouched, 0xa5, sizeof(untouched));
	for (i = 0; i < ARRAY_SIZE(sizes); i++) {
		memset(got, 0xa5, sizeof(got));
		CHECK_INT(nine_read_many(w.hs, NENTITIES, got, sizes[i], errs),
			  ==, -EINVAL);
		for (j = 0; j < NENTITIES; j++)
			CHECK_INT(errs[j], ==, -EINVAL);
		CHECK_MEM(got, untouched, sizeof(got));
	}

	world_free(&w);
}

/*
 * Without a read_many of the file server, a read shorter than the size asked
 * for fails with -EINVAL.
 */
static void test_short_read_fallback(const struct nine_file_server_ops *ops)
{
	struct world w;
	unsigned char got[NENTITIES][POS_SIZE + 4];
	int errs[NENTITIES];
	int i;

	world_init(&w, ops);
	CHECK_INT(nine_read_many(w.hs, NENTITIES, got, POS_SIZE + 4, errs), ==,
		  -EINVAL);
	for (i = 0; i < NENTITIES; i++)
		CHECK_INT(errs[i], ==, -EINVAL);

	world_free(&w);
}

static void run_demo_tests(const struct nine_file_server_ops *ops)
{
	test_directory_item(ops);
	test_same_bytes_as_single_reads(ops);
	test_destroyed_entity(ops);
	test_first_error_returned(ops);
}

int main(void)
{
	struct nine_file_server_ops no_read_many = *demo_ops();

	no_read_many.read_many = NULL;

	run_demo_tests(demo_ops());
	run_demo_tests(&no_read_many);
	test_wrong_size();
	test_short_read_fallback(&no_read_many);
	test_one_call_per_run();
	test_read_many_fallback();
	test_bad_handle_ends_run();
	test_empty_batch();
	return 0;
}
