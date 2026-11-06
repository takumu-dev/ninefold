/* SPDX-License-Identifier: Zlib */
#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "internal.h"

_Static_assert(sizeof(struct nine_handle) == 40, "a handle is 40 bytes");

/*
 * The only allocation pair of the framework. Only setup calls use it, so no
 * call after setup can run out of memory.
 */
static void *tree_alloc(size_t size)
{
	return calloc(1, size);
}

static void tree_release(void *p)
{
	free(p);
}

struct nine_tree *nine_tree_new(void)
{
	struct nine_tree *t;

	t = tree_alloc(sizeof(*t));
	if (!t)
		return NULL;

	t->root.tree = t;
	return t;
}

void nine_tree_free(struct nine_tree *t)
{
	struct nine_mount *m, *next;

	if (!t)
		return;

	for (m = t->mounts; m; m = next) {
		next = m->next;
		tree_release(m);
	}
	tree_release(t);
}

struct nine_ns *nine_tree_ns(struct nine_tree *t)
{
	return &t->root;
}

static bool ops_complete(const struct nine_file_server_ops *ops)
{
	return ops && ops->lookup && ops->list && ops->stat && ops->read;
}

int nine_mount(struct nine_tree *t, const char *path,
	       const struct nine_file_server_ops *ops, void *priv)
{
	struct nine_mount *m;
	size_t len;

	if (!nine_path_valid(path) || !ops_complete(ops))
		return -EINVAL;

	len = strlen(path);
	m = tree_alloc(sizeof(*m) + len + 1);
	if (!m)
		return -ENOMEM;

	m->ops = ops;
	m->priv = priv;
	m->path = (char *)(m + 1);
	memcpy(m->path, path, len + 1);
	m->next = t->mounts;
	t->mounts = m;
	return 0;
}
