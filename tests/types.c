/* SPDX-License-Identifier: Zlib */
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ninefold.h"

#include "check.h"
#include "listing.h"

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

/*
 * Made at run time. The macro INFINITY is a constant expression that
 * overflows, and some compilers warn of it.
 */
static double pos_inf;

static const struct nine_field one_field[] = {
	{ .name = "v", .elem = NINE_I32 },
};

static const struct nine_schema schema_s = {
	.name = "s",
	.fields = one_field,
	.nfields = ARRAY_SIZE(one_field),
};

static const struct nine_enum_value states[] = {
	{ "idle", 0 },
	{ "walk", 1 },
	{ "run", 2 },
};

static struct nine_tree *tree_new(void)
{
	struct nine_tree *t = nine_tree_new();

	CHECK_PTR(t, !=, NULL);
	return t;
}

static struct nine_handle open_path(struct nine_tree *t, const char *path,
				    int mode)
{
	struct nine_handle h;

	CHECK_INT(nine_open(nine_tree_ns(t), path, mode, &h), ==, 0);
	return h;
}

/* The names of the directory at @path, each followed by a space. */
static const char *list_path(struct nine_tree *t, const char *path, size_t len,
			     char *out, size_t size)
{
	struct nine_handle h = open_path(t, path, NINE_RDONLY);

	return list_names(&h, len, out, size);
}

/* A tree with @s added and the schemas mounted at /types. */
static struct nine_tree *types_tree(const struct nine_schema *s)
{
	struct nine_tree *t = tree_new();

	CHECK_INT(nine_add_schema(t, s), ==, 0);
	CHECK_INT(nine_mount_types(t, "/types"), ==, 0);
	return t;
}

static void test_lists_each_schema_and_its_fields(void)
{
	static const struct nine_field fields[] = {
		{ .name = "pos", .elem = NINE_F32, .shape = NINE_VEC3 },
		{ .name = "rot", .elem = NINE_F32, .shape = NINE_QUAT },
	};
	static const struct nine_schema transform = {
		.name = "transform",
		.fields = fields,
		.nfields = ARRAY_SIZE(fields),
	};
	struct nine_tree *t = types_tree(&transform);
	char out[256];

	CHECK_INT(nine_add_schema(t, &schema_s), ==, 0);
	CHECK_STR(list_path(t, "/types", LISTING_MAX_BUF, out, sizeof(out)),
		  "transform s ");
	CHECK_STR(list_path(t, "/types/transform", LISTING_MAX_BUF, out,
			    sizeof(out)),
		  "pos rot ");
	CHECK_STR(list_path(t, "/types/s", LISTING_MAX_BUF, out, sizeof(out)),
		  "v ");

	/* A buffer that holds one name at a time still lists each once. */
	CHECK_STR(list_path(t, "/types", sizeof("transform"), out, sizeof(out)),
		  "transform s ");
	CHECK_STR(list_path(t, "/types/transform", 4, out, sizeof(out)),
		  "pos rot ");
	nine_tree_free(t);
}

static void test_name_that_does_not_fit(void)
{
	struct nine_tree *t = types_tree(&schema_s);
	struct nine_handle h = open_path(t, "/types", NINE_RDONLY);
	uint64_t cursor = 0;
	char buf[2];

	CHECK_INT(nine_list(&h, &cursor, buf, 1), ==, -ERANGE);
	CHECK_UINT(cursor, ==, 0);
	CHECK_INT(nine_list(&h, &cursor, buf, 2), ==, 1);
	CHECK_MEM(buf, "s", 2);
	CHECK_INT(nine_list(&h, &cursor, buf, 2), ==, 0);
	nine_tree_free(t);
}

static void test_same_descriptor_twice_lists_once(void)
{
	struct nine_tree *t = tree_new();
	char out[64];

	CHECK_INT(nine_add_schema(t, &schema_s), ==, 0);
	CHECK_INT(nine_add_schema(t, &schema_s), ==, 0);
	CHECK_INT(nine_mount_types(t, "/types"), ==, 0);
	CHECK_STR(list_path(t, "/types", LISTING_MAX_BUF, out, sizeof(out)),
		  "s ");
	nine_tree_free(t);
}

static void test_schema_added_after_the_mount(void)
{
	struct nine_tree *t = tree_new();
	char out[64];

	CHECK_INT(nine_mount_types(t, "/types"), ==, 0);
	CHECK_STR(list_path(t, "/types", LISTING_MAX_BUF, out, sizeof(out)),
		  "");
	CHECK_INT(nine_add_schema(t, &schema_s), ==, 0);
	CHECK_STR(list_path(t, "/types", LISTING_MAX_BUF, out, sizeof(out)),
		  "s ");
	nine_tree_free(t);
}

/*
 * The line of @f is @want, from nine_field_text() and from a read of its
 * file in /types/, and stat gives its length as the size of an untyped file.
 */
static void check_line(const struct nine_field *f, const char *want)
{
	const struct nine_schema s = { .name = "s", .fields = f, .nfields = 1 };
	char path[64];
	size_t len = strlen(want);
	char buf[NINE_TEXT_MAX];
	struct nine_tree *t;
	struct nine_handle h;
	struct nine_stat st;

	CHECK_INT(nine_field_text(f, buf, sizeof(buf)), ==, (ptrdiff_t)len);
	CHECK_MEM(buf, want, len);

	t = types_tree(&s);
	snprintf(path, sizeof(path), "/types/s/%s", f->name);
	h = open_path(t, path, NINE_RDONLY);
	CHECK_INT(nine_stat(&h, &st), ==, 0);
	CHECK_INT(st.kind, ==, NINE_FILE);
	CHECK_UINT(st.size, ==, len);
	CHECK_PTR(st.type.field, ==, NULL);
	CHECK_UINT(st.type.flags, ==, 0);
	memset(buf, 0, sizeof(buf));
	CHECK_INT(nine_read(&h, buf, sizeof(buf), 0), ==, (ptrdiff_t)len);
	CHECK_MEM(buf, want, len);
	nine_tree_free(t);
}

static void test_example_lines(void)
{
	static const char state_line[] =
		"enum u8 values=idle:0,walk:1,run:2 message=Pick a known state.\n";

	check_line(&(struct nine_field){ .name = "pos",
					 .elem = NINE_F32,
					 .shape = NINE_VEC3,
					 .unit = "m" },
		   "vec3 f32 unit=m\n");
	check_line(&(struct nine_field){ .name = "rot",
					 .elem = NINE_F32,
					 .shape = NINE_QUAT },
		   "quat f32\n");
	check_line(
		&(struct nine_field){ .name = "health",
				      .elem = NINE_I32,
				      .range.i = { 0, 100 },
				      .message =
					      "Health must be from 0 to 100." },
		"i32 range=0..100 message=Health must be from 0 to 100.\n");
	check_line(&(struct nine_field){ .name = "alive", .elem = NINE_BOOL },
		   "bool\n");
	check_line(&(struct nine_field){ .name = "state",
					 .elem = NINE_U8,
					 .values = states,
					 .nvalues = ARRAY_SIZE(states),
					 .message = "Pick a known state." },
		   state_line);
}

static void test_each_element_type_and_shape(void)
{
	static const char *const lines[] = {
		[NINE_I8] = "i8\n",	[NINE_I16] = "i16\n",
		[NINE_I32] = "i32\n",	[NINE_I64] = "i64\n",
		[NINE_U8] = "u8\n",	[NINE_U16] = "u16\n",
		[NINE_U32] = "u32\n",	[NINE_U64] = "u64\n",
		[NINE_F32] = "f32\n",	[NINE_F64] = "f64\n",
		[NINE_BOOL] = "bool\n",
	};
	int e;

	for (e = NINE_I8; e <= NINE_BOOL; e++)
		check_line(&(struct nine_field){ .name = "v",
						 .elem = (enum nine_elem)e },
			   lines[e]);

	check_line(&(struct nine_field){ .name = "v",
					 .elem = NINE_I16,
					 .shape = NINE_VEC2 },
		   "vec2 i16\n");
	check_line(&(struct nine_field){ .name = "v",
					 .elem = NINE_U64,
					 .shape = NINE_VEC4 },
		   "vec4 u64\n");
	check_line(&(struct nine_field){ .name = "v",
					 .elem = NINE_F64,
					 .shape = NINE_QUAT },
		   "quat f64\n");
}

static void test_integer_ranges(void)
{
	check_line(&(struct nine_field){ .name = "v",
					 .elem = NINE_I64,
					 .range.i = { INT64_MIN, INT64_MAX } },
		   "i64 range=-9223372036854775808..9223372036854775807\n");
	check_line(&(struct nine_field){ .name = "v",
					 .elem = NINE_I8,
					 .range.i = { -128, -1 } },
		   "i8 range=-128..-1\n");
	check_line(&(struct nine_field){ .name = "v",
					 .elem = NINE_U64,
					 .range.u = { 0, UINT64_MAX } },
		   "u64 range=0..18446744073709551615\n");
	check_line(&(struct nine_field){ .name = "v",
					 .elem = NINE_U8,
					 .range.u = { 255, 255 } },
		   "u8 range=255..255\n");
	check_line(&(struct nine_field){ .name = "v",
					 .elem = NINE_I32,
					 .range.i = { -1, 0 } },
		   "i32 range=-1..0\n");
	check_line(&(struct nine_field){ .name = "v",
					 .elem = NINE_I16,
					 .shape = NINE_VEC3,
					 .range.i = { -5, 5 } },
		   "vec3 i16 range=-5..5\n");
}

static void test_enum_values_in_the_element_type(void)
{
	static const struct nine_enum_value values[] = {
		{ "low", -32768 },
		{ "mid", 0 },
		{ "high", 32767 },
	};
	static const struct nine_enum_value big[] = {
		{ "top", INT64_MAX },
	};

	check_line(&(struct nine_field){ .name = "v",
					 .elem = NINE_I16,
					 .values = values,
					 .nvalues = ARRAY_SIZE(values) },
		   "enum i16 values=low:-32768,mid:0,high:32767\n");
	check_line(&(struct nine_field){ .name = "v",
					 .elem = NINE_U64,
					 .values = big,
					 .nvalues = ARRAY_SIZE(big) },
		   "enum u64 values=top:9223372036854775807\n");
}

static void test_items_in_order(void)
{
	static const char delay_line[] =
		"u16 unit=ms range=1..65535 message=Pick a delay = 1..65535 ms.\n";

	check_line(
		&(struct nine_field){ .name = "delay",
				      .elem = NINE_U16,
				      .unit = "ms",
				      .range.u = { 1, 65535 },
				      .message =
					      "Pick a delay = 1..65535 ms." },
		delay_line);
	check_line(&(struct nine_field){ .name = "v",
					 .elem = NINE_I32,
					 .message = "Any number." },
		   "i32 message=Any number.\n");
	check_line(&(struct nine_field){ .name = "v",
					 .elem = NINE_BOOL,
					 .message = "Yes or no." },
		   "bool message=Yes or no.\n");
}

/* An empty unit or message is left out of the line, as NULL is. */
static void test_empty_unit_and_message(void)
{
	check_line(&(struct nine_field){ .name = "v",
					 .elem = NINE_I32,
					 .unit = "",
					 .range.i = { 0, 100 },
					 .message = "" },
		   "i32 range=0..100\n");
}

/* The limits of an f32 become f32s, so 0.1 shows as the f32 nearest it. */
static void test_f32_ranges(void)
{
	check_line(&(struct nine_field){ .name = "v",
					 .elem = NINE_F32,
					 .unit = "m",
					 .range.f = { 0, 0.1 },
					 .message = "Pick 0 to 0.1." },
		   "f32 unit=m range=0..0.1 message=Pick 0 to 0.1.\n");
	check_line(&(struct nine_field){ .name = "v",
					 .elem = NINE_F32,
					 .range.f = { -pos_inf, 5 } },
		   "f32 range=-inf..5\n");
	check_line(&(struct nine_field){ .name = "v",
					 .elem = NINE_F32,
					 .range.f = { 0, pos_inf } },
		   "f32 range=0..inf\n");
	check_line(&(struct nine_field){ .name = "v",
					 .elem = NINE_F32,
					 .shape = NINE_VEC3,
					 .range.f = { -1e10, 1e-7 } },
		   "vec3 f32 range=-1e10..1e-7\n");
	check_line(&(struct nine_field){ .name = "v",
					 .elem = NINE_F32,
					 .range.f = { -3.4028234663852886e38,
						      0.000123456789 } },
		   "f32 range=-3.4028235e38..0.00012345679\n");
}

/* The limits of an f64 show as they are, with the shortest digits. */
static void test_f64_ranges(void)
{
	check_line(&(struct nine_field){ .name = "v",
					 .elem = NINE_F64,
					 .unit = "m",
					 .range.f = { 0, 0.1 },
					 .message = "Pick 0 to 0.1." },
		   "f64 unit=m range=0..0.1 message=Pick 0 to 0.1.\n");
	check_line(&(struct nine_field){ .name = "v",
					 .elem = NINE_F64,
					 .range.f = { -pos_inf, 5 } },
		   "f64 range=-inf..5\n");
	check_line(&(struct nine_field){ .name = "v",
					 .elem = NINE_F64,
					 .shape = NINE_QUAT,
					 .range.f = { -1, 1 } },
		   "quat f64 range=-1..1\n");
	check_line(&(struct nine_field){ .name = "v",
					 .elem = NINE_F64,
					 .shape = NINE_VEC4,
					 .range.f = { -1.7976931348623157e308,
						      5e-324 } },
		   "vec4 f64 range=-1.7976931348623157e308..5e-324\n");
	check_line(&(struct nine_field){ .name = "v",
					 .elem = NINE_F64,
					 .range.f = { 0.0001,
						      999999999.9999999 } },
		   "f64 range=0.0001..999999999.9999999\n");
}

static void test_field_text_that_does_not_fit(void)
{
	static const char want[] = "i32 range=0..100\n";
	const struct nine_field f = { .name = "v",
				      .elem = NINE_I32,
				      .range.i = { 0, 100 } };
	char buf[sizeof(want) - 1];

	CHECK_INT(nine_field_text(&f, buf, sizeof(buf)), ==, sizeof(buf));
	CHECK_MEM(buf, want, sizeof(buf));
	CHECK_INT(nine_field_text(&f, buf, sizeof(buf) - 1), ==, -ERANGE);
	CHECK_INT(nine_field_text(&f, NULL, 0), ==, -ERANGE);
}

static void test_read_at_an_offset(void)
{
	static const char want[] =
		"i32 range=0..100 message=Health must be from 0 to 100.\n";
	static const struct nine_field fields[] = {
		{ .name = "health",
		  .elem = NINE_I32,
		  .range.i = { 0, 100 },
		  .message = "Health must be from 0 to 100." },
	};
	static const struct nine_schema unit = { .name = "unit",
						 .fields = fields,
						 .nfields = 1 };
	struct nine_tree *t = types_tree(&unit);
	struct nine_handle h = open_path(t, "/types/unit/health", NINE_RDONLY);
	uint64_t size = sizeof(want) - 1;
	char buf[NINE_TEXT_MAX];

	CHECK_INT(nine_read(&h, buf, 5, 4), ==, 5);
	CHECK_MEM(buf, "range", 5);
	CHECK_INT(nine_read(&h, buf, sizeof(buf), 10), ==,
		  (ptrdiff_t)size - 10);
	CHECK_MEM(buf, want + 10, size - 10);
	CHECK_INT(nine_read(&h, buf, sizeof(buf), size - 1), ==, 1);
	CHECK_MEM(buf, "\n", 1);
	CHECK_INT(nine_read(&h, buf, sizeof(buf), size), ==, 0);
	CHECK_INT(nine_read(&h, buf, sizeof(buf), UINT64_MAX), ==, 0);
	CHECK_INT(nine_read(&h, buf, 0, 0), ==, 0);
	nine_tree_free(t);
}

/* A line longer than NINE_TEXT_MAX reads in pieces as any file does. */
static void test_long_line(void)
{
	static char message[1500];
	static char want[sizeof(message) + 32];
	static char got[sizeof(want)];
	struct nine_field fields[1] = { { .name = "v", .elem = NINE_U8 } };
	const struct nine_schema s = { .name = "s",
				       .fields = fields,
				       .nfields = 1 };
	struct nine_tree *t;
	struct nine_handle h;
	struct nine_stat st;
	ptrdiff_t len, n;
	uint64_t off;

	memset(message, 'x', sizeof(message) - 1);
	fields[0].message = message;
	len = nine_field_text(&fields[0], want, sizeof(want));
	CHECK_INT(len, ==,
		  (ptrdiff_t)strlen("u8 message=\n") +
			  (ptrdiff_t)sizeof(message) - 1);
	CHECK_INT(nine_field_text(&fields[0], want, NINE_TEXT_MAX), ==,
		  -ERANGE);

	t = types_tree(&s);
	h = open_path(t, "/types/s/v", NINE_RDONLY);
	CHECK_INT(nine_stat(&h, &st), ==, 0);
	CHECK_UINT(st.size, ==, (uint64_t)len);
	for (off = 0; (n = nine_read(&h, got + off, 7, off)) > 0; off += n)
		CHECK_INT(n, <=, 7);
	CHECK_INT(n, ==, 0);
	CHECK_UINT(off, ==, (uint64_t)len);
	CHECK_MEM(got, want, (size_t)len);
	nine_tree_free(t);
}

static void test_write_is_refused(void)
{
	struct nine_tree *t = types_tree(&schema_s);
	struct nine_handle h = open_path(t, "/types/s/v", NINE_RDWR);

	CHECK_INT(nine_write(&h, "i64\n", 4, 0), ==, -EACCES);
	CHECK_INT(nine_write(&h, "", 0, 0), ==, -EACCES);
	nine_tree_free(t);
}

static void test_no_text_form(void)
{
	struct nine_tree *t = types_tree(&schema_s);
	struct nine_handle file = open_path(t, "/types/s/v", NINE_RDWR);
	struct nine_handle dir = open_path(t, "/types/s", NINE_RDWR);
	char buf[NINE_TEXT_MAX];

	CHECK_INT(nine_read_text(&file, buf, sizeof(buf)), ==, -ENOTSUP);
	CHECK_INT(nine_write_text(&file, "1", 1), ==, -ENOTSUP);
	CHECK_INT(nine_read_text(&dir, buf, sizeof(buf)), ==, -ENOTSUP);
	CHECK_INT(nine_write_text(&dir, "1", 1), ==, -ENOTSUP);
	nine_tree_free(t);
}

static void test_nodes_that_are_not_there(void)
{
	struct nine_tree *t = types_tree(&schema_s);
	struct nine_handle file = open_path(t, "/types/s/v", NINE_RDONLY);
	struct nine_handle dir = open_path(t, "/types/s", NINE_RDONLY);
	struct nine_handle h;
	struct nine_stat st;
	uint64_t cursor = 0;
	char buf[16];

	CHECK_INT(nine_open(nine_tree_ns(t), "/types/t", NINE_RDONLY, &h), ==,
		  -ENOENT);
	CHECK_INT(nine_open(nine_tree_ns(t), "/types/s/w", NINE_RDONLY, &h), ==,
		  -ENOENT);
	CHECK_INT(nine_open(nine_tree_ns(t), "/types/s/vv", NINE_RDONLY, &h),
		  ==, -ENOENT);
	CHECK_INT(nine_open(nine_tree_ns(t), "/types/s/v/x", NINE_RDONLY, &h),
		  ==, -ENOTDIR);
	CHECK_INT(nine_list(&file, &cursor, buf, sizeof(buf)), ==, -ENOTDIR);
	CHECK_INT(nine_read(&dir, buf, sizeof(buf), 0), ==, -EISDIR);
	CHECK_INT(nine_stat(&dir, &st), ==, 0);
	CHECK_INT(st.kind, ==, NINE_DIR);
	CHECK_UINT(st.size, ==, 0);
	CHECK_PTR(st.type.field, ==, NULL);
	nine_tree_free(t);
}

static int no_lookup(void *priv, const struct nine_node *dir, const char *name,
		     size_t len, struct nine_node *out)
{
	(void)priv;
	(void)dir;
	(void)name;
	(void)len;
	(void)out;
	return -ENOENT;
}

static int no_list(void *priv, const struct nine_node *dir, uint64_t *cursor,
		   char *buf, size_t len)
{
	(void)priv;
	(void)dir;
	(void)cursor;
	(void)buf;
	(void)len;
	return 0;
}

static int dir_stat(void *priv, const struct nine_node *node,
		    struct nine_stat *st)
{
	(void)priv;
	(void)node;
	st->kind = NINE_DIR;
	return 0;
}

static ptrdiff_t no_read(void *priv, const struct nine_node *node, void *buf,
			 size_t len, uint64_t off)
{
	(void)priv;
	(void)node;
	(void)buf;
	(void)len;
	(void)off;
	return -EISDIR;
}

/* An empty directory, to mount at paths that /types/ conflicts with. */
static const struct nine_file_server_ops empty_ops = {
	.lookup = no_lookup,
	.list = no_list,
	.stat = dir_stat,
	.read = no_read,
};

static void test_mount_at_a_conflicting_path(void)
{
	struct nine_tree *t = tree_new();

	CHECK_INT(nine_mount(t, "/types", &empty_ops, NULL), ==, 0);
	CHECK_INT(nine_mount_types(t, "/types"), ==, -EBUSY);
	CHECK_INT(nine_mount_types(t, "/types/x"), ==, -EBUSY);
	CHECK_INT(nine_mount_types(t, "/"), ==, -EBUSY);
	CHECK_INT(nine_mount_types(t, "types"), ==, -EINVAL);
	CHECK_INT(nine_mount_types(t, "/meta/types"), ==, 0);
	CHECK_INT(nine_mount(t, "/meta", &empty_ops, NULL), ==, -EBUSY);
	CHECK_INT(nine_mount_types(t, "/meta/types"), ==, -EBUSY);
	nine_tree_free(t);
}

static void test_mount_at_any_path(void)
{
	struct nine_tree *t = tree_new();
	char out[64];

	CHECK_INT(nine_add_schema(t, &schema_s), ==, 0);
	CHECK_INT(nine_mount_types(t, "/"), ==, 0);
	CHECK_STR(list_path(t, "/", LISTING_MAX_BUF, out, sizeof(out)), "s ");
	CHECK_STR(list_path(t, "/s", LISTING_MAX_BUF, out, sizeof(out)), "v ");
	nine_tree_free(t);

	t = tree_new();
	CHECK_INT(nine_add_schema(t, &schema_s), ==, 0);
	CHECK_INT(nine_mount_types(t, "/meta/types"), ==, 0);
	CHECK_STR(list_path(t, "/meta/types/s", LISTING_MAX_BUF, out,
			    sizeof(out)),
		  "v ");
	nine_tree_free(t);
}

static void test_no_types_without_the_mount(void)
{
	struct nine_tree *t = tree_new();
	struct nine_handle h;
	char out[64];

	CHECK_INT(nine_add_schema(t, &schema_s), ==, 0);
	CHECK_INT(nine_mount(t, "/world", &empty_ops, NULL), ==, 0);
	CHECK_INT(nine_open(nine_tree_ns(t), "/types", NINE_RDONLY, &h), ==,
		  -ENOENT);
	CHECK_STR(list_path(t, "/", LISTING_MAX_BUF, out, sizeof(out)),
		  "world ");
	nine_tree_free(t);
}

int main(void)
{
	pos_inf = strtod("inf", NULL);

	test_lists_each_schema_and_its_fields();
	test_name_that_does_not_fit();
	test_same_descriptor_twice_lists_once();
	test_schema_added_after_the_mount();
	test_example_lines();
	test_each_element_type_and_shape();
	test_integer_ranges();
	test_enum_values_in_the_element_type();
	test_items_in_order();
	test_empty_unit_and_message();
	test_f32_ranges();
	test_f64_ranges();
	test_field_text_that_does_not_fit();
	test_read_at_an_offset();
	test_long_line();
	test_write_is_refused();
	test_no_text_form();
	test_nodes_that_are_not_there();
	test_mount_at_a_conflicting_path();
	test_mount_at_any_path();
	test_no_types_without_the_mount();
	return 0;
}
