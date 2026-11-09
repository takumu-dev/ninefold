/* SPDX-License-Identifier: Zlib */
/*
 * The demo program. It mounts two demo instances in a tree, then walks the
 * tree from / with nine_list(), nine_stat() and nine_read(). For each node
 * it prints a line with its path, its kind and its size, and after a file's
 * line, the file's bytes in hex. Then it writes a good and a bad value as
 * text to one entity's health, and prints the error and the human message.
 */
#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "demo.h"
#include "ninefold.h"

/* The bytes of a file are printed this many to a line. */
#define BYTES_PER_LINE 12

static int fail(const char *what, const char *path, int err)
{
	fprintf(stderr, "walk: %s %s: error %d\n", what, path, -err);
	return err;
}

static void print_bytes(const unsigned char *bytes, size_t n)
{
	size_t i;

	for (i = 0; i < n; i++) {
		printf(i % BYTES_PER_LINE ? " %02x" : "    %02x", bytes[i]);
		if (i % BYTES_PER_LINE == BYTES_PER_LINE - 1 || i == n - 1)
			putchar('\n');
	}
}

static int print_file(const struct nine_handle *h, const char *path,
		      uint64_t size)
{
	unsigned char *bytes;
	ptrdiff_t n;

	if (size > SIZE_MAX)
		return fail("read", path, -ENOMEM);
	bytes = malloc(size ? (size_t)size : 1);
	if (!bytes)
		return fail("read", path, -ENOMEM);

	n = nine_read(h, bytes, (size_t)size, 0);
	if (n < 0) {
		free(bytes);
		return fail("read", path, (int)n);
	}
	print_bytes(bytes, (size_t)n);
	free(bytes);
	return 0;
}

static int walk(struct nine_ns *ns, const char *path);

/* Walk the node @name in the directory at @dir. */
static int walk_child(struct nine_ns *ns, const char *dir, const char *name)
{
	size_t dirlen = strlen(dir), namelen = strlen(name);
	char *path;
	int err;

	/* The root's path already ends with a slash. */
	if (dir[dirlen - 1] == '/')
		dirlen--;
	path = malloc(dirlen + 1 + namelen + 1);
	if (!path)
		return fail("walk", dir, -ENOMEM);

	memcpy(path, dir, dirlen);
	path[dirlen] = '/';
	memcpy(path + dirlen + 1, name, namelen + 1);
	err = walk(ns, path);
	free(path);
	return err;
}

/*
 * Walk each node in the directory at @path. The buffer for the names starts
 * small and doubles each time the next name does not fit.
 */
static int walk_dir(struct nine_ns *ns, const struct nine_handle *dir,
		    const char *path)
{
	uint64_t cursor = 0;
	size_t len = 8;
	char *buf, *bigger;
	const char *name;
	int n, i, err = 0;

	buf = malloc(len);
	if (!buf)
		return fail("list", path, -ENOMEM);

	while ((n = nine_list(dir, &cursor, buf, len)) != 0) {
		if (n == -ERANGE) {
			bigger = realloc(buf, 2 * len);
			if (!bigger) {
				err = fail("list", path, -ENOMEM);
				goto out;
			}
			buf = bigger;
			len *= 2;
			continue;
		}
		if (n < 0) {
			err = fail("list", path, n);
			goto out;
		}

		for (i = 0, name = buf; i < n; i++, name += strlen(name) + 1) {
			err = walk_child(ns, path, name);
			if (err)
				goto out;
		}
	}
out:
	free(buf);
	return err;
}

/* Print the node at @path, and walk on below it. */
static int walk(struct nine_ns *ns, const char *path)
{
	struct nine_handle h;
	struct nine_stat st;
	int err;

	err = nine_open(ns, path, NINE_RDONLY, &h);
	if (err)
		return fail("open", path, err);
	err = nine_stat(&h, &st);
	if (err)
		return fail("stat", path, err);

	printf("%s %s %" PRIu64 "\n", path,
	       st.kind == NINE_DIR ? "dir" : "file", st.size);
	if (st.kind == NINE_DIR)
		return walk_dir(ns, &h, path);

	return print_file(&h, path, st.size);
}

/* Write @text to the value file at @h, and print what came of it. */
static int write_text(const struct nine_handle *h, const char *path,
		      const char *text)
{
	struct nine_stat st;
	int err;

	err = nine_write_text(h, text, strlen(text));
	if (!err) {
		printf("write %s to %s: ok\n", text, path);
		return 0;
	}
	if (err != -EINVAL)
		return fail("write", path, err);

	err = nine_stat(h, &st);
	if (err)
		return fail("stat", path, err);

	printf("write %s to %s: EINVAL: %s\n", text, path,
	       st.type.field->message);
	return 0;
}

static int print_text(const struct nine_handle *h, const char *path)
{
	char line[NINE_TEXT_MAX];
	ptrdiff_t n;

	n = nine_read_text(h, line, sizeof(line));
	if (n < 0)
		return fail("read", path, (int)n);

	printf("read %s: %.*s", path, (int)n, line);
	return 0;
}

/* Write a good and a bad value as text. The bad one changes nothing. */
static int show_text(struct nine_ns *ns)
{
	static const char path[] = "/world/entities/0/components/unit/health";
	struct nine_handle h;
	int err;

	err = nine_open(ns, path, NINE_RDWR, &h);
	if (err)
		return fail("open", path, err);

	err = write_text(&h, path, "75");
	if (!err)
		err = print_text(&h, path);
	if (!err)
		err = write_text(&h, path, "150");
	if (!err)
		err = print_text(&h, path);
	return err;
}

/*
 * Fill @d with @n entities, entity i at (i, 10 i, 100 i) plus @shift, then
 * destroy entity 1 when there is one, so that the last entity moves into its
 * slot.
 */
static int fill(struct demo *d, int n, float shift)
{
	float pos[3];
	uint32_t id;
	int i, err;

	for (i = 0; i < n; i++) {
		err = demo_create(d, &id);
		if (err)
			return err;
		pos[0] = (float)i + shift;
		pos[1] = 10.0f * (float)i + shift;
		pos[2] = 100.0f * (float)i + shift;
		err = demo_set_pos(d, id, pos);
		if (err)
			return err;
	}
	return n > 1 ? demo_destroy(d, 1) : 0;
}

int main(void)
{
	struct nine_tree *t;
	struct demo *world, *sandbox;
	int err = -ENOMEM;

	t = nine_tree_new();
	world = demo_new();
	sandbox = demo_new();
	if (!t || !world || !sandbox)
		goto out;

	err = fill(world, 4, 0.0f);
	if (!err)
		err = fill(sandbox, 3, 0.5f);
	if (!err)
		err = demo_add_schemas(t);
	if (!err)
		err = nine_mount(t, "/world", demo_ops(), world);
	if (!err)
		err = nine_mount(t, "/sandbox/world", demo_ops(), sandbox);
	if (!err)
		err = walk(nine_tree_ns(t), "/");
	if (!err)
		err = show_text(nine_tree_ns(t));
out:
	if (err)
		fprintf(stderr, "walk: error %d\n", -err);
	nine_tree_free(t);
	demo_free(world);
	demo_free(sandbox);
	return err ? EXIT_FAILURE : EXIT_SUCCESS;
}
