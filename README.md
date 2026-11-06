<picture>
  <source media="(prefers-color-scheme: dark)" srcset="branding/ninefold-logo-dark.svg">
  <img src="branding/ninefold-logo.svg" alt="Ninefold" width="289" height="64">
</picture>

Ninefold is a C library that shows the live state of an app as a virtual
file tree, in the way Plan 9 made everything a file. Code, tools and people
all reach the app's state through the same few operations: open, list,
stat, read and write.

The file is the interface, not the storage. The real data stays in the
app's own native structures, such as dense arrays of components, and the
tree is a layer of names on top of them. A tool reaches a single value by
its path. Fast code opens a path once and then reads through the handle it
gets back, or reads a whole array at once through a bulk file. Every one of
these routes reaches the same memory.

The tree lives inside the app's process. It isn't an operating system
mount such as FUSE.

## Stability

Ninefold is at version 0.x. There is no API or ABI stability before 1.0:
any 0.x release can change or remove anything, so pin the version you
build against.

## Contributions

Ninefold accepts no outside code before 1.0. Keeping a single copyright
holder until then leaves the licence free to change.

## Building

Ninefold needs CMake 3.25 or later. The presets below also need Ninja.
Each preset has a workflow preset of the same name that runs it from start
to finish in one command, for most of them configuring, building and
running the tests:

```sh
cmake --workflow --preset gcc-debug
```

| Preset | Compiler | Build |
|---|---|---|
| `gcc-debug`, `gcc-release` | GCC | Debug, Release |
| `clang-debug`, `clang-release` | Clang | Debug, Release |
| `msvc-debug`, `msvc-release` | MSVC | Debug, Release |
| `asan` | Clang | Debug, with AddressSanitizer and UndefinedBehaviorSanitizer |
| `tsan` | Clang | Debug, with ThreadSanitizer |
| `msvc-asan` | MSVC | RelWithDebInfo, with AddressSanitizer |
| `lint` | None | Checks the formatting and runs checkpatch |

Each preset builds in `build/<preset>`. The presets treat warnings as
errors. A plain CMake build doesn't, so a newer compiler with a new warning
never breaks an app that builds Ninefold.

To see a tree, run the demo program, `ninefold_walk` in
`build/<preset>/demo`. It mounts two demo file servers, each holding a few
entities with positions, then walks the tree from `/` and prints each
node's path, kind and size, and the bytes of each file.

## Using it in your project

Ninefold builds as a static library with one public header,
`ninefold.h`. A CMake project can use it in two ways, and both give the
target `ninefold::ninefold`.

From an installed copy:

```sh
cmake --workflow --preset gcc-release
cmake --install build/gcc-release --prefix /opt/ninefold
```

```cmake
find_package(ninefold REQUIRED)
target_link_libraries(app PRIVATE ninefold::ninefold)
```

Pass `-DCMAKE_PREFIX_PATH=/opt/ninefold` when you configure the app, or
install to a prefix that CMake already searches.

From a copy of the source inside your project:

```cmake
add_subdirectory(third_party/ninefold)
target_link_libraries(app PRIVATE ninefold::ninefold)
```

Added this way, Ninefold builds only the library. Its tests, demo and
benchmark stay off unless you set `NINEFOLD_BUILD_TESTS`,
`NINEFOLD_BUILD_DEMO` or `NINEFOLD_BUILD_BENCH`, and it installs nothing
unless you set `NINEFOLD_INSTALL`.

## Tested versions

CI builds and tests every push on Linux and Windows with:

| Tool | Version |
|---|---|
| GCC | 16 |
| Clang | 23 |
| MSVC | 14.44 (Visual Studio 2022) |
| clang-format | 22.1.8 |

The GCC job uses CMake 3.25.3, the oldest version Ninefold supports.
There is no minimum compiler version: older compilers may work, but
nothing tests them. Other versions of clang-format format some code
differently, so use 22.1.8 to format code for Ninefold.

## Licence

Ninefold is available under the Zlib licence, in [`LICENSE`](LICENSE). The
licence covers every file in this repository except the files in
[`branding/`](branding/), such as the logos, which are not licensed for
any use.
Any third-party file copied into the repository keeps its own licence.
