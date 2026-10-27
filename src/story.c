/**
 * @file story.c
 * @brief The story screens: a port of bank_0D's EnterBridgeScene,
 *        EnterEndingScene, Story_DoPage, Story_DrawText, Story_Wait, the
 *        shutter effects and DrawFancyTheEndGraphic.
 *
 * Two nametables hold the same picture (the bridge, or the epilogue).
 * $2000, the "open" one, gets the story box and text and shows the box
 * black; $2400, the "closed" one, keeps the box empty and blue
 * (lut_StorySecondaryAttrib).  Story_OpenShutters / _CloseShutters switch
 * the PPU between them several times per frame with cycle-counted waits,
 * so bands of the box open or close a line at a time.  That timing is
 * simulated here from the cycle counts the disassembly gives, and each
 * band drawn with a clip rectangle.
 *
 * The ending then draws "The End" into CHR a pixel per frame: the outline
 * traced from data_TheEndDrawData, then traced again filling leftwards.
 * The BG pattern table is kept in CHR-RAM for that (tiles $00-$7F the
 * picture, $80-$FF the font, which the ending partly clears).
 *
 * (The bridge and ending music is started by game.c.)  Not ported: the
 * menustall pacing of drawing a page (done
 * while the shutters are closed, so invisible; here it takes no frames).
 */

#include <string.h>
#include "story.h"
#include "ppu.h"
#include "text.h"
#include "../data/generated/story_data.h"

#define NT_W 32
#define NT_H 30

enum { NT_OPEN, NT_CLOSED };    /* $2000, $2400 */
typedef enum {
    PH_OPEN, PH_WAIT, PH_CLOSE, PH_STALL,
    /* DrawFancyTheEndGraphic */
    PH_TE_CLEAR, PH_TE_NT, PH_TE_ATTR, PH_TE_PAL, PH_TE_RAM, PH_TE_OUTLINE, PH_TE_FILL, PH_TE_DONE,
} Phase;

static uint8_t nt[2][0x400];    /* 960 tiles + 64 attribute bytes each */
static uint8_t chr[0x1000];     /* the BG pattern table */
static uint8_t pal[16];
static int ending;              /* the ending (else the bridge scene) */
static Phase phase;
static int credits;             /* story_credits */
static int page;                /* story_page */
static int shutter_a, shutter_b;
static int soft_nt;             /* soft2000: the nametable a frame starts with */
static uint8_t framecounter;
static int story_timer, stall;
static int final_open;          /* the shutters opening on the empty box */
/* "The End" */
static int te_step, te_x, te_y, te_src, filling, fill_x, fill_y;

/* DrawBox at 4,4, $12 x $0C on the $2000 nametable (erasing the last page). */
static void draw_box(void) {
    uint8_t* t = nt[NT_OPEN];
    for (int r = 0; r < 0x0C; r++)
        for (int c = 0; c < 0x12; c++) {
            uint8_t v;
            if (r == 0)          v = (c == 0) ? 0xF7 : (c == 0x11) ? 0xF9 : 0xF8;
            else if (r == 0x0B)  v = (c == 0) ? 0xFC : (c == 0x11) ? 0xFE : 0xFD;
            else                 v = (c == 0) ? 0xFA : (c == 0x11) ? 0xFB : 0xFF;
            t[(4 + r) * NT_W + 4 + c] = v;
        }
}

/* Story_DoPage's drawing: the box, and this page's text. */
static void draw_page(void) {
    uint8_t* t = nt[NT_OPEN];
    draw_box();
    if (!credits) {                             /* a complex string at DrawBox's 5,6 */
        static TextGlyph g[TEXT_MAX_GLYPHS];
        int n = Text_FormatComplex(story_text_bytes + story_text_offsets[page], NULL, g, TEXT_MAX_GLYPHS);
        int x = 5, y = 6;
        for (int i = 0; i < n; i++) {
            if (g[i] == TEXT_NEWLINE) { x = 5; y++; continue; }
            if (x < NT_W && y < NT_H) t[y * NT_W + x] = (uint8_t)g[i];
            x++;
        }
        return;
    }
    /* Credits: simple strings, each a PPU address then tiles, ended by $01
     * (another string follows) or $00 (end of the page). */
    const uint8_t* p = credits_text_bytes + credits_text_offsets[page];
    for (;;) {
        int addr = (p[0] | (p[1] << 8)) - 0x2000;
        p += 2;
        uint8_t b;
        while ((b = *p++) > 0x01)
            if (addr >= 0 && addr < 0x3C0) t[addr++] = b;
        if (b == 0x00) break;
    }
}

static void open_shutters(void) {               /* Story_OpenShutters */
    shutter_a = 16;
    shutter_b = 1;
    phase = PH_OPEN;
}

/* Story_DoPage: shutters closed, the page drawn, then they open. */
static void start_page(void) {
    soft_nt = NT_CLOSED;
    draw_page();
    open_shutters();
}

/* LoadBridgeSceneGFX / LoadEpilogueSceneGFX (the picture's CHR, its
 * nametable twice, the font) and Story_FillSecAttrib. */
static void load_scene(const uint8_t* scene_chr, const uint8_t* scene_nt, const uint8_t* palette) {
    if (scene_chr) memcpy(chr, scene_chr, 0x800);
    else for (int t = 0; t < 0x80; t++) memcpy(chr + t * 16, Ppu_ChrTile(CHR_BANK_BRIDGE, t), 16);
    for (int t = 0x80; t < 0x100; t++) memcpy(chr + t * 16, Ppu_ChrTile(CHR_BANK_09, t), 16);
    memcpy(nt[NT_OPEN], scene_nt, 0x400);
    memcpy(nt[NT_CLOSED], scene_nt, 0x400);
    memcpy(&nt[NT_CLOSED][0x3C0], lut_StorySecondaryAttrib, 0x20);
    memcpy(pal, palette, 16);
    framecounter = 0;
    credits = 0;
    final_open = 0;
}

void Story_OpenBridge(void) {
    ending = 0;
    load_scene(NULL, data_BridgeNT, lut_BridgeBGPal);           /* CHR_BANK_BRIDGE */
    page = 0;
    start_page();
}

void Story_OpenEnding(void) {
    ending = 1;
    load_scene(data_EpilogueCHR, data_EpilogueNT, lut_EndingBGPal);
    page = lut_Bridge_LastPage[0];              /* the ending's pages follow the bridge's */
    start_page();
}

const uint8_t* Story_Palette(void) { return pal; }

/* After a page's shutters close: the next page, the credits, or the end. */
static void next_page(SaveData* save) {
    if (ending) {
        if (page < lut_Ending_LastPage[0]) { start_page(); return; }
        draw_box();                             /* @StoryExit: the empty box */
        final_open = 1;
        open_shutters();
        return;
    }
    if (!credits && page >= lut_Bridge_LastPage[0]) {   /* the story is done: credits */
        credits = 1;
        page = 0;
    }
    if (credits && page >= 4) {
        save->bridgescene = 0x80;               /* done: never again */
        stall = 0x20;
        phase = PH_STALL;
        return;
    }
    start_page();
}

/* ---- "The End" ---- */

/* TheEnd_MovePos: bit 0 right, 1 down, 2 left, 3 up (left/up win). */
static void te_move(uint8_t a, int* x, int* y) {
    if (a & 0x05) *x += (a & 0x04) ? -1 : 1;
    if (a & 0x0A) *y += (a & 0x08) ? -1 : 1;
}

/* The CHR address of pixel (x, y) and its bit (lut_TheEndPixelPosition_*). */
static int te_addr(int x, int y) {
    const uint8_t* ylo = lut_TheEndPixelPosition, *yhi = ylo + 0x50, *xs = yhi + 0x50;
    return (xs[x & 0xFF] + ylo[y & 0xFF] + (yhi[y & 0xFF] << 8)) & 0xFFF;
}
static uint8_t te_mask(int x) { return lut_TheEndPixelPosition[0xF0 + (x & 7)]; }

/* One frame of DrawFancyTheEndGraphic. */
static void the_end(void) {
    switch (phase) {
    case PH_TE_CLEAR:                           /* tiles $80-$EF, $80 bytes a frame */
        memset(chr + 0x800 + te_step * 0x80, 0, 0x80);
        if (++te_step == 14) { te_step = 0; phase = PH_TE_NT; }
        break;
    case PH_TE_NT:                              /* a 10x10 block of tiles $80+ at $20A8 */
        for (int c = 0; c < 10; c++)
            nt[NT_OPEN][(5 + te_step) * NT_W + 8 + c] = (uint8_t)(0x80 + te_step * 10 + c);
        if (++te_step == 10) phase = PH_TE_ATTR;
        break;
    case PH_TE_ATTR:                            /* palette 0 for it ($23CA) */
        memcpy(&nt[NT_OPEN][0x3CA], lut_TheEnd_AttrTable, 19);
        phase = PH_TE_PAL;
        break;
    case PH_TE_PAL:
        pal[1] = 0x30;                          /* white */
        pal[2] = 0x16;                          /* red (not used?) */
        phase = PH_TE_RAM;
        break;
    case PH_TE_RAM:                             /* theend_drawbuf cleared: a frame */
        te_x = 0x0C; te_y = 0; te_src = 0;
        phase = PH_TE_OUTLINE;
        break;
    case PH_TE_OUTLINE: {                       /* TheEnd_MoveAndSet, a pixel a frame */
        uint8_t a = data_TheEndDrawData[te_src];
        if (!a) {                               /* @StartFill */
            te_x = 0x0C; te_y = 0; te_src = 0;
            filling = 0;
            phase = PH_TE_FILL;
            break;
        }
        te_move(a, &te_x, &te_y);
        chr[te_addr(te_x, te_y)] |= te_mask(te_x);
        te_src++;
        break;
    }
    case PH_TE_FILL:
        /* Trace again; on moves $02/$03/$07, fill leftwards from the old
         * position until a set pixel (rows lut_FillGradient allows).  Each
         * filled pixel takes a frame; the rest takes none. */
        for (;;) {
            if (filling) {
                if (--fill_x < 0) { filling = 0; te_src++; continue; }
                int at = te_addr(fill_x, fill_y);
                if (chr[at] & te_mask(fill_x)) { filling = 0; te_src++; continue; }
                chr[at] |= te_mask(fill_x);
                return;
            }
            uint8_t a = data_TheEndDrawData[te_src];
            if (!a) { phase = PH_TE_DONE; return; }
            if (a == 0x02 || a == 0x03 || a == 0x07) {
                fill_x = te_x;
                fill_y = te_y;
                te_move(a, &te_x, &te_y);
                if (lut_FillGradient[fill_y]) { filling = 1; continue; }
            } else {
                te_move(a, &te_x, &te_y);
            }
            te_src++;
        }
    default:                                    /* @FinalLoop: forever */
        break;
    }
}

int Story_Update(SaveData* save, const InputState* in) {
    switch (phase) {
    case PH_OPEN:
    case PH_CLOSE:
        /* The shutters move a line every other frame (framecounter parity). */
        framecounter++;
        if (framecounter & 1) break;
        shutter_b++;
        if (--shutter_a) break;
        if (phase == PH_CLOSE) {
            soft_nt = NT_CLOSED;
            next_page(save);
        } else if (final_open) {                /* the empty box: "The End" */
            soft_nt = NT_OPEN;
            te_step = 0;
            phase = PH_TE_CLEAR;
        } else {                                /* fully open: Story_Wait */
            soft_nt = NT_OPEN;
            story_timer = 2;
            framecounter = 0;
            phase = PH_WAIT;
        }
        break;
    case PH_WAIT:
        /* up to 512 frames; A/B ends it early, except in the ending
         * (story_dropinput) */
        if (ending || !(in->pressed & (BTN_A | BTN_B))) {
            if (++framecounter) break;
            if (--story_timer) break;
        }
        page++;
        shutter_a = 16;                         /* Story_CloseShutters */
        shutter_b = 1;
        phase = PH_CLOSE;
        break;
    case PH_STALL:
        if (--stall == 0) return 1;
        break;
    default:
        the_end();
        break;
    }
    return 0;
}

int Story_Finished(void) { return ending && phase == PH_TE_DONE; }

/* One nametable, scanlines y0..y1-1 only. */
static void draw_nt(int which, int y0, int y1) {
    if (y1 <= y0) return;
    Ppu_SetClip(0, y0, 256, y1 - y0);
    const uint8_t* t = nt[which];
    for (int y = y0 / 8; y < NT_H && y * 8 < y1; y++)
        for (int x = 0; x < NT_W; x++) {
            uint8_t a = t[0x3C0 + (y / 4) * 8 + x / 4];
            int p = (a >> (((y % 4) / 2) * 4 + ((x % 4) / 2) * 2)) & 3;
            Ppu_DrawBgTile(CHR_BANK_RAM, t[y * NT_W + x], x * 8, y * 8, p);
        }
    Ppu_ResetClip();
}

/* The scanline from which a $2000 write at CPU cycle `t` (counted from the
 * start of VBlank) takes effect: rendering starts 21 lines (VBlank and the
 * pre-render line) in, and a write shows from the next line. */
static int write_line(double t) {
    double line = (t - 21 * 341.0 / 3) / (341.0 / 3);
    int l = (int)line + 1;
    return l < 0 ? 0 : (l > 240 ? 240 : l);
}

void Story_Draw(void) {
    for (int i = 0; i < 4; i++) Ppu_SetPalette(i, pal + i * 4);
    Ppu_SetRamChr(chr, 0x100);
    Ppu_Clear(0);
    if (phase != PH_OPEN && phase != PH_CLOSE) {
        draw_nt(soft_nt, 0, 240);
        return;
    }
    /* @ShutterLoop, 7 times from ~3209 cycles into the frame: wait x1 units
     * (105 cycles each), switch to the open NT; wait x2 units, switch to
     * the closed one; 1814 cycles per pass.  Opening waits a (closed) then
     * b (open); closing waits b then a. */
    int x1 = (phase == PH_OPEN) ? shutter_a : shutter_b;
    int x2 = (phase == PH_OPEN) ? shutter_b : shutter_a;
    int cur = (phase == PH_OPEN) ? NT_CLOSED : NT_OPEN;   /* soft2000 at the frame's top */
    int y = 0;
    double t = 3209;
    for (int i = 0; i < 7; i++) {
        int l1 = write_line(t + 11 + x1 * 105.0);
        draw_nt(cur, y, l1);
        y = l1 > y ? l1 : y;
        cur = NT_OPEN;
        int l2 = write_line(t + 11 + x1 * 105.0 + 8 + x2 * 105.0);
        draw_nt(cur, y, l2);
        y = l2 > y ? l2 : y;
        cur = NT_CLOSED;
        t += 1814;
    }
    draw_nt(cur, y, 240);
}
