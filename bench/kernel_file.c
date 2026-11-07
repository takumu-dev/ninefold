/* SPDX-License-Identifier: Zlib */
/*
 * The only benchmark file that uses POSIX. Strict C11 hides the POSIX names
 * in glibc, so this file asks for them.
 */
#define _POSIX_C_SOURCE 200809L

#include <errno.h>

#include "kernel_file.h"

#ifdef __linux__

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int kernel_file_make(struct kernel_file *kf, const void *data, size_t len)
{
	const char *dir = getenv("TMPDIR");
	ssize_t n;
	int fd, n_path;

	if (!dir || !*dir)
		dir = "/tmp";
	n_path = snprintf(kf->path, sizeof(kf->path),
			  "%s/ninefold-bench-XXXXXX", dir);
	if (n_path < 0 || (size_t)n_path >= sizeof(kf->path))
		return -ENAMETOOLONG;

	fd = mkstemp(kf->path);
	if (fd < 0)
		return -errno;

	n = write(fd, data, len);
	if (n < 0 || (size_t)n != len) {
		close(fd);
		unlink(kf->path);
		return n < 0 ? -errno : -EIO;
	}
	close(fd);
	return 0;
}

int kernel_file_read(const struct kernel_file *kf, void *buf, size_t len)
{
	ssize_t n;
	int fd;

	fd = open(kf->path, O_RDONLY);
	if (fd < 0)
		return -errno;

	n = read(fd, buf, len);
	if (n < 0)
		n = -errno;
	close(fd);
	return (int)n;
}

void kernel_file_remove(const struct kernel_file *kf)
{
	unlink(kf->path);
}

#else

int kernel_file_make(struct kernel_file *kf, const void *data, size_t len)
{
	(void)kf;
	(void)data;
	(void)len;
	return -ENOTSUP;
}

int kernel_file_read(const struct kernel_file *kf, void *buf, size_t len)
{
	(void)kf;
	(void)buf;
	(void)len;
	return -ENOTSUP;
}

void kernel_file_remove(const struct kernel_file *kf)
{
	(void)kf;
}

#endif
