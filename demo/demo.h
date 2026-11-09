/* SPDX-License-Identifier: Zlib */
#ifndef DEMO_H
#define DEMO_H

/*
 * The demo file server. It keeps dense columns, one value per entity, of
 * vec3 f32 positions and of i32 health from 0 to 100, and shows each two ways.
 * The bulk files are the whole column:
 *
 *	components/transform/pos	n x 12 bytes, the positions in slot order
 *	components/transform/ids	n x 4 bytes, the uint32_t entity IDs in
 *					slot order, read only
 *	components/unit/health		n x 4 bytes, the health in slot order
 *
 * Each live entity's value also shows as a value file:
 *
 *	entities/<id>/components/transform/pos		12 bytes
 *	entities/<id>/components/unit/health		4 bytes
 *
 * Both files of health have the field health of the schema unit as their
 * type, the bulk file with NINE_TYPE_BULK. A text write checks the range, and
 * a binary write or demo_set_health() does not.
 *
 * entities/ lists the IDs of the live entities in decimal, smallest first.
 *
 * Both views reach the same memory. A bulk write follows the order of ids
 * at the time of the write. A batched read reads whole files only: a size
 * other than a file's fails with -EINVAL.
 *
 * Destroying an entity moves the last one into its slot, so the order of
 * ids changes, and the column moves in memory as it grows. A handle to an
 * entity's node keeps finding it through both. Once the entity is
 * destroyed, every operation through such a handle returns -ESTALE, even
 * after a new entity has taken its ID.
 *
 * The native calls below change the data as a real app would. They are
 * setup calls for the file server: none may run at the same time as any
 * other call to the same instance.
 */

#include <stdint.h>

#include "ninefold.h"

struct demo;

/* demo_new() - Make an empty instance. Return: it, or NULL. */
struct demo *demo_new(void);

/* demo_free() - Free an instance, or do nothing for NULL. */
void demo_free(struct demo *d);

/*
 * demo_add_schemas() - Add the schemas of the demo to a tree, once for each
 * tree, before any tool reads a type. It is a setup call for the tree.
 *
 * Return: 0, or an error from nine_add_schema().
 */
int demo_add_schemas(struct nine_tree *t);

/*
 * demo_create() - Make a new entity, at position (0, 0, 0) and with health 0.
 * It takes the ID
 * of a destroyed entity when there is one.
 *
 * Return: 0 with its ID in *@id, or -ENOMEM.
 */
int demo_create(struct demo *d, uint32_t *id);

/*
 * demo_destroy() - Destroy an entity. The last entity in the column moves
 * into its slot, and its ID is free for a later demo_create().
 *
 * Return: 0 or -ENOENT.
 */
int demo_destroy(struct demo *d, uint32_t id);

/* demo_get_pos() - Read an entity's position. Return: 0 or -ENOENT. */
int demo_get_pos(const struct demo *d, uint32_t id, float pos[3]);

/* demo_set_pos() - Set an entity's position. Return: 0 or -ENOENT. */
int demo_set_pos(struct demo *d, uint32_t id, const float pos[3]);

/* demo_get_health() - Read an entity's health. Return: 0 or -ENOENT. */
int demo_get_health(const struct demo *d, uint32_t id, int32_t *health);

/*
 * demo_set_health() - Set an entity's health, with no check of its range.
 *
 * Return: 0 or -ENOENT.
 */
int demo_set_health(struct demo *d, uint32_t id, int32_t health);

/*
 * struct demo_column - The native arrays of an instance, for a plain C loop
 * such as a benchmark's baseline.
 * @pos: The positions, @n of them, in slot order.
 * @slots: The slot of each live entity, indexed by its ID.
 * @n: The number of live entities.
 *
 * The pointers stay valid until the next demo_create() or demo_destroy().
 */
struct demo_column {
	float (*pos)[3];
	const uint32_t *slots;
	uint32_t n;
};

/* demo_get_column() - Get the native arrays of an instance in *@col. */
void demo_get_column(struct demo *d, struct demo_column *col);

/* demo_ops() - The operations to mount an instance with, as its priv. */
const struct nine_file_server_ops *demo_ops(void);

/*
 * demo_variant_ops() - The operations of a second variant, to mount an
 * instance with instead. They give the same results as demo_ops(), but
 * their read is separate code, so that a benchmark can read through two
 * file servers whose reads differ.
 */
const struct nine_file_server_ops *demo_variant_ops(void);

#endif /* DEMO_H */
