/* SPDX-License-Identifier: Zlib */
#ifndef NINEFOLD_INTERNAL_H
#define NINEFOLD_INTERNAL_H

#include <stdbool.h>
#include <stddef.h>

#include "ninefold.h"

/*
 * struct nine_mount - A file server attached to a tree.
 *
 * Each mount is its own allocation, with its copy of the path right after
 * it. Its address, which handles hold, stays the same until the tree is
 * freed.
 */
struct nine_mount {
	struct nine_mount *next;
	const struct nine_file_server_ops *ops;
	void *priv;
	char *path;
};

struct nine_ns {
	struct nine_tree *tree;
};

/*
 * struct nine_tree - A tree.
 * @root: Its root namespace.
 * @mounts: Its mounts, in the order they were made.
 * @dirs: The file server of the directories above the mount points, which
 *	no path in @mounts reaches. Its path is NULL.
 */
struct nine_tree {
	struct nine_ns root;
	struct nine_mount *mounts;
	struct nine_mount dirs;
};

extern const struct nine_file_server_ops nine_dir_ops;

const char *nine_path_next(const char *p, size_t *len);
bool nine_path_valid(const char *path);
const char *nine_path_skip(const char *prefix, const char *path);

#endif /* NINEFOLD_INTERNAL_H */
