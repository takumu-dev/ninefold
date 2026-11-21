/* SPDX-License-Identifier: Zlib */
#include <errno.h>
#include <stdint.h>
#include <string.h>

#include "internal.h"

/*
 * The line of a field, and the read-only file server that shows the schemas
 * of a tree: one directory for each schema, and in it one file for each
 * field, which holds the line.
 *
 * A node holds the schema entry in w[0] and the index of the field plus one
 * in w[1]. The root holds no schema, and a schema directory no field, so the
 * root's node is all zeros, as a mount root's is.
 */

/* The longest number of any element type, with its newline. */
#define NUMBER_MAX 32

static const char *const elem_names[] = {
	[NINE_I8] = "i8",   [NINE_I16] = "i16",	  [NINE_I32] = "i32",
	[NINE_I64] = "i64", [NINE_U8] = "u8",	  [NINE_U16] = "u16",
	[NINE_U32] = "u32", [NINE_U64] = "u64",	  [NINE_F32] = "f32",
	[NINE_F64] = "f64", [NINE_BOOL] = "bool",
};

static const char *const shape_names[] = {
	[NINE_VEC2] = "vec2",
	[NINE_VEC3] = "vec3",
	[NINE_VEC4] = "vec4",
	[NINE_QUAT] = "quat",
};

/*
 * struct line - Where the bytes of a line go.
 * @buf: Gets the bytes from offset @off.
 * @len: The size of @buf in bytes.
 * @off: The offset in the line of the first byte of @buf.
 * @size: The number of bytes of the line so far.
 *
 * Only the @len bytes from offset @off reach @buf. Thus a line of any length
 * needs no buffer of its own, and a read at an offset makes the line once.
 */
struct line {
	char *buf;
	size_t len;
	uint64_t off;
	uint64_t size;
};

/* Add the @n bytes at @s to the line @l. */
static void put(struct line *l, const char *s, size_t n)
{
	uint64_t end = l->size + n, from, at;
	size_t count;

	if (end > l->off) {
		from = l->size > l->off ? l->size : l->off;
		at = from - l->off;
		if (at < l->len) {
			count = end - from < l->len - at ?
					(size_t)(end - from) :
					l->len - (size_t)at;
			memcpy(l->buf + at, s + (from - l->size), count);
		}
	}
	l->size = end;
}

static void put_str(struct line *l, const char *s)
{
	put(l, s, strlen(s));
}

/* Write the low @size bytes of @v to @bin. */
static void store(void *bin, size_t size, uint64_t v)
{
	uint8_t v8 = (uint8_t)v;
	uint16_t v16 = (uint16_t)v;
	uint32_t v32 = (uint32_t)v;

	switch (size) {
	case 1:
		memcpy(bin, &v8, size);
		break;
	case 2:
		memcpy(bin, &v16, size);
		break;
	case 4:
		memcpy(bin, &v32, size);
		break;
	default:
		memcpy(bin, &v, size);
		break;
	}
}

/*
 * Add the integer of element type @e whose bit pattern is @v, in the text
 * form of @e: the text of a scalar of that type, without its newline.
 */
static void put_number(struct line *l, enum nine_elem e, uint64_t v)
{
	const struct nine_field scalar = { .elem = e };
	unsigned char bin[NINE_VALUE_MAX];
	char text[NUMBER_MAX];
	ptrdiff_t n;

	store(bin, nine_value_size(&scalar), v);
	n = nine_value_format(&scalar, bin, text, sizeof(text));
	put(l, text, (size_t)n - 1);
}

static void put_type(struct line *l, const struct nine_field *f)
{
	if (f->values) {
		put_str(l, "enum ");
	} else if (f->shape != NINE_SCALAR) {
		put_str(l, shape_names[f->shape]);
		put_str(l, " ");
	}
	put_str(l, elem_names[f->elem]);
}

static void put_values(struct line *l, const struct nine_field *f)
{
	size_t i;

	if (!f->values)
		return;

	put_str(l, " values=");
	for (i = 0; i < f->nvalues; i++) {
		if (i)
			put_str(l, ",");
		put_str(l, f->values[i].name);
		put_str(l, ":");
		put_number(l, f->elem, (uint64_t)f->values[i].value);
	}
}

/*
 * The limits of an integer read through range.u: the union holds the same
 * bits for a signed limit, which put_number() takes as its bit pattern.
 * The limits of a float are not shown yet, as floats have no text form yet.
 */
static void put_range(struct line *l, const struct nine_field *f)
{
	if (!f->range.u.min && !f->range.u.max)
		return;
	if (f->elem == NINE_F32 || f->elem == NINE_F64)
		return;

	put_str(l, " range=");
	put_number(l, f->elem, f->range.u.min);
	put_str(l, "..");
	put_number(l, f->elem, f->range.u.max);
}

/* Add the item @key and its text @s. Text that is NULL or empty is none. */
static void put_item(struct line *l, const char *key, const char *s)
{
	if (!s || !*s)
		return;

	put_str(l, key);
	put_str(l, s);
}

/* Make the line of @f, a field of a valid schema, into @l. */
static void make_line(struct line *l, const struct nine_field *f)
{
	put_type(l, f);
	put_values(l, f);
	put_item(l, " unit=", f->unit);
	put_range(l, f);
	put_item(l, " message=", f->message);
	put_str(l, "\n");
}

ptrdiff_t nine_field_text(const struct nine_field *f, char *buf, size_t len)
{
	struct line l = { .buf = buf, .len = len };

	make_line(&l, f);
	if (l.size > len)
		return -ERANGE;

	return (ptrdiff_t)l.size;
}

static const struct nine_schema_entry *node_entry(const struct nine_node *node)
{
	return (const struct nine_schema_entry *)(uintptr_t)node->w[0];
}

/* The field of the file @node, or NULL for a directory. */
static const struct nine_field *node_field(const struct nine_node *node)
{
	if (!node->w[1])
		return NULL;

	return &node_entry(node)->schema->fields[node->w[1] - 1];
}

static void make_node(struct nine_node *node, const struct nine_schema_entry *e,
		      uint64_t field)
{
	node->w[0] = (uintptr_t)e;
	node->w[1] = field;
	node->w[2] = 0;
}

/* Tell whether @s is @name of @len bytes. */
static bool name_is(const char *s, const char *name, size_t len)
{
	return strncmp(s, name, len) == 0 && s[len] == '\0';
}

static int types_lookup(void *priv, const struct nine_node *dir,
			const char *name, size_t len, struct nine_node *out)
{
	const struct nine_tree *t = priv;
	const struct nine_schema_entry *e = node_entry(dir);
	size_t i;

	if (node_field(dir))
		return -ENOTDIR;

	if (!e) {
		for (e = t->schemas; e; e = e->next) {
			if (name_is(e->schema->name, name, len)) {
				make_node(out, e, 0);
				return 0;
			}
		}
		return -ENOENT;
	}

	for (i = 0; i < e->schema->nfields; i++) {
		if (name_is(e->schema->fields[i].name, name, len)) {
			make_node(out, e, i + 1);
			return 0;
		}
	}
	return -ENOENT;
}

/* The name of entry number @i of the directory @dir, or NULL past the end. */
static const char *child_name(const struct nine_tree *t,
			      const struct nine_node *dir, uint64_t i)
{
	const struct nine_schema_entry *e = node_entry(dir);

	if (e)
		return i < e->schema->nfields ? e->schema->fields[i].name :
						NULL;

	for (e = t->schemas; e && i; e = e->next, i--)
		;
	return e ? e->schema->name : NULL;
}

/* The cursor is the number of the next entry to list. */
static int types_list(void *priv, const struct nine_node *dir, uint64_t *cursor,
		      char *buf, size_t len)
{
	const char *name;
	size_t used = 0;
	uint64_t i;
	int n = 0;

	if (node_field(dir))
		return -ENOTDIR;

	for (i = *cursor; (name = child_name(priv, dir, i)); i++) {
		if (!nine_dir_put_name(buf, len, &used, name, strlen(name)))
			break;
		n++;
	}
	if (!n && name)
		return -ERANGE;

	*cursor = i;
	return n;
}

static int types_stat(void *priv, const struct nine_node *node,
		      struct nine_stat *st)
{
	const struct nine_field *f = node_field(node);
	struct line l = { 0 };

	(void)priv;
	if (!f) {
		st->kind = NINE_DIR;
		return 0;
	}

	make_line(&l, f);
	st->kind = NINE_FILE;
	st->size = l.size;
	return 0;
}

static ptrdiff_t types_read(void *priv, const struct nine_node *node, void *buf,
			    size_t len, uint64_t off)
{
	const struct nine_field *f = node_field(node);
	struct line l = { .buf = buf, .len = len, .off = off };

	(void)priv;
	if (!f)
		return -EISDIR;

	make_line(&l, f);
	if (off >= l.size)
		return 0;

	return l.size - off < len ? (ptrdiff_t)(l.size - off) : (ptrdiff_t)len;
}

static const struct nine_file_server_ops types_ops = {
	.lookup = types_lookup,
	.list = types_list,
	.stat = types_stat,
	.read = types_read,
};

int nine_mount_types(struct nine_tree *t, const char *path)
{
	return nine_mount(t, path, &types_ops, t);
}
