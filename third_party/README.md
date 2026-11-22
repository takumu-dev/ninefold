# Third-party code

The files in this directory are copies of other projects' code. Each keeps
its own licence, the Boost Software License 1.0, whose text is beside it.
The Boost licence puts no condition on object code, so a program that links
Ninefold needs no notice for them.

| Directory | Project | Upstream | Commit | Files |
|---|---|---|---|---|
| `ryu/` | Ryu, by Ulf Adams | <https://github.com/ulfjack/ryu> | `4c0618b0e44f7ef027ebae05d2cc7812048f7c8f` | `LICENSE-Boost`, and from `ryu/`: `common.h`, `d2s.c`, `d2s_full_table.h`, `d2s_intrinsics.h`, `digit_table.h`, `f2s.c`, `f2s_full_table.h`, `f2s_intrinsics.h`, `ryu.h` |
| `ffc/` | ffc.h, a C port of fast_float | <https://github.com/kolemannix/ffc.h> | `7e85c6d11a0289d5eead7c1404de8203c52f8cf8` | `ffc.h`, `LICENSE-BOOST` |

Ryu and ffc.h each offer more than one licence. Ninefold takes them under
the Boost licence only.

Each file is a byte-for-byte copy of the file at that commit, so a change
here comes from a new copy at a new commit, never from an edit. The core
builds them in `src/number.c`, the only file that includes them. Ryu gives
the shortest digits of a float, and ffc.h parses a number. Neither calls
`printf` or `strtod`, so the C locale cannot change the text.

The formatting check, checkpatch and the SPDX line check do not look in
this directory. The `check-licences` target checks instead that each copied
source file grants the Boost licence, and that each project's directory
holds the licence text.
