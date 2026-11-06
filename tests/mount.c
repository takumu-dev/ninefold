/* SPDX-License-Identifier: Zlib */
#include <errno.h>
#include <string.h>

#include "ninefold.h"

#include "check.h"
#include "recorder.h"

static void drop_op(struct nine_file_server_ops *ops, enum recorder_op op)
{
	switch (op) {
	case RECORDER_LOOKUP:
		ops->lookup = NULL;
		break;
	case RECORDER_LIST:
		ops->list = NULL;
		break;
	case RECORDER_STAT:
		ops->stat = NULL;
		break;
	case RECORDER_READ:
		ops->read = NULL;
		break;
	case RECORDER_WRITE:
		ops->write = NULL;
		break;
	case RECORDER_READ_MANY:
		ops->read_many = NULL;
		break;
	}
}

static int mount_without(enum recorder_op op)
{
	struct nine_tree *t;
	struct recorder r;
	int err;

	recorder_init(&r);
	drop_op(&r.ops, op);
	t = nine_tree_new();
	CHECK_PTR(t, !=, NULL);
	err = nine_mount(t, "/rec", &r.ops, &r);
	nine_tree_free(t);
	return err;
}

static void test_required_ops(void)
{
	CHECK_INT(mount_without(RECORDER_LOOKUP), ==, -EINVAL);
	CHECK_INT(mount_without(RECORDER_LIST), ==, -EINVAL);
	CHECK_INT(mount_without(RECORDER_STAT), ==, -EINVAL);
	CHECK_INT(mount_without(RECORDER_READ), ==, -EINVAL);
	CHECK_INT(mount_without(RECORDER_WRITE), ==, 0);
	CHECK_INT(mount_without(RECORDER_READ_MANY), ==, 0);
}

static void test_invalid_paths(void)
{
	struct nine_tree *t;
	struct recorder r;

	recorder_init(&r);
	t = nine_tree_new();
	CHECK_PTR(t, !=, NULL);
	CHECK_INT(nine_mount(t, "", &r.ops, &r), ==, -EINVAL);
	CHECK_INT(nine_mount(t, "rec", &r.ops, &r), ==, -EINVAL);
	CHECK_INT(nine_mount(t, "/rec/.", &r.ops, &r), ==, -EINVAL);
	CHECK_INT(nine_mount(t, "/a/../rec", &r.ops, &r), ==, -EINVAL);

	/* None of the failed mounts took the path. */
	CHECK_INT(nine_mount(t, "/rec", &r.ops, &r), ==, 0);
	nine_tree_free(t);
}

/* The tree keeps its own copy of the mount path. */
static void test_path_copied(void)
{
	struct nine_tree *t;
	struct nine_handle h;
	struct recorder r;
	char path[] = "/rec";

	recorder_init(&r);
	t = nine_tree_new();
	CHECK_PTR(t, !=, NULL);
	CHECK_INT(nine_mount(t, path, &r.ops, &r), ==, 0);
	memcpy(path, "/xyz", sizeof(path));

	CHECK_INT(nine_open(nine_tree_ns(t), "/rec/f", NINE_RDONLY, &h), ==, 0);
	CHECK_INT(nine_open(nine_tree_ns(t), "/xyz/f", NINE_RDONLY, &h), ==,
		  -ENOENT);
	nine_tree_free(t);
}

/* An untidy mount path names the same mount point as its tidy form. */
static void test_untidy_mount_path(void)
{
	struct nine_tree *t;
	struct nine_handle h;
	struct recorder r;

	recorder_init(&r);
	t = nine_tree_new();
	CHECK_PTR(t, !=, NULL);
	CHECK_INT(nine_mount(t, "//a///rec/", &r.ops, &r), ==, 0);
	CHECK_INT(nine_open(nine_tree_ns(t), "/a/rec/f", NINE_RDONLY, &h), ==,
		  0);
	CHECK_UINT(r.ncalls, ==, 1);
	CHECK_UINT(r.calls[0].len, ==, 1);
	nine_tree_free(t);
}

/* A mount at the root gets every name of the path. */
static void test_root_mount(void)
{
	struct nine_tree *t;
	struct nine_handle h;
	struct recorder r;

	recorder_init(&r);
	t = nine_tree_new();
	CHECK_PTR(t, !=, NULL);
	CHECK_INT(nine_mount(t, "/", &r.ops, &r), ==, 0);
	CHECK_INT(nine_open(nine_tree_ns(t), "/a/b", NINE_RDONLY, &h), ==, 0);
	CHECK_UINT(r.ncalls, ==, 2);
	CHECK_UINT(nine_handle_node(&h)->w[0], ==, 2);

	CHECK_INT(nine_open(nine_tree_ns(t), "/", NINE_RDONLY, &h), ==, 0);
	CHECK_UINT(r.ncalls, ==, 2);
	CHECK_UINT(nine_handle_node(&h)->w[0], ==, 0);
	nine_tree_free(t);
}

int main(void)
{
	test_required_ops();
	test_invalid_paths();
	test_path_copied();
	test_untidy_mount_path();
	test_root_mount();
	return 0;
}
