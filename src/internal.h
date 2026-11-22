/* SPDX-License-Identifier: Zlib */
#ifndef NINEFOLD_INTERNAL_H
#define NINEFOLD_INTERNAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

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

bool nine_dir_put_name(char *buf, size_t size, size_t *used, const char *name,
		       size_t len);

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

/* The longest text of an f32, such as -1.16047665e-36 or -0.000100000005. */
#define NINE_F32_TEXT_MAX 15

/*
 * nine_f32_format() - Write the text of the f32 whose bit pattern is @bits
 * to @buf, which has room for NINE_F32_TEXT_MAX bytes, with no newline.
 * Return: the number of bytes.
 */
size_t nine_f32_format(uint32_t bits, char *buf);

/*
 * nine_f32_parse() - Parse the @len bytes at @text, a whole number with no
 * newline, into @v. A NaN becomes one quiet NaN.
 * Return: 0, or -EINVAL for bad syntax, a value too large for an f32, or a
 * value that is not zero but becomes zero.
 */
int nine_f32_parse(const char *text, size_t len, float *v);

/* The longest text of an f64, such as -2.2250738585072014e-308. */
#define NINE_F64_TEXT_MAX 24

/*
 * nine_f64_format() - Write the text of the f64 whose bit pattern is @bits
 * to @buf, which has room for NINE_F64_TEXT_MAX bytes, with no newline.
 * Return: the number of bytes.
 */
size_t nine_f64_format(uint64_t bits, char *buf);

/*
 * nine_f64_parse() - Parse the @len bytes at @text, a whole number with no
 * newline, into @v. A NaN becomes one quiet NaN.
 * Return: 0, or -EINVAL for bad syntax, a value too large for an f64, or a
 * value that is not zero but becomes zero.
 */
int nine_f64_parse(const char *text, size_t len, double *v);

#endif /* NINEFOLD_INTERNAL_H */
