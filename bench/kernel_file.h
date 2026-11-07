/* SPDX-License-Identifier: Zlib */
#ifndef KERNEL_FILE_H
#define KERNEL_FILE_H

/*
 * The file of the benchmark's kernel file row: a small file in the
 * temporary directory, read with the POSIX calls open(), read() and
 * close(). These calls exist on Linux only. Elsewhere, kernel_file_make()
 * returns -ENOTSUP and the row is skipped.
 */

#include <stddef.h>

struct kernel_file {
	char path[256];
};

/*
 * kernel_file_make() - Make a new file holding the @len bytes at @data.
 *
 * Return: 0, -ENOTSUP on a system without the POSIX calls, or another
 * negative error code.
 */
int kernel_file_make(struct kernel_file *kf, const void *data, size_t len);

/*
 * kernel_file_read() - Open the file, read up to @len bytes from its start
 * into @buf, and close it again.
 *
 * Return: The number of bytes read, or a negative error code.
 */
ptrdiff_t kernel_file_read(const struct kernel_file *kf, void *buf, size_t len);

/* kernel_file_remove() - Remove the file that kernel_file_make() made. */
void kernel_file_remove(const struct kernel_file *kf);

#endif /* KERNEL_FILE_H */
