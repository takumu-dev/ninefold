/* SPDX-License-Identifier: Zlib */
#include <errno.h>
#include <string.h>

#include "ninefold.h"

#include "check.h"
#include "recorder.h"

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

struct rec_tree {
	struct nine_tree *t;
	struct nine_ns *ns;
	struct recorder r;
};

static void rec_tree_init(struct rec_tree *rt)
{
	recorder_init(&rt->r);
	rt->t = nine_tree_new();
	CHECK_PTR(rt->t, !=, NULL);
	CHECK_INT(nine_mount(rt->t, "/rec", &rt->r.ops, &rt->r), ==, 0);
	rt->ns = nine_tree_ns(rt->t);
}

static void check_zero(const struct nine_handle *h)
{
	static const struct nine_handle zero;

	CHECK_MEM(h, &zero, sizeof(*h));
}

/* Fill a handle with bytes that a failed open must not leave behind. */
static void scribble(struct nine_handle *h)
{
	memset(h, 0xa5, sizeof(*h));
}

static void test_invalid_modes(void)
{
	static const int modes[] = { 0, 4, 7, -1, NINE_RDWR + 1 };
	struct rec_tree rt;
	struct nine_handle h;
	size_t i;

	rec_tree_init(&rt);
	for (i = 0; i < ARRAY_SIZE(modes); i++) {
		scribble(&h);
		CHECK_INT(nine_open(rt.ns, "/rec/f", modes[i], &h), ==,
			  -EINVAL);
		check_zero(&h);
	}
	CHECK_UINT(rt.r.ncalls, ==, 0);
	nine_tree_free(rt.t);
}

static void test_valid_modes(void)
{
	struct rec_tree rt;
	struct nine_handle h;
	char buf[4];

	rec_tree_init(&rt);
	CHECK_INT(nine_open(rt.ns, "/rec/f", NINE_RDONLY, &h), ==, 0);
	CHECK_INT(nine_read(&h, buf, sizeof(buf), 0), ==, 0);
	CHECK_INT(nine_open(rt.ns, "/rec/f", NINE_RDWR, &h), ==, 0);
	CHECK_INT(nine_read(&h, buf, sizeof(buf), 0), ==, 0);
	CHECK_INT(nine_open(rt.ns, "/rec/f", NINE_WRONLY, &h), ==, 0);
	CHECK_UINT(rt.r.ncalls, ==, 5);
	CHECK_INT(rt.r.calls[1].op, ==, RECORDER_READ);
	CHECK_INT(rt.r.calls[3].op, ==, RECORDER_READ);
	nine_tree_free(rt.t);
}

/* A read through a write-only handle never reaches the file server. */
static void test_read_write_only(void)
{
	struct rec_tree rt;
	struct nine_handle h;
	char buf[4];

	rec_tree_init(&rt);
	CHECK_INT(nine_open(rt.ns, "/rec/f", NINE_WRONLY, &h), ==, 0);
	rt.r.ncalls = 0;
	CHECK_INT(nine_read(&h, buf, sizeof(buf), 0), ==, -EBADF);
	CHECK_UINT(rt.r.ncalls, ==, 0);
	nine_tree_free(rt.t);
}

static void test_zero_handle(void)
{
	static const struct nine_handle zero;
	struct nine_handle h;
	char buf[4];

	memset(&h, 0, sizeof(h));
	CHECK_INT(nine_read(&h, buf, sizeof(buf), 0), ==, -EBADF);
	CHECK_INT(nine_read(&zero, buf, sizeof(buf), 0), ==, -EBADF);
}

/* Every failed open leaves an all-zero handle, whatever the cause. */
static void test_failed_open_zeroes(void)
{
	struct rec_tree rt;
	struct nine_handle h;

	rec_tree_init(&rt);

	scribble(&h);
	CHECK_INT(nine_open(rt.ns, "rec/f", NINE_RDONLY, &h), ==, -EINVAL);
	check_zero(&h);

	scribble(&h);
	CHECK_INT(nine_open(rt.ns, "/rec/../f", NINE_RDONLY, &h), ==, -EINVAL);
	check_zero(&h);

	scribble(&h);
	CHECK_INT(nine_open(rt.ns, "/other", NINE_RDONLY, &h), ==, -ENOENT);
	check_zero(&h);

	nine_tree_free(rt.t);
}

/* An error from the file server's lookup also leaves an all-zero handle. */
static int failing_lookup(void *priv, const struct nine_node *dir,
			  const char *name, size_t len, struct nine_node *out)
{
	(void)priv;
	(void)dir;
	(void)name;
	(void)len;
	memset(out, 0x5a, sizeof(*out));
	return -ENOTDIR;
}

static void test_lookup_error_zeroes(void)
{
	struct rec_tree rt;
	struct nine_handle h;
	char buf[4];

	recorder_init(&rt.r);
	rt.r.ops.lookup = failing_lookup;
	rt.t = nine_tree_new();
	CHECK_PTR(rt.t, !=, NULL);
	CHECK_INT(nine_mount(rt.t, "/rec", &rt.r.ops, &rt.r), ==, 0);

	scribble(&h);
	CHECK_INT(nine_open(nine_tree_ns(rt.t), "/rec/a/b", NINE_RDWR, &h), ==,
		  -ENOTDIR);
	check_zero(&h);
	CHECK_INT(nine_read(&h, buf, sizeof(buf), 0), ==, -EBADF);

	nine_tree_free(rt.t);
}

int main(void)
{
	test_invalid_modes();
	test_valid_modes();
	test_read_write_only();
	test_zero_handle();
	test_failed_open_zeroes();
	test_lookup_error_zeroes();
	return 0;
}
