/* SPDX-License-Identifier: Zlib */
#include <errno.h>
#include <string.h>

#include "internal.h"

/*
 * nine_path_next() - Find the next name in a path.
 *
 * Skips any slashes at @p. Return: the start of the name, with its length in
 * *@len, or NULL at the end of the path.
 */
const char *nine_path_next(const char *p, size_t *len)
{
	while (*p == '/')
		p++;
	if (!*p)
		return NULL;

	*len = strcspn(p, "/");
	return p;
}

static bool is_dot_name(const char *name, size_t len)
{
	return (len == 1 && name[0] == '.') ||
	       (len == 2 && name[0] == '.' && name[1] == '.');
}

/* An absolute path with no "." or ".." component. */
bool nine_path_valid(const char *path)
{
	const char *name;
	size_t len;

	if (path[0] != '/')
		return false;

	for (name = nine_path_next(path, &len); name;
	     name = nine_path_next(name + len, &len)) {
		if (is_dot_name(name, len))
			return false;
	}
	return true;
}

/*
 * nine_path_skip() - Skip the names of @prefix at the start of @path.
 *
 * Return: the rest of @path, or NULL when @path does not start with every
 * name of @prefix.
 */
const char *nine_path_skip(const char *prefix, const char *path)
{
	const char *pre, *name;
	size_t prelen, len;

	for (pre = nine_path_next(prefix, &prelen); pre;
	     pre = nine_path_next(pre + prelen, &prelen)) {
		name = nine_path_next(path, &len);
		if (!name || len != prelen || memcmp(name, pre, len) != 0)
			return NULL;
		path = name + len;
	}
	return path;
}

static const struct nine_mount *find_mount(const struct nine_tree *t,
					   const char *path, const char **rest)
{
	const struct nine_mount *m;

	for (m = t->mounts; m; m = m->next) {
		*rest = nine_path_skip(m->path, path);
		if (*rest)
			return m;
	}
	return NULL;
}

/* Look up each name of @path in turn, from the root of mount @m. */
static int walk(const struct nine_mount *m, const char *path,
		struct nine_node *node)
{
	struct nine_node next;
	const char *name;
	size_t len;
	int err;

	memset(node, 0, sizeof(*node));
	for (name = nine_path_next(path, &len); name;
	     name = nine_path_next(name + len, &len)) {
		err = m->ops->lookup(m->priv, node, name, len, &next);
		if (err)
			return err;
		*node = next;
	}
	return 0;
}

static bool mode_valid(int mode)
{
	return mode == NINE_RDONLY || mode == NINE_WRONLY || mode == NINE_RDWR;
}

int nine_open(struct nine_ns *ns, const char *path, int mode,
	      struct nine_handle *h)
{
	const struct nine_mount *m;
	struct nine_node node;
	const char *rest;
	int err;

	memset(h, 0, sizeof(*h));
	if (!mode_valid(mode) || !nine_path_valid(path))
		return -EINVAL;

	/* A path that reaches no mount stays in the framework's directories. */
	m = find_mount(ns->tree, path, &rest);
	if (!m) {
		m = &ns->tree->dirs;
		rest = path;
	}

	err = walk(m, rest, &node);
	if (err)
		return err;

	h->mount = m;
	h->node = node;
	h->mode = mode;
	return 0;
}
