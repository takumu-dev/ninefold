/* SPDX-License-Identifier: Zlib */
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ninefold.h"

#include "check.h"
#include "demo.h"

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#define MESSAGE "Health must be from 0 to 100."

struct world {
	struct nine_tree *t;
	struct demo *d;
	struct nine_ns *ns;
};

/* A tree with the schemas and a demo instance at /world, of @n entities. */
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
	CHECK_INT(demo_add_schemas(w->t), ==, 0);
	CHECK_INT(nine_mount(w->t, "/world", demo_ops(), w->d), ==, 0);
	w->ns = nine_tree_ns(w->t);
}

static void world_free(struct world *w)
{
	nine_tree_free(w->t);
	demo_free(w->d);
}

/* Open the value file of the column @name of unit of an entity. */
static void open_unit(struct world *w, uint32_t id, const char *name, int mode,
		      struct nine_handle *h)
{
	char path[64];

	snprintf(path, sizeof(path), "/world/entities/%lu/components/unit/%s",
		 (unsigned long)id, name);
	CHECK_INT(nine_open(w->ns, path, mode, h), ==, 0);
}

static void open_health(struct world *w, uint32_t id, int mode,
			struct nine_handle *h)
{
	open_unit(w, id, "health", mode, h);
}

static void check_text(const struct nine_handle *h, const char *line)
{
	char buf[NINE_TEXT_MAX];
	size_t len = strlen(line);

	CHECK_INT(nine_read_text(h, buf, sizeof(buf)), ==, len);
	CHECK_MEM(buf, line, len);
}

static void check_health(const struct world *w, uint32_t id, int32_t want)
{
	int32_t got;

	CHECK_INT(demo_get_health(w->d, id, &got), ==, 0);
	CHECK_INT(got, ==, want);
}

static void test_text_round_trip(void)
{
	struct nine_handle h;
	struct world w;
	int32_t binary;

	world_init(&w, 2);
	open_health(&w, 1, NINE_RDWR, &h);
	check_text(&h, "0\n");

	CHECK_INT(nine_write_text(&h, "75\n", 3), ==, 0);
	check_text(&h, "75\n");
	check_health(&w, 1, 75);
	check_health(&w, 0, 0);

	/* One handle serves both forms. */
	CHECK_INT(nine_read(&h, &binary, sizeof(binary), 0), ==,
		  sizeof(binary));
	CHECK_INT(binary, ==, 75);
	world_free(&w);
}

static void test_bad_value_leaves_health_and_gives_message(void)
{
	struct nine_handle h;
	struct nine_stat st;
	struct world w;

	world_init(&w, 1);
	open_health(&w, 0, NINE_WRONLY, &h);
	CHECK_INT(nine_write_text(&h, "60", 2), ==, 0);
	CHECK_INT(nine_write_text(&h, "150", 3), ==, -EINVAL);
	check_health(&w, 0, 60);

	CHECK_INT(nine_stat(&h, &st), ==, 0);
	CHECK_STR(st.type.field->message, MESSAGE);
	world_free(&w);
}

/* Native code and binary writes are not checked, and a text read shows it. */
static void test_unchecked_value_shows_in_text(void)
{
	const int32_t bad = -3;
	struct nine_handle h;
	struct world w;

	world_init(&w, 1);
	open_health(&w, 0, NINE_RDWR, &h);
	CHECK_INT(demo_set_health(w.d, 0, 150), ==, 0);
	check_text(&h, "150\n");
	CHECK_INT(nine_write(&h, &bad, sizeof(bad), 0), ==, 0);
	check_text(&h, "-3\n");
	world_free(&w);
}

static void test_value_file_type(void)
{
	struct nine_handle h;
	struct nine_stat st;
	struct world w;

	world_init(&w, 1);
	open_health(&w, 0, NINE_RDONLY, &h);
	CHECK_INT(nine_stat(&h, &st), ==, 0);
	CHECK_INT(st.kind, ==, NINE_FILE);
	CHECK_UINT(st.size, ==, sizeof(int32_t));
	CHECK_PTR(st.type.field, !=, NULL);
	CHECK_STR(st.type.field->name, "health");
	CHECK_INT(st.type.field->elem, ==, NINE_I32);
	CHECK_INT(st.type.field->shape, ==, NINE_SCALAR);
	CHECK_INT(st.type.field->range.i.min, ==, 0);
	CHECK_INT(st.type.field->range.i.max, ==, 100);
	CHECK_UINT(st.type.flags, ==, 0);
	world_free(&w);
}

/* The bulk file has the same field and the flag, and stays binary. */
static void test_bulk_file_is_typed_and_binary(void)
{
	const int32_t want[2] = { 10, 20 };
	struct nine_handle h, entity;
	struct nine_stat st, entity_st;
	struct world w;
	char buf[NINE_TEXT_MAX];
	int32_t got[2];

	world_init(&w, 2);
	CHECK_INT(
		nine_open(w.ns, "/world/components/unit/health", NINE_RDWR, &h),
		==, 0);
	open_health(&w, 0, NINE_RDONLY, &entity);
	CHECK_INT(nine_stat(&h, &st), ==, 0);
	CHECK_INT(nine_stat(&entity, &entity_st), ==, 0);
	CHECK_UINT(st.size, ==, sizeof(got));
	CHECK_PTR(st.type.field, ==, entity_st.type.field);
	CHECK_UINT(st.type.flags, ==, NINE_TYPE_BULK);

	CHECK_INT(nine_read_text(&h, buf, sizeof(buf)), ==, -ENOTSUP);
	CHECK_INT(nine_write_text(&h, "50", 2), ==, -ENOTSUP);

	CHECK_INT(nine_write(&h, want, sizeof(want), 0), ==, 0);
	CHECK_INT(nine_read(&h, got, sizeof(got), 0), ==, sizeof(got));
	CHECK_MEM(got, want, sizeof(want));
	check_health(&w, 0, 10);
	check_health(&w, 1, 20);
	world_free(&w);
}

static void test_ids_and_directories_have_no_text_form(void)
{
	static const char *const paths[] = {
		"/world/components/transform/ids",
		"/world/components/unit",
		"/world/entities/0",
	};
	struct nine_handle h;
	struct nine_stat st;
	struct world w;
	char buf[NINE_TEXT_MAX];
	size_t i;

	world_init(&w, 1);
	for (i = 0; i < ARRAY_SIZE(paths); i++) {
		CHECK_INT(nine_open(w.ns, paths[i], NINE_RDWR, &h), ==, 0);
		CHECK_INT(nine_stat(&h, &st), ==, 0);
		CHECK_PTR(st.type.field, ==, NULL);
		CHECK_INT(nine_read_text(&h, buf, sizeof(buf)), ==, -ENOTSUP);
		CHECK_INT(nine_write_text(&h, "1", 1), ==, -ENOTSUP);
	}
	world_free(&w);
}

static void test_stale_handle(void)
{
	struct nine_handle h;
	struct world w;
	char buf[NINE_TEXT_MAX];

	world_init(&w, 2);
	open_health(&w, 1, NINE_RDWR, &h);
	CHECK_INT(demo_destroy(w.d, 1), ==, 0);
	CHECK_INT(nine_read_text(&h, buf, sizeof(buf)), ==, -ESTALE);
	CHECK_INT(nine_write_text(&h, "1", 1), ==, -ESTALE);
	world_free(&w);
}

/* A swap-remove moves the health with its entity. */
static void test_health_follows_its_entity(void)
{
	struct nine_handle h;
	struct world w;

	world_init(&w, 3);
	open_health(&w, 2, NINE_RDWR, &h);
	CHECK_INT(nine_write_text(&h, "42", 2), ==, 0);
	CHECK_INT(demo_destroy(w.d, 0), ==, 0);
	check_text(&h, "42\n");
	check_health(&w, 2, 42);
	world_free(&w);
}

static void check_alive(const struct world *w, uint32_t id, bool want)
{
	bool got;

	CHECK_INT(demo_get_alive(w->d, id, &got), ==, 0);
	CHECK_INT(got, ==, want);
}

static void check_state(const struct world *w, uint32_t id, uint8_t want)
{
	uint8_t got;

	CHECK_INT(demo_get_state(w->d, id, &got), ==, 0);
	CHECK_UINT(got, ==, want);
}

static void test_alive_round_trip(void)
{
	struct nine_handle h;
	struct world w;

	world_init(&w, 2);
	open_unit(&w, 1, "alive", NINE_RDWR, &h);
	check_text(&h, "false\n");
	CHECK_INT(nine_write_text(&h, "true\n", 5), ==, 0);
	check_text(&h, "true\n");
	check_alive(&w, 1, true);
	check_alive(&w, 0, false);

	CHECK_INT(nine_write_text(&h, "1", 1), ==, -EINVAL);
	check_alive(&w, 1, true);
	CHECK_INT(demo_set_alive(w.d, 1, false), ==, 0);
	check_text(&h, "false\n");
	world_free(&w);
}

static void test_state_round_trip(void)
{
	struct nine_handle h;
	struct nine_stat st;
	struct world w;

	world_init(&w, 2);
	open_unit(&w, 0, "state", NINE_RDWR, &h);
	check_text(&h, "idle\n");
	CHECK_INT(nine_write_text(&h, "run", 3), ==, 0);
	check_text(&h, "run\n");
	check_state(&w, 0, DEMO_RUN);
	check_state(&w, 1, DEMO_IDLE);
	CHECK_INT(demo_set_state(w.d, 0, DEMO_WALK), ==, 0);
	check_text(&h, "walk\n");

	CHECK_INT(nine_write_text(&h, "jump", 4), ==, -EINVAL);
	CHECK_INT(nine_write_text(&h, "2", 1), ==, -EINVAL);
	check_state(&w, 0, DEMO_WALK);
	CHECK_INT(nine_stat(&h, &st), ==, 0);
	CHECK_STR(st.type.field->message, "Pick a known state.");
	world_free(&w);
}

/* Native code may store a state that is not listed, and a read shows it. */
static void test_unlisted_state_shows_as_number(void)
{
	struct nine_handle h;
	struct world w;

	world_init(&w, 1);
	open_unit(&w, 0, "state", NINE_RDONLY, &h);
	CHECK_INT(demo_set_state(w.d, 0, 7), ==, 0);
	check_text(&h, "7\n");
	world_free(&w);
}

/* Each new column has its type on both files, and its bulk file is binary. */
static void test_new_columns_are_typed(void)
{
	static const struct {
		const char *name;
		enum nine_elem elem;
	} columns[] = {
		{ "alive", NINE_BOOL },
		{ "state", NINE_U8 },
	};
	const uint8_t want[2] = { 1, 2 };
	struct nine_handle bulk, entity;
	struct nine_stat st, entity_st;
	char path[64];
	struct world w;
	uint8_t got[2];
	size_t i;

	world_init(&w, 2);
	for (i = 0; i < ARRAY_SIZE(columns); i++) {
		snprintf(path, sizeof(path), "/world/components/unit/%s",
			 columns[i].name);
		CHECK_INT(nine_open(w.ns, path, NINE_RDWR, &bulk), ==, 0);
		open_unit(&w, 1, columns[i].name, NINE_RDONLY, &entity);
		CHECK_INT(nine_stat(&bulk, &st), ==, 0);
		CHECK_INT(nine_stat(&entity, &entity_st), ==, 0);
		CHECK_STR(entity_st.type.field->name, columns[i].name);
		CHECK_INT(entity_st.type.field->elem, ==, columns[i].elem);
		CHECK_UINT(entity_st.size, ==, 1);
		CHECK_UINT(entity_st.type.flags, ==, 0);
		CHECK_PTR(st.type.field, ==, entity_st.type.field);
		CHECK_UINT(st.type.flags, ==, NINE_TYPE_BULK);
		CHECK_UINT(st.size, ==, sizeof(got));

		CHECK_INT(nine_write(&bulk, want, sizeof(want), 0), ==, 0);
		CHECK_INT(nine_read(&bulk, got, sizeof(got), 0), ==,
			  sizeof(got));
		CHECK_MEM(got, want, sizeof(want));
	}
	check_alive(&w, 0, true);
	check_state(&w, 1, DEMO_RUN);
	world_free(&w);
}

/* A swap-remove moves every column of unit with its entity. */
static void test_unit_follows_its_entity(void)
{
	struct nine_handle alive, state;
	struct world w;

	world_init(&w, 3);
	open_unit(&w, 2, "alive", NINE_RDWR, &alive);
	open_unit(&w, 2, "state", NINE_RDWR, &state);
	CHECK_INT(nine_write_text(&alive, "true", 4), ==, 0);
	CHECK_INT(nine_write_text(&state, "walk", 4), ==, 0);
	CHECK_INT(demo_destroy(w.d, 0), ==, 0);
	check_text(&alive, "true\n");
	check_text(&state, "walk\n");
	check_alive(&w, 2, true);
	check_state(&w, 2, DEMO_WALK);
	world_free(&w);
}

int main(void)
{
	test_text_round_trip();
	test_bad_value_leaves_health_and_gives_message();
	test_unchecked_value_shows_in_text();
	test_value_file_type();
	test_bulk_file_is_typed_and_binary();
	test_ids_and_directories_have_no_text_form();
	test_stale_handle();
	test_health_follows_its_entity();
	test_alive_round_trip();
	test_state_round_trip();
	test_unlisted_state_shows_as_number();
	test_new_columns_are_typed();
	test_unit_follows_its_entity();
	return 0;
}
