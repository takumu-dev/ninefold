/* SPDX-License-Identifier: Zlib */
#include <string.h>

#include "check.h"
#include "listing.h"

const char *list_names(const struct nine_handle *dir, size_t len, char *out,
		       size_t size)
{
	char buf[LISTING_MAX_BUF];
	uint64_t cursor = 0;
	size_t used = 0, namelen;
	const char *p;
	int n, i;

	CHECK_UINT(len, <=, sizeof(buf));
	CHECK_UINT(size, >, 0);
	out[0] = '\0';
	while ((n = nine_list(dir, &cursor, buf, len)) > 0) {
		for (i = 0, p = buf; i < n; i++, p += namelen + 1) {
			namelen = strlen(p);
			CHECK_UINT(used + namelen + 2, <=, size);
			memcpy(out + used, p, namelen);
			out[used + namelen] = ' ';
			used += namelen + 1;
			out[used] = '\0';
		}
	}
	CHECK_INT(n, ==, 0);
	return out;
}
