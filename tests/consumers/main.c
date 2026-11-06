/* SPDX-License-Identifier: Zlib */
#include <stdlib.h>

#include <ninefold.h>

int main(void)
{
	struct nine_tree *t;

	t = nine_tree_new();
	if (!t)
		return EXIT_FAILURE;

	nine_tree_free(t);
	return EXIT_SUCCESS;
}
