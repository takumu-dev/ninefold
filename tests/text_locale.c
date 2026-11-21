/* SPDX-License-Identifier: Zlib */
#include <errno.h>
#include <locale.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ninefold.h"

#include "check.h"
#include "recorder.h"

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

/* CTest counts a test that exits with this code as skipped. */
#define SKIPPED 77

/*
 * Switch LC_NUMERIC to a locale whose decimal separator is a comma, by the
 * names glibc and the Microsoft C library give them.
 * Return: whether one was there.
 */
static int use_comma_locale(void)
{
	static const char *const names[] = {
		"de_DE.UTF-8", "de_DE.utf8", "de_DE", "fr_FR.UTF-8",
		"fr_FR.utf8",  "fr_FR",	     "de-DE", "German_Germany",
	};
	size_t i;

	for (i = 0; i < ARRAY_SIZE(names); i++) {
		if (setlocale(LC_NUMERIC, names[i]) &&
		    !strcmp(localeconv()->decimal_point, ","))
			return 1;
	}
	return 0;
}

/* Write @text to a vec2 of f32, then read back @line and the bytes @want. */
static void check_round_trip(struct nine_handle *h, struct recorder *r,
			     const char *text, const char *line,
			     const float want[2])
{
	char buf[NINE_TEXT_MAX];
	size_t len = strlen(line);

	CHECK_INT(nine_write_text(h, text, strlen(text)), ==, 0);
	CHECK_MEM(r->value, want, 2 * sizeof(float));
	CHECK_INT(nine_read_text(h, buf, sizeof(buf)), ==, len);
	CHECK_MEM(buf, line, len);
}

/* The text form keeps its point, and refuses a comma, in any C locale. */
int main(void)
{
	static const struct nine_field field = { .name = "v",
						 .elem = NINE_F32,
						 .shape = NINE_VEC2,
						 .range.f = { -0.5, 1e10 } };
	static const char field_line[] = "vec2 f32 range=-0.5..1e10\n";
	static const float want[2] = { 1.5f, -0.25f };
	char buf[NINE_TEXT_MAX];
	struct nine_handle h;
	struct nine_tree *t;
	struct recorder r;

	if (!use_comma_locale()) {
		puts("No locale with a decimal comma: skipped");
		return SKIPPED;
	}
	/* The C library's own conversions now use the comma. */
	snprintf(buf, sizeof(buf), "%.1f", 1.5);
	CHECK_STR(buf, "1,5");

	recorder_init(&r);
	r.st.kind = NINE_FILE;
	r.st.size = sizeof(want);
	r.st.type.field = &field;
	r.value_len = sizeof(want);
	t = nine_tree_new();
	CHECK_PTR(t, !=, NULL);
	CHECK_INT(nine_mount(t, "/rec", &r.ops, &r), ==, 0);
	CHECK_INT(nine_open(nine_tree_ns(t), "/rec/f", NINE_RDWR, &h), ==, 0);

	check_round_trip(&h, &r, "1.5 -0.25", "1.5 -0.25\n", want);
	check_round_trip(&h, &r, "1.50 -25e-2\n", "1.5 -0.25\n", want);
	CHECK_INT(nine_write_text(&h, "1,5 -0,25", 9), ==, -EINVAL);
	CHECK_INT(nine_write_text(&h, "1.5 -0,25", 9), ==, -EINVAL);
	CHECK_MEM(r.value, want, sizeof(want));

	CHECK_INT(nine_field_text(&field, buf, sizeof(buf)), ==,
		  sizeof(field_line) - 1);
	CHECK_MEM(buf, field_line, sizeof(field_line) - 1);

	nine_tree_free(t);
	setlocale(LC_NUMERIC, "C");
	return 0;
}
