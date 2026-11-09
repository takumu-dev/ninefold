/* SPDX-License-Identifier: Zlib */
#ifndef RECORDER_H
#define RECORDER_H

/*
 * A recording file server for the tests. It records each call into it, and
 * a test can set any of its operations to NULL before it mounts it.
 *
 * Every lookup succeeds, so any path opens. The node it makes is one deeper
 * than its directory: w[0] counts the names walked from the mount root. A
 * read_many writes no bytes and sets the result of every handle to 0.
 *
 * A test can set what the operations answer for every node: what stat
 * reports, the bytes a read gives, and an error for stat, read or write.
 * With none of these set, a stat writes nothing and a read returns 0 bytes.
 */

#include <stddef.h>
#include <stdint.h>

#include "ninefold.h"

#define RECORDER_MAX_CALLS 64
#define RECORDER_VALUE_MAX 32

enum recorder_op {
	RECORDER_LOOKUP,
	RECORDER_LIST,
	RECORDER_STAT,
	RECORDER_READ,
	RECORDER_WRITE,
	RECORDER_READ_MANY,
};

/*
 * struct recorder_call - One call into the file server.
 * @op: The operation called.
 * @node: The node it was called on.
 * @name: For a lookup, the name pointer it got.
 * @len: For a lookup, the name length it got. For a read or a write, the
 *	number of bytes it was asked for. For a read_many, the number of
 *	handles it got, the first of them in @node.
 * @off: For a read or a write, the offset it got.
 */
struct recorder_call {
	enum recorder_op op;
	struct nine_node node;
	const char *name;
	size_t len;
	uint64_t off;
};

/*
 * struct recorder - A recording file server. Mount &r->ops with r as priv.
 * @ops: Its operations, which the test may change before the mount.
 * @calls: The calls so far, in order.
 * @ncalls: The number of calls so far.
 * @st: What a stat reports. A zero kind makes a stat write nothing.
 * @value: The stored bytes. A write that fits in them changes them.
 * @value_len: How many of @value a read can give, so that a read past it is
 *	short.
 * @stat_err: When not 0, what a stat returns.
 * @read_err: When not 0, what a read returns.
 * @write_err: When not 0, what a write returns. It then stores nothing.
 */
struct recorder {
	struct nine_file_server_ops ops;
	struct recorder_call calls[RECORDER_MAX_CALLS];
	size_t ncalls;
	struct nine_stat st;
	unsigned char value[RECORDER_VALUE_MAX];
	size_t value_len;
	int stat_err;
	ptrdiff_t read_err;
	int write_err;
};

/* recorder_init() - Set up a recorder with every operation and no calls. */
void recorder_init(struct recorder *r);

#endif /* RECORDER_H */
