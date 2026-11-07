/* SPDX-License-Identifier: Zlib */
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ninefold.h"

#include "check.h"
#include "demo.h"

#define NENTITIES 4

#define POS_SIZE (3 * sizeof(float))

struct world {
	struct nine_tree *t;
	struct demo *d;
	struct nine_ns *ns;
	uint32_t ids[NENTITIES];
};

/* The position entity number @i of a test starts at. */
static void start_pos(int i, float pos[3])
{
	pos[0] = (float)i + 0.5f;
	pos[1] = (float)i * -10.0f;
	pos[2] = (float)i * 100.0f + 0.25f;
}

/*
 * A tree with a demo instance at /world, mounted with the second variant's
 * operations, holding NENTITIES entities in the order of ids[], entity i at
 * start_pos(i). Entity 1 is destroyed, so entity 3 has moved into its slot.
 */
static void world_init(struct world *w)
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
	CHECK_INT(demo_destroy(w->d, w->ids[1]), ==, 0);
	CHECK_INT(nine_mount(w->t, "/world", demo_variant_ops(), w->d), ==, 0);
	w->ns = nine_tree_ns(w->t);
}

static void world_free(struct world *w)
{
	nine_tree_free(w->t);
	demo_free(w->d);
}

static void open_value(struct world *w, uint32_t id, struct nine_handle *h)
{
	char path[64];

	snprintf(path, sizeof(path),
		 "/world/entities/%lu/components/transform/pos",
		 (unsigned long)id);
	CHECK_INT(nine_open(w->ns, path, NINE_RDONLY, h), ==, 0);
}

/* The variant's value files read like the first variant's. */
static void test_value_file(void)
{
	struct nine_handle h;
	struct world w;
	float want[3], got[3];

	world_init(&w);
	open_value(&w, w.ids[3], &h);
	start_pos(3, want);

	CHECK_INT(nine_read(&h, got, sizeof(got), 0), ==,
		  (ptrdiff_t)sizeof(got));
	CHECK_MEM(got, want, sizeof(got));

	/* A read past the end is short, and one at the end reads nothing. */
	memset(got, 0, sizeof(got));
	CHECK_INT(nine_read(&h, got, sizeof(got), sizeof(float)), ==,
		  (ptrdiff_t)(2 * sizeof(float)));
	CHECK_MEM(got, &want[1], 2 * sizeof(float));
	CHECK_INT(nine_read(&h, got, sizeof(got), POS_SIZE), ==, 0);
	CHECK_INT(nine_read(&h, got, sizeof(got), UINT64_MAX), ==, 0);
	world_free(&w);
}

/* The variant's bulk files are the column and the IDs, in slot order. */
static void test_bulk_files(void)
{
	const uint32_t want_ids[] = { 0, 3, 2 };
	float want[3][3], got[3][3];
	uint32_t ids[3];
	struct nine_handle h;
	struct world w;

	world_init(&w);
	start_pos(0, want[0]);
	start_pos(3, want[1]);
	start_pos(2, want[2]);

	CHECK_INT(nine_open(w.ns, "/world/components/transform/pos",
			    NINE_RDONLY, &h),
		  ==, 0);
	CHECK_INT(nine_read(&h, got, sizeof(got) + 1, 0), ==,
		  (ptrdiff_t)sizeof(got));
	CHECK_MEM(got, want, sizeof(got));

	CHECK_INT(nine_open(w.ns, "/world/components/transform/ids",
			    NINE_RDONLY, &h),
		  ==, 0);
	CHECK_INT(nine_read(&h, ids, sizeof(ids), 0), ==,
		  (ptrdiff_t)sizeof(ids));
	CHECK_MEM(ids, want_ids, sizeof(ids));
	world_free(&w);
}

/* A directory fails as with the first variant, and so does a stale node. */
static void test_errors(void)
{
	struct nine_handle dir, entity, value;
	unsigned char buf[POS_SIZE];
	struct world w;

	world_init(&w);
	CHECK_INT(nine_open(w.ns, "/world/entities", NINE_RDONLY, &dir), ==, 0);
	CHECK_INT(nine_read(&dir, buf, sizeof(buf), 0), ==, -EISDIR);

	CHECK_INT(nine_open(w.ns, "/world/entities/2", NINE_RDONLY, &entity),
		  ==, 0);
	open_value(&w, w.ids[2], &value);
	CHECK_INT(demo_destroy(w.d, w.ids[2]), ==, 0);
	CHECK_INT(nine_read(&entity, buf, sizeof(buf), 0), ==, -ESTALE);
	CHECK_INT(nine_read(&value, buf, sizeof(buf), 0), ==, -ESTALE);
	world_free(&w);
}

int main(void)
{
	test_value_file();
	test_bulk_files();
	test_errors();
	return 0;
}
