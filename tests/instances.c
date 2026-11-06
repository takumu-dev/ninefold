/* SPDX-License-Identifier: Zlib */
#include <errno.h>
#include <stdint.h>
#include <string.h>

#include "ninefold.h"

#include "check.h"
#include "demo.h"
#include "listing.h"

#define POS_SIZE (3 * sizeof(float))

/*
 * Two demo instances in one tree: a at /world with entities 0 to 2, and b at
 * /levels/two/world with entities 0 and 1. Every position starts at zero.
 */
struct worlds {
	struct nine_tree *t;
	struct nine_ns *ns;
	struct demo *a, *b;
};

static struct demo *new_demo(int n)
{
	struct demo *d = demo_new();
	uint32_t id;
	int i;

	CHECK_PTR(d, !=, NULL);
	for (i = 0; i < n; i++)
		CHECK_INT(demo_create(d, &id), ==, 0);
	return d;
}

static void worlds_init(struct worlds *w)
{
	w->t = nine_tree_new();
	CHECK_PTR(w->t, !=, NULL);
	w->a = new_demo(3);
	w->b = new_demo(2);
	CHECK_INT(nine_mount(w->t, "/world", demo_ops(), w->a), ==, 0);
	CHECK_INT(nine_mount(w->t, "/levels/two/world", demo_ops(), w->b), ==,
		  0);
	w->ns = nine_tree_ns(w->t);
}

static void worlds_free(struct worlds *w)
{
	nine_tree_free(w->t);
	demo_free(w->a);
	demo_free(w->b);
}

static void open_rdwr(struct worlds *w, const char *path, struct nine_handle *h)
{
	CHECK_INT(nine_open(w->ns, path, NINE_RDWR, h), ==, 0);
}

static void check_pos(const struct demo *d, uint32_t id, const float want[3])
{
	float got[3];

	CHECK_INT(demo_get_pos(d, id, got), ==, 0);
	CHECK_MEM(got, want, POS_SIZE);
}

/* A write through a value file of one instance leaves the other alone. */
static void test_value_write(void)
{
	static const float zero[3], pos[3] = { 1.0f, 2.0f, 3.0f };
	struct worlds w;
	struct nine_handle h;
	float got[3];

	worlds_init(&w);
	open_rdwr(&w, "/world/entities/1/components/transform/pos", &h);
	CHECK_INT(nine_write(&h, pos, sizeof(pos), 0), ==, 0);
	check_pos(w.a, 1, pos);
	check_pos(w.b, 1, zero);

	open_rdwr(&w, "/levels/two/world/entities/1/components/transform/pos",
		  &h);
	CHECK_INT(nine_read(&h, got, sizeof(got), 0), ==,
		  (ptrdiff_t)sizeof(got));
	CHECK_MEM(got, zero, sizeof(got));
	worlds_free(&w);
}

/* A bulk write to one instance leaves the other alone. */
static void test_bulk_write(void)
{
	static const float zero[3];
	static const float pos[2][3] = { { 4.0f, 5.0f, 6.0f },
					 { 7.0f, 8.0f, 9.0f } };
	struct worlds w;
	struct nine_handle h;

	worlds_init(&w);
	open_rdwr(&w, "/levels/two/world/components/transform/pos", &h);
	CHECK_INT(nine_write(&h, pos, sizeof(pos), 0), ==, 0);
	check_pos(w.b, 0, pos[0]);
	check_pos(w.b, 1, pos[1]);
	check_pos(w.a, 0, zero);
	check_pos(w.a, 1, zero);
	check_pos(w.a, 2, zero);
	worlds_free(&w);
}

/* Each instance answers for its own entities only. */
static void test_entities(void)
{
	struct worlds w;
	struct nine_handle h;
	struct nine_stat st;
	char names[64];

	worlds_init(&w);
	open_rdwr(&w, "/world/entities", &h);
	CHECK_STR(list_names(&h, 16, names, sizeof(names)), "0 1 2 ");
	open_rdwr(&w, "/levels/two/world/entities", &h);
	CHECK_STR(list_names(&h, 16, names, sizeof(names)), "0 1 ");
	CHECK_INT(nine_open(w.ns, "/levels/two/world/entities/2", NINE_RDONLY,
			    &h),
		  ==, -ENOENT);

	open_rdwr(&w, "/levels/two/world/components/transform/pos", &h);
	CHECK_INT(nine_stat(&h, &st), ==, 0);
	CHECK_UINT(st.size, ==, 2 * POS_SIZE);
	open_rdwr(&w, "/world/components/transform/pos", &h);
	CHECK_INT(nine_stat(&h, &st), ==, 0);
	CHECK_UINT(st.size, ==, 3 * POS_SIZE);
	worlds_free(&w);
}

/* The root and the directories down to b's mount list the mount names. */
static void test_dirs(void)
{
	struct worlds w;
	struct nine_handle h;
	char names[64];

	worlds_init(&w);
	open_rdwr(&w, "/", &h);
	CHECK_STR(list_names(&h, 16, names, sizeof(names)), "world levels ");
	open_rdwr(&w, "/levels", &h);
	CHECK_STR(list_names(&h, 16, names, sizeof(names)), "two ");
	open_rdwr(&w, "/levels/two", &h);
	CHECK_STR(list_names(&h, 16, names, sizeof(names)), "world ");
	open_rdwr(&w, "/levels/two/world", &h);
	CHECK_STR(list_names(&h, 16, names, sizeof(names)),
		  "components entities ");
	worlds_free(&w);
}

int main(void)
{
	test_value_write();
	test_bulk_write();
	test_entities();
	test_dirs();
	return 0;
}
