/* SPDX-License-Identifier: Zlib */
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ninefold.h"

#include "check.h"
#include "demo.h"

struct world {
	struct nine_tree *t;
	struct demo *d;
	struct nine_ns *ns;
	uint32_t id;
};

/* A tree with a demo instance at /world, holding one entity. */
static void world_init(struct world *w)
{
	w->t = nine_tree_new();
	w->d = demo_new();
	CHECK_PTR(w->t, !=, NULL);
	CHECK_PTR(w->d, !=, NULL);
	CHECK_INT(demo_create(w->d, &w->id), ==, 0);
	CHECK_INT(nine_mount(w->t, "/world", demo_ops(), w->d), ==, 0);
	w->ns = nine_tree_ns(w->t);
}

static void world_free(struct world *w)
{
	nine_tree_free(w->t);
	demo_free(w->d);
}

/* Open @rest under the entity's directory, such as "components/transform". */
static void open_entity(struct world *w, const char *rest, int mode,
			struct nine_handle *h)
{
	char path[64];

	snprintf(path, sizeof(path), "/world/entities/%lu/%s",
		 (unsigned long)w->id, rest);
	CHECK_INT(nine_open(w->ns, path, mode, h), ==, 0);
}

/* Open the value file of the entity's position. */
static void open_pos(struct world *w, int mode, struct nine_handle *h)
{
	open_entity(w, "components/transform/pos", mode, h);
}

/* A write through the tree shows in the native data at once. */
static void test_native_reads_write(void)
{
	static const float pos[3] = { 1.5f, -2.0f, 4.25f };
	struct nine_handle h;
	struct world w;
	float got[3];

	world_init(&w);
	open_pos(&w, NINE_WRONLY, &h);
	CHECK_INT(nine_write(&h, pos, sizeof(pos), 0), ==, 0);
	CHECK_INT(demo_get_pos(w.d, w.id, got), ==, 0);
	CHECK_MEM(got, pos, sizeof(got));
	world_free(&w);
}

/* A native change shows in the next read through the same handle. */
static void test_round_trip(void)
{
	static const float first[3] = { 1.0f, 2.0f, 3.0f };
	static const float second[3] = { -4.0f, 5.5f, 6.0f };
	struct nine_handle h;
	struct world w;
	float got[3];

	world_init(&w);
	open_pos(&w, NINE_RDWR, &h);
	CHECK_INT(nine_write(&h, first, sizeof(first), 0), ==, 0);
	CHECK_INT(nine_read(&h, got, sizeof(got), 0), ==,
		  (ptrdiff_t)sizeof(got));
	CHECK_MEM(got, first, sizeof(got));

	CHECK_INT(demo_set_pos(w.d, w.id, second), ==, 0);
	CHECK_INT(nine_read(&h, got, sizeof(got), 0), ==,
		  (ptrdiff_t)sizeof(got));
	CHECK_MEM(got, second, sizeof(got));
	world_free(&w);
}

/* A write at an offset changes only the bytes it covers. */
static void test_partial_write(void)
{
	static const float start[3] = { 1.0f, 2.0f, 3.0f };
	static const float y = 20.0f;
	static const float want[3] = { 1.0f, 20.0f, 3.0f };
	static const unsigned char last = 0x7f;
	struct nine_handle h;
	struct world w;
	unsigned char bytes[12];
	float got[3];

	world_init(&w);
	CHECK_INT(demo_set_pos(w.d, w.id, start), ==, 0);
	open_pos(&w, NINE_WRONLY, &h);

	CHECK_INT(nine_write(&h, &y, sizeof(y), 4), ==, 0);
	CHECK_INT(demo_get_pos(w.d, w.id, got), ==, 0);
	CHECK_MEM(got, want, sizeof(got));

	CHECK_INT(nine_write(&h, &last, 1, 11), ==, 0);
	CHECK_INT(demo_get_pos(w.d, w.id, got), ==, 0);
	memcpy(bytes, want, sizeof(bytes));
	bytes[11] = last;
	CHECK_MEM(got, bytes, sizeof(got));

	/* An empty write inside the file, or at its end, changes nothing. */
	CHECK_INT(nine_write(&h, &last, 0, 0), ==, 0);
	CHECK_INT(nine_write(&h, &last, 0, 12), ==, 0);
	CHECK_INT(demo_get_pos(w.d, w.id, got), ==, 0);
	CHECK_MEM(got, bytes, sizeof(got));
	world_free(&w);
}

/* A write that runs past the end of the file writes nothing. */
static void test_write_past_end(void)
{
	static const float start[3] = { 1.0f, 2.0f, 3.0f };
	static const unsigned char buf[16] = { 0xff, 0xff, 0xff, 0xff };
	struct nine_handle h;
	struct world w;
	float got[3];

	world_init(&w);
	CHECK_INT(demo_set_pos(w.d, w.id, start), ==, 0);
	open_pos(&w, NINE_WRONLY, &h);

	CHECK_INT(nine_write(&h, buf, 13, 0), ==, -EINVAL);
	CHECK_INT(nine_write(&h, buf, 12, 4), ==, -EINVAL);
	CHECK_INT(nine_write(&h, buf, 8, 8), ==, -EINVAL);
	CHECK_INT(nine_write(&h, buf, 1, 12), ==, -EINVAL);
	CHECK_INT(nine_write(&h, buf, 0, 13), ==, -EINVAL);
	CHECK_INT(nine_write(&h, buf, 4, UINT64_MAX), ==, -EINVAL);
	CHECK_INT(nine_write(&h, buf, SIZE_MAX, 4), ==, -EINVAL);

	CHECK_INT(demo_get_pos(w.d, w.id, got), ==, 0);
	CHECK_MEM(got, start, sizeof(got));
	world_free(&w);
}

/* A directory refuses every write, even an empty one. */
static void test_write_dir(void)
{
	static const float pos[3] = { 1.0f, 2.0f, 3.0f };
	struct nine_handle h;
	struct world w;

	world_init(&w);
	CHECK_INT(nine_open(w.ns, "/world/entities", NINE_WRONLY, &h), ==, 0);
	CHECK_INT(nine_write(&h, pos, sizeof(pos), 0), ==, -EISDIR);
	open_entity(&w, "components/transform", NINE_RDWR, &h);
	CHECK_INT(nine_write(&h, pos, 0, 0), ==, -EISDIR);
	world_free(&w);
}

int main(void)
{
	test_native_reads_write();
	test_round_trip();
	test_partial_write();
	test_write_past_end();
	test_write_dir();
	return 0;
}
