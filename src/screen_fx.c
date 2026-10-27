/**
 * @file screen_fx.c
 * @brief Frame-stepped ports of ScreenWipe_Open/_Close, CyclePalettes and
 *        BattleTransition (bank_0F), with their sound effects.
 */

#include "screen_fx.h"
#include "ppu.h"
#include "apu.h"
#include "music.h"
#include <string.h>

/* ScreenWipeFrame counts lines from 11 before the visible picture starts. */
#define WIPE_LINE_OFFSET 11

/* BattleTransition loops its counter from $00 to $40. */
#define BATTLE_FX_FRAMES 0x41

/* DoAltarEffect counts its scanline delay from AltarFrame, about 15 lines
 * before the picture starts. */
#define ALTAR_LINE_OFFSET 15
/* Each beam line is monochrome for 40 CPU cycles = 120 pixels. */
#define ALTAR_BEAM_WIDTH 120

void ScreenFx_Start(ScreenFx* fx, ScreenFxKind kind, const uint8_t bg_pal[16]) {
    memset(fx, 0, sizeof(*fx));
    fx->kind = kind;

    switch (kind) {             /* the sounds' setup */
    case SCREENFX_WIPE_OPEN:
    case SCREENFX_WIPE_CLOSE:     /* StartScreenWipe: square 1 only, a fast downward sweep */
        Apu_Write(0x4015, 0x01);
        Apu_Write(0x4000, 0x38);
        Apu_Write(0x4001, 0x8A);
        break;
    case SCREENFX_BATTLE:         /* everything but the noise silenced */
        Apu_Write(0x4015, 0x08);
        break;
    case SCREENFX_ALTAR:          /* all volumes 0; noise $E and square 2 at $500 start */
        Apu_Write(0x400C, 0x30);
        Apu_Write(0x4004, 0x30);
        Apu_Write(0x4000, 0x30);
        Apu_Write(0x4008, 0x30);
        Apu_Write(0x400E, 0x0E);
        Apu_Write(0x400F, 0x00);
        Apu_Write(0x4006, 0x00);
        Apu_Write(0x4007, 0x05);
        break;
    default:
        break;
    }

    switch (kind) {
    case SCREENFX_WIPE_OPEN:      /* from 1 line just below the center */
        fx->wipe_top = 122 + 11;
        fx->wipe_rows = 1;
        break;
    case SCREENFX_WIPE_CLOSE:     /* from lines 10-230 */
        fx->wipe_top = 10 + 11;
        fx->wipe_rows = 220 + 1;
        break;
    case SCREENFX_PALCYC_OUT:     /* PalCyc_GetInitialPal: the current palette */
        memcpy(fx->pal, bg_pal, 16);
        fx->counter = 1;
        break;
    case SCREENFX_PALCYC_IN:
        /* Cycling back in starts every non-black color at hue $C of its
         * brightness row, then steps down to the real color. */
        memcpy(fx->target, bg_pal, 16);
        for (int i = 0; i < 16; i++)
            fx->pal[i] = (bg_pal[i] == 0x0F) ? 0x0F : (uint8_t)((bg_pal[i] & 0x30) | 0x0C);
        fx->counter = 1;
        break;
    case SCREENFX_ALTAR:
        fx->altar_delay = 138;
        fx->altar_lines = 2;
        break;
    default:
        break;
    }
}

/* PalCyc_Step: one step through the cycle.  Returns how many colors changed
 * (0 = cycling complete). */
static int palcyc_step(ScreenFx* fx) {
    int changed = 0;
    for (int i = 0; i < 16; i++) {
        if (fx->kind == SCREENFX_PALCYC_OUT) {
            if (fx->pal[i] == 0x0F) continue;
            uint8_t hue = (uint8_t)((fx->pal[i] & 0x0F) + 1);
            fx->pal[i] = (hue >= 0x0D) ? 0x0F : (uint8_t)((fx->pal[i] & 0x30) | hue);
        } else {
            if (fx->pal[i] == fx->target[i]) continue;
            fx->pal[i]--;
        }
        changed++;
    }
    return changed;
}

/* ScreenWipeFrame: when bits 2-3 of the visible line count are clear,
 * square 1's period is set to -lines * 8 (fewer lines, lower pitch). */
static void wipe_sfx(int rows) {
    if (rows & 0x0C) return;
    uint8_t a = (uint8_t)~rows;
    Apu_Write(0x4002, (uint8_t)(a << 3));
    Apu_Write(0x4003, (uint8_t)(a >> 5));     /* (tmp's other bits only set the length) */
}

int ScreenFx_Step(ScreenFx* fx) {
    switch (fx->kind) {               /* this frame's sound */
    case SCREENFX_WIPE_OPEN:
    case SCREENFX_WIPE_CLOSE:
        wipe_sfx(fx->wipe_rows);
        break;
    case SCREENFX_BATTLE:             /* noise at (counter / 2) | 3, restarted each frame */
        Apu_Write(0x400C, 0x0F);
        Apu_Write(0x400E, (uint8_t)((fx->counter >> 1) | 0x03));
        Apu_Write(0x400F, 0x00);
        break;
    case SCREENFX_ALTAR: {            /* AltarFrame: the volume follows the beam */
        uint8_t v = (uint8_t)((fx->altar_lines >> 3) | 0x30);
        Apu_Write(0x400C, v);
        Apu_Write(0x4004, v);
        break;
    }
    default:
        break;
    }

    switch (fx->kind) {
    case SCREENFX_WIPE_OPEN:
        fx->wipe_top -= 2;
        fx->wipe_rows += 4;
        if (fx->wipe_rows >= 224) fx->kind = SCREENFX_NONE;
        if (!fx->kind) { Apu_Write(0x4002, 0); Apu_Write(0x4003, 0); }  /* ScreenWipe_Finalize */
        break;
    case SCREENFX_WIPE_CLOSE:
        fx->wipe_top += 2;
        fx->wipe_rows -= 4;
        if (fx->wipe_rows == 1) fx->kind = SCREENFX_NONE;
        if (!fx->kind) { Apu_Write(0x4002, 0); Apu_Write(0x4003, 0); }
        break;
    case SCREENFX_PALCYC_OUT:
    case SCREENFX_PALCYC_IN:
        /* A step is taken every 4th frame. */
        fx->counter++;
        if ((fx->counter & 3) == 0 && palcyc_step(fx) == 0) fx->kind = SCREENFX_NONE;
        break;
    case SCREENFX_BATTLE:
        if (++fx->counter >= BATTLE_FX_FRAMES) {
            fx->kind = SCREENFX_NONE;
            Apu_Write(0x4015, 0x00);          /* and the APU off */
        }
        break;
    case SCREENFX_ALTAR:
        if (!fx->altar_retract) {          /* the beam grows upward... */
            fx->altar_lines += 2;
            fx->altar_delay -= 2;
            if (fx->altar_delay < 32) fx->altar_retract = 1;
        } else {                           /* ...then shrinks from the top */
            fx->altar_lines -= 2;
            if (fx->altar_lines < 8) {
                fx->kind = SCREENFX_NONE;
                /* Squares and noise restarted for the music; square 2
                 * keeps the effect's pitch until its next note. */
                Apu_Write(0x4007, 0x00);
                Apu_Write(0x400C, 0x00);
                Apu_Write(0x4004, 0x00);
                g_sq2_sfx = 1;
            }
        }
        break;
    default:
        break;
    }
    return fx->kind == SCREENFX_NONE;
}

void ScreenFx_Apply(const ScreenFx* fx) {
    switch (fx->kind) {
    case SCREENFX_WIPE_OPEN:
    case SCREENFX_WIPE_CLOSE:
        g_ppu.bg_top = fx->wipe_top - WIPE_LINE_OFFSET;
        g_ppu.bg_rows = fx->wipe_rows;
        break;
    case SCREENFX_PALCYC_OUT:
    case SCREENFX_PALCYC_IN:
        memcpy(g_ppu.palette, fx->pal, 16);    /* BG colors only */
        break;
    case SCREENFX_BATTLE:
        /* A new emphasis mode every 4 frames: (counter << 3) & $E0. */
        g_ppu.emphasis = (uint8_t)((fx->counter >> 2) & 7);
        break;
    case SCREENFX_ALTAR: {
        /* Each line is waited 1 dot longer than a scanline, so the monochrome
         * span moves one pixel right per line: a diagonal beam from the
         * party toward the top-left.  (Its exact horizontal start depends on
         * CPU timing; it is placed to end at the party, as in the game.) */
        int top = fx->altar_delay - ALTAR_LINE_OFFSET;
        for (int i = 0; i < fx->altar_lines; i++) {
            int y = top + i;
            int x0 = y - 116;
            Ppu_SetMonochrome(y, x0, x0 + ALTAR_BEAM_WIDTH);
        }
        /* AltarFrame: shake the screen by 1 pixel with bit 0 of lines/8. */
        g_ppu.shake_x = (int8_t)((fx->altar_lines >> 3) & 1);
        break;
    }
    default:
        break;
    }
}

int ScreenFx_ShowsSprites(const ScreenFx* fx) {
    return fx->kind == SCREENFX_ALTAR;     /* AltarFrame keeps doing sprite DMA */
}
