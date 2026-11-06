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

/*
 * Read one value for a file server with no read_many operation. The stat
 * refuses a file of another size before the read can write to @buf, and
 * leaves a directory to fail in the read with the file server's error.
 */
static int read_one(const struct nine_handle *h, void *buf, size_t size)
{
	const struct nine_mount *m = h->mount;
	struct nine_stat st;
	ptrdiff_t ret;
	int err;

	err = nine_stat(h, &st);
	if (err)
		return err;
	if (st.kind == NINE_FILE && st.size != size)
		return -EINVAL;

	ret = m->ops->read(m->priv, &h->node, buf, size, 0);
	if (ret < 0)
		return (int)ret;

	return (size_t)ret == size ? 0 : -EINVAL;
}

/*
 * Read a run of @n handles, all open for reading and of one mount, in one
 * call to its file server, or in one read for each handle when it has no
 * read_many operation.
 */
static void read_run(const struct nine_handle *hs, size_t n, char *buf,
		     size_t size, int *errs)
{
	const struct nine_mount *m = hs->mount;
	size_t i;

	if (m->ops->read_many) {
		m->ops->read_many(m->priv, hs, n, buf, size, errs);
		return;
	}

	for (i = 0; i < n; i++)
		errs[i] = read_one(&hs[i], buf + i * size, size);
}

/*
 * Count the handles from @hs on that belong to the run of @hs[0], which the
 * caller has found open for reading: those open for reading and of its
 * mount, in a row.
 */
static size_t run_length(const struct nine_handle *hs, size_t n)
{
	size_t i;

	for (i = 1; i < n; i++) {
		if (!may_read(&hs[i]) || hs[i].mount != hs->mount)
			break;
	}
	return i;
}

/*
 * Find the first error of @n results. The results of a file server's
 * read_many decide, not its return value.
 */
static int first_error(const int *errs, size_t n)
{
	size_t i;

	for (i = 0; i < n; i++) {
		if (errs[i])
			return errs[i];
	}
	return 0;
}

int nine_read_many(const struct nine_handle *hs, size_t n, void *buf,
		   size_t size, int *errs)
{
	char *p = buf;
	size_t i, len;
	int ret = 0;

	for (i = 0; i < n; i += len) {
		if (may_read(&hs[i])) {
			len = run_length(hs + i, n - i);
			read_run(hs + i, len, p + i * size, size, errs + i);
		} else {
			len = 1;
			errs[i] = -EBADF;
		}
		if (!ret)
			ret = first_error(errs + i, len);
	}
	return ret;
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
