/* SPDX-License-Identifier: Zlib */
#include <errno.h>

#include "internal.h"

/* A handle of all zeros has mode 0, so it allows nothing. */
static bool may_read(const struct nine_handle *h)
{
	return h->mode & NINE_RDONLY;
}

static bool may_write(const struct nine_handle *h)
{
	return h->mode & NINE_WRONLY;
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
