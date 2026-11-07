/* SPDX-License-Identifier: Zlib */
#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "ninefold.h"

#include "check.h"
#include "demo.h"

#define NENTITIES 5

static void open_entity_pos(struct nine_ns *ns, uint32_t id,
			    struct nine_handle *h)
{
	char path[64];
	int err;

	snprintf(path, sizeof(path),
		 "/world/entities/%lu/components/transform/pos",
		 (unsigned long)id);
	err = nine_open(ns, path, NINE_RDONLY, h);
	CHECK_INT(err, ==, 0);
}

static ptrdiff_t read_entity_pos(struct nine_ns *ns, uint32_t id, void *buf,
				 size_t len, uint64_t off)
{
	struct nine_handle h;

	open_entity_pos(ns, id, &h);
	return nine_read(&h, buf, len, off);
}

/* Each entity's value file holds the same 12 bytes as its native position. */
static void test_reads_native_positions(void)
{
	struct nine_tree *t;
	struct demo *d;
	uint32_t ids[NENTITIES];
	float want[3], got[3];
	int i;

	t = nine_tree_new();
	d = demo_new();
	CHECK_PTR(t, !=, NULL);
	CHECK_PTR(d, !=, NULL);

	for (i = 0; i < NENTITIES; i++) {
		CHECK_INT(demo_create(d, &ids[i]), ==, 0);
		want[0] = (float)i;
		want[1] = (float)i * 10.0f;
		want[2] = (float)-i;
		CHECK_INT(demo_set_pos(d, ids[i], want), ==, 0);
	}
	CHECK_INT(nine_mount(t, "/world", demo_ops(), d), ==, 0);

	for (i = 0; i < NENTITIES; i++) {
		CHECK_INT(demo_get_pos(d, ids[i], want), ==, 0);
		CHECK_INT(read_entity_pos(nine_tree_ns(t), ids[i], got,
					  sizeof(got), 0),
			  ==, (ptrdiff_t)sizeof(got));
		CHECK_MEM(got, want, sizeof(got));
	}

	nine_tree_free(t);
	demo_free(d);
}

/* A read is short only at the end of the file, and empty at or after it. */
static void test_read_counts(void)
{
	static const float pos[3] = { 1.5f, -2.0f, 4.25f };
	struct nine_tree *t;
	struct nine_ns *ns;
	struct demo *d;
	unsigned char buf[32];
	uint32_t id;

	t = nine_tree_new();
	d = demo_new();
	CHECK_PTR(t, !=, NULL);
	CHECK_PTR(d, !=, NULL);
	CHECK_INT(demo_create(d, &id), ==, 0);
	CHECK_INT(demo_set_pos(d, id, pos), ==, 0);
	CHECK_INT(nine_mount(t, "/world", demo_ops(), d), ==, 0);
	ns = nine_tree_ns(t);

	CHECK_INT(read_entity_pos(ns, id, buf, 12, 0), ==, 12);
	CHECK_INT(read_entity_pos(ns, id, buf, sizeof(buf), 0), ==, 12);
	CHECK_MEM(buf, pos, 12);

	CHECK_INT(read_entity_pos(ns, id, buf, 4, 0), ==, 4);
	CHECK_MEM(buf, &pos[0], 4);
	CHECK_INT(read_entity_pos(ns, id, buf, 4, 4), ==, 4);
	CHECK_MEM(buf, &pos[1], 4);

	CHECK_INT(read_entity_pos(ns, id, buf, sizeof(buf), 8), ==, 4);
	CHECK_MEM(buf, &pos[2], 4);
	CHECK_INT(read_entity_pos(ns, id, buf, sizeof(buf), 11), ==, 1);

	CHECK_INT(read_entity_pos(ns, id, buf, sizeof(buf), 12), ==, 0);
	CHECK_INT(read_entity_pos(ns, id, buf, sizeof(buf), 13), ==, 0);
	CHECK_INT(read_entity_pos(ns, id, buf, sizeof(buf), UINT64_MAX), ==, 0);
	CHECK_INT(read_entity_pos(ns, id, buf, 0, 0), ==, 0);

	nine_tree_free(t);
	demo_free(d);
}

/* A read of the whole value into a longer buffer fills only the value's bytes. */
static void test_whole_value_buffer(void)
{
	static const float pos[3] = { 1.5f, -2.0f, 4.25f };
	unsigned char buf[32], fill[32];
	struct nine_tree *t;
	struct demo *d;
	uint32_t id;

	t = nine_tree_new();
	d = demo_new();
	CHECK_PTR(t, !=, NULL);
	CHECK_PTR(d, !=, NULL);
	CHECK_INT(demo_create(d, &id), ==, 0);
	CHECK_INT(demo_set_pos(d, id, pos), ==, 0);
	CHECK_INT(nine_mount(t, "/world", demo_ops(), d), ==, 0);

	memset(fill, 0xa5, sizeof(fill));
	memcpy(buf, fill, sizeof(buf));
	CHECK_INT(read_entity_pos(nine_tree_ns(t), id, buf, sizeof(buf), 0), ==,
		  12);
	CHECK_MEM(buf, pos, 12);
	CHECK_MEM(buf + 12, fill, sizeof(buf) - 12);

	nine_tree_free(t);
	demo_free(d);
}

/* A read through a stale node fails and copies nothing. */
static void test_stale_read(void)
{
	unsigned char buf[32], fill[32];
	struct nine_tree *t;
	struct nine_handle h;
	struct demo *d;
	uint32_t id;

	t = nine_tree_new();
	d = demo_new();
	CHECK_PTR(t, !=, NULL);
	CHECK_PTR(d, !=, NULL);
	CHECK_INT(demo_create(d, &id), ==, 0);
	CHECK_INT(nine_mount(t, "/world", demo_ops(), d), ==, 0);
	open_entity_pos(nine_tree_ns(t), id, &h);
	CHECK_INT(demo_destroy(d, id), ==, 0);

	memset(fill, 0xa5, sizeof(fill));
	memcpy(buf, fill, sizeof(buf));
	CHECK_INT(nine_read(&h, buf, sizeof(buf), 0), ==, -ESTALE);
	CHECK_MEM(buf, fill, sizeof(buf));

	nine_tree_free(t);
	demo_free(d);
}

int main(void)
{
	test_reads_native_positions();
	test_read_counts();
	test_whole_value_buffer();
	test_stale_read();
	return 0;
}
