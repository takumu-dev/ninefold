/* SPDX-License-Identifier: Zlib */
#ifndef LISTING_H
#define LISTING_H

#include <stddef.h>

#include "ninefold.h"

/* The largest buffer list_names() can list with. */
#define LISTING_MAX_BUF 64

/*
 * list_names() - List a directory from cursor 0 to the end, with a buffer
 * of @len bytes for each nine_list() call, and check that it ends with 0.
 *
 * Writes the names to @out of @size bytes, each followed by a space.
 * Return: @out.
 */
const char *list_names(const struct nine_handle *dir, size_t len, char *out,
		       size_t size);

#endif /* LISTING_H */
