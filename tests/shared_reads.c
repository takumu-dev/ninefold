/* SPDX-License-Identifier: Zlib */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ninefold.h"

#include "check.h"
#include "demo.h"
#include "listing.h"
#include "thread.h"

#define NTHREADS 8
#define NROUNDS 200
#define NENTITIES 6

#define POS_SIZE (3 * sizeof(float))

/*
 * A tree with a demo instance at /world, holding entities 0 to
 * NENTITIES - 1, entity i at start_pos(i). After setup, the threads only
 * read it, through the handles in hs[] that they share and through handles
 * of their own.
 */
struct world {
	struct nine_tree *t;
	struct demo *d;
	struct nine_ns *ns;
	uint32_t ids[NENTITIES];
	float pos[NENTITIES][3];
	struct nine_handle hs[NENTITIES];
};

/* The position entity number @i starts at. */
static void start_pos(int i, float pos[3])
{
	pos[0] = (float)i + 0.5f;
	pos[1] = (float)i * -10.0f;
	pos[2] = (float)i * 100.0f + 0.25f;
}

static void open_entity_pos(struct nine_ns *ns, uint32_t id,
			    struct nine_handle *h)
{
	char path[64];

	snprintf(path, sizeof(path),
		 "/world/entities/%lu/components/transform/pos",
		 (unsigned long)id);
	CHECK_INT(nine_open(ns, path, NINE_RDONLY, h), ==, 0);
}

static void world_init(struct world *w)
{
	int i;

	w->t = nine_tree_new();
	w->d = demo_new();
	CHECK_PTR(w->t, !=, NULL);
	CHECK_PTR(w->d, !=, NULL);

	for (i = 0; i < NENTITIES; i++) {
		CHECK_INT(demo_create(w->d, &w->ids[i]), ==, 0);
		CHECK_UINT(w->ids[i], ==, i);
		start_pos(i, w->pos[i]);
		CHECK_INT(demo_set_pos(w->d, w->ids[i], w->pos[i]), ==, 0);
		CHECK_INT(demo_set_health(w->d, w->ids[i], i), ==, 0);
	}
	CHECK_INT(demo_add_schemas(w->t), ==, 0);
	CHECK_INT(nine_mount(w->t, "/world", demo_ops(), w->d), ==, 0);
	w->ns = nine_tree_ns(w->t);

	for (i = 0; i < NENTITIES; i++)
		open_entity_pos(w->ns, w->ids[i], &w->hs[i]);
}

static void world_free(struct world *w)
{
	nine_tree_free(w->t);
	demo_free(w->d);
}

/* Read each entity through a handle of this thread's own. */
static void read_values(const struct world *w)
{
	struct nine_handle h;
	float got[3];
	int i;

	for (i = 0; i < NENTITIES; i++) {
		open_entity_pos(w->ns, w->ids[i], &h);
		CHECK_INT(nine_read(&h, got, sizeof(got), 0), ==,
			  (ptrdiff_t)sizeof(got));
		CHECK_MEM(got, w->pos[i], sizeof(got));
	}
}

/* The text of the position of each entity, from start_pos(). */
static const char *const pos_lines[NENTITIES] = {
	"0.5 -0 0.25\n",    "1.5 -10 100.25\n", "2.5 -20 200.25\n",
	"3.5 -30 300.25\n", "4.5 -40 400.25\n", "5.5 -50 500.25\n",
};

/*
 * Read each entity's health and position as text, through handles of this
 * thread's own, and its position through the shared handles too.
 */
static void read_text(const struct world *w)
{
	char path[64], want[2], buf[NINE_TEXT_MAX];
	struct nine_handle h;
	size_t len;
	int i;

	for (i = 0; i < NENTITIES; i++) {
		snprintf(path, sizeof(path),
			 "/world/entities/%lu/components/unit/health",
			 (unsigned long)w->ids[i]);
		CHECK_INT(nine_open(w->ns, path, NINE_RDONLY, &h), ==, 0);
		want[0] = (char)('0' + i);
		want[1] = '\n';
		CHECK_INT(nine_read_text(&h, buf, sizeof(buf)), ==,
			  sizeof(want));
		CHECK_MEM(buf, want, sizeof(want));

		len = strlen(pos_lines[i]);
		open_entity_pos(w->ns, w->ids[i], &h);
		CHECK_INT(nine_read_text(&h, buf, sizeof(buf)), ==, len);
		CHECK_MEM(buf, pos_lines[i], len);
		CHECK_INT(nine_read_text(&w->hs[i], buf, sizeof(buf)), ==, len);
		CHECK_MEM(buf, pos_lines[i], len);
	}
}

/* Read every entity in one batch through the shared handles. */
static void read_batch(const struct world *w)
{
	float got[NENTITIES][3];
	int errs[NENTITIES];

	CHECK_INT(nine_read_many(w->hs, NENTITIES, got, POS_SIZE, errs), ==, 0);
	CHECK_MEM(got, w->pos, sizeof(got));
}

/* Read the whole column and the order of the IDs through the bulk files. */
static void read_bulk(const struct world *w)
{
	struct nine_handle h;
	struct nine_stat st;
	float pos[NENTITIES][3];
	uint32_t ids[NENTITIES];

	CHECK_INT(nine_open(w->ns, "/world/components/transform/pos",
			    NINE_RDONLY, &h),
		  ==, 0);
	CHECK_INT(nine_stat(&h, &st), ==, 0);
	CHECK_UINT(st.size, ==, sizeof(pos));
	CHECK_INT(nine_read(&h, pos, sizeof(pos), 0), ==,
		  (ptrdiff_t)sizeof(pos));
	CHECK_MEM(pos, w->pos, sizeof(pos));

	CHECK_INT(nine_open(w->ns, "/world/components/transform/ids",
			    NINE_RDONLY, &h),
		  ==, 0);
	CHECK_INT(nine_read(&h, ids, sizeof(ids), 0), ==,
		  (ptrdiff_t)sizeof(ids));
	CHECK_MEM(ids, w->ids, sizeof(ids));
}

static void read_dirs(const struct world *w)
{
	struct nine_handle h;
	char names[64];

	CHECK_INT(nine_open(w->ns, "/", NINE_RDONLY, &h), ==, 0);
	CHECK_STR(list_names(&h, 16, names, sizeof(names)), "world ");
	CHECK_INT(nine_open(w->ns, "/world/entities", NINE_RDONLY, &h), ==, 0);
	CHECK_STR(list_names(&h, 16, names, sizeof(names)), "0 1 2 3 4 5 ");
}

static void read_world(void *arg)
{
	const struct world *w = arg;
	int i;

	for (i = 0; i < NROUNDS; i++) {
		read_values(w);
		read_text(w);
		read_batch(w);
		read_bulk(w);
		read_dirs(w);
	}
}

/* Many threads read one demo instance at the same time, with no writes. */
int main(void)
{
	struct test_thread threads[NTHREADS];
	struct world w;
	int i;

	world_init(&w);
	for (i = 0; i < NTHREADS; i++)
		CHECK_INT(test_thread_start(&threads[i], read_world, &w), ==,
			  0);
	for (i = 0; i < NTHREADS; i++)
		CHECK_INT(test_thread_join(&threads[i]), ==, 0);
	world_free(&w);
	return 0;
}
