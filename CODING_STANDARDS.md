# Coding standards

## Style

All C code follows the
[Linux kernel coding style](https://www.kernel.org/doc/html/latest/process/coding-style.html).
In short:

- Indent with tabs, 8 columns wide. Keep lines within 80 columns.
- Put the opening brace of a block on the same line, and the opening brace
  of a function on a line of its own.
- Leave out braces around a single statement, unless another branch of the
  same conditional needs them.
- Put a space after `if`, `switch`, `case`, `for`, `do` and `while`, but
  not after `sizeof`.
- Put the `*` next to the name: `char *p`.
- Use `lower_case_with_underscores`: descriptive names for global symbols,
  short ones for locals.
- No `typedef` for structs or pointers: write `struct nine_tree`. The
  exceptions are the ones the kernel style allows, such as a fully opaque
  type.
- Write constant macros and enum labels in capitals. Wrap a macro of more
  than one statement in `do { } while (0)`.
- Keep functions short, each doing one thing. When a function can fail at
  several points, clean up with `goto` to a shared exit label.
- Use `/* */` comments, saying what the code does and why, not how.

Ninefold is a userspace library, so the rules apply but the kernel's own
APIs don't: no `kmalloc`, `printk` or kernel types such as `u32`. The
public API uses `<stdint.h>` types.

## Language

- Strict C11 with no compiler extensions: `-std=c11` with extensions off on
  GCC and Clang, `/std:c11` on MSVC.
- ISO C only, except in the platform file `src/platform.c`, which holds all
  the code for any one operating system. It is the only library file that
  defines `_POSIX_C_SOURCE`. Strict C11 hides the POSIX names in glibc, so a
  POSIX call anywhere else fails to build on Linux too.
- The public header also compiles as C++11: it has `extern "C"` guards and
  no C-only syntax such as `restrict`.

## Conventions

- Public functions and structs start with `nine_`. Macros and enum labels
  start with `NINE_`.
- A function that performs an action returns 0 on success and a negative
  error code from `<errno.h>` on failure, such as `-EINVAL`. A predicate
  returns a boolean. Refer to error codes by name, never by number: the
  numbers differ between platforms.
- Every public call has a kernel-doc comment, with a `Context:` line that
  gives its thread rule:

  ```c
  /**
   * nine_tree_free() - Free a tree and everything it allocated.
   * @tree: The tree to free, or NULL.
   *
   * Context: No other call may use the tree or its handles at the same time.
   */
  ```

- Every `.c` and `.h` file starts with this line:

  ```c
  /* SPDX-License-Identifier: Zlib */
  ```

## Tools

`.clang-format` sets the layout, for clang-format 22.1.8 exactly; other
versions format some code differently. `.checkpatch.conf` holds the options
for the kernel's `checkpatch.pl`, which the build downloads at a pinned
kernel commit into the build directory. Any checkpatch warning is a
failure, and each `--ignore` in that file carries a comment giving its
reason.

From a configured build directory:

```sh
cmake --build build/<preset> --target format        # reformat every source file
cmake --build build/<preset> --target check-format  # list unformatted files
cmake --build build/<preset> --target checkpatch    # run checkpatch on every .c and .h file
```

To run both checks as CI does:

```sh
cmake --workflow --preset lint
```
