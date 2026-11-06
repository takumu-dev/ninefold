/* SPDX-License-Identifier: Zlib */
#ifndef NINEFOLD_H
#define NINEFOLD_H

#ifdef __cplusplus
extern "C" {
#endif

struct nine_tree;
struct nine_ns;

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

#ifdef __cplusplus
}
#endif

#endif /* NINEFOLD_H */
