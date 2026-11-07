/* SPDX-License-Identifier: Zlib */
#include "ninefold.h"

#include "check.h"

int main(void)
{
	struct nine_tree *t;
	struct nine_ns *ns;

	t = nine_tree_new();
	CHECK_PTR(t, !=, NULL);

	ns = nine_tree_ns(t);
	CHECK_PTR(ns, !=, NULL);
	CHECK_PTR(nine_tree_ns(t), ==, ns);

	nine_tree_free(t);
	nine_tree_free(NULL);
	return 0;
}
