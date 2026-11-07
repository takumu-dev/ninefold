/* SPDX-License-Identifier: Zlib */
#include <errno.h>
#include <string.h>

#include "ninefold.h"

#include "check.h"
#include "demo.h"
#include "recorder.h"

static const float origin_pos[3] = { 7.0f, 8.0f, 9.0f };

struct world {
	struct nine_tree *t;
	struct demo *d;
	struct nine_ns *ns;
};

/* A tree with a demo instance at /world, holding entity 0. */
static void world_init(struct world *w)
{
	uint32_t id;

	w->t = nine_tree_new();
	w->d = demo_new();
	CHECK_PTR(w->t, !=, NULL);
	CHECK_PTR(w->d, !=, NULL);
	CHECK_INT(demo_create(w->d, &id), ==, 0);
	CHECK_UINT(id, ==, 0);
	CHECK_INT(demo_set_pos(w->d, id, origin_pos), ==, 0);
	CHECK_INT(nine_mount(w->t, "/world", demo_ops(), w->d), ==, 0);
	w->ns = nine_tree_ns(w->t);
}

static void world_free(struct world *w)
{
	nine_tree_free(w->t);
	demo_free(w->d);
}

static int open_path(struct world *w, const char *path)
{
	struct nine_handle h;

	return nine_open(w->ns, path, NINE_RDONLY, &h);
}

/* Open @path and read entity 0's position through it. */
static void check_reads_pos(struct world *w, const char *path)
{
	struct nine_handle h;
	float got[3];

	CHECK_INT(nine_open(w->ns, path, NINE_RDONLY, &h), ==, 0);
	CHECK_INT(nine_read(&h, got, sizeof(got), 0), ==,
		  (ptrdiff_t)sizeof(got));
	CHECK_MEM(got, origin_pos, sizeof(got));
}

static void test_untidy_slashes(void)
{
	struct world w;

	world_init(&w);
	check_reads_pos(&w, "/world/entities/0/components/transform/pos");
	check_reads_pos(&w, "//world///entities/0//components/transform/pos");
	check_reads_pos(&w, "/world/entities/0/components/transform/pos/");
	check_reads_pos(&w, "/world/entities/0/components/transform/pos///");
	world_free(&w);
}

static void test_invalid_paths(void)
{
	struct world w;

	world_init(&w);
	CHECK_INT(open_path(&w, ""), ==, -EINVAL);
	CHECK_INT(open_path(&w, "world/entities/0"), ==, -EINVAL);
	CHECK_INT(open_path(&w, "./world"), ==, -EINVAL);
	CHECK_INT(open_path(&w, "/world/./entities"), ==, -EINVAL);
	CHECK_INT(open_path(&w, "/world/entities/.."), ==, -EINVAL);
	CHECK_INT(open_path(&w, "/world/entities/../entities/0"), ==, -EINVAL);
	CHECK_INT(open_path(&w, "/world/missing/.."), ==, -EINVAL);
	/* Names that only start with dots are ordinary names. */
	CHECK_INT(open_path(&w, "/world/.entities"), ==, -ENOENT);
	CHECK_INT(open_path(&w, "/world/..."), ==, -ENOENT);
	world_free(&w);
}

static void test_missing_nodes(void)
{
	struct world w;

	world_init(&w);
	CHECK_INT(open_path(&w, "/elsewhere"), ==, -ENOENT);
	CHECK_INT(open_path(&w, "/worlds/entities"), ==, -ENOENT);
	CHECK_INT(open_path(&w, "/world/missing"), ==, -ENOENT);
	CHECK_INT(open_path(&w, "/world/entities/1"), ==, -ENOENT);
	CHECK_INT(open_path(&w, "/world/entities/00"), ==, -ENOENT);
	CHECK_INT(open_path(&w, "/world/entities/x"), ==, -ENOENT);
	CHECK_INT(open_path(&w, "/world/entities/0/components/transform/rot"),
		  ==, -ENOENT);
	world_free(&w);
}

static void test_path_through_file(void)
{
	struct world w;

	world_init(&w);
	CHECK_INT(open_path(&w, "/world/entities/0/components/transform/pos/x"),
		  ==, -ENOTDIR);
	world_free(&w);
}

static void test_read_on_directory(void)
{
	struct nine_handle h;
	struct world w;
	char buf[16];

	world_init(&w);
	CHECK_INT(nine_open(w.ns, "/world/entities/0", NINE_RDONLY, &h), ==, 0);
	CHECK_INT(nine_read(&h, buf, sizeof(buf), 0), ==, -EISDIR);
	CHECK_INT(nine_open(w.ns, "/world", NINE_RDONLY, &h), ==, 0);
	CHECK_INT(nine_read(&h, buf, sizeof(buf), 0), ==, -EISDIR);
	world_free(&w);
}

/* Each name reaches lookup as a pointer into the caller's path. */
static void test_lookup_gets_caller_names(void)
{
	static const char path[] = "/rec//abc/de///f/";
	struct nine_tree *t;
	struct nine_handle h;
	struct recorder r;
	size_t i;

	recorder_init(&r);
	t = nine_tree_new();
	CHECK_PTR(t, !=, NULL);
	CHECK_INT(nine_mount(t, "/rec", &r.ops, &r), ==, 0);

	CHECK_INT(nine_open(nine_tree_ns(t), path, NINE_RDONLY, &h), ==, 0);
	CHECK_UINT(r.ncalls, ==, 3);
	for (i = 0; i < r.ncalls; i++) {
		CHECK_INT(r.calls[i].op, ==, RECORDER_LOOKUP);
		CHECK_UINT(r.calls[i].node.w[0], ==, i);
	}
	CHECK_PTR(r.calls[0].name, ==, path + 6);
	CHECK_UINT(r.calls[0].len, ==, 3);
	CHECK_PTR(r.calls[1].name, ==, path + 10);
	CHECK_UINT(r.calls[1].len, ==, 2);
	CHECK_PTR(r.calls[2].name, ==, path + 15);
	CHECK_UINT(r.calls[2].len, ==, 1);
	CHECK_UINT(nine_handle_node(&h)->w[0], ==, 3);

	nine_tree_free(t);
}

/* A name has no length limit. */
static void test_long_name(void)
{
	static char path[5000];
	struct nine_tree *t;
	struct nine_handle h;
	struct recorder r;

	memcpy(path, "/rec/", 5);
	memset(path + 5, 'n', sizeof(path) - 6);

	recorder_init(&r);
	t = nine_tree_new();
	CHECK_PTR(t, !=, NULL);
	CHECK_INT(nine_mount(t, "/rec", &r.ops, &r), ==, 0);
	CHECK_INT(nine_open(nine_tree_ns(t), path, NINE_RDONLY, &h), ==, 0);
	CHECK_UINT(r.ncalls, ==, 1);
	CHECK_PTR(r.calls[0].name, ==, path + 5);
	CHECK_UINT(r.calls[0].len, ==, sizeof(path) - 6);
	nine_tree_free(t);
}

int main(void)
{
	test_untidy_slashes();
	test_invalid_paths();
	test_missing_nodes();
	test_path_through_file();
	test_read_on_directory();
	test_lookup_gets_caller_names();
	test_long_name();
	return 0;
}
