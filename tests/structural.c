/* SPDX-License-Identifier: Zlib */
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ninefold.h"

#include "check.h"
#include "demo.h"

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

#define NENTITIES 6

/* Enough entities to grow the column of NENTITIES several times. */
#define NGROWN 50

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
 * A tree with a demo instance at /world, holding NENTITIES entities in the
 * order of ids[], entity i at start_pos(i).
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
	CHECK_INT(nine_mount(w->t, "/world", demo_ops(), w->d), ==, 0);
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
	CHECK_INT(nine_open(w->ns, path, NINE_RDWR, h), ==, 0);
}

static void check_reads(const struct nine_handle *h, const float want[3])
{
	float got[3];

	CHECK_INT(nine_read(h, got, sizeof(got), 0), ==,
		  (ptrdiff_t)sizeof(got));
	CHECK_MEM(got, want, sizeof(got));
}

/*
 * Destroying an entity moves the last one into its slot. A handle to the
 * moved entity still reads it, and a handle to the destroyed one is stale.
 */
static void test_swap_remove(void)
{
	struct nine_handle h[NENTITIES];
	struct world w;
	float want[3];
	unsigned char buf[POS_SIZE];
	int i;

	world_init(&w);
	for (i = 0; i < NENTITIES; i++)
		open_value(&w, w.ids[i], &h[i]);

	CHECK_INT(demo_destroy(w.d, w.ids[1]), ==, 0);

	CHECK_INT(nine_read(&h[1], buf, sizeof(buf), 0), ==, -ESTALE);
	for (i = 0; i < NENTITIES; i++) {
		if (i == 1)
			continue;
		start_pos(i, want);
		check_reads(&h[i], want);
	}
	world_free(&w);
}

static void open_bulk(struct world *w, const char *name, struct nine_handle *h)
{
	char path[64];

	snprintf(path, sizeof(path), "/world/components/transform/%s", name);
	CHECK_INT(nine_open(w->ns, path, NINE_RDWR, h), ==, 0);
}

/*
 * The bulk files hold exactly the @n live entities in @want_ids, in that
 * order, each at its native position.
 */
static void check_bulk(struct world *w, const uint32_t *want_ids, size_t n)
{
	/* Room for one entity more, so that a read shows any extra one. */
	uint32_t ids[NGROWN + 1];
	float pos[NGROWN + 1][3], want[3];
	struct nine_handle h;
	size_t i;

	CHECK_UINT(n, <=, NGROWN);
	open_bulk(w, "ids", &h);
	CHECK_INT(nine_read(&h, ids, sizeof(ids), 0), ==,
		  (ptrdiff_t)(n * sizeof(*ids)));
	CHECK_MEM(ids, want_ids, n * sizeof(*ids));

	open_bulk(w, "pos", &h);
	CHECK_INT(nine_read(&h, pos, sizeof(pos), 0), ==,
		  (ptrdiff_t)(n * sizeof(*pos)));
	for (i = 0; i < n; i++) {
		CHECK_INT(demo_get_pos(w->d, ids[i], want), ==, 0);
		CHECK_MEM(pos[i], want, sizeof(want));
	}
}

/*
 * After destroys, the bulk files are one entity shorter each time, with the
 * last entity in the destroyed one's slot.
 */
static void test_bulk_after_destroy(void)
{
	struct world w;
	uint32_t want[NENTITIES];

	world_init(&w);
	memcpy(want, w.ids, sizeof(want));

	CHECK_INT(demo_destroy(w.d, w.ids[0]), ==, 0);
	want[0] = w.ids[5];
	check_bulk(&w, want, 5);

	CHECK_INT(demo_destroy(w.d, w.ids[3]), ==, 0);
	want[3] = w.ids[4];
	check_bulk(&w, want, 4);

	/* The last entity leaves no gap, so nothing moves. */
	CHECK_INT(demo_destroy(w.d, w.ids[4]), ==, 0);
	check_bulk(&w, want, 3);
	world_free(&w);
}

/*
 * A new entity takes the destroyed one's ID. The old handle stays stale,
 * and a fresh open of the same path reads the new entity.
 */
static void test_id_reuse(void)
{
	static const float pos[3] = { 9.0f, -9.5f, 0.75f };
	struct nine_handle old, fresh;
	struct world w;
	unsigned char buf[POS_SIZE];
	uint32_t id;

	world_init(&w);
	open_value(&w, w.ids[2], &old);
	CHECK_INT(demo_destroy(w.d, w.ids[2]), ==, 0);
	CHECK_INT(demo_create(w.d, &id), ==, 0);
	CHECK_UINT(id, ==, w.ids[2]);
	CHECK_INT(demo_set_pos(w.d, id, pos), ==, 0);

	CHECK_INT(nine_read(&old, buf, sizeof(buf), 0), ==, -ESTALE);
	open_value(&w, id, &fresh);
	check_reads(&fresh, pos);
	world_free(&w);
}

/*
 * Writes through the stale handle @h return -ESTALE and leave the column of
 * @n entities as it was.
 */
static void check_stale_write(struct world *w, const struct nine_handle *h,
			      size_t n)
{
	static const float pos[3] = { 1.0f, 2.0f, 3.0f };
	unsigned char before[NENTITIES * POS_SIZE], after[NENTITIES * POS_SIZE];
	struct nine_handle bulk;

	open_bulk(w, "pos", &bulk);
	CHECK_INT(nine_read(&bulk, before, sizeof(before), 0), ==,
		  (ptrdiff_t)(n * POS_SIZE));
	CHECK_INT(nine_write(h, pos, sizeof(pos), 0), ==, -ESTALE);
	CHECK_INT(nine_write(h, pos, sizeof(float), 2 * sizeof(float)), ==,
		  -ESTALE);
	CHECK_INT(nine_read(&bulk, after, sizeof(after), 0), ==,
		  (ptrdiff_t)(n * POS_SIZE));
	CHECK_MEM(after, before, n * POS_SIZE);
}

/*
 * A write through a stale handle changes nothing, even once a new entity
 * has the destroyed one's ID.
 */
static void test_stale_write(void)
{
	struct nine_handle h;
	struct world w;
	uint32_t id;

	world_init(&w);
	open_value(&w, w.ids[1], &h);
	CHECK_INT(demo_destroy(w.d, w.ids[1]), ==, 0);
	check_stale_write(&w, &h, NENTITIES - 1);

	CHECK_INT(demo_create(w.d, &id), ==, 0);
	check_stale_write(&w, &h, NENTITIES);
	world_free(&w);
}

/*
 * Every operation through a stale node returns -ESTALE: a value file and the
 * directories of the destroyed entity alike. The file server's operations
 * are called as the framework calls them, with the handle's node.
 */
static void test_stale_every_op(void)
{
	static const char *const paths[] = {
		"",
		"/components",
		"/components/transform",
		"/components/transform/pos",
	};
	const struct nine_file_server_ops *ops = demo_ops();
	struct nine_handle h[ARRAY_SIZE(paths)];
	const struct nine_node *node;
	struct nine_node out;
	struct nine_stat st;
	struct world w;
	char path[96], buf[64];
	uint64_t cursor;
	size_t i;

	world_init(&w);
	for (i = 0; i < ARRAY_SIZE(paths); i++) {
		snprintf(path, sizeof(path), "/world/entities/%lu%s",
			 (unsigned long)w.ids[4], paths[i]);
		CHECK_INT(nine_open(w.ns, path, NINE_RDWR, &h[i]), ==, 0);
	}
	CHECK_INT(demo_destroy(w.d, w.ids[4]), ==, 0);

	for (i = 0; i < ARRAY_SIZE(paths); i++) {
		node = nine_handle_node(&h[i]);
		cursor = 0;
		CHECK_INT(nine_read(&h[i], buf, sizeof(buf), 0), ==, -ESTALE);
		CHECK_INT(nine_write(&h[i], buf, 4, 0), ==, -ESTALE);
		CHECK_INT(ops->stat(w.d, node, &st), ==, -ESTALE);
		CHECK_INT(ops->list(w.d, node, &cursor, buf, sizeof(buf)), ==,
			  -ESTALE);
		CHECK_INT(ops->lookup(w.d, node, "components",
				      strlen("components"), &out),
			  ==, -ESTALE);
	}

	/* The destroyed entity's paths are gone. */
	snprintf(path, sizeof(path), "/world/entities/%lu",
		 (unsigned long)w.ids[4]);
	CHECK_INT(nine_open(w.ns, path, NINE_RDONLY, &h[0]), ==, -ENOENT);
	world_free(&w);
}

/*
 * Handles opened before the column grows read and write the same entities
 * after it has grown, and the bulk files hold every entity in order.
 */
static void test_growth(void)
{
	static const float pos[3] = { -4.0f, 4.5f, 40.0f };
	struct nine_handle h[NENTITIES];
	struct world w;
	uint32_t ids[NGROWN];
	float want[3], got[3];
	int i;

	world_init(&w);
	for (i = 0; i < NENTITIES; i++)
		open_value(&w, w.ids[i], &h[i]);

	memcpy(ids, w.ids, sizeof(w.ids));
	for (i = NENTITIES; i < NGROWN; i++)
		CHECK_INT(demo_create(w.d, &ids[i]), ==, 0);

	for (i = 0; i < NENTITIES; i++) {
		start_pos(i, want);
		check_reads(&h[i], want);
	}
	CHECK_INT(nine_write(&h[2], pos, sizeof(pos), 0), ==, 0);
	CHECK_INT(demo_get_pos(w.d, w.ids[2], got), ==, 0);
	CHECK_MEM(got, pos, sizeof(got));
	check_bulk(&w, ids, NGROWN);

	/* A destroy after growth still moves the last entity. */
	CHECK_INT(demo_destroy(w.d, w.ids[0]), ==, 0);
	ids[0] = ids[NGROWN - 1];
	check_bulk(&w, ids, NGROWN - 1);
	for (i = 1; i < NENTITIES; i++) {
		if (i != 2)
			start_pos(i, want);
		else
			memcpy(want, pos, sizeof(want));
		check_reads(&h[i], want);
	}
	world_free(&w);
}

/*
 * A destroyed or never-made entity cannot be destroyed or reached natively.
 * Destroying every entity empties the column, and new entities then take
 * the freed IDs.
 */
static void test_destroy_all(void)
{
	float pos[3] = { 0 };
	struct world w;
	uint32_t fresh[2];
	int i, j;

	world_init(&w);
	CHECK_INT(demo_destroy(w.d, w.ids[3]), ==, 0);
	CHECK_INT(demo_destroy(w.d, w.ids[3]), ==, -ENOENT);
	CHECK_INT(demo_get_pos(w.d, w.ids[3], pos), ==, -ENOENT);
	CHECK_INT(demo_set_pos(w.d, w.ids[3], pos), ==, -ENOENT);
	CHECK_INT(demo_destroy(w.d, 1000), ==, -ENOENT);
	CHECK_INT(demo_destroy(w.d, UINT32_MAX), ==, -ENOENT);

	for (i = 0; i < NENTITIES; i++) {
		if (i != 3)
			CHECK_INT(demo_destroy(w.d, w.ids[i]), ==, 0);
	}
	check_bulk(&w, NULL, 0);

	for (i = 0; i < 2; i++) {
		CHECK_INT(demo_create(w.d, &fresh[i]), ==, 0);
		for (j = 0; j < NENTITIES; j++) {
			if (fresh[i] == w.ids[j])
				break;
		}
		CHECK_INT(j, <, NENTITIES);
	}
	CHECK_UINT(fresh[0], !=, fresh[1]);
	check_bulk(&w, fresh, 2);
	world_free(&w);
}

int main(void)
{
	test_swap_remove();
	test_bulk_after_destroy();
	test_id_reuse();
	test_stale_write();
	test_stale_every_op();
	test_growth();
	test_destroy_all();
	return 0;
}
