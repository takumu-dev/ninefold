/* SPDX-License-Identifier: Zlib */
/*
 * The public header compiles as C++11, and its calls link from C++. Without
 * its extern "C" guards, the link fails.
 */
#include "ninefold.h"

/* ESTALE comes with the header, also where <errno.h> has no such name. */
static_assert(ESTALE > 0, "ESTALE is a positive error code");

/* A schema descriptor compiles as C++11, which has no designators. */
static const struct nine_field fields[] = {
	{ "health",
	  NINE_I32,
	  NINE_SCALAR,
	  nullptr,
	  { { 0, 100 } },
	  nullptr,
	  0,
	  "Health must be from 0 to 100." },
};
static const struct nine_schema schema = { "unit", fields, 1 };

int main()
{
	struct nine_tree *t = nine_tree_new();
	struct nine_handle h;
	char buf[4];
	int err;

	if (!t || !nine_tree_ns(t))
		return 1;

	/* With nothing mounted, the open fails and zeroes the handle. */
	if (nine_open(nine_tree_ns(t), "/a", NINE_RDONLY, &h) == 0)
		return 1;
	if (nine_handle_node(&h)->w[0] != 0)
		return 1;
	if (nine_read(&h, buf, sizeof(buf), 0) >= 0)
		return 1;
	if (nine_read_many(&h, 1, buf, sizeof(buf), &err) >= 0)
		return 1;
	if (nine_read_text(&h, buf, sizeof(buf)) >= 0)
		return 1;
	if (nine_write_text(&h, buf, sizeof(buf)) >= 0)
		return 1;
	if (nine_add_schema(t, &schema) != 0)
		return 1;

	nine_tree_free(t);
	return 0;
}
