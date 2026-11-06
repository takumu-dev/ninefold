/* SPDX-License-Identifier: Zlib */
#include <errno.h>
#include <string.h>

#include "internal.h"

/*
 * The directories above the mount points, served as a file server that the
 * tree mounts for itself. Each directory lists the next name of each mount
 * path that goes through it.
 *
 * A node holds the directory's depth, its number of names from the root, in
 * w[1], and in w[0] a mount whose path goes through it, to give those names.
 * The root needs no mount, so its node is all zeros, as a mount root's is.
 */

static const struct nine_mount *node_mount(const struct nine_node *node)
{
	return (const struct nine_mount *)(uintptr_t)node->w[0];
}

static uint64_t node_depth(const struct nine_node *node)
{
	return node->w[1];
}

static void make_node(struct nine_node *node, const struct nine_mount *m,
		      uint64_t depth)
{
	node->w[0] = (uintptr_t)m;
	node->w[1] = depth;
	node->w[2] = 0;
}

/*
 * name_below() - Find the name that the path of @m has right below @dir.
 *
 * Return: the name, with its length in *@len, or NULL when the path of @m
 * does not go through @dir.
 */
static const char *name_below(const struct nine_node *dir,
			      const struct nine_mount *m, size_t *len)
{
	const struct nine_mount *ref = node_mount(dir);
	const char *name = m->path, *want = ref ? ref->path : "";
	size_t wantlen;
	uint64_t i;

	for (i = 0; i < node_depth(dir); i++) {
		name = nine_path_next(name, len);
		want = nine_path_next(want, &wantlen);
		if (!name || *len != wantlen || memcmp(name, want, *len) != 0)
			return NULL;
		name += *len;
		want += wantlen;
	}
	return nine_path_next(name, len);
}

/* Find the first mount whose path has @name of @len bytes below @dir. */
static const struct nine_mount *first_with(const struct nine_tree *t,
					   const struct nine_node *dir,
					   const char *name, size_t len)
{
	const struct nine_mount *m;
	const char *below;
	size_t belowlen;

	for (m = t->mounts; m; m = m->next) {
		below = name_below(dir, m, &belowlen);
		if (below && belowlen == len && memcmp(below, name, len) == 0)
			return m;
	}
	return NULL;
}

static int dir_lookup(void *priv, const struct nine_node *dir, const char *name,
		      size_t len, struct nine_node *out)
{
	const struct nine_mount *m = first_with(priv, dir, name, len);

	if (!m)
		return -ENOENT;

	make_node(out, m, node_depth(dir) + 1);
	return 0;
}

/*
 * Put @name of @len bytes and a NUL in @buf of @size bytes, after the *@used
 * bytes there. Return: true, or false when it does not fit.
 */
static bool put_name(char *buf, size_t size, size_t *used, const char *name,
		     size_t len)
{
	if (len >= size - *used)
		return false;

	memcpy(buf + *used, name, len);
	buf[*used + len] = '\0';
	*used += len + 1;
	return true;
}

/*
 * The cursor is the index in the mount list of the next mount to list a name
 * for. A mount lists its name only when no mount before it has the same one.
 */
static int dir_list(void *priv, const struct nine_node *dir, uint64_t *cursor,
		    char *buf, size_t len)
{
	const struct nine_tree *t = priv;
	const struct nine_mount *m;
	const char *name;
	size_t namelen, used = 0;
	uint64_t i;
	int n = 0;

	for (m = t->mounts, i = 0; m && i < *cursor; m = m->next, i++)
		;

	for (; m; m = m->next, i++) {
		name = name_below(dir, m, &namelen);
		if (!name || first_with(t, dir, name, namelen) != m)
			continue;
		if (!put_name(buf, len, &used, name, namelen))
			break;
		n++;
	}
	if (!n && m)
		return -ERANGE;

	*cursor = i;
	return n;
}

static int dir_stat(void *priv, const struct nine_node *node,
		    struct nine_stat *st)
{
	(void)priv;
	(void)node;
	st->kind = NINE_DIR;
	return 0;
}

static ptrdiff_t dir_read(void *priv, const struct nine_node *node, void *buf,
			  size_t len, uint64_t off)
{
	(void)priv;
	(void)node;
	(void)buf;
	(void)len;
	(void)off;
	return -EISDIR;
}

static int dir_write(void *priv, const struct nine_node *node, const void *buf,
		     size_t len, uint64_t off)
{
	(void)priv;
	(void)node;
	(void)buf;
	(void)len;
	(void)off;
	return -EISDIR;
}

const struct nine_file_server_ops nine_dir_ops = {
	.lookup = dir_lookup,
	.list = dir_list,
	.stat = dir_stat,
	.read = dir_read,
	.write = dir_write,
};
