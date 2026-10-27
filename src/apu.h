#pragma once
#include <stdint.h>

/*
 * apu.h -- the NES 2A03 sound chip: two pulse channels, the triangle and
 * noise (FF1 doesn't use the DMC), driven by writes to $4000-$4017 the
 * way the game's code writes them, and rendered to 16-bit mono samples.
 * Plain C (no SDL): audio.c feeds the samples to the sound device.
 */

/** Power-on state: everything silent. */
void Apu_Reset(void);

/** A write to an APU register ($4000-$4017). */
void Apu_Write(uint16_t addr, uint8_t value);

/** Run the chip for `count` output samples at `sample_rate` Hz and write
 *  them to `out` (signed 16-bit mono). */
void Apu_Render(int16_t* out, int count, int sample_rate);
