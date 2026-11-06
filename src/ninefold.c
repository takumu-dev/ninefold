/* SPDX-License-Identifier: Zlib */
#include <stdlib.h>

#include "ninefold.h"

struct nine_ns {
	struct nine_tree *tree;
};

struct nine_tree {
	struct nine_ns root;
};

struct nine_tree *nine_tree_new(void)
{
	struct nine_tree *t;

	t = calloc(1, sizeof(*t));
	if (!t)
		return NULL;

	t->root.tree = t;
	return t;
}

void nine_tree_free(struct nine_tree *t)
{
	free(t);
}

struct nine_ns *nine_tree_ns(struct nine_tree *t)
{
	return &t->root;
}
