/* SPDX-License-Identifier: Zlib */
#include <errno.h>
#include <string.h>

#include "internal.h"

/* A handle of all zeros has mode 0, so it allows nothing. */
static bool is_valid(const struct nine_handle *h)
{
	return h->mode != 0;
}

static bool may_read(const struct nine_handle *h)
{
	return h->mode & NINE_RDONLY;
}

static bool may_write(const struct nine_handle *h)
{
	return h->mode & NINE_WRONLY;
}

int nine_list(const struct nine_handle *dir, uint64_t *cursor, char *buf,
	      size_t len)
{
	const struct nine_mount *m = dir->mount;

	if (!may_read(dir))
		return -EBADF;

	return m->ops->list(m->priv, &dir->node, cursor, buf, len);
}

int nine_stat(const struct nine_handle *h, struct nine_stat *st)
{
	const struct nine_mount *m = h->mount;

	if (!is_valid(h))
		return -EBADF;

	memset(st, 0, sizeof(*st));
	return m->ops->stat(m->priv, &h->node, st);
}

ptrdiff_t nine_read(const struct nine_handle *h, void *buf, size_t len,
		    uint64_t off)
{
	const struct nine_mount *m = h->mount;

	if (!may_read(h))
		return -EBADF;

	return m->ops->read(m->priv, &h->node, buf, len, off);
}

int nine_write(const struct nine_handle *h, const void *buf, size_t len,
	       uint64_t off)
{
	const struct nine_mount *m = h->mount;

	if (!may_write(h))
		return -EBADF;
	if (!m->ops->write)
		return -EACCES;

	return m->ops->write(m->priv, &h->node, buf, len, off);
}
