/* SPDX-License-Identifier: Zlib */
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ninefold.h"

#include "check.h"
#include "demo.h"

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

/* More entities than the column first holds, so it has grown. */
#define NENTITIES 6

struct world {
	struct nine_tree *t;
	struct demo *d;
	struct nine_ns *ns;
	uint32_t ids[NENTITIES];
};

/*
 * A tree with a demo instance at /world, holding NENTITIES entities in the
 * order of ids[], each at a position of its own.
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
		pos[0] = (float)i + 0.5f;
		pos[1] = (float)i * -10.0f;
		pos[2] = (float)i * 100.0f + 0.25f;
		CHECK_INT(demo_set_pos(w->d, w->ids[i], pos), ==, 0);
	}
	CHECK_INT(nine_mount(w->t, "/world", demo_ops(), w->d), ==, 0);
	w->ns = nine_tree_ns(w->t);
}

static void world_free(struct world *w)
{
	nine_tree_free(w->t);
	demo_free(w->d);
}

static void open_bulk(struct world *w, const char *name, int mode,
		      struct nine_handle *h)
{
	char path[64];

	snprintf(path, sizeof(path), "/world/components/transform/%s", name);
	CHECK_INT(nine_open(w->ns, path, mode, h), ==, 0);
}

/* The slot of an entity, found through a read of ids. */
static size_t slot_of(struct world *w, uint32_t id)
{
	uint32_t ids[NENTITIES];
	struct nine_handle h;
	size_t i;

	open_bulk(w, "ids", NINE_RDONLY, &h);
	CHECK_INT(nine_read(&h, ids, sizeof(ids), 0), ==,
		  (ptrdiff_t)sizeof(ids));
	for (i = 0; i < NENTITIES; i++) {
		if (ids[i] == id)
			break;
	}
	CHECK_UINT(i, <, NENTITIES);
	return i;
}

static void open_value(struct world *w, uint32_t id, int mode,
		       struct nine_handle *h)
{
	char path[64];

	snprintf(path, sizeof(path),
		 "/world/entities/%lu/components/transform/pos",
		 (unsigned long)id);
	CHECK_INT(nine_open(w->ns, path, mode, h), ==, 0);
}

/* The native positions of every entity, each at its slot. */
static void native_column(struct world *w, float col[NENTITIES][3])
{
	int i;

	for (i = 0; i < NENTITIES; i++)
		CHECK_INT(demo_get_pos(w->d, w->ids[i],
				       col[slot_of(w, w->ids[i])]),
			  ==, 0);
}

/* Each entity's value file reads the same as its slot of @col. */
static void check_values(struct world *w, float col[NENTITIES][3])
{
	struct nine_handle h;
	float got[3];
	int i;

	for (i = 0; i < NENTITIES; i++) {
		open_value(w, w->ids[i], NINE_RDONLY, &h);
		CHECK_INT(nine_read(&h, got, sizeof(got), 0), ==,
			  (ptrdiff_t)sizeof(got));
		CHECK_MEM(got, col[slot_of(w, w->ids[i])], sizeof(got));
	}
}

/* One read of pos gives the whole native column, n x 12 bytes. */
static void test_read_pos(void)
{
	float want[NENTITIES][3];
	unsigned char got[sizeof(want) + 16];
	struct nine_handle h;
	struct world w;

	world_init(&w);
	native_column(&w, want);
	open_bulk(&w, "pos", NINE_RDONLY, &h);

	CHECK_INT(nine_read(&h, got, sizeof(got), 0), ==,
		  (ptrdiff_t)sizeof(want));
	CHECK_MEM(got, want, sizeof(want));

	/* A read at any offset and length, across entities' values. */
	CHECK_INT(nine_read(&h, got, 17, 7), ==, 17);
	CHECK_MEM(got, (const unsigned char *)want + 7, 17);
	CHECK_INT(nine_read(&h, got, sizeof(got), sizeof(want) - 5), ==, 5);
	CHECK_MEM(got, (const unsigned char *)want + sizeof(want) - 5, 5);
	CHECK_INT(nine_read(&h, got, sizeof(got), sizeof(want)), ==, 0);
	CHECK_INT(nine_read(&h, got, sizeof(got), UINT64_MAX), ==, 0);
	world_free(&w);
}

/*
 * ids gives the entity IDs in slot order, one uint32_t each. With no entity
 * destroyed, the slot order is the order of creation.
 */
static void test_read_ids(void)
{
	uint32_t got[NENTITIES + 4];
	struct nine_handle h;
	struct world w;

	world_init(&w);
	open_bulk(&w, "ids", NINE_RDONLY, &h);

	CHECK_INT(nine_read(&h, got, sizeof(got), 0), ==,
		  (ptrdiff_t)sizeof(w.ids));
	CHECK_MEM(got, w.ids, sizeof(w.ids));

	CHECK_INT(nine_read(&h, got, 6, 2), ==, 6);
	CHECK_MEM(got, (const unsigned char *)w.ids + 2, 6);
	CHECK_INT(nine_read(&h, got, sizeof(got), sizeof(w.ids)), ==, 0);
	world_free(&w);
}

/* A write through an entity's value file shows in pos at its slot. */
static void test_value_write_shows_in_bulk(void)
{
	static const float pos[3] = { -7.5f, 8.0f, 0.125f };
	struct nine_handle value, bulk;
	struct world w;
	float got[3];
	size_t slot;

	world_init(&w);
	open_value(&w, w.ids[3], NINE_WRONLY, &value);
	CHECK_INT(nine_write(&value, pos, sizeof(pos), 0), ==, 0);

	slot = slot_of(&w, w.ids[3]);
	open_bulk(&w, "pos", NINE_RDONLY, &bulk);
	CHECK_INT(nine_read(&bulk, got, sizeof(got), slot * sizeof(got)), ==,
		  (ptrdiff_t)sizeof(got));
	CHECK_MEM(got, pos, sizeof(got));
	world_free(&w);
}

/* A bulk write of the whole column shows in every entity's value file. */
static void test_bulk_write_shows_in_values(void)
{
	float col[NENTITIES][3];
	struct nine_handle bulk;
	struct world w;
	size_t i;

	world_init(&w);
	for (i = 0; i < NENTITIES; i++) {
		col[i][0] = (float)i * 3.0f;
		col[i][1] = 1000.0f - (float)i;
		col[i][2] = -0.5f * (float)i;
	}
	open_bulk(&w, "pos", NINE_WRONLY, &bulk);
	CHECK_INT(nine_write(&bulk, col, sizeof(col), 0), ==, 0);
	check_values(&w, col);
	world_free(&w);
}

/*
 * A bulk write at an offset changes only the bytes it covers, here the end
 * of one entity's value, the whole of the next, and the start of a third.
 */
static void test_partial_bulk_write(void)
{
	unsigned char bytes[20];
	float want[NENTITIES][3];
	struct nine_handle bulk;
	struct world w;
	size_t i;

	for (i = 0; i < sizeof(bytes); i++)
		bytes[i] = (unsigned char)(i + 1);
	world_init(&w);
	native_column(&w, want);
	memcpy((unsigned char *)want + 20, bytes, sizeof(bytes));

	open_bulk(&w, "pos", NINE_WRONLY, &bulk);
	CHECK_INT(nine_write(&bulk, bytes, sizeof(bytes), 20), ==, 0);
	check_values(&w, want);
	world_free(&w);
}

/* ids refuses every write, even one that would change nothing. */
static void test_write_ids(void)
{
	uint32_t got[NENTITIES];
	struct nine_handle h;
	struct world w;

	world_init(&w);
	open_bulk(&w, "ids", NINE_RDWR, &h);
	CHECK_INT(nine_write(&h, w.ids, sizeof(w.ids), 0), ==, -EACCES);
	CHECK_INT(nine_write(&h, &w.ids[1], sizeof(w.ids[1]), 0), ==, -EACCES);
	CHECK_INT(nine_write(&h, w.ids, 0, 0), ==, -EACCES);

	CHECK_INT(nine_read(&h, got, sizeof(got), 0), ==,
		  (ptrdiff_t)sizeof(got));
	CHECK_MEM(got, w.ids, sizeof(got));
	world_free(&w);
}

/* A bulk write that would make pos larger writes nothing. */
static void test_bulk_write_past_end(void)
{
	float want[NENTITIES][3];
	unsigned char buf[sizeof(want) + 12];
	unsigned char got[sizeof(want)];
	struct nine_handle h;
	struct world w;
	size_t size = sizeof(want);

	world_init(&w);
	native_column(&w, want);
	memset(buf, 0xff, sizeof(buf));
	open_bulk(&w, "pos", NINE_RDWR, &h);

	CHECK_INT(nine_write(&h, buf, size + 12, 0), ==, -EINVAL);
	CHECK_INT(nine_write(&h, buf, size + 1, 0), ==, -EINVAL);
	CHECK_INT(nine_write(&h, buf, 12, size - 6), ==, -EINVAL);
	CHECK_INT(nine_write(&h, buf, 12, size), ==, -EINVAL);
	CHECK_INT(nine_write(&h, buf, 0, size + 1), ==, -EINVAL);
	CHECK_INT(nine_write(&h, buf, 4, UINT64_MAX), ==, -EINVAL);
	CHECK_INT(nine_write(&h, buf, SIZE_MAX, 4), ==, -EINVAL);

	/* An empty write at the end changes nothing. */
	CHECK_INT(nine_write(&h, buf, 0, size), ==, 0);

	CHECK_INT(nine_read(&h, got, sizeof(got), 0), ==,
		  (ptrdiff_t)sizeof(got));
	CHECK_MEM(got, want, sizeof(got));
	world_free(&w);
}

/* With no entities, both bulk files are empty and pos takes no bytes. */
static void test_empty(void)
{
	static const unsigned char byte = 1;
	struct nine_tree *t;
	struct nine_ns *ns;
	struct demo *d;
	struct nine_handle pos, ids;
	unsigned char buf[16];

	t = nine_tree_new();
	d = demo_new();
	CHECK_PTR(t, !=, NULL);
	CHECK_PTR(d, !=, NULL);
	CHECK_INT(nine_mount(t, "/world", demo_ops(), d), ==, 0);
	ns = nine_tree_ns(t);

	CHECK_INT(nine_open(ns, "/world/components/transform/pos", NINE_RDWR,
			    &pos),
		  ==, 0);
	CHECK_INT(nine_open(ns, "/world/components/transform/ids", NINE_RDWR,
			    &ids),
		  ==, 0);
	CHECK_INT(nine_read(&pos, buf, sizeof(buf), 0), ==, 0);
	CHECK_INT(nine_read(&ids, buf, sizeof(buf), 0), ==, 0);
	CHECK_INT(nine_write(&pos, &byte, 0, 0), ==, 0);
	CHECK_INT(nine_write(&pos, &byte, 1, 0), ==, -EINVAL);
	CHECK_INT(nine_write(&ids, &byte, 0, 0), ==, -EACCES);

	nine_tree_free(t);
	demo_free(d);
}

/* The directories above the bulk files refuse reads and writes. */
static void test_bulk_dirs(void)
{
	static const char *const dirs[] = {
		"/world/components",
		"/world/components/transform",
	};
	unsigned char buf[4] = { 0 };
	struct nine_handle h;
	struct world w;
	size_t i;

	world_init(&w);
	for (i = 0; i < ARRAY_SIZE(dirs); i++) {
		CHECK_INT(nine_open(w.ns, dirs[i], NINE_RDWR, &h), ==, 0);
		CHECK_INT(nine_read(&h, buf, sizeof(buf), 0), ==, -EISDIR);
		CHECK_INT(nine_write(&h, buf, sizeof(buf), 0), ==, -EISDIR);
	}
	CHECK_INT(nine_open(w.ns, "/world/components/transform/pos/x",
			    NINE_RDONLY, &h),
		  ==, -ENOTDIR);
	CHECK_INT(
		nine_open(w.ns, "/world/components/rotation", NINE_RDONLY, &h),
		==, -ENOENT);
	world_free(&w);
}

int main(void)
{
	test_read_pos();
	test_read_ids();
	test_value_write_shows_in_bulk();
	test_bulk_write_shows_in_values();
	test_partial_bulk_write();
	test_write_ids();
	test_bulk_write_past_end();
	test_empty();
	test_bulk_dirs();
	return 0;
}
