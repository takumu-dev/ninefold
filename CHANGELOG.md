# Changelog

Each release of Ninefold is a tagged version on `main`. There is no API or
ABI stability before 1.0, so any 0.x release can change or remove anything.

## 0.1.0 - 2026-11-07

The first release: the core tree, as a static library with one public
header, `ninefold.h`.

### Added

- Trees. `nine_tree_new()` makes a tree and `nine_tree_free()` frees it,
  `nine_mount()` mounts a file server at a path, and `nine_tree_ns()` gives
  the tree's root namespace, which path calls take. The directories above
  the mounts show in the tree too.
- Handles. `nine_open()` turns a path into a handle, a plain value that
  holds no resources, with an access mode: `NINE_RDONLY`, `NINE_WRONLY` or
  `NINE_RDWR`. After that, `nine_list()`, `nine_stat()`, `nine_read()`,
  `nine_read_many()` and `nine_write()` work through the handle with no
  path lookup. A call through a handle that isn't open for it returns
  `-EBADF`.
- The file server interface, `struct nine_file_server_ops`: `lookup`,
  `list`, `stat`, `read`, and the optional `write` and `read_many`. A file
  server owns its data and finds a node again from the node value it made.
  A `read_many` operation gets each handle's node value with
  `nine_handle_node()`.
- Immediate writes. A write takes effect before `nine_write()` returns.
  When a file server has no `write` operation, every write returns
  `-EACCES`.
- `ESTALE` in the public header, for C libraries that lack it. A file
  server returns it for a handle to a node that no longer exists.
- Thread rules for every call. After setup, any number of threads can call
  into one tree at the same time, as long as the app keeps a write apart
  from every other call that uses the same data. Each tree is independent
  of the others.
- A demo file server, `ninefold_demo`, that keeps one dense column of
  `vec3 f32` positions with swap-remove and growth. It shows the column as
  bulk files, `pos` and `ids`, and as one value file per entity, made on
  demand. Both views reach the same memory, and a handle to a destroyed
  entity returns `-ESTALE`.
- A demo program, `ninefold_walk`, that walks the tree and prints it.
- A benchmark, `ninefold_bench`, that times handle, batched, path and bulk
  access against a plain C loop over the same data and `memcpy` of the
  same bytes. It also times handle reads that alternate between two file
  servers, and, on Linux, a kernel file read with open, read and close.
- A CMake build with presets for GCC, Clang and MSVC, sanitizers and lint,
  install rules with `find_package(ninefold)` support, and CI on Linux and
  Windows.
