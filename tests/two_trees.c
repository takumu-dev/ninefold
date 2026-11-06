/* SPDX-License-Identifier: Zlib */
#include <stdint.h>
#include <stdio.h>

#include "ninefold.h"

#include "check.h"
#include "demo.h"
#include "listing.h"
#include "thread.h"

#define NTREES 2
#define NROUNDS 200
#define NENTITIES 4

#define POS_SIZE (3 * sizeof(float))

/*
 * One tree and its own two demo instances: a at /world and b at
 * /levels/two/world, each with entities 0 to NENTITIES - 1. Each tree
 * belongs to one thread, which makes it, uses it and frees it.
 */
struct worlds {
	int seed;
	struct nine_tree *t;
	struct nine_ns *ns;
	struct demo *a, *b;
};

static struct demo *new_demo(void)
{
	struct demo *d = demo_new();
	uint32_t id;
	int i;

	CHECK_PTR(d, !=, NULL);
	for (i = 0; i < NENTITIES; i++) {
		CHECK_INT(demo_create(d, &id), ==, 0);
		CHECK_UINT(id, ==, i);
	}
	return d;
}

static void worlds_init(struct worlds *w)
{
	w->t = nine_tree_new();
	CHECK_PTR(w->t, !=, NULL);
	w->a = new_demo();
	w->b = new_demo();
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

/*
 * The positions of a round, different for each tree, each round and each
 * entity, so that a write that reached the wrong tree shows.
 */
static void round_pos(const struct worlds *w, int round,
		      float pos[NENTITIES][3])
{
	int i;

	for (i = 0; i < NENTITIES; i++) {
		pos[i][0] = (float)w->seed;
		pos[i][1] = (float)round;
		pos[i][2] = (float)i;
	}
}

/* Open the pos file of entity @id under the mount at @mount. */
static void open_entity_pos(struct nine_ns *ns, const char *mount, uint32_t id,
			    struct nine_handle *h)
{
	char path[64];

	snprintf(path, sizeof(path), "%s/entities/%lu/components/transform/pos",
		 mount, (unsigned long)id);
	CHECK_INT(nine_open(ns, path, NINE_RDWR, h), ==, 0);
}

/* Write each entity of a through its value file, and read them back. */
static void use_values(const struct worlds *w, float pos[NENTITIES][3])
{
	struct nine_handle hs[NENTITIES];
	float got[NENTITIES][3];
	int errs[NENTITIES];
	uint32_t i;

	for (i = 0; i < NENTITIES; i++) {
		open_entity_pos(w->ns, "/world", i, &hs[i]);
		CHECK_INT(nine_write(&hs[i], pos[i], POS_SIZE, 0), ==, 0);
	}
	CHECK_INT(nine_read_many(hs, NENTITIES, got, POS_SIZE, errs), ==, 0);
	CHECK_MEM(got, pos, sizeof(got));
}

/* Write the column of b through its bulk file, and read it back. */
static void use_bulk(const struct worlds *w, float pos[NENTITIES][3])
{
	struct nine_handle h;
	struct nine_stat st;
	float got[NENTITIES][3];

	CHECK_INT(nine_open(w->ns, "/levels/two/world/components/transform/pos",
			    NINE_RDWR, &h),
		  ==, 0);
	CHECK_INT(nine_stat(&h, &st), ==, 0);
	CHECK_UINT(st.size, ==, sizeof(got));
	CHECK_INT(nine_write(&h, pos, sizeof(got), 0), ==, 0);
	CHECK_INT(nine_read(&h, got, sizeof(got), 0), ==,
		  (ptrdiff_t)sizeof(got));
	CHECK_MEM(got, pos, sizeof(got));
}

/* Each view of a mount reads what the other view of it wrote. */
static void check_views(const struct worlds *w, float pos[NENTITIES][3])
{
	struct nine_handle h;
	float got[NENTITIES][3];
	uint32_t i;

	CHECK_INT(nine_open(w->ns, "/world/components/transform/pos",
			    NINE_RDONLY, &h),
		  ==, 0);
	CHECK_INT(nine_read(&h, got, sizeof(got), 0), ==,
		  (ptrdiff_t)sizeof(got));
	CHECK_MEM(got, pos, sizeof(got));

	for (i = 0; i < NENTITIES; i++) {
		open_entity_pos(w->ns, "/levels/two/world", i, &h);
		CHECK_INT(nine_read(&h, got[i], POS_SIZE, 0), ==,
			  (ptrdiff_t)POS_SIZE);
	}
	CHECK_MEM(got, pos, sizeof(got));
}

static void check_dirs(const struct worlds *w)
{
	struct nine_handle h;
	char names[64];

	CHECK_INT(nine_open(w->ns, "/", NINE_RDONLY, &h), ==, 0);
	CHECK_STR(list_names(&h, 16, names, sizeof(names)), "world levels ");
	CHECK_INT(
		nine_open(w->ns, "/levels/two/world/entities", NINE_RDONLY, &h),
		==, 0);
	CHECK_STR(list_names(&h, 16, names, sizeof(names)), "0 1 2 3 ");
}

static void use_tree(void *arg)
{
	struct worlds *w = arg;
	float pos[NENTITIES][3];
	int i;

	worlds_init(w);
	for (i = 0; i < NROUNDS; i++) {
		round_pos(w, i, pos);
		use_values(w, pos);
		use_bulk(w, pos);
		check_views(w, pos);
		check_dirs(w);
	}
	worlds_free(w);
}

/* Two trees, each with its own demo instances, on two threads at once. */
int main(void)
{
	struct test_thread threads[NTREES];
	struct worlds worlds[NTREES];
	int i;

	for (i = 0; i < NTREES; i++) {
		worlds[i].seed = i + 1;
		CHECK_INT(test_thread_start(&threads[i], use_tree, &worlds[i]),
			  ==, 0);
	}
	for (i = 0; i < NTREES; i++)
		CHECK_INT(test_thread_join(&threads[i]), ==, 0);
	return 0;
}
