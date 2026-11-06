/* SPDX-License-Identifier: Zlib */
/*
 * The public header compiles as C++11, and its calls link from C++. Without
 * its extern "C" guards, the link fails.
 */
#include "ninefold.h"

/* ESTALE comes with the header, also where <errno.h> has no such name. */
static_assert(ESTALE > 0, "ESTALE is a positive error code");

int main()
{
	struct nine_tree *t = nine_tree_new();
	struct nine_handle h;
	char buf[4];

	if (!t || !nine_tree_ns(t))
		return 1;

	/* With nothing mounted, the open fails and zeroes the handle. */
	if (nine_open(nine_tree_ns(t), "/a", NINE_RDONLY, &h) == 0)
		return 1;
	if (nine_handle_node(&h)->w[0] != 0)
		return 1;
	if (nine_read(&h, buf, sizeof(buf), 0) >= 0)
		return 1;

	nine_tree_free(t);
	return 0;
}
