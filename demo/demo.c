/* SPDX-License-Identifier: Zlib */
#include <errno.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "demo.h"

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#define POS_SIZE (3 * sizeof(float))

/*
 * The column is dense: slot i < n holds the position of the entity ids[i].
 * An ID finds its slot through slots[], and gens[] holds its generation.
 *
 * Past the live entities, ids[n] to ids[nids - 1] hold the free IDs, each
 * with its index in slots[] too. Destroying an entity swaps its ID with the
 * last live one, so it becomes the first free ID, and demo_create() takes
 * the first free ID before it makes a new one.
 */
struct demo {
	float (*pos)[3];
	uint32_t *ids;
	uint32_t *slots;
	uint32_t *gens;
	uint32_t n;
	uint32_t nids;
	uint32_t cap;
};

/*
 * A node value holds the node kind in the low half of w[0], the entity's
 * generation in the high half, and the entity ID in w[1]. w[2] stays zero.
 * The root, all zeros, has kind NODE_ROOT. The nodes outside entities/ belong
 * to no entity, so their ID and generation stay zero.
 */
enum node_kind {
	NODE_ROOT,
	NODE_COMPONENTS,
	NODE_TRANSFORM,
	NODE_POS,
	NODE_IDS,
	NODE_ENTITIES,
	/* The nodes from here on belong to an entity. */
	NODE_ENTITY,
	NODE_ENTITY_COMPONENTS,
	NODE_ENTITY_TRANSFORM,
	NODE_ENTITY_POS,
};

static enum node_kind node_kind(const struct nine_node *node)
{
	return (enum node_kind)(node->w[0] & 0xffffffff);
}

static uint32_t node_gen(const struct nine_node *node)
{
	return (uint32_t)(node->w[0] >> 32);
}

static uint32_t node_id(const struct nine_node *node)
{
	return (uint32_t)node->w[1];
}

static void make_node(struct nine_node *node, enum node_kind kind, uint32_t id,
		      uint32_t gen)
{
	node->w[0] = (uint64_t)gen << 32 | (uint64_t)kind;
	node->w[1] = id;
	node->w[2] = 0;
}

struct demo *demo_new(void)
{
	return calloc(1, sizeof(struct demo));
}

void demo_free(struct demo *d)
{
	if (!d)
		return;

	free(d->pos);
	free(d->ids);
	free(d->slots);
	free(d->gens);
	free(d);
}

/*
 * Grow every array by reallocation, so the column's memory really moves. An
 * array that grew before a failure keeps its new size, which does no harm.
 */
static int grow(struct demo *d)
{
	uint32_t cap = d->cap ? 2 * d->cap : 4;
	float (*pos)[3];
	uint32_t *ids, *slots, *gens;

	pos = realloc(d->pos, cap * sizeof(*pos));
	if (!pos)
		return -ENOMEM;
	d->pos = pos;

	ids = realloc(d->ids, cap * sizeof(*ids));
	if (!ids)
		return -ENOMEM;
	d->ids = ids;

	slots = realloc(d->slots, cap * sizeof(*slots));
	if (!slots)
		return -ENOMEM;
	d->slots = slots;

	gens = realloc(d->gens, cap * sizeof(*gens));
	if (!gens)
		return -ENOMEM;
	d->gens = gens;

	d->cap = cap;
	return 0;
}

int demo_create(struct demo *d, uint32_t *id)
{
	int err;

	if (d->n == d->cap) {
		err = grow(d);
		if (err)
			return err;
	}

	if (d->n == d->nids) {
		d->gens[d->nids] = 0;
		d->slots[d->nids] = d->n;
		d->ids[d->n] = d->nids++;
	}
	*id = d->ids[d->n];
	memset(d->pos[d->n], 0, POS_SIZE);
	d->n++;
	return 0;
}

static bool is_live(const struct demo *d, uint32_t id)
{
	return id < d->nids && d->slots[id] < d->n;
}

/* Swap the entities or free IDs at index @i and @j of ids[]. */
static void swap_ids(struct demo *d, uint32_t i, uint32_t j)
{
	uint32_t id = d->ids[i];

	d->ids[i] = d->ids[j];
	d->ids[j] = id;
	d->slots[d->ids[i]] = i;
	d->slots[d->ids[j]] = j;
}

int demo_destroy(struct demo *d, uint32_t id)
{
	uint32_t slot, last;

	if (!is_live(d, id))
		return -ENOENT;

	slot = d->slots[id];
	last = d->n - 1;
	memcpy(d->pos[slot], d->pos[last], POS_SIZE);
	swap_ids(d, slot, last);
	d->n--;
	d->gens[id]++;
	return 0;
}

int demo_get_pos(const struct demo *d, uint32_t id, float pos[3])
{
	if (!is_live(d, id))
		return -ENOENT;

	memcpy(pos, d->pos[d->slots[id]], POS_SIZE);
	return 0;
}

int demo_set_pos(struct demo *d, uint32_t id, const float pos[3])
{
	if (!is_live(d, id))
		return -ENOENT;

	memcpy(d->pos[d->slots[id]], pos, POS_SIZE);
	return 0;
}

void demo_get_column(struct demo *d, struct demo_column *col)
{
	col->pos = d->pos;
	col->slots = d->slots;
	col->n = d->n;
}

/* Parse a decimal ID in canonical form: digits only, no leading zero. */
static bool parse_id(const char *name, size_t len, uint32_t *id)
{
	uint64_t v = 0;
	size_t i;

	if (len == 0 || len > 10 || (len > 1 && name[0] == '0'))
		return false;

	for (i = 0; i < len; i++) {
		if (name[i] < '0' || name[i] > '9')
			return false;
		v = v * 10 + (uint64_t)(name[i] - '0');
	}
	if (v > UINT32_MAX)
		return false;

	*id = (uint32_t)v;
	return true;
}

static bool name_is(const char *name, size_t len, const char *want)
{
	return strlen(want) == len && memcmp(name, want, len) == 0;
}

/* The directories with fixed names, and the node each name leads to. */
static const struct {
	enum node_kind dir;
	const char *name;
	enum node_kind kind;
} fixed_names[] = {
	{ NODE_ROOT, "components", NODE_COMPONENTS },
	{ NODE_COMPONENTS, "transform", NODE_TRANSFORM },
	{ NODE_TRANSFORM, "pos", NODE_POS },
	{ NODE_TRANSFORM, "ids", NODE_IDS },
	{ NODE_ROOT, "entities", NODE_ENTITIES },
	{ NODE_ENTITY, "components", NODE_ENTITY_COMPONENTS },
	{ NODE_ENTITY_COMPONENTS, "transform", NODE_ENTITY_TRANSFORM },
	{ NODE_ENTITY_TRANSFORM, "pos", NODE_ENTITY_POS },
};

/*
 * Tell whether @node belongs to an entity destroyed since the node was made.
 * Its ID may be live again, as a new entity with a later generation.
 */
static bool is_stale(const struct demo *d, const struct nine_node *node)
{
	return node_kind(node) >= NODE_ENTITY &&
	       node_gen(node) != d->gens[node_id(node)];
}

static bool is_dir(enum node_kind kind)
{
	return kind != NODE_POS && kind != NODE_IDS && kind != NODE_ENTITY_POS;
}

static int lookup_entity(const struct demo *d, const char *name, size_t len,
			 struct nine_node *out)
{
	uint32_t id;

	if (!parse_id(name, len, &id) || !is_live(d, id))
		return -ENOENT;

	make_node(out, NODE_ENTITY, id, d->gens[id]);
	return 0;
}

static int demo_lookup(void *priv, const struct nine_node *dir,
		       const char *name, size_t len, struct nine_node *out)
{
	const struct demo *d = priv;
	enum node_kind kind = node_kind(dir);
	size_t i;

	if (is_stale(d, dir))
		return -ESTALE;
	if (!is_dir(kind))
		return -ENOTDIR;
	if (kind == NODE_ENTITIES)
		return lookup_entity(d, name, len, out);

	for (i = 0; i < ARRAY_SIZE(fixed_names); i++) {
		if (fixed_names[i].dir == kind &&
		    name_is(name, len, fixed_names[i].name)) {
			make_node(out, fixed_names[i].kind, node_id(dir),
				  node_gen(dir));
			return 0;
		}
	}
	return -ENOENT;
}

/*
 * Put @name and its NUL in @buf of @len bytes, after the *@used bytes there.
 * Return: true, or false when it does not fit.
 */
static bool put_name(char *buf, size_t len, size_t *used, const char *name)
{
	size_t size = strlen(name) + 1;

	if (size > len - *used)
		return false;

	memcpy(buf + *used, name, size);
	*used += size;
	return true;
}

/* The cursor of a directory with fixed names is its next index in the table. */
static int list_fixed(enum node_kind dir, uint64_t *cursor, char *buf,
		      size_t len)
{
	size_t used = 0;
	uint64_t i;
	int n = 0;

	for (i = *cursor; i < ARRAY_SIZE(fixed_names); i++) {
		if (fixed_names[i].dir != dir)
			continue;
		if (!put_name(buf, len, &used, fixed_names[i].name))
			break;
		n++;
	}
	if (!n && i < ARRAY_SIZE(fixed_names))
		return -ERANGE;

	*cursor = i;
	return n;
}

/* The cursor of entities/ is the next ID to list, so the IDs come in order. */
static int list_entities(const struct demo *d, uint64_t *cursor, char *buf,
			 size_t len)
{
	char name[sizeof("4294967295")];
	size_t used = 0;
	uint64_t id;
	int n = 0;

	for (id = *cursor; id < d->nids; id++) {
		if (!is_live(d, (uint32_t)id))
			continue;
		snprintf(name, sizeof(name), "%" PRIu64, id);
		if (!put_name(buf, len, &used, name))
			break;
		n++;
	}
	if (!n && id < d->nids)
		return -ERANGE;

	*cursor = id;
	return n;
}

static int demo_list(void *priv, const struct nine_node *dir, uint64_t *cursor,
		     char *buf, size_t len)
{
	enum node_kind kind = node_kind(dir);

	if (is_stale(priv, dir))
		return -ESTALE;
	if (!is_dir(kind))
		return -ENOTDIR;
	if (kind == NODE_ENTITIES)
		return list_entities(priv, cursor, buf, len);

	return list_fixed(kind, cursor, buf, len);
}

/*
 * Find the memory of the file at @node and its size in bytes. The bulk files
 * are the whole column, and a value file is one entity's slot in it.
 *
 * Return: true, or false when @node is a directory.
 */
static bool file_data(const struct demo *d, const struct nine_node *node,
		      void **data, size_t *size)
{
	switch (node_kind(node)) {
	case NODE_POS:
		*data = d->pos;
		*size = d->n * POS_SIZE;
		return true;
	case NODE_IDS:
		*data = d->ids;
		*size = d->n * sizeof(*d->ids);
		return true;
	case NODE_ENTITY_POS:
		*data = d->pos[d->slots[node_id(node)]];
		*size = POS_SIZE;
		return true;
	default:
		return false;
	}
}

/* Copy the part of a file of @size bytes at @src that a read asks for. */
static ptrdiff_t read_range(const void *src, size_t size, void *buf, size_t len,
			    uint64_t off)
{
	if (off >= size)
		return 0;
	if (len > size - off)
		len = size - (size_t)off;

	memcpy(buf, (const char *)src + off, len);
	return (ptrdiff_t)len;
}

/*
 * Most reads take an entity's whole position. They copy it at a fixed size,
 * which the compiler inlines, so the common read stays short. Every other read
 * takes the general path.
 */
static ptrdiff_t demo_read(void *priv, const struct nine_node *node, void *buf,
			   size_t len, uint64_t off)
{
	const struct demo *d = priv;
	void *data;
	size_t size;

	if (is_stale(d, node))
		return -ESTALE;
	if (node_kind(node) == NODE_ENTITY_POS && off == 0 && len >= POS_SIZE) {
		memcpy(buf, d->pos[d->slots[node_id(node)]], POS_SIZE);
		return (ptrdiff_t)POS_SIZE;
	}
	if (!file_data(d, node, &data, &size))
		return -EISDIR;

	return read_range(data, size, buf, len, off);
}

/*
 * Copy a write into the part of a file of @size bytes at @dst, or refuse it
 * when its range does not fit in the file. An empty file may have no memory,
 * so an empty write copies nothing.
 */
static int write_range(void *dst, size_t size, const void *buf, size_t len,
		       uint64_t off)
{
	if (off > size || len > size - off)
		return -EINVAL;

	if (len)
		memcpy((char *)dst + off, buf, len);
	return 0;
}

static int demo_write(void *priv, const struct nine_node *node, const void *buf,
		      size_t len, uint64_t off)
{
	void *data;
	size_t size;

	if (is_stale(priv, node))
		return -ESTALE;
	if (node_kind(node) == NODE_IDS)
		return -EACCES;
	if (!file_data(priv, node, &data, &size))
		return -EISDIR;

	return write_range(data, size, buf, len, off);
}

static int demo_stat(void *priv, const struct nine_node *node,
		     struct nine_stat *st)
{
	void *data;
	size_t size;

	if (is_stale(priv, node))
		return -ESTALE;

	if (file_data(priv, node, &data, &size)) {
		st->kind = NINE_FILE;
		st->size = size;
	} else {
		st->kind = NINE_DIR;
	}
	return 0;
}

/* Copy the whole file at @node, which must be @size bytes long, to @buf. */
static int read_value(const struct demo *d, const struct nine_node *node,
		      void *buf, size_t size)
{
	void *data;
	size_t file_size;

	if (is_stale(d, node))
		return -ESTALE;
	if (!file_data(d, node, &data, &file_size))
		return -EISDIR;
	if (file_size != size)
		return -EINVAL;

	if (size)
		memcpy(buf, data, size);
	return 0;
}

/*
 * A batch of positions copies each at a fixed size, which the compiler
 * inlines, so it makes no call for each value. Every other value, and a
 * stale node, takes read_value().
 */
static int demo_read_many(void *priv, const struct nine_handle *hs, size_t n,
			  void *buf, size_t size, int *errs)
{
	const struct demo *d = priv;
	const struct nine_node *node;
	char *p = buf;
	size_t i;
	int ret = 0;

	for (i = 0; i < n; i++) {
		node = nine_handle_node(&hs[i]);
		if (size == POS_SIZE && node_kind(node) == NODE_ENTITY_POS &&
		    !is_stale(d, node)) {
			memcpy(p + i * size, d->pos[d->slots[node_id(node)]],
			       POS_SIZE);
			errs[i] = 0;
			continue;
		}

		errs[i] = read_value(d, node, p + i * size, size);
		if (!ret)
			ret = errs[i];
	}
	return ret;
}

static const struct nine_file_server_ops ops = {
	.lookup = demo_lookup,
	.list = demo_list,
	.stat = demo_stat,
	.read = demo_read,
	.write = demo_write,
	.read_many = demo_read_many,
};

const struct nine_file_server_ops *demo_ops(void)
{
	return &ops;
}

/*
 * The read of the second variant. It is written apart from demo_read(), and
 * shares only the stale check and the node accessors with it, so the two
 * variants run different read code. Like demo_read(), it copies a whole
 * position at a fixed size.
 */
static ptrdiff_t variant_read(void *priv, const struct nine_node *node,
			      void *buf, size_t len, uint64_t off)
{
	const struct demo *d = priv;
	const char *src;
	size_t size;

	if (is_stale(d, node))
		return -ESTALE;

	switch (node_kind(node)) {
	case NODE_ENTITY_POS:
		src = (const char *)d->pos[d->slots[node_id(node)]];
		if (!off && len >= POS_SIZE) {
			memcpy(buf, src, POS_SIZE);
			return (ptrdiff_t)POS_SIZE;
		}
		size = POS_SIZE;
		break;
	case NODE_POS:
		src = (const char *)d->pos;
		size = d->n * POS_SIZE;
		break;
	case NODE_IDS:
		src = (const char *)d->ids;
		size = d->n * sizeof(*d->ids);
		break;
	default:
		return -EISDIR;
	}

	if (off >= size)
		return 0;
	size -= (size_t)off;
	if (size > len)
		size = len;
	memcpy(buf, src + off, size);
	return (ptrdiff_t)size;
}

static const struct nine_file_server_ops variant_ops = {
	.lookup = demo_lookup,
	.list = demo_list,
	.stat = demo_stat,
	.read = variant_read,
	.write = demo_write,
	.read_many = demo_read_many,
};

const struct nine_file_server_ops *demo_variant_ops(void)
{
	return &variant_ops;
}
