/* SPDX-License-Identifier: Zlib */
/*
 * The public header compiles as C++11, and its calls link from C++. Without
 * its extern "C" guards, the link fails.
 */
#include "ninefold.h"

int main()
{
	struct nine_tree *t = nine_tree_new();

	if (!t || !nine_tree_ns(t))
		return 1;

	nine_tree_free(t);
	return 0;
}
