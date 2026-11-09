/* SPDX-License-Identifier: Zlib */
#include <errno.h>
#include <stdint.h>
#include <string.h>

#include "ninefold.h"

#include "check.h"
#include "recorder.h"

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

struct fixture {
	struct nine_tree *t;
	struct recorder r;
	struct nine_handle h;
};

/*
 * A recorder mounted at /rec that reports every node as a value file of
 * @size bytes with the type @field, and a handle to it open for reading and
 * writing. The calls of the open are forgotten.
 */
static void fixture_init(struct fixture *f, const struct nine_field *field,
			 size_t size)
{
	recorder_init(&f->r);
	f->r.st.kind = NINE_FILE;
	f->r.st.size = size;
	f->r.st.type.field = field;
	f->r.value_len = size;
	f->t = nine_tree_new();
	CHECK_PTR(f->t, !=, NULL);
	CHECK_INT(nine_mount(f->t, "/rec", &f->r.ops, &f->r), ==, 0);
	CHECK_INT(nine_open(nine_tree_ns(f->t), "/rec/f", NINE_RDWR, &f->h), ==,
		  0);
	f->r.ncalls = 0;
}

static void fixture_free(struct fixture *f)
{
	nine_tree_free(f->t);
}

static void test_read_gives_the_line(void)
{
	const struct nine_field field = { .name = "v", .elem = NINE_I32 };
	const int32_t value = 75;
	struct fixture f;
	char buf[NINE_TEXT_MAX];

	fixture_init(&f, &field, sizeof(value));
	memcpy(f.r.value, &value, sizeof(value));

	CHECK_INT(nine_read_text(&f.h, buf, sizeof(buf)), ==, 3);
	CHECK_MEM(buf, "75\n", 3);
	CHECK_UINT(f.r.ncalls, ==, 2);
	CHECK_INT(f.r.calls[0].op, ==, RECORDER_STAT);
	CHECK_INT(f.r.calls[1].op, ==, RECORDER_READ);
	CHECK_UINT(f.r.calls[1].len, ==, sizeof(value));
	CHECK_UINT(f.r.calls[1].off, ==, 0);
	fixture_free(&f);
}

/*
 * Writing @text, with and without a newline, gives the file server exactly
 * the @size bytes at @want at offset 0, and a read gives back the same text
 * and the same bytes.
 */
static void check_round_trip(enum nine_elem elem, const void *want, size_t size,
			     const char *text)
{
	const struct nine_field field = { .name = "v", .elem = elem };
	size_t len = strlen(text);
	char line[NINE_TEXT_MAX];
	char buf[NINE_TEXT_MAX];
	struct fixture f;
	int i;

	memcpy(line, text, len);
	line[len] = '\n';

	for (i = 0; i < 2; i++) {
		fixture_init(&f, &field, size);
		CHECK_INT(nine_write_text(&f.h, line, len + (size_t)i), ==, 0);
		CHECK_UINT(f.r.ncalls, ==, 2);
		CHECK_INT(f.r.calls[1].op, ==, RECORDER_WRITE);
		CHECK_UINT(f.r.calls[1].len, ==, size);
		CHECK_UINT(f.r.calls[1].off, ==, 0);
		CHECK_MEM(f.r.value, want, size);

		CHECK_INT(nine_read_text(&f.h, buf, sizeof(buf)), ==, len + 1);
		CHECK_MEM(buf, line, len + 1);
		fixture_free(&f);
	}
}

#define CHECK_ROUND_TRIP(elem, type, text, value)                  \
	do {                                                       \
		type want = value;                                 \
		check_round_trip(elem, &want, sizeof(want), text); \
	} while (0)

static void test_round_trip_of_each_integer_type(void)
{
	CHECK_ROUND_TRIP(NINE_I8, int8_t, "-128", INT8_MIN);
	CHECK_ROUND_TRIP(NINE_I8, int8_t, "-1", -1);
	CHECK_ROUND_TRIP(NINE_I8, int8_t, "0", 0);
	CHECK_ROUND_TRIP(NINE_I8, int8_t, "127", INT8_MAX);
	CHECK_ROUND_TRIP(NINE_I16, int16_t, "-32768", INT16_MIN);
	CHECK_ROUND_TRIP(NINE_I16, int16_t, "32767", INT16_MAX);
	CHECK_ROUND_TRIP(NINE_I32, int32_t, "-2147483648", INT32_MIN);
	CHECK_ROUND_TRIP(NINE_I32, int32_t, "75", 75);
	CHECK_ROUND_TRIP(NINE_I32, int32_t, "2147483647", INT32_MAX);
	CHECK_ROUND_TRIP(NINE_I64, int64_t, "-9223372036854775808", INT64_MIN);
	CHECK_ROUND_TRIP(NINE_I64, int64_t, "9223372036854775807", INT64_MAX);
	CHECK_ROUND_TRIP(NINE_U8, uint8_t, "0", 0);
	CHECK_ROUND_TRIP(NINE_U8, uint8_t, "255", UINT8_MAX);
	CHECK_ROUND_TRIP(NINE_U16, uint16_t, "0", 0);
	CHECK_ROUND_TRIP(NINE_U16, uint16_t, "65535", UINT16_MAX);
	CHECK_ROUND_TRIP(NINE_U32, uint32_t, "0", 0);
	CHECK_ROUND_TRIP(NINE_U32, uint32_t, "4294967295", UINT32_MAX);
	CHECK_ROUND_TRIP(NINE_U64, uint64_t, "0", 0);
	CHECK_ROUND_TRIP(NINE_U64, uint64_t, "18446744073709551615",
			 UINT64_MAX);
}

/* The call takes a slice of a larger buffer, with no NUL at its end. */
static void test_write_takes_a_slice(void)
{
	const struct nine_field field = { .name = "v", .elem = NINE_I32 };
	const int32_t want = 42;
	struct fixture f;

	fixture_init(&f, &field, sizeof(want));
	CHECK_INT(nine_write_text(&f.h, "4299", 2), ==, 0);
	CHECK_MEM(f.r.value, &want, sizeof(want));
	fixture_free(&f);
}

/* @text of @len bytes is refused for @field, and nothing is written. */
static void check_refused(const struct nine_field *field, size_t size,
			  const char *text, size_t len)
{
	struct fixture f;

	fixture_init(&f, field, size);
	CHECK_INT(nine_write_text(&f.h, text, len), ==, -EINVAL);
	CHECK_UINT(f.r.ncalls, ==, 1);
	CHECK_INT(f.r.calls[0].op, ==, RECORDER_STAT);
	fixture_free(&f);
}

/* The integer forms that a read never gives, refused for every type. */
static void test_refused_forms(void)
{
	static const struct {
		const char *text;
		size_t len;
	} forms[] = {
		{ "", 0 },	{ "\n", 1 },	{ "-", 1 },    { "-\n", 2 },
		{ "+1", 2 },	{ "01", 2 },	{ "00", 2 },   { "-01", 3 },
		{ "-0", 2 },	{ "-0\n", 3 },	{ "0x10", 4 }, { "1f", 2 },
		{ " 1", 2 },	{ "1 ", 2 },	{ "\t1", 2 },  { "1\t", 2 },
		{ "1\r\n", 3 }, { "1\n\n", 3 }, { "\n1", 2 },  { "1.0", 3 },
		{ "1e3", 3 },	{ "--1", 3 },	{ "abc", 3 },  { "1\0", 2 },
		{ ".5", 2 },	{ "5.", 2 },	{ "1E3", 3 },  { "1e+3", 4 },
		{ "75m", 3 },
	};
	static const struct {
		enum nine_elem elem;
		size_t size;
	} types[] = {
		{ NINE_I8, 1 },	 { NINE_I16, 2 }, { NINE_I32, 4 },
		{ NINE_I64, 8 }, { NINE_U8, 1 },  { NINE_U16, 2 },
		{ NINE_U32, 4 }, { NINE_U64, 8 },
	};
	size_t i, j;

	for (i = 0; i < ARRAY_SIZE(types); i++) {
		const struct nine_field field = { .name = "v",
						  .elem = types[i].elem };

		for (j = 0; j < ARRAY_SIZE(forms); j++)
			check_refused(&field, types[i].size, forms[j].text,
				      forms[j].len);
	}
}

static void check_does_not_fit(enum nine_elem elem, size_t size,
			       const char *text)
{
	const struct nine_field field = { .name = "v", .elem = elem };

	check_refused(&field, size, text, strlen(text));
}

static void test_integer_that_does_not_fit(void)
{
	check_does_not_fit(NINE_I8, 1, "128");
	check_does_not_fit(NINE_I8, 1, "-129");
	check_does_not_fit(NINE_I16, 2, "32768");
	check_does_not_fit(NINE_I16, 2, "-32769");
	check_does_not_fit(NINE_I32, 4, "2147483648");
	check_does_not_fit(NINE_I32, 4, "-2147483649");
	check_does_not_fit(NINE_I64, 8, "9223372036854775808");
	check_does_not_fit(NINE_I64, 8, "-9223372036854775809");
	check_does_not_fit(NINE_U8, 1, "256");
	check_does_not_fit(NINE_U8, 1, "-1");
	check_does_not_fit(NINE_U16, 2, "65536");
	check_does_not_fit(NINE_U32, 4, "4294967296");
	check_does_not_fit(NINE_U64, 8, "18446744073709551616");
	check_does_not_fit(NINE_U64, 8, "-1");
	/* These overflow 64 bits many times over. */
	check_does_not_fit(NINE_U64, 8, "99999999999999999999999999");
	check_does_not_fit(NINE_I64, 8, "-99999999999999999999999999");
}

static void check_accepted(const struct nine_field *field, size_t size,
			   const char *text)
{
	struct fixture f;

	fixture_init(&f, field, size);
	CHECK_INT(nine_write_text(&f.h, text, strlen(text)), ==, 0);
	CHECK_UINT(f.r.ncalls, ==, 2);
	fixture_free(&f);
}

static void test_range_includes_its_limits(void)
{
	const struct nine_field field = { .name = "v",
					  .elem = NINE_I32,
					  .range.i = { 0, 100 } };

	check_accepted(&field, 4, "0");
	check_accepted(&field, 4, "100");
	check_refused(&field, 4, "-1", 2);
	check_refused(&field, 4, "101", 3);
	check_refused(&field, 4, "150", 3);
}

/* Signed limits compare as signed values. */
static void test_range_of_negative_values(void)
{
	const struct nine_field field = { .name = "v",
					  .elem = NINE_I64,
					  .range.i = { INT64_MIN, -5 } };

	check_accepted(&field, 8, "-9223372036854775808");
	check_accepted(&field, 8, "-5");
	check_refused(&field, 8, "-4", 2);
	check_refused(&field, 8, "5", 1);
}

/* Limits of an unsigned type compare as unsigned values. */
static void test_range_of_large_unsigned_values(void)
{
	const struct nine_field field = {
		.name = "v",
		.elem = NINE_U64,
		.range.u = { UINT64_MAX - 1, UINT64_MAX },
	};

	check_accepted(&field, 8, "18446744073709551614");
	check_accepted(&field, 8, "18446744073709551615");
	check_refused(&field, 8, "18446744073709551613", 20);
	check_refused(&field, 8, "1", 1);
}

/* One limit of zero is still a range: only all zeros means no range. */
static void test_range_with_a_limit_of_zero(void)
{
	const struct nine_field upper = { .name = "v",
					  .elem = NINE_I32,
					  .range.i = { -5, 0 } };
	const struct nine_field lower = { .name = "v",
					  .elem = NINE_U8,
					  .range.u = { 0, 9 } };
	const struct nine_field none = { .name = "v", .elem = NINE_I32 };

	check_accepted(&upper, 4, "-5");
	check_accepted(&upper, 4, "0");
	check_refused(&upper, 4, "1", 1);
	check_accepted(&lower, 1, "9");
	check_refused(&lower, 1, "10", 2);
	check_accepted(&none, 4, "2147483647");
	check_accepted(&none, 4, "-2147483648");
}

/* The range binds a text write only. */
static void test_read_and_binary_write_ignore_the_range(void)
{
	const struct nine_field field = { .name = "v",
					  .elem = NINE_I32,
					  .range.i = { 0, 100 } };
	const int32_t bad = 150;
	struct fixture f;
	char buf[NINE_TEXT_MAX];

	fixture_init(&f, &field, sizeof(bad));
	CHECK_INT(nine_write(&f.h, &bad, sizeof(bad), 0), ==, 0);
	CHECK_MEM(f.r.value, &bad, sizeof(bad));
	CHECK_INT(nine_read_text(&f.h, buf, sizeof(buf)), ==, 4);
	CHECK_MEM(buf, "150\n", 4);
	fixture_free(&f);
}

/* The file server got exactly the @n calls of @ops, in order. */
static void check_calls(const struct recorder *r, size_t n,
			const enum recorder_op *ops)
{
	size_t i;

	CHECK_UINT(r->ncalls, ==, n);
	for (i = 0; i < n; i++)
		CHECK_INT(r->calls[i].op, ==, ops[i]);
}

static const enum recorder_op stat_only[] = { RECORDER_STAT };
static const enum recorder_op stat_read[] = { RECORDER_STAT, RECORDER_READ };
static const enum recorder_op stat_write[] = { RECORDER_STAT, RECORDER_WRITE };

static const struct nine_field int_field = { .name = "v",
					     .elem = NINE_I32,
					     .range.i = { 0, 100 } };

static void test_ebadf_comes_before_stat(void)
{
	struct nine_handle zero;
	struct nine_handle ro, wo;
	struct fixture f;
	char buf[NINE_TEXT_MAX];

	fixture_init(&f, &int_field, 4);
	f.r.stat_err = -ESTALE;
	CHECK_INT(nine_open(nine_tree_ns(f.t), "/rec/f", NINE_RDONLY, &ro), ==,
		  0);
	CHECK_INT(nine_open(nine_tree_ns(f.t), "/rec/f", NINE_WRONLY, &wo), ==,
		  0);
	memset(&zero, 0, sizeof(zero));
	f.r.ncalls = 0;

	CHECK_INT(nine_write_text(&ro, "1", 1), ==, -EBADF);
	CHECK_INT(nine_read_text(&wo, buf, sizeof(buf)), ==, -EBADF);
	CHECK_INT(nine_write_text(&zero, "1", 1), ==, -EBADF);
	CHECK_INT(nine_read_text(&zero, buf, sizeof(buf)), ==, -EBADF);
	CHECK_UINT(f.r.ncalls, ==, 0);
	fixture_free(&f);
}

static void test_stat_error_is_returned_unchanged(void)
{
	struct fixture f;
	char buf[NINE_TEXT_MAX];

	fixture_init(&f, &int_field, 4);
	f.r.stat_err = -ESTALE;
	CHECK_INT(nine_read_text(&f.h, buf, sizeof(buf)), ==, -ESTALE);
	CHECK_INT(nine_write_text(&f.h, "bad", 3), ==, -ESTALE);
	CHECK_UINT(f.r.ncalls, ==, 2);
	CHECK_INT(f.r.calls[0].op, ==, RECORDER_STAT);
	CHECK_INT(f.r.calls[1].op, ==, RECORDER_STAT);
	fixture_free(&f);
}

/* No text form: the wrong size and the missing write do not come first. */
static void check_no_text_form(struct fixture *f)
{
	char buf[NINE_TEXT_MAX];

	f->r.ops.write = NULL;
	f->r.st.size = 3;
	CHECK_INT(nine_read_text(&f->h, buf, sizeof(buf)), ==, -ENOTSUP);
	check_calls(&f->r, 1, stat_only);
	f->r.ncalls = 0;
	CHECK_INT(nine_write_text(&f->h, "bad", 3), ==, -ENOTSUP);
	check_calls(&f->r, 1, stat_only);
	fixture_free(f);
}

static void test_untyped_file_has_no_text_form(void)
{
	struct fixture f;

	fixture_init(&f, NULL, 4);
	check_no_text_form(&f);
}

static void test_bulk_file_has_no_text_form(void)
{
	struct fixture f;

	fixture_init(&f, &int_field, 4);
	f.r.st.type.flags = NINE_TYPE_BULK;
	check_no_text_form(&f);
}

static void test_directory_has_no_text_form(void)
{
	struct fixture f;

	fixture_init(&f, &int_field, 0);
	f.r.st.kind = NINE_DIR;
	check_no_text_form(&f);
}

/* An untyped node keeps its binary calls. */
static void test_untyped_file_works_in_binary(void)
{
	const int32_t value = 7;
	struct fixture f;
	int32_t got;

	fixture_init(&f, NULL, sizeof(value));
	CHECK_INT(nine_write(&f.h, &value, sizeof(value), 0), ==, 0);
	CHECK_INT(nine_read(&f.h, &got, sizeof(got), 0), ==, sizeof(got));
	CHECK_INT(got, ==, value);
	fixture_free(&f);
}

/* A file server with no write comes before a wrong size and a bad value. */
static void test_eacces_comes_before_size_and_value(void)
{
	struct fixture f;

	fixture_init(&f, &int_field, 3);
	f.r.ops.write = NULL;
	CHECK_INT(nine_write_text(&f.h, "bad", 3), ==, -EACCES);
	check_calls(&f.r, 1, stat_only);
	fixture_free(&f);
}

/* A wrong size is the file server's error, not a bad value of the user's. */
static void test_wrong_size_is_eio(void)
{
	struct fixture f;
	char buf[NINE_TEXT_MAX];

	fixture_init(&f, &int_field, 3);
	CHECK_INT(nine_write_text(&f.h, "bad", 3), ==, -EIO);
	check_calls(&f.r, 1, stat_only);
	f.r.ncalls = 0;
	CHECK_INT(nine_write_text(&f.h, "50", 2), ==, -EIO);
	check_calls(&f.r, 1, stat_only);
	f.r.ncalls = 0;
	CHECK_INT(nine_read_text(&f.h, buf, sizeof(buf)), ==, -EIO);
	check_calls(&f.r, 1, stat_only);
	fixture_free(&f);
}

static void test_short_read_is_eio(void)
{
	struct fixture f;
	char buf[NINE_TEXT_MAX];

	fixture_init(&f, &int_field, 4);
	f.r.value_len = 2;
	CHECK_INT(nine_read_text(&f.h, buf, sizeof(buf)), ==, -EIO);
	check_calls(&f.r, 2, stat_read);
	f.r.ncalls = 0;
	/* It comes before the line can be found not to fit. */
	CHECK_INT(nine_read_text(&f.h, buf, 0), ==, -EIO);
	fixture_free(&f);
}

static void test_read_error_is_returned_unchanged(void)
{
	struct fixture f;
	char buf[NINE_TEXT_MAX];

	fixture_init(&f, &int_field, 4);
	f.r.read_err = -ESTALE;
	f.r.value_len = 0;
	CHECK_INT(nine_read_text(&f.h, buf, sizeof(buf)), ==, -ESTALE);
	check_calls(&f.r, 2, stat_read);
	fixture_free(&f);
}

static void test_write_error_is_returned_unchanged(void)
{
	struct fixture f;

	fixture_init(&f, &int_field, 4);
	f.r.write_err = -ESTALE;
	CHECK_INT(nine_write_text(&f.h, "50", 2), ==, -ESTALE);
	check_calls(&f.r, 2, stat_write);
	f.r.ncalls = 0;
	f.r.write_err = -EINVAL;
	CHECK_INT(nine_write_text(&f.h, "50", 2), ==, -EINVAL);
	check_calls(&f.r, 2, stat_write);
	fixture_free(&f);
}

static void test_line_that_does_not_fit(void)
{
	const struct nine_field field = { .name = "v", .elem = NINE_I32 };
	const int32_t value = INT32_MIN;
	static const char line[] = "-2147483648\n";
	char buf[NINE_TEXT_MAX];
	struct fixture f;

	fixture_init(&f, &field, sizeof(value));
	memcpy(f.r.value, &value, sizeof(value));
	CHECK_INT(nine_read_text(&f.h, buf, 0), ==, -ERANGE);
	CHECK_INT(nine_read_text(&f.h, buf, sizeof(line) - 2), ==, -ERANGE);
	CHECK_INT(nine_read_text(&f.h, buf, sizeof(line) - 1), ==,
		  sizeof(line) - 1);
	CHECK_MEM(buf, line, sizeof(line) - 1);
	CHECK_INT(nine_read_text(&f.h, buf, NINE_TEXT_MAX), ==,
		  sizeof(line) - 1);
	fixture_free(&f);
}

/* After -EINVAL, the message comes from a stat, in any mode of handle. */
static void test_message_through_stat(void)
{
	const struct nine_field with = { .name = "v",
					 .elem = NINE_I32,
					 .range.i = { 0, 100 },
					 .message = "From 0 to 100." };
	const struct nine_field without = { .name = "v",
					    .elem = NINE_I32,
					    .range.i = { 0, 100 } };
	struct nine_handle wo;
	struct nine_stat st;
	struct fixture f;

	fixture_init(&f, &with, 4);
	CHECK_INT(nine_open(nine_tree_ns(f.t), "/rec/f", NINE_WRONLY, &wo), ==,
		  0);
	CHECK_INT(nine_write_text(&wo, "150", 3), ==, -EINVAL);
	CHECK_INT(nine_stat(&wo, &st), ==, 0);
	CHECK_STR(st.type.field->message, "From 0 to 100.");
	fixture_free(&f);

	fixture_init(&f, &without, 4);
	CHECK_INT(nine_stat(&f.h, &st), ==, 0);
	CHECK_PTR(st.type.field->message, ==, NULL);
	fixture_free(&f);
}

static void test_add_schema(void)
{
	static const struct nine_field fields[] = {
		{ .name = "v", .elem = NINE_I32 },
	};
	static const struct nine_schema schema = { .name = "s",
						   .fields = fields,
						   .nfields = 1 };
	struct nine_tree *t = nine_tree_new();

	CHECK_PTR(t, !=, NULL);
	CHECK_INT(nine_add_schema(t, &schema), ==, 0);
	nine_tree_free(t);
}

int main(void)
{
	test_read_gives_the_line();
	test_round_trip_of_each_integer_type();
	test_write_takes_a_slice();
	test_refused_forms();
	test_integer_that_does_not_fit();
	test_range_includes_its_limits();
	test_range_of_negative_values();
	test_range_of_large_unsigned_values();
	test_range_with_a_limit_of_zero();
	test_read_and_binary_write_ignore_the_range();
	test_ebadf_comes_before_stat();
	test_stat_error_is_returned_unchanged();
	test_untyped_file_has_no_text_form();
	test_bulk_file_has_no_text_form();
	test_directory_has_no_text_form();
	test_untyped_file_works_in_binary();
	test_eacces_comes_before_size_and_value();
	test_wrong_size_is_eio();
	test_short_read_is_eio();
	test_read_error_is_returned_unchanged();
	test_write_error_is_returned_unchanged();
	test_line_that_does_not_fit();
	test_message_through_stat();
	test_add_schema();
	return 0;
}
