/* SPDX-License-Identifier: Zlib */
#ifndef NINEFOLD_H
#define NINEFOLD_H

#include <errno.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Thread rules. nine_tree_new(), nine_tree_free() and nine_mount() are setup
 * calls: each needs the tree to itself. After setup, every other call takes
 * no lock and changes no shared state of the framework, so any number of
 * threads can make them on one tree at the same time. Calls into one file
 * server follow the overlap rule of struct nine_file_server_ops.
 */

/**
 * define NINE_RDONLY - Access mode of nine_open(): open for reading only.
 */
#define NINE_RDONLY 1

/**
 * define NINE_WRONLY - Access mode of nine_open(): open for writing only.
 */
#define NINE_WRONLY 2

/**
 * define NINE_RDWR - Access mode of nine_open(): open for reading and writing,
 * NINE_RDONLY and NINE_WRONLY together.
 */
#define NINE_RDWR 3

/**
 * define ESTALE - Error code of a file server for a handle to a node that no
 * longer exists, such as a deleted object's.
 *
 * Not every C library has the name: the Windows one has none. There it takes
 * the value of Winsock's WSAESTALE, which no errno value uses.
 */
#ifndef ESTALE
#define ESTALE 10070
#endif

struct nine_tree;
struct nine_ns;
struct nine_mount;

/**
 * enum nine_kind - The kind of a node.
 * @NINE_DIR: A directory, which holds other nodes.
 * @NINE_FILE: A file, which holds bytes: a value file or a bulk file.
 */
enum nine_kind {
	NINE_DIR = 1,
	NINE_FILE = 2,
};

/**
 * struct nine_node - A node value, as a file server makes it.
 * @w: The content. Only the file server that made the value reads it.
 *
 * A file server puts in the node value everything it needs to find the node
 * again, such as the node's kind, an object ID and a generation. The value of
 * the root of a mount is all zeros.
 */
struct nine_node {
	uint64_t w[3];
};

/**
 * struct nine_stat - What nine_stat() tells about a node.
 * @kind: The kind of the node.
 * @size: The size of a file in bytes. A directory has size 0.
 *
 * Later fields are added at the end only. The framework zeroes the struct
 * before it calls a file server, so a field the file server does not know
 * reads as zero.
 */
struct nine_stat {
	enum nine_kind kind;
	uint64_t size;
};

/**
 * struct nine_handle - A reference to one node, from nine_open().
 *
 * A handle is a plain value of 40 bytes. The caller keeps it anywhere,
 * copies it or discards it: it holds no resources, so nothing closes it.
 * Its fields are private. File server code reads the node value through
 * nine_handle_node(). A handle that is all zeros is not valid, and every call
 * through it returns -EBADF.
 */
struct nine_handle {
	/* private: */
	const struct nine_mount *mount;
	struct nine_node node;
	int mode;
};

/**
 * struct nine_file_server_ops - The operations of a file server.
 * @lookup: Find the node called @name in the directory @dir, and write its
 *	node value to @out. @name points into the caller's path: it is @len
 *	bytes long, with no NUL at its end. Return 0 or a negative error code.
 *	Required.
 * @list: List the directory @dir from *@cursor: put the names that fit
 *	into @buf, each with a NUL at its end, and return their number, 0 at
 *	the end, or -ERANGE when the next name does not fit. Required.
 * @stat: Describe @node in @st, which the framework has zeroed: its kind,
 *	and its size, 0 for a directory. Required.
 * @read: Read from @node, as nine_read() does. Required.
 * @write: Write all @len bytes of @buf to @node at @off, or none, and return
 *	0 or a negative error code. A write never changes the size of a file.
 *	Optional: when NULL, every write returns -EACCES.
 * @read_many: Read exactly @size bytes for each of the @n handles @hs into
 *	@buf at i * @size, with the result of each in @errs[i], and return 0
 *	or the first error in @errs, as nine_read_many() does. The handles
 *	are all of this file server and open for reading. Optional: when NULL,
 *	the framework calls @read for each handle.
 *
 * Every operation gets the private pointer given to nine_mount() as @priv.
 * The framework never shows a file server the access mode of a handle.
 *
 * Context: Calls to one file server can come from many threads at the same
 * time. A file server must allow a read at the same time as other reads. The
 * app must prevent a write at the same time as any other call that uses the
 * same data, and a native change to the structure of the data, such as a
 * create or a destroy, at the same time as any call to the file server.
 */
struct nine_file_server_ops {
	int (*lookup)(void *priv, const struct nine_node *dir, const char *name,
		      size_t len, struct nine_node *out);
	int (*list)(void *priv, const struct nine_node *dir, uint64_t *cursor,
		    char *buf, size_t len);
	int (*stat)(void *priv, const struct nine_node *node,
		    struct nine_stat *st);
	ptrdiff_t (*read)(void *priv, const struct nine_node *node, void *buf,
			  size_t len, uint64_t off);
	int (*write)(void *priv, const struct nine_node *node, const void *buf,
		     size_t len, uint64_t off);
	int (*read_many)(void *priv, const struct nine_handle *hs, size_t n,
			 void *buf, size_t size, int *errs);
};

/**
 * nine_tree_new() - Make a new, empty tree.
 *
 * Context: Any thread. Trees share no state, so each tree can be made and
 * used on its own thread.
 *
 * Return: The new tree, or NULL when out of memory.
 */
struct nine_tree *nine_tree_new(void);

/**
 * nine_tree_free() - Free a tree and everything it allocated.
 * @t: The tree to free, or NULL.
 *
 * The file servers mounted in the tree stay the app's to free. Handles of the
 * tree must not be used after this call.
 *
 * Context: No other call may use the tree or its handles at the same time.
 */
void nine_tree_free(struct nine_tree *t);

/**
 * nine_tree_ns() - Get the root namespace of a tree.
 * @t: The tree.
 *
 * The root namespace is part of the tree. It stays valid until
 * nine_tree_free(), and the caller does not free it.
 *
 * Context: Any thread, at the same time as any call except nine_tree_free().
 *
 * Return: The root namespace. This call cannot fail.
 */
struct nine_ns *nine_tree_ns(struct nine_tree *t);

/**
 * nine_mount() - Attach a file server to a tree at a path.
 * @t: The tree.
 * @path: The absolute path to mount at. The tree keeps its own copy.
 * @ops: The operations of the file server.
 * @priv: The private pointer that each operation gets.
 *
 * The tree does not copy @ops or @priv. Both must stay valid until
 * nine_tree_free() returns.
 *
 * Mounts do not nest: no mount path is equal to, above or below another, so
 * a mount at "/" is the only mount of its tree. The framework serves the
 * directories above the mount points. Each lists the next name of the mount
 * paths below it.
 *
 * Context: No other call may use the tree at the same time.
 *
 * Return: 0, -EINVAL when @path is not a valid absolute path or a required
 * operation is NULL, -EBUSY when @path is equal to, above or below the path
 * of a mount of the tree, or -ENOMEM.
 */
int nine_mount(struct nine_tree *t, const char *path,
	       const struct nine_file_server_ops *ops, void *priv);

/**
 * nine_open() - Find the node at a path and make a handle to it.
 * @ns: The namespace to find the path in.
 * @path: An absolute path. Repeated slashes and a slash at the end have no
 *	effect. A "." or ".." component is not allowed.
 * @mode: The access mode: NINE_RDONLY, NINE_WRONLY or NINE_RDWR.
 * @h: Gets the handle. On failure, it is set to all zeros.
 *
 * Each name in the path goes to the file server's lookup as a pointer into
 * @path and a length, with no copy, so names have no length limit.
 *
 * Context: Any thread, at the same time as any call except a setup call on
 * the same tree.
 *
 * Return: 0, -EINVAL for a bad path or mode, or an error from the file
 * server, such as -ENOENT for a missing node or -ENOTDIR for a path through a
 * file.
 */
int nine_open(struct nine_ns *ns, const char *path, int mode,
	      struct nine_handle *h);

/**
 * nine_list() - List the names in a directory through a handle.
 * @dir: A handle to a directory, open for reading.
 * @cursor: Where to go on from. It starts at 0, and each call moves it past
 *	the names it returned. Only the file server knows what it means.
 * @buf: Gets the names that fit, each with a NUL at its end.
 * @len: The size of @buf in bytes.
 *
 * A loop of calls lists each name once, as long as the data does not change
 * in between. A native change to the structure of the data between two
 * calls can skip or repeat names. The list gives no kinds: nine_stat() does.
 *
 * Context: Any thread, at the same time as any call except a setup call on
 * the handle's tree.
 *
 * Return: The number of names put in @buf, 0 at the end of the directory,
 * -ERANGE when the next name does not fit in @buf, -EBADF for a handle that
 * is not valid or not open for reading, or an error from the file server,
 * such as -ENOTDIR for a file.
 */
int nine_list(const struct nine_handle *dir, uint64_t *cursor, char *buf,
	      size_t len);

/**
 * nine_stat() - Describe a node through a handle.
 * @h: A handle, open in any mode.
 * @st: Gets the kind of the node, and the size of a file in bytes.
 *
 * The framework zeroes @st before the file server fills it in.
 *
 * Context: Any thread, at the same time as any call except a setup call on
 * the handle's tree.
 *
 * Return: 0, -EBADF for a handle that is not valid, or an error from the
 * file server.
 */
int nine_stat(const struct nine_handle *h, struct nine_stat *st);

/**
 * nine_read() - Read from a file through a handle.
 * @h: A handle open for reading.
 * @buf: Gets the bytes.
 * @len: The number of bytes to read.
 * @off: The offset in the file to read from.
 *
 * The read is short only at the end of the file.
 *
 * Context: Any thread, at the same time as any call except a setup call on
 * the handle's tree.
 *
 * Return: The number of bytes read, 0 at or after the end of the file,
 * -EBADF for a handle that is not valid or not open for reading, or an error
 * from the file server, such as -EISDIR for a directory.
 */
ptrdiff_t nine_read(const struct nine_handle *h, void *buf, size_t len,
		    uint64_t off);

/**
 * nine_read_many() - Read the values of many files through their handles.
 * @hs: The handles, each open for reading.
 * @n: The number of handles.
 * @buf: Gets the value of the file of @hs[i] at @buf + i * @size.
 * @size: The size of each value in bytes.
 * @errs: Gets 0 or a negative error code for each handle.
 *
 * Each handle reads exactly @size bytes from the start of its file. A
 * handle that fails does not stop the others, and its part of @buf is left
 * as it was. The handles can belong to different file servers. Each run of
 * handles of one mount in a row is one call to its file server, so a batch
 * grouped by mount costs the fewest calls. The batch is not sorted.
 *
 * A file server with no read_many operation gets a read for each handle.
 * There, a handle whose file is shorter than @size fails with -EINVAL, and
 * its part of @buf may hold the bytes that were read.
 *
 * Context: Any thread, at the same time as any call except a setup call on
 * the handles' trees.
 *
 * Return: 0 when every handle succeeds, or else the error of the first one
 * that failed in @errs: -EBADF for a handle that is not valid or not open
 * for reading, or an error from the file server, such as -ESTALE.
 */
int nine_read_many(const struct nine_handle *hs, size_t n, void *buf,
		   size_t size, int *errs);

/**
 * nine_write() - Write to a file through a handle.
 * @h: A handle open for writing.
 * @buf: The bytes to write.
 * @len: The number of bytes to write.
 * @off: The offset in the file to write at.
 *
 * The write applies all @len bytes or none, and never changes the size of
 * the file. It takes effect before the call returns, so every later read
 * through any view of the node shows it.
 *
 * Context: Any thread, at the same time as any call except a setup call on
 * the handle's tree, or a call that uses the same data of the file server.
 *
 * Return: 0, -EBADF for a handle that is not valid or not open for writing,
 * -EACCES when the file server has no write operation, or an error from the
 * file server, such as -EISDIR for a directory or -EINVAL for a range past
 * the end of the file.
 */
int nine_write(const struct nine_handle *h, const void *buf, size_t len,
	       uint64_t off);

/**
 * nine_handle_node() - Get the node value of a handle.
 * @h: The handle.
 *
 * This is for file server code, such as a read_many operation that serves
 * the caller's array of handles.
 *
 * Context: Any thread.
 *
 * Return: The node value that the file server made for the handle's node.
 */
static inline const struct nine_node *
nine_handle_node(const struct nine_handle *h)
{
	return &h->node;
}

#ifdef __cplusplus
}
#endif

#endif /* NINEFOLD_H */
