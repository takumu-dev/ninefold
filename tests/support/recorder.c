/* SPDX-License-Identifier: Zlib */
#include <string.h>

#include "check.h"
#include "recorder.h"

static void record(struct recorder *r, enum recorder_op op,
		   const struct nine_node *node, const char *name, size_t len)
{
	struct recorder_call *c;

	CHECK_UINT(r->ncalls, <, RECORDER_MAX_CALLS);
	c = &r->calls[r->ncalls++];
	c->op = op;
	c->node = *node;
	c->name = name;
	c->len = len;
}

static int rec_lookup(void *priv, const struct nine_node *dir, const char *name,
		      size_t len, struct nine_node *out)
{
	record(priv, RECORDER_LOOKUP, dir, name, len);
	*out = *dir;
	out->w[0]++;
	return 0;
}

static int rec_list(void *priv, const struct nine_node *dir, uint64_t *cursor,
		    char *buf, size_t len)
{
	(void)cursor;
	(void)buf;
	(void)len;
	record(priv, RECORDER_LIST, dir, NULL, 0);
	return 0;
}

static int rec_stat(void *priv, const struct nine_node *node,
		    struct nine_stat *st)
{
	(void)st;
	record(priv, RECORDER_STAT, node, NULL, 0);
	return 0;
}

static ptrdiff_t rec_read(void *priv, const struct nine_node *node, void *buf,
			  size_t len, uint64_t off)
{
	(void)buf;
	(void)len;
	(void)off;
	record(priv, RECORDER_READ, node, NULL, 0);
	return 0;
}

static int rec_write(void *priv, const struct nine_node *node, const void *buf,
		     size_t len, uint64_t off)
{
	(void)buf;
	(void)len;
	(void)off;
	record(priv, RECORDER_WRITE, node, NULL, 0);
	return 0;
}

static int rec_read_many(void *priv, const struct nine_handle *hs, size_t n,
			 void *buf, size_t size, int *errs)
{
	size_t i;

	(void)buf;
	(void)size;
	record(priv, RECORDER_READ_MANY, nine_handle_node(hs), NULL, n);
	for (i = 0; i < n; i++)
		errs[i] = 0;
	return 0;
}

void recorder_init(struct recorder *r)
{
	memset(r, 0, sizeof(*r));
	r->ops.lookup = rec_lookup;
	r->ops.list = rec_list;
	r->ops.stat = rec_stat;
	r->ops.read = rec_read;
	r->ops.write = rec_write;
	r->ops.read_many = rec_read_many;
}
