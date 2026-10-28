/**
 * @file view3d.h
 * @brief What ff1core.c and the 3D view's files (view3d_world.c,
 *        view3d_battle.c) share; not part of the library's API.
 */
#pragma once
#include <stdint.h>
#include "ff1core.h"
#include "game.h"

extern GameContext g_ctx;       /* the game the library runs (ff1core.c) */

/* An NES colour as R, G, B, A bytes. */
void nes_rgba(uint8_t color, uint8_t* out);

/* A pixel's colour index (0-3) in an 8x8 CHR tile. */
int chr_pixel(const uint8_t* t, int row, int col);

/* Vertices being written: `count` keeps counting past `max` (the size
 * needed). */
typedef struct { Ff1Vertex* out; int max, count; } Emit;

/* Two triangles: corners p[0..3] with texels uv[0..3] (atlas pixels). */
void emit_quad(Emit* e, const float p[4][3], const float uv[4][2], uint8_t shade);
