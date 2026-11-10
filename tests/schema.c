/* SPDX-License-Identifier: Zlib */
#include <errno.h>
#include <float.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "ninefold.h"

#include "check.h"

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

/*
 * Made at run time. The macros INFINITY and NAN are constant expressions that
 * overflow, and some compilers warn of it.
 */
static double pos_inf;
static double quiet_nan;

static const struct nine_field one_field[] = {
	{ .name = "v", .elem = NINE_I32 },
};

static struct nine_tree *tree_new(void)
{
	struct nine_tree *t = nine_tree_new();

	CHECK_PTR(t, !=, NULL);
	return t;
}

static void test_same_descriptor_twice_has_no_effect(void)
{
	static const struct nine_schema s = { .name = "s",
					      .fields = one_field,
					      .nfields = 1 };
	struct nine_tree *t = tree_new();

	CHECK_INT(nine_add_schema(t, &s), ==, 0);
	CHECK_INT(nine_add_schema(t, &s), ==, 0);
	nine_tree_free(t);
}

static void test_different_descriptor_with_a_taken_name(void)
{
	static const struct nine_schema first = { .name = "s",
						  .fields = one_field,
						  .nfields = 1 };
	static const struct nine_schema same_text = { .name = "s",
						      .fields = one_field,
						      .nfields = 1 };
	static const struct nine_schema other = { .name = "t",
						  .fields = one_field,
						  .nfields = 1 };
	struct nine_tree *t = tree_new();

	CHECK_INT(nine_add_schema(t, &first), ==, 0);
	CHECK_INT(nine_add_schema(t, &other), ==, 0);
	CHECK_INT(nine_add_schema(t, &same_text), ==, -EEXIST);
	CHECK_INT(nine_add_schema(t, &first), ==, 0);
	nine_tree_free(t);
}

/* What nine_add_schema() returns for @s on a tree of its own. */
static int add_schema(const struct nine_schema *s)
{
	struct nine_tree *t = tree_new();
	int err = nine_add_schema(t, s);

	nine_tree_free(t);
	return err;
}

/* What nine_add_schema() returns for a schema "s" of just @f. */
static int add_field(const struct nine_field *f)
{
	const struct nine_schema s = { .name = "s", .fields = f, .nfields = 1 };

	return add_schema(&s);
}

/* What nine_add_schema() returns for a schema of one good field named @name. */
static int add_schema_named(const char *name)
{
	const struct nine_schema s = { .name = name,
				       .fields = one_field,
				       .nfields = 1 };

	return add_schema(&s);
}

#define ACCEPT_FIELD(...) \
	CHECK_INT(add_field(&(struct nine_field)__VA_ARGS__), ==, 0)
#define REFUSE_FIELD(...) \
	CHECK_INT(add_field(&(struct nine_field)__VA_ARGS__), ==, -EINVAL)

static void test_schema_names(void)
{
	CHECK_INT(add_schema_named(NULL), ==, -EINVAL);
	CHECK_INT(add_schema_named(""), ==, -EINVAL);
	CHECK_INT(add_schema_named("."), ==, -EINVAL);
	CHECK_INT(add_schema_named(".."), ==, -EINVAL);
	CHECK_INT(add_schema_named("a/b"), ==, -EINVAL);
	CHECK_INT(add_schema_named("/a"), ==, -EINVAL);
	CHECK_INT(add_schema_named("a/"), ==, -EINVAL);

	CHECK_INT(add_schema_named("transform"), ==, 0);
	CHECK_INT(add_schema_named("a.b"), ==, 0);
	CHECK_INT(add_schema_named("..."), ==, 0);
	CHECK_INT(add_schema_named("a b"), ==, 0);
}

static void test_field_names(void)
{
	REFUSE_FIELD({ .name = NULL, .elem = NINE_I32 });
	REFUSE_FIELD({ .name = "", .elem = NINE_I32 });
	REFUSE_FIELD({ .name = ".", .elem = NINE_I32 });
	REFUSE_FIELD({ .name = "..", .elem = NINE_I32 });
	REFUSE_FIELD({ .name = "a/b", .elem = NINE_I32 });

	ACCEPT_FIELD({ .name = "pos", .elem = NINE_I32 });
	ACCEPT_FIELD({ .name = "a.b", .elem = NINE_I32 });
}

static void test_schema_with_no_fields(void)
{
	const struct nine_schema none = { .name = "s",
					  .fields = one_field,
					  .nfields = 0 };
	const struct nine_schema null = { .name = "s", .nfields = 1 };
	struct nine_tree *t = tree_new();

	CHECK_INT(nine_add_schema(t, &none), ==, -EINVAL);
	CHECK_INT(nine_add_schema(t, &null), ==, -EINVAL);
	CHECK_INT(nine_add_schema(t, NULL), ==, -EINVAL);
	nine_tree_free(t);
}

static void test_duplicate_field_names(void)
{
	static const struct nine_field fields[] = {
		{ .name = "a", .elem = NINE_I32 },
		{ .name = "b", .elem = NINE_I32 },
		{ .name = "a", .elem = NINE_U8 },
	};
	const struct nine_schema s = { .name = "s",
				       .fields = fields,
				       .nfields = ARRAY_SIZE(fields) };
	const struct nine_schema first_two = { .name = "s",
					       .fields = fields,
					       .nfields = 2 };
	struct nine_tree *t = tree_new();

	CHECK_INT(nine_add_schema(t, &s), ==, -EINVAL);
	CHECK_INT(nine_add_schema(t, &first_two), ==, 0);
	nine_tree_free(t);
}

/* A mistake in the last field is found as surely as one in the first. */
static void test_whole_descriptor_is_examined(void)
{
	static const struct nine_field fields[] = {
		{ .name = "a", .elem = NINE_I32 },
		{ .name = "b", .elem = NINE_I32 },
		{ .name = "", .elem = NINE_I32 },
	};
	const struct nine_schema s = { .name = "s",
				       .fields = fields,
				       .nfields = ARRAY_SIZE(fields) };
	struct nine_tree *t = tree_new();

	CHECK_INT(nine_add_schema(t, &s), ==, -EINVAL);
	nine_tree_free(t);
}

/* A refused descriptor leaves its name free, and the tree still works. */
static void test_refused_descriptor_leaves_the_tree_unchanged(void)
{
	static const struct nine_field bad_fields[] = {
		{ .name = "", .elem = NINE_I32 },
	};
	static const struct nine_schema bad = { .name = "s",
						.fields = bad_fields,
						.nfields = 1 };
	static const struct nine_schema good = { .name = "s",
						 .fields = one_field,
						 .nfields = 1 };
	static const struct nine_schema clash = { .name = "s",
						  .fields = one_field,
						  .nfields = 1 };
	struct nine_tree *t = tree_new();

	CHECK_INT(nine_add_schema(t, &bad), ==, -EINVAL);
	CHECK_INT(nine_add_schema(t, &good), ==, 0);
	CHECK_INT(nine_add_schema(t, &clash), ==, -EEXIST);
	nine_tree_free(t);
}

/* A descriptor that is not valid is refused as such, whatever its name. */
static void test_bad_descriptor_with_a_taken_name(void)
{
	static const struct nine_field bad_fields[] = {
		{ .name = "", .elem = NINE_I32 },
	};
	static const struct nine_schema bad = { .name = "s",
						.fields = bad_fields,
						.nfields = 1 };
	static const struct nine_schema good = { .name = "s",
						 .fields = one_field,
						 .nfields = 1 };
	struct nine_tree *t = tree_new();

	CHECK_INT(nine_add_schema(t, &good), ==, 0);
	CHECK_INT(nine_add_schema(t, &bad), ==, -EINVAL);
	nine_tree_free(t);
}

static void test_element_type_not_valid(void)
{
	REFUSE_FIELD({ .name = "v", .elem = (enum nine_elem)0 });
	REFUSE_FIELD({ .name = "v", .elem = (enum nine_elem)(NINE_BOOL + 1) });
	REFUSE_FIELD({ .name = "v", .elem = (enum nine_elem)(-1) });
}

static void test_shape_not_valid(void)
{
	REFUSE_FIELD({ .name = "v",
		       .elem = NINE_I32,
		       .shape = (enum nine_shape)(NINE_QUAT + 1) });
	REFUSE_FIELD({ .name = "v",
		       .elem = NINE_I32,
		       .shape = (enum nine_shape)(-1) });
}

/* Every element type with every shape, written out by hand. */
static void test_shapes_of_each_element_type(void)
{
	static const struct {
		enum nine_elem elem;
		bool vector;
		bool quat;
	} elems[] = {
		{ NINE_I8, true, false },    { NINE_I16, true, false },
		{ NINE_I32, true, false },   { NINE_I64, true, false },
		{ NINE_U8, true, false },    { NINE_U16, true, false },
		{ NINE_U32, true, false },   { NINE_U64, true, false },
		{ NINE_F32, true, true },    { NINE_F64, true, true },
		{ NINE_BOOL, false, false },
	};
	enum shape_kind {
		ANY_ELEM,
		VECTOR_ELEM,
		QUAT_ELEM
	};
	static const struct {
		enum nine_shape shape;
		enum shape_kind kind;
	} shapes[] = {
		{ NINE_SCALAR, ANY_ELEM },  { NINE_VEC2, VECTOR_ELEM },
		{ NINE_VEC3, VECTOR_ELEM }, { NINE_VEC4, VECTOR_ELEM },
		{ NINE_QUAT, QUAT_ELEM },
	};
	size_t i, j;

	for (i = 0; i < ARRAY_SIZE(elems); i++) {
		for (j = 0; j < ARRAY_SIZE(shapes); j++) {
			const struct nine_field f = { .name = "v",
						      .elem = elems[i].elem,
						      .shape =
							      shapes[j].shape };
			bool ok =
				shapes[j].kind == ANY_ELEM ||
				(shapes[j].kind == VECTOR_ELEM &&
				 elems[i].vector) ||
				(shapes[j].kind == QUAT_ELEM && elems[i].quat);

			CHECK_INT(add_field(&f), ==, ok ? 0 : -EINVAL);
		}
	}
}

static const struct nine_enum_value two_values[] = {
	{ "a", 0 },
	{ "b", 1 },
};

static void test_unit_on_a_field_that_is_not_numeric(void)
{
	REFUSE_FIELD({ .name = "v", .elem = NINE_BOOL, .unit = "m" });
	REFUSE_FIELD({ .name = "v",
		       .elem = NINE_U8,
		       .unit = "m",
		       .values = two_values,
		       .nvalues = 2 });
}

static void test_range_on_a_field_that_is_not_numeric(void)
{
	REFUSE_FIELD({ .name = "v", .elem = NINE_BOOL, .range.i = { 0, 1 } });
	REFUSE_FIELD({ .name = "v", .elem = NINE_BOOL, .range.u = { 0, 1 } });
	REFUSE_FIELD({ .name = "v", .elem = NINE_BOOL, .range.f = { 0, 1 } });
	REFUSE_FIELD({ .name = "v",
		       .elem = NINE_U8,
		       .range.u = { 0, 1 },
		       .values = two_values,
		       .nvalues = 2 });
	REFUSE_FIELD({ .name = "v",
		       .elem = NINE_I8,
		       .range.i = { -1, 0 },
		       .values = two_values,
		       .nvalues = 2 });
}

static void test_unit_text(void)
{
	REFUSE_FIELD({ .name = "v", .elem = NINE_F32, .unit = "m s" });
	REFUSE_FIELD({ .name = "v", .elem = NINE_F32, .unit = " m" });
	REFUSE_FIELD({ .name = "v", .elem = NINE_F32, .unit = "m " });
	REFUSE_FIELD({ .name = "v", .elem = NINE_F32, .unit = "m\ts" });
	REFUSE_FIELD({ .name = "v", .elem = NINE_F32, .unit = "m\n" });
	REFUSE_FIELD({ .name = "v", .elem = NINE_F32, .unit = "\xc2\xb5m" });

	ACCEPT_FIELD({ .name = "v", .elem = NINE_F32, .unit = "m" });
	ACCEPT_FIELD({ .name = "v", .elem = NINE_F32, .unit = "m/s" });
	ACCEPT_FIELD({ .name = "v", .elem = NINE_I32, .unit = "deg" });
	ACCEPT_FIELD({ .name = "v",
		       .elem = NINE_F32,
		       .shape = NINE_VEC3,
		       .unit = "m" });
}

static void test_float_range_limit_is_nan(void)
{
	REFUSE_FIELD(
		{ .name = "v", .elem = NINE_F32, .range.f = { quiet_nan, 1 } });
	REFUSE_FIELD(
		{ .name = "v", .elem = NINE_F32, .range.f = { 0, quiet_nan } });
	REFUSE_FIELD(
		{ .name = "v", .elem = NINE_F64, .range.f = { quiet_nan, 1 } });
	REFUSE_FIELD(
		{ .name = "v", .elem = NINE_F64, .range.f = { 0, quiet_nan } });
	REFUSE_FIELD({ .name = "v",
		       .elem = NINE_F64,
		       .range.f = { quiet_nan, quiet_nan } });
}

static void test_range_min_above_max(void)
{
	REFUSE_FIELD({ .name = "v", .elem = NINE_I32, .range.i = { 1, 0 } });
	REFUSE_FIELD({ .name = "v", .elem = NINE_I32, .range.i = { 0, -1 } });
	REFUSE_FIELD({ .name = "v", .elem = NINE_U32, .range.u = { 1, 0 } });
	REFUSE_FIELD({ .name = "v", .elem = NINE_U32, .range.u = { 2, 1 } });
	REFUSE_FIELD({ .name = "v", .elem = NINE_F32, .range.f = { 1, 0 } });
	REFUSE_FIELD({ .name = "v", .elem = NINE_F64, .range.f = { 1, 0.5 } });
	REFUSE_FIELD({ .name = "v",
		       .elem = NINE_F64,
		       .range.f = { pos_inf, -pos_inf } });

	/* Both limits are included, so a range can hold one value. */
	ACCEPT_FIELD({ .name = "v", .elem = NINE_I32, .range.i = { 5, 5 } });
	ACCEPT_FIELD({ .name = "v", .elem = NINE_U32, .range.u = { 5, 5 } });
	ACCEPT_FIELD({ .name = "v", .elem = NINE_F64, .range.f = { 5, 5 } });
}

#define CHECK_SIGNED_LIMITS(e, lo, hi)                                      \
	do {                                                                \
		ACCEPT_FIELD(                                               \
			{ .name = "v", .elem = e, .range.i = { lo, hi } }); \
		REFUSE_FIELD({ .name = "v",                                 \
			       .elem = e,                                   \
			       .range.i = { 0, (hi) + 1LL } });             \
		REFUSE_FIELD({ .name = "v",                                 \
			       .elem = e,                                   \
			       .range.i = { (lo) - 1LL, 0 } });             \
	} while (0)

#define CHECK_UNSIGNED_LIMITS(e, hi)                                       \
	do {                                                               \
		ACCEPT_FIELD(                                              \
			{ .name = "v", .elem = e, .range.u = { 0, hi } }); \
		REFUSE_FIELD({ .name = "v",                                \
			       .elem = e,                                  \
			       .range.u = { 0, (hi) + 1ULL } });           \
	} while (0)

static void test_integer_limit_the_type_cannot_hold(void)
{
	CHECK_SIGNED_LIMITS(NINE_I8, INT8_MIN, INT8_MAX);
	CHECK_SIGNED_LIMITS(NINE_I16, INT16_MIN, INT16_MAX);
	CHECK_SIGNED_LIMITS(NINE_I32, INT32_MIN, INT32_MAX);
	CHECK_UNSIGNED_LIMITS(NINE_U8, UINT8_MAX);
	CHECK_UNSIGNED_LIMITS(NINE_U16, UINT16_MAX);
	CHECK_UNSIGNED_LIMITS(NINE_U32, UINT32_MAX);

	ACCEPT_FIELD({ .name = "v",
		       .elem = NINE_I64,
		       .range.i = { INT64_MIN, INT64_MAX } });
	ACCEPT_FIELD({ .name = "v",
		       .elem = NINE_U64,
		       .range.u = { UINT64_MAX - 1, UINT64_MAX } });
	/* The range of a vector is the range of each of its elements. */
	REFUSE_FIELD({ .name = "v",
		       .elem = NINE_I8,
		       .shape = NINE_VEC3,
		       .range.i = { 0, 128 } });
}

static void test_float_limit_the_type_cannot_hold(void)
{
	/* Too large for an f32. */
	REFUSE_FIELD({ .name = "v", .elem = NINE_F32, .range.f = { 0, 1e39 } });
	REFUSE_FIELD(
		{ .name = "v", .elem = NINE_F32, .range.f = { -1e39, 0 } });
	REFUSE_FIELD(
		{ .name = "v", .elem = NINE_F32, .range.f = { 0, DBL_MAX } });
	/* Rounds up to infinity, from the first double at the halfway point. */
	REFUSE_FIELD({ .name = "v",
		       .elem = NINE_F32,
		       .range.f = { 0, 0x1.ffffffp127 } });
	/* Too small for an f32: not zero, and it would become zero. */
	REFUSE_FIELD(
		{ .name = "v", .elem = NINE_F32, .range.f = { 1e-60, 1 } });
	REFUSE_FIELD(
		{ .name = "v", .elem = NINE_F32, .range.f = { -1, -1e-60 } });
	REFUSE_FIELD(
		{ .name = "v", .elem = NINE_F32, .range.f = { 0x1p-150, 1 } });

	/* These round to a finite f32 that is not zero. */
	ACCEPT_FIELD(
		{ .name = "v", .elem = NINE_F32, .range.f = { 0, FLT_MAX } });
	ACCEPT_FIELD({ .name = "v",
		       .elem = NINE_F32,
		       .range.f = { 0, 3.4028235e38 } });
	ACCEPT_FIELD({ .name = "v", .elem = NINE_F32, .range.f = { 0, 0.1 } });
	ACCEPT_FIELD(
		{ .name = "v", .elem = NINE_F32, .range.f = { 1e-45, 1 } });

	/* An f64 holds every double that is not nan. */
	ACCEPT_FIELD({ .name = "v",
		       .elem = NINE_F64,
		       .range.f = { -DBL_MAX, DBL_MAX } });
	ACCEPT_FIELD(
		{ .name = "v", .elem = NINE_F64, .range.f = { 1e-300, 1 } });
}

/* The limit of the type at one end means a limit at the other end only. */
static void test_range_open_at_one_end(void)
{
	ACCEPT_FIELD(
		{ .name = "v", .elem = NINE_I32, .range.i = { 0, INT32_MAX } });
	ACCEPT_FIELD(
		{ .name = "v", .elem = NINE_F32, .range.f = { 0, pos_inf } });
	ACCEPT_FIELD(
		{ .name = "v", .elem = NINE_F32, .range.f = { -pos_inf, 0 } });
	ACCEPT_FIELD({ .name = "v",
		       .elem = NINE_F64,
		       .range.f = { -pos_inf, pos_inf } });
}

static void test_enum_with_no_values(void)
{
	REFUSE_FIELD({ .name = "v",
		       .elem = NINE_U8,
		       .values = two_values,
		       .nvalues = 0 });
	REFUSE_FIELD({ .name = "v", .elem = NINE_U8, .nvalues = 2 });
}

/* An enum is an integer field, and always a scalar. */
static void test_enum_type(void)
{
	REFUSE_FIELD({ .name = "v",
		       .elem = NINE_F32,
		       .values = two_values,
		       .nvalues = 2 });
	REFUSE_FIELD({ .name = "v",
		       .elem = NINE_F64,
		       .values = two_values,
		       .nvalues = 2 });
	REFUSE_FIELD({ .name = "v",
		       .elem = NINE_BOOL,
		       .values = two_values,
		       .nvalues = 2 });
	REFUSE_FIELD({ .name = "v",
		       .elem = NINE_U8,
		       .shape = NINE_VEC2,
		       .values = two_values,
		       .nvalues = 2 });
	REFUSE_FIELD({ .name = "v",
		       .elem = NINE_U8,
		       .shape = NINE_QUAT,
		       .values = two_values,
		       .nvalues = 2 });
}

static void test_enum_on_each_integer_type(void)
{
	static const enum nine_elem elems[] = {
		NINE_I8, NINE_I16, NINE_I32, NINE_I64,
		NINE_U8, NINE_U16, NINE_U32, NINE_U64,
	};
	size_t i;

	for (i = 0; i < ARRAY_SIZE(elems); i++)
		ACCEPT_FIELD({ .name = "v",
			       .elem = elems[i],
			       .values = two_values,
			       .nvalues = 2 });
}

/* What nine_add_schema() returns for an enum of @elem with the value @v. */
static int add_enum_value(enum nine_elem elem, int64_t v)
{
	const struct nine_enum_value values[] = { { "a", v } };
	const struct nine_field f = { .name = "v",
				      .elem = elem,
				      .values = values,
				      .nvalues = 1 };

	return add_field(&f);
}

static void test_enum_value_that_does_not_fit(void)
{
	CHECK_INT(add_enum_value(NINE_I8, INT8_MIN), ==, 0);
	CHECK_INT(add_enum_value(NINE_I8, INT8_MAX), ==, 0);
	CHECK_INT(add_enum_value(NINE_I8, INT8_MIN - 1), ==, -EINVAL);
	CHECK_INT(add_enum_value(NINE_I8, INT8_MAX + 1), ==, -EINVAL);
	CHECK_INT(add_enum_value(NINE_I16, INT16_MAX + 1), ==, -EINVAL);
	CHECK_INT(add_enum_value(NINE_I32, INT32_MAX + 1LL), ==, -EINVAL);
	CHECK_INT(add_enum_value(NINE_I64, INT64_MIN), ==, 0);
	CHECK_INT(add_enum_value(NINE_I64, INT64_MAX), ==, 0);

	CHECK_INT(add_enum_value(NINE_U8, UINT8_MAX), ==, 0);
	CHECK_INT(add_enum_value(NINE_U8, UINT8_MAX + 1), ==, -EINVAL);
	CHECK_INT(add_enum_value(NINE_U8, -1), ==, -EINVAL);
	CHECK_INT(add_enum_value(NINE_U16, UINT16_MAX + 1), ==, -EINVAL);
	CHECK_INT(add_enum_value(NINE_U32, UINT32_MAX + 1LL), ==, -EINVAL);
	CHECK_INT(add_enum_value(NINE_U64, INT64_MAX), ==, 0);
	CHECK_INT(add_enum_value(NINE_U64, -1), ==, -EINVAL);
}

/* What nine_add_schema() returns for an enum with the one name @name. */
static int add_enum_name(const char *name)
{
	const struct nine_enum_value values[] = { { name, 0 } };
	const struct nine_field f = { .name = "v",
				      .elem = NINE_U8,
				      .values = values,
				      .nvalues = 1 };

	return add_field(&f);
}

static void test_enum_names(void)
{
	CHECK_INT(add_enum_name(NULL), ==, -EINVAL);
	CHECK_INT(add_enum_name(""), ==, -EINVAL);
	CHECK_INT(add_enum_name("1a"), ==, -EINVAL);
	CHECK_INT(add_enum_name("7"), ==, -EINVAL);
	CHECK_INT(add_enum_name("a-b"), ==, -EINVAL);
	CHECK_INT(add_enum_name("a b"), ==, -EINVAL);
	CHECK_INT(add_enum_name("a.b"), ==, -EINVAL);
	CHECK_INT(add_enum_name("a:b"), ==, -EINVAL);
	CHECK_INT(add_enum_name("\xc3\xa9t\xc3\xa9"), ==, -EINVAL);

	CHECK_INT(add_enum_name("walk"), ==, 0);
	CHECK_INT(add_enum_name("Walk"), ==, 0);
	CHECK_INT(add_enum_name("_"), ==, 0);
	CHECK_INT(add_enum_name("_1"), ==, 0);
	CHECK_INT(add_enum_name("run2_fast"), ==, 0);
}

/* A name of 511 bytes fits in NINE_TEXT_MAX with its newline. */
static void test_enum_name_length(void)
{
	char name[NINE_TEXT_MAX + 1];

	CHECK_INT(NINE_TEXT_MAX, ==, 512);
	memset(name, 'a', sizeof(name));
	name[NINE_TEXT_MAX - 1] = '\0';
	CHECK_INT(add_enum_name(name), ==, 0);

	name[NINE_TEXT_MAX - 1] = 'a';
	name[NINE_TEXT_MAX] = '\0';
	CHECK_INT(add_enum_name(name), ==, -EINVAL);
}

static void test_duplicate_enum_names(void)
{
	static const struct nine_enum_value twice[] = {
		{ "a", 0 },
		{ "b", 1 },
		{ "a", 2 },
	};
	static const struct nine_enum_value case_differs[] = {
		{ "a", 0 },
		{ "A", 1 },
	};
	static const struct nine_enum_value shared_value[] = {
		{ "a", 1 },
		{ "b", 1 },
	};

	REFUSE_FIELD({ .name = "v",
		       .elem = NINE_U8,
		       .values = twice,
		       .nvalues = ARRAY_SIZE(twice) });
	ACCEPT_FIELD({ .name = "v",
		       .elem = NINE_U8,
		       .values = case_differs,
		       .nvalues = ARRAY_SIZE(case_differs) });
	ACCEPT_FIELD({ .name = "v",
		       .elem = NINE_U8,
		       .values = shared_value,
		       .nvalues = ARRAY_SIZE(shared_value) });
}

static void test_message(void)
{
	REFUSE_FIELD({ .name = "v", .elem = NINE_I32, .message = "a\nb" });
	REFUSE_FIELD({ .name = "v", .elem = NINE_I32, .message = "a\n" });
	REFUSE_FIELD({ .name = "v", .elem = NINE_I32, .message = "\n" });

	ACCEPT_FIELD({ .name = "v",
		       .elem = NINE_I32,
		       .message = "Health must be from 0 to 100." });
	ACCEPT_FIELD({ .name = "v", .elem = NINE_I32, .message = NULL });
	ACCEPT_FIELD(
		{ .name = "v", .elem = NINE_BOOL, .message = "Yes or no." });
	ACCEPT_FIELD({ .name = "v",
		       .elem = NINE_U8,
		       .values = two_values,
		       .nvalues = 2,
		       .message = "Pick a known state." });
}

int main(void)
{
	pos_inf = strtod("inf", NULL);
	quiet_nan = strtod("nan", NULL);

	test_same_descriptor_twice_has_no_effect();
	test_different_descriptor_with_a_taken_name();
	test_schema_names();
	test_field_names();
	test_schema_with_no_fields();
	test_duplicate_field_names();
	test_whole_descriptor_is_examined();
	test_refused_descriptor_leaves_the_tree_unchanged();
	test_bad_descriptor_with_a_taken_name();
	test_element_type_not_valid();
	test_shape_not_valid();
	test_shapes_of_each_element_type();
	test_unit_on_a_field_that_is_not_numeric();
	test_range_on_a_field_that_is_not_numeric();
	test_unit_text();
	test_float_range_limit_is_nan();
	test_range_min_above_max();
	test_integer_limit_the_type_cannot_hold();
	test_float_limit_the_type_cannot_hold();
	test_range_open_at_one_end();
	test_enum_with_no_values();
	test_enum_type();
	test_enum_on_each_integer_type();
	test_enum_value_that_does_not_fit();
	test_enum_names();
	test_enum_name_length();
	test_duplicate_enum_names();
	test_message();
	return 0;
}
