/* SPDX-License-Identifier: Zlib */
#include <errno.h>
#include <stdint.h>
#include <string.h>

#include "ninefold.h"

#include "check.h"
#include "demo.h"
#include "listing.h"

#define NENTITIES 12

struct world {
	struct nine_tree *t;
	struct demo *d;
	struct nine_ns *ns;
};

/* A tree with a demo instance at /world, holding entities 0 to @n - 1. */
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

/* List @h with a buffer of @len bytes. Return: its names, space after each. */
static const char *list_all(const struct nine_handle *h, size_t len)
{
	static char names[256];

	return list_names(h, len, names, sizeof(names));
}

static void open_dir(struct world *w, const char *path, struct nine_handle *h)
{
	CHECK_INT(nine_open(w->ns, path, NINE_RDONLY, h), ==, 0);
}

static void check_list(struct world *w, const char *path, const char *want)
{
	struct nine_handle h;

	open_dir(w, path, &h);
	CHECK_STR(list_all(&h, 64), want);
}

static void test_fixed_dirs(void)
{
	struct world w;

	world_init(&w, 2);
	check_list(&w, "/world", "components entities ");
	check_list(&w, "/world/components", "transform unit ");
	check_list(&w, "/world/components/transform", "pos ids ");
	check_list(&w, "/world/entities/1", "components ");
	check_list(&w, "/world/entities/1/components", "transform unit ");
	check_list(&w, "/world/entities/1/components/transform", "pos ");
	world_free(&w);
}

/* entities/ lists the live IDs in decimal, smallest first. */
static void test_entities(void)
{
	struct world w;

	world_init(&w, NENTITIES);
	check_list(&w, "/world/entities", "0 1 2 3 4 5 6 7 8 9 10 11 ");

	/* Destroying 3 moves 11 into its slot. */
	CHECK_INT(demo_destroy(w.d, 3), ==, 0);
	CHECK_INT(demo_destroy(w.d, 0), ==, 0);
	check_list(&w, "/world/entities", "1 2 4 5 6 7 8 9 10 11 ");
	world_free(&w);
}

static void test_empty(void)
{
	struct world w;
	struct nine_handle h;
	uint64_t cursor = 0;
	char buf[16];

	world_init(&w, 0);
	open_dir(&w, "/world/entities", &h);
	CHECK_INT(nine_list(&h, &cursor, buf, sizeof(buf)), ==, 0);
	world_free(&w);
}

/* A buffer that holds one name at a time still lists each name once. */
static void test_one_name_per_call(void)
{
	struct world w;
	struct nine_handle h;

	world_init(&w, NENTITIES);
	open_dir(&w, "/world/entities", &h);
	CHECK_STR(list_all(&h, 3), "0 1 2 3 4 5 6 7 8 9 10 11 ");
	open_dir(&w, "/world", &h);
	CHECK_STR(list_all(&h, 11), "components entities ");
	world_free(&w);
}

static void test_range(void)
{
	struct world w;
	struct nine_handle h;
	uint64_t cursor = 0;
	char buf[16];

	world_init(&w, NENTITIES);
	open_dir(&w, "/world", &h);
	CHECK_INT(nine_list(&h, &cursor, buf, 0), ==, -ERANGE);
	/* "components" needs 11 bytes with its NUL. */
	CHECK_INT(nine_list(&h, &cursor, buf, 10), ==, -ERANGE);
	CHECK_INT(nine_list(&h, &cursor, buf, 11), ==, 1);
	CHECK_STR(buf, "components");
	CHECK_INT(nine_list(&h, &cursor, buf, 8), ==, -ERANGE);
	CHECK_INT(nine_list(&h, &cursor, buf, 9), ==, 1);
	CHECK_STR(buf, "entities");
	CHECK_INT(nine_list(&h, &cursor, buf, 9), ==, 0);

	/* A full buffer stops before the name that does not fit. */
	cursor = 0;
	open_dir(&w, "/world/entities", &h);
	CHECK_INT(nine_list(&h, &cursor, buf, 15), ==, 7);
	CHECK_MEM(buf,
		  "0\0"
		  "1\0"
		  "2\0"
		  "3\0"
		  "4\0"
		  "5\0"
		  "6",
		  14);
	CHECK_INT(nine_list(&h, &cursor, buf, 2), ==, 1);
	CHECK_STR(buf, "7");
	world_free(&w);
}

static void test_errors(void)
{
	struct world w;
	struct nine_handle h;
	uint64_t cursor = 0;
	char buf[32];

	world_init(&w, 2);
	open_dir(&w, "/world/components/transform/pos", &h);
	CHECK_INT(nine_list(&h, &cursor, buf, sizeof(buf)), ==, -ENOTDIR);
	open_dir(&w, "/world/entities/1/components/transform/pos", &h);
	CHECK_INT(nine_list(&h, &cursor, buf, sizeof(buf)), ==, -ENOTDIR);

	open_dir(&w, "/world/entities/1", &h);
	CHECK_INT(demo_destroy(w.d, 1), ==, 0);
	CHECK_INT(nine_list(&h, &cursor, buf, sizeof(buf)), ==, -ESTALE);

	CHECK_INT(nine_open(w.ns, "/world", NINE_WRONLY, &h), ==, 0);
	CHECK_INT(nine_list(&h, &cursor, buf, sizeof(buf)), ==, -EBADF);
	CHECK_INT(nine_open(w.ns, "/world", NINE_RDWR, &h), ==, 0);
	CHECK_INT(nine_list(&h, &cursor, buf, sizeof(buf)), ==, 2);

	memset(&h, 0, sizeof(h));
	cursor = 0;
	CHECK_INT(nine_list(&h, &cursor, buf, sizeof(buf)), ==, -EBADF);
	world_free(&w);
}

int main(void)
{
	test_fixed_dirs();
	test_entities();
	test_empty();
	test_one_name_per_call();
	test_range();
	test_errors();
	return 0;
}
