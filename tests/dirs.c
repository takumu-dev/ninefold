/* SPDX-License-Identifier: Zlib */
#include <errno.h>
#include <stdint.h>
#include <string.h>

#include "ninefold.h"

#include "check.h"
#include "listing.h"
#include "recorder.h"

/*
 * A tree with recorders mounted at /a/x, /a/y/z and /b, in that order. The
 * framework serves /, /a and /a/y.
 */
struct mounts {
	struct nine_tree *t;
	struct nine_ns *ns;
	struct recorder r[3];
};

static void mounts_init(struct mounts *ms)
{
	static const char *const paths[] = { "/a/x", "/a/y/z", "/b" };
	int i;

	ms->t = nine_tree_new();
	CHECK_PTR(ms->t, !=, NULL);
	for (i = 0; i < 3; i++) {
		recorder_init(&ms->r[i]);
		CHECK_INT(nine_mount(ms->t, paths[i], &ms->r[i].ops, &ms->r[i]),
			  ==, 0);
	}
	ms->ns = nine_tree_ns(ms->t);
}

/* Free the tree, and check that no recorder got a call. */
static void mounts_free(struct mounts *ms)
{
	int i;

	for (i = 0; i < 3; i++)
		CHECK_UINT(ms->r[i].ncalls, ==, 0);
	nine_tree_free(ms->t);
}

static const char *list_path(struct nine_ns *ns, const char *path, size_t len)
{
	static char names[256];
	struct nine_handle h;

	CHECK_INT(nine_open(ns, path, NINE_RDONLY, &h), ==, 0);
	return list_names(&h, len, names, sizeof(names));
}

/* Each directory lists the next name of each mount path below it, once. */
static void test_list(void)
{
	struct mounts ms;

	mounts_init(&ms);
	CHECK_STR(list_path(ms.ns, "/", 64), "a b ");
	CHECK_STR(list_path(ms.ns, "//", 64), "a b ");
	CHECK_STR(list_path(ms.ns, "/a", 64), "x y ");
	CHECK_STR(list_path(ms.ns, "/a/", 64), "x y ");
	CHECK_STR(list_path(ms.ns, "/a/y", 64), "z ");

	/* One name per call. */
	CHECK_STR(list_path(ms.ns, "/", 2), "a b ");
	CHECK_STR(list_path(ms.ns, "/a", 2), "x y ");
	mounts_free(&ms);
}

static void test_range(void)
{
	struct mounts ms;
	struct nine_handle h;
	uint64_t cursor = 0;
	char buf[8];

	mounts_init(&ms);
	CHECK_INT(nine_open(ms.ns, "/a", NINE_RDONLY, &h), ==, 0);
	CHECK_INT(nine_list(&h, &cursor, buf, 0), ==, -ERANGE);
	CHECK_INT(nine_list(&h, &cursor, buf, 1), ==, -ERANGE);
	CHECK_INT(nine_list(&h, &cursor, buf, 3), ==, 1);
	CHECK_STR(buf, "x");
	CHECK_INT(nine_list(&h, &cursor, buf, 1), ==, -ERANGE);
	CHECK_INT(nine_list(&h, &cursor, buf, sizeof(buf)), ==, 1);
	CHECK_STR(buf, "y");
	CHECK_INT(nine_list(&h, &cursor, buf, sizeof(buf)), ==, 0);
	mounts_free(&ms);
}

static void check_dir_stat(struct nine_ns *ns, const char *path)
{
	struct nine_handle h;
	struct nine_stat st;

	CHECK_INT(nine_open(ns, path, NINE_RDONLY, &h), ==, 0);
	memset(&st, 0xff, sizeof(st));
	CHECK_INT(nine_stat(&h, &st), ==, 0);
	CHECK_INT(st.kind, ==, NINE_DIR);
	CHECK_UINT(st.size, ==, 0);
}

static void test_stat(void)
{
	struct mounts ms;

	mounts_init(&ms);
	check_dir_stat(ms.ns, "/");
	check_dir_stat(ms.ns, "/a");
	check_dir_stat(ms.ns, "/a/y");
	mounts_free(&ms);
}

/* The directories answer with the codes a file server would. */
static void test_errors(void)
{
	struct mounts ms;
	struct nine_handle h;
	uint64_t cursor = 0;
	char buf[8];

	mounts_init(&ms);
	CHECK_INT(nine_open(ms.ns, "/c", NINE_RDONLY, &h), ==, -ENOENT);
	CHECK_INT(nine_open(ms.ns, "/a/q", NINE_RDONLY, &h), ==, -ENOENT);
	CHECK_INT(nine_open(ms.ns, "/a/y/q", NINE_RDONLY, &h), ==, -ENOENT);
	CHECK_INT(nine_open(ms.ns, "/a/q/z", NINE_RDONLY, &h), ==, -ENOENT);
	CHECK_INT(nine_open(ms.ns, "/x", NINE_RDONLY, &h), ==, -ENOENT);
	CHECK_INT(nine_open(ms.ns, "/a/.", NINE_RDONLY, &h), ==, -EINVAL);

	CHECK_INT(nine_open(ms.ns, "/a", NINE_RDWR, &h), ==, 0);
	CHECK_INT(nine_read(&h, buf, sizeof(buf), 0), ==, -EISDIR);
	CHECK_INT(nine_write(&h, buf, sizeof(buf), 0), ==, -EISDIR);

	CHECK_INT(nine_open(ms.ns, "/a", NINE_WRONLY, &h), ==, 0);
	CHECK_INT(nine_list(&h, &cursor, buf, sizeof(buf)), ==, -EBADF);
	mounts_free(&ms);
}

/* A path into a mount goes to its file server, from the mount's root. */
static void test_into_mount(void)
{
	struct mounts ms;
	struct nine_handle h;

	mounts_init(&ms);
	CHECK_INT(nine_open(ms.ns, "/a/y/z", NINE_RDONLY, &h), ==, 0);
	CHECK_UINT(ms.r[1].ncalls, ==, 0);
	CHECK_UINT(nine_handle_node(&h)->w[0], ==, 0);

	CHECK_INT(nine_open(ms.ns, "/a/y/z/f", NINE_RDONLY, &h), ==, 0);
	CHECK_UINT(ms.r[1].ncalls, ==, 1);
	CHECK_UINT(nine_handle_node(&h)->w[0], ==, 1);
	ms.r[1].ncalls = 0;
	mounts_free(&ms);
}

/* With no mount, the root is an empty directory. */
static void test_no_mounts(void)
{
	struct nine_tree *t;
	struct nine_handle h;
	struct nine_stat st;
	uint64_t cursor = 0;
	char buf[8];

	t = nine_tree_new();
	CHECK_PTR(t, !=, NULL);
	CHECK_INT(nine_open(nine_tree_ns(t), "/", NINE_RDONLY, &h), ==, 0);
	CHECK_INT(nine_list(&h, &cursor, buf, sizeof(buf)), ==, 0);
	CHECK_INT(nine_stat(&h, &st), ==, 0);
	CHECK_INT(st.kind, ==, NINE_DIR);
	CHECK_INT(nine_open(nine_tree_ns(t), "/a", NINE_RDONLY, &h), ==,
		  -ENOENT);
	nine_tree_free(t);
}

int main(void)
{
	test_list();
	test_range();
	test_stat();
	test_errors();
	test_into_mount();
	test_no_mounts();
	return 0;
}
