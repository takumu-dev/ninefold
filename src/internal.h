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

/*
 * struct nine_schema_entry - A schema added to a tree. The tree keeps only
 * the pointer to the descriptor, which the app keeps valid.
 */
struct nine_schema_entry {
	struct nine_schema_entry *next;
	const struct nine_schema *schema;
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
 * @schemas: Its schemas, in the order they were added.
 */
struct nine_tree {
	struct nine_ns root;
	struct nine_mount *mounts;
	struct nine_mount dirs;
	struct nine_schema_entry *schemas;
};

extern const struct nine_file_server_ops nine_dir_ops;

const char *nine_path_next(const char *p, size_t *len);
bool nine_path_valid(const char *path);
bool nine_path_name_valid(const char *name);
const char *nine_path_skip(const char *prefix, const char *path);

bool nine_schema_valid(const struct nine_schema *s);

/* The largest binary form of a value, a vec4 or a quat of f64, in bytes. */
#define NINE_VALUE_MAX 32

bool nine_value_has_text(const struct nine_field *f);
size_t nine_value_size(const struct nine_field *f);
ptrdiff_t nine_value_format(const struct nine_field *f, const void *bin,
			    char *buf, size_t len);
int nine_value_parse(const struct nine_field *f, const char *text, size_t len,
		     void *bin);

#endif /* NINEFOLD_INTERNAL_H */
