/* SPDX-License-Identifier: Zlib */
#include <stdint.h>
#include <string.h>

#include "check.h"
#include "demo.h"

#define NENTITIES 5

/* The position entity number @i of a test starts at. */
static void start_pos(int i, float pos[3])
{
	pos[0] = (float)i + 0.5f;
	pos[1] = (float)i * -10.0f;
	pos[2] = (float)i * 100.0f + 0.25f;
}

/*
 * The column's arrays find each live entity's position through its slot,
 * after a swap-remove has moved the last entity, and they are the memory
 * that the native calls change.
 */
static void test_column(void)
{
	struct demo_column col;
	uint32_t ids[NENTITIES];
	struct demo *d;
	float want[3], got[3];
	int i;

	d = demo_new();
	CHECK_PTR(d, !=, NULL);
	for (i = 0; i < NENTITIES; i++) {
		CHECK_INT(demo_create(d, &ids[i]), ==, 0);
		start_pos(i, want);
		CHECK_INT(demo_set_pos(d, ids[i], want), ==, 0);
	}
	CHECK_INT(demo_destroy(d, ids[1]), ==, 0);

	demo_get_column(d, &col);
	CHECK_UINT(col.n, ==, NENTITIES - 1);
	CHECK_UINT(col.slots[ids[4]], ==, 1);
	for (i = 0; i < NENTITIES; i++) {
		if (i == 1)
			continue;
		start_pos(i, want);
		CHECK_MEM(col.pos[col.slots[ids[i]]], want, sizeof(want));
	}

	want[0] = 7.0f;
	want[1] = 8.0f;
	want[2] = 9.0f;
	memcpy(col.pos[col.slots[ids[2]]], want, sizeof(want));
	CHECK_INT(demo_get_pos(d, ids[2], got), ==, 0);
	CHECK_MEM(got, want, sizeof(want));
	demo_free(d);
}

int main(void)
{
	test_column();
	return 0;
}
