/* SPDX-License-Identifier: Zlib */
#ifndef DEMO_H
#define DEMO_H

/*
 * The demo file server. It keeps one dense column of vec3 f32 positions, one
 * per entity, and shows each live entity's position as a 12-byte value file:
 *
 *	entities/<id>/components/transform/pos
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
 * demo_create() - Make a new entity, at position (0, 0, 0).
 *
 * Return: 0 with its ID in *@id, or -ENOMEM.
 */
int demo_create(struct demo *d, uint32_t *id);

/* demo_get_pos() - Read an entity's position. Return: 0 or -ENOENT. */
int demo_get_pos(const struct demo *d, uint32_t id, float pos[3]);

/* demo_set_pos() - Set an entity's position. Return: 0 or -ENOENT. */
int demo_set_pos(struct demo *d, uint32_t id, const float pos[3]);

/* demo_ops() - The operations to mount an instance with, as its priv. */
const struct nine_file_server_ops *demo_ops(void);

#endif /* DEMO_H */
