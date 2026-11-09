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
 * Thread rules. nine_tree_new(), nine_tree_free(), nine_mount(),
 * nine_add_schema() and nine_mount_types() are setup calls: each needs the
 * tree to itself. After setup, every other call takes no lock and changes no
 * shared state of the framework, so any number of threads can make them on
 * one tree at the same time. Calls into one file
 * server follow the overlap rule of struct nine_file_server_ops: the app
 * keeps a write apart from every other call that uses the same data, and a
 * native change to the structure of the data apart from every call to that
 * file server.
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
 * enum nine_elem - The element type of a field: the kind of number, or bool,
 * that a value is made of.
 * @NINE_I8: An int8_t.
 * @NINE_I16: An int16_t.
 * @NINE_I32: An int32_t.
 * @NINE_I64: An int64_t.
 * @NINE_U8: A uint8_t.
 * @NINE_U16: A uint16_t.
 * @NINE_U32: A uint32_t.
 * @NINE_U64: A uint64_t.
 * @NINE_F32: A 32-bit float.
 * @NINE_F64: A 64-bit float.
 * @NINE_BOOL: One byte: 0 is false, and any other value is true.
 */
enum nine_elem {
	NINE_I8 = 1,
	NINE_I16,
	NINE_I32,
	NINE_I64,
	NINE_U8,
	NINE_U16,
	NINE_U32,
	NINE_U64,
	NINE_F32,
	NINE_F64,
	NINE_BOOL,
};

/**
 * enum nine_shape - The arrangement of the elements of a value. The binary
 * form is the elements with no space between them.
 * @NINE_SCALAR: One element.
 * @NINE_VEC2: Two elements.
 * @NINE_VEC3: Three elements.
 * @NINE_VEC4: Four elements.
 * @NINE_QUAT: Four f32 or f64 elements, in the order x y z w.
 */
enum nine_shape {
	NINE_SCALAR,
	NINE_VEC2,
	NINE_VEC3,
	NINE_VEC4,
	NINE_QUAT,
};

/**
 * struct nine_enum_value - One name of an enum field.
 * @name: The name, which is the text form of the value.
 * @value: The value.
 */
struct nine_enum_value {
	const char *name;
	int64_t value;
};

/**
 * struct nine_field - One named value of a schema, and the type of the value
 * files and bulk files that show it.
 * @name: The name, one valid path component.
 * @elem: The element type.
 * @shape: The shape.
 * @unit: The unit, ASCII with no spaces, or NULL for none. The framework
 *	shows it and never converts it.
 * @range: The limits of each element, both included, or all zeros for no
 *	range. Use the member that matches @elem: @range.i for a signed
 *	integer, @range.u for an unsigned one and @range.f for a float. Only
 *	nine_write_text() checks it.
 * @values: The names of an enum, or NULL when the field is not one.
 * @nvalues: The number of names in @values.
 * @message: One line that says what a good value is, or NULL for none.
 *
 * Later members are added at the end only, and a member that is zero is not
 * used.
 */
struct nine_field {
	const char *name;
	enum nine_elem elem;
	enum nine_shape shape;
	const char *unit;
	union {
		struct {
			int64_t min, max;
		} i;
		struct {
			uint64_t min, max;
		} u;
		struct {
			double min, max;
		} f;
	} range;
	const struct nine_enum_value *values;
	size_t nvalues;
	const char *message;
};

/**
 * struct nine_schema - The description of one object type or component type.
 * @name: The name, unique in the tree and one valid path component.
 * @fields: The fields.
 * @nfields: The number of fields.
 *
 * Later members are added at the end only, and a member that is zero is not
 * used.
 */
struct nine_schema {
	const char *name;
	const struct nine_field *fields;
	size_t nfields;
};

/**
 * define NINE_TYPE_BULK - Flag of struct nine_type: the file is the bulk file
 * of the column of the field, not a value file.
 */
#define NINE_TYPE_BULK 1

/**
 * struct nine_type - The type of a node.
 * @field: The field of a schema that the tree has, or NULL for no type.
 * @flags: NINE_TYPE_BULK or 0.
 *
 * A type of all zeros means no type: the node has no text form and works
 * as any untyped node. The type of a node never changes.
 */
struct nine_type {
	const struct nine_field *field;
	uint32_t flags;
};

/**
 * struct nine_stat - What nine_stat() tells about a node.
 * @kind: The kind of the node.
 * @size: The size of a file in bytes. A directory has size 0.
 * @type: The type of the node. A directory has no type.
 *
 * Later fields are added at the end only. The framework zeroes the struct
 * before it calls a file server, so a field the file server does not know
 * reads as zero.
 */
struct nine_stat {
	enum nine_kind kind;
	uint64_t size;
	struct nine_type type;
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
 *	its size, 0 for a directory, and its type, if it has one. The size of
 *	a typed value file is the binary size of its field, and the size of a
 *	typed bulk file a multiple of it. Required.
 * @read: Read from @node, as nine_read() does. Required.
 * @write: Write all @len bytes of @buf to @node at @off, or none, and return
 *	0 or a negative error code. A write never changes the size of a file.
 *	Optional: when NULL, every write returns -EACCES.
 * @read_many: Read exactly @size bytes for each of the @n handles @hs into
 *	@buf at i * @size, with the result of each in @errs[i], and return 0
 *	or the first error in @errs, as nine_read_many() does. A file whose
 *	size is not @size fails with -EINVAL. The handles are all of this
 *	file server and open for reading. Optional: when NULL, the framework
 *	calls @stat and @read for each handle.
 *
 * Every operation gets the private pointer given to nine_mount() as @priv.
 * The framework never shows a file server the access mode of a handle.
 *
 * Context: Calls to one file server can come from many threads at the same
 * time. A file server must allow a read at the same time as other reads. The
 * app must prevent a write at the same time as any other call that uses the
 * same data, and a native change to the structure of the data, such as a
 * create, a destroy, a swap-remove or growth, at the same time as any call
 * to the file server.
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
 * Context: Any thread, at the same time as any call except a setup call on
 * the same tree.
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
 * nine_add_schema() - Add a schema to a tree.
 * @t: The tree.
 * @s: The schema. The tree does not copy it: @s and all that it points to
 *	must stay valid until nine_tree_free() returns.
 *
 * Adding the same descriptor again has no effect, so two file servers can
 * show one component.
 *
 * Context: No other call may use the tree at the same time.
 *
 * Return: 0, -EINVAL for a descriptor that is not valid, -EEXIST when a
 * different descriptor has the same name in the tree, or -ENOMEM.
 */
int nine_add_schema(struct nine_tree *t, const struct nine_schema *s);

/**
 * nine_mount_types() - Mount the file server of the tree's schemas at a path.
 * @t: The tree.
 * @path: The absolute path to mount at, by convention "/types".
 *
 * Each schema shows as a directory, and each of its fields as one read-only
 * file that holds the line nine_field_text() gives.
 *
 * Context: No other call may use the tree at the same time.
 *
 * Return: 0, or an error as for nine_mount().
 */
int nine_mount_types(struct nine_tree *t, const char *path);

/**
 * nine_field_text() - Write the line that describes a field.
 * @f: The field.
 * @buf: Gets the line and its newline, with no NUL at the end.
 * @len: The size of @buf in bytes.
 *
 * Context: Any thread.
 *
 * Return: The number of bytes written, or -ERANGE when the line does not fit
 * in @buf. The contents of @buf are then not defined.
 */
ptrdiff_t nine_field_text(const struct nine_field *f, char *buf, size_t len);

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
 * the same tree, a write to data that a lookup uses, or a native change to
 * the structure of the data of a file server on the path.
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
 * the handle's tree, a write to the same data, or a native change to the
 * structure of the file server's data.
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
 * @st: Gets the kind of the node, the size of a file in bytes, and the type
 *	of the node.
 *
 * The framework zeroes @st before the file server fills it in.
 *
 * Context: Any thread, at the same time as any call except a setup call on
 * the handle's tree, a write to the same data, or a native change to the
 * structure of the file server's data.
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
 * the handle's tree, a write to the same data, or a native change to the
 * structure of the file server's data.
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
 * A file server with no read_many operation gets a stat and then a read for
 * each handle.
 *
 * Context: Any thread, at the same time as any call except a setup call on
 * the handles' trees, a write to the same data, or a native change to the
 * structure of the data of their file servers.
 *
 * Return: 0 when every handle succeeds, or else the error of the first one
 * that failed in @errs: -EBADF for a handle that is not valid or not open
 * for reading, -EINVAL for a file whose size is not @size, or an error from
 * the file server, such as -ESTALE.
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
 * the handle's tree, another call that uses the same data, or a native
 * change to the structure of the file server's data.
 *
 * Return: 0, -EBADF for a handle that is not valid or not open for writing,
 * -EACCES when the file server has no write operation, or an error from the
 * file server, such as -EISDIR for a directory or -EINVAL for a range past
 * the end of the file.
 */
int nine_write(const struct nine_handle *h, const void *buf, size_t len,
	       uint64_t off);

/**
 * define NINE_TEXT_MAX - A buffer of this many bytes holds the text form of
 * any value, with its newline.
 */
#define NINE_TEXT_MAX 512

/**
 * nine_read_text() - Read the text form of a value through a handle.
 * @h: A handle open for reading.
 * @buf: Gets the line and its newline, with no NUL at the end.
 * @len: The size of @buf in bytes. NINE_TEXT_MAX always fits.
 *
 * The call makes a stat and then a read of the file server. The range of the
 * field is not checked, so a tool can see a bad value that native code
 * stored.
 *
 * Context: Any thread, at the same time as any call except a setup call on
 * the handle's tree, a write to the same data, or a native change to the
 * structure of the file server's data. The call takes no lock and allocates
 * nothing.
 *
 * Return: The number of bytes written. Otherwise, the first of these that
 * applies: -EBADF for a handle that is not valid or not open for reading, an
 * error from the stat of the file server, -ENOTSUP for a node with no text
 * form, which is a file with no type, a bulk file or a directory, -EIO for a
 * file whose size is not the size of its field, an error from the read of
 * the file server, -EIO for a short read, or -ERANGE when the line does not
 * fit in @buf. After -ERANGE, the contents of @buf are not defined.
 */
ptrdiff_t nine_read_text(const struct nine_handle *h, char *buf, size_t len);

/**
 * nine_write_text() - Write the text form of a value through a handle.
 * @h: A handle open for writing.
 * @buf: The text. It needs no NUL at the end, and may end with one newline.
 * @len: The length of the text in bytes.
 *
 * The call makes a stat of the file server, checks the text against the
 * field, and gives the write of the file server the binary value, at offset
 * 0 and at the full size of the field. After -EINVAL, the caller gets the
 * message of the field from nine_stat(), through a handle in any mode.
 *
 * Context: Any thread, at the same time as any call except a setup call on
 * the handle's tree, another call that uses the same data, or a native change
 * to the structure of the file server's data. The call takes no lock and
 * allocates nothing.
 *
 * Return: 0. Otherwise, the first of these that applies: -EBADF for a handle
 * that is not valid or not open for writing, an error from the stat of the
 * file server, -ENOTSUP for a node with no text form, which is a file with no
 * type, a bulk file or a directory, -EACCES when the file server has no write
 * operation, -EIO for a file whose size is not the size of its field,
 * -EINVAL for a bad value, or an error from the write of the file server.
 */
int nine_write_text(const struct nine_handle *h, const char *buf, size_t len);

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
