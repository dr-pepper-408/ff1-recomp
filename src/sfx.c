/**
 * @file sfx.c
 * @brief Sound effects outside battle: the original's APU writes, as is.
 */

#include "sfx.h"
#include "apu.h"
#include "music.h"
#include "../data/generated/battle_sfx_data.h"

static int error_frames;

void Sfx_MenuSel(void) {                        /* PlaySFX_MenuSel (bank_0E) */
    Apu_Write(0x4004, 0xBA);                    /* 50% duty, volume $A */
    Apu_Write(0x4005, 0xBA);                    /* sweep up, speed 3, shift 2 */
    Apu_Write(0x4006, 0x40);
    Apu_Write(0x4007, 0x00);
    g_sq2_sfx = 0x1F;
}

void Sfx_MenuMove(void) {                       /* PlaySFX_MenuMove (bank_0E) */
    Apu_Write(0x4004, 0x7A);                    /* 25% duty, volume $A */
    Apu_Write(0x4005, 0x9B);                    /* sweep up, speed 1, shift 3 */
    Apu_Write(0x4006, 0x20);
    Apu_Write(0x4007, 0x10);
    g_sq2_sfx = 0x10;
}

void Sfx_Error(void) {                          /* PlaySFX_Error (bank_0F) */
    g_sq2_sfx = 1;
    Apu_Write(0x4000, 0x30);                    /* square 1 and noise silent */
    Apu_Write(0x4008, 0x30);
    Apu_Write(0x400C, 0x30);
    error_frames = 16;
}

int Sfx_ErrorFrame(void) {
    if (!error_frames) return 0;
    Apu_Write(0x4004, 0x8C);                    /* @Frame: 50% duty, decaying */
    Apu_Write(0x4005, 0x89);                    /* fast upward sweep */
    Apu_Write(0x4006, 0x80);
    Apu_Write(0x4007, 0x00);
    if (--error_frames == 0) {
        Apu_Write(0x4004, 0x30);                /* then square 2 silent */
        Apu_Write(0x4006, 0x00);
    }
    return 1;
}

void Sfx_JoyFrame(const InputState* in, uint8_t dirs) {
    if (in->pressed & (BTN_A | BTN_B)) { Sfx_MenuSel(); return; }
    uint8_t now = in->held & dirs;
    uint8_t before = (uint8_t)(((in->held | in->released) & ~in->pressed) & dirs);
    if (now && now != before) Sfx_MenuMove();
}

void Sfx_Door(void) {                           /* PlayDoorSFX */
    Apu_Write(0x400C, 0x0C);                    /* decaying, speed $C */
    Apu_Write(0x400E, 0x0E);
    Apu_Write(0x400F, 0x30);
}

/* DialogueBox_Sfx: A is the sweep ($8E opening, $95 closing); the low
 * byte of the period is the sweep value ROR'd and inverted. */
static void dialogue_sfx(uint8_t a) {
    Apu_Write(0x4004, 0x38);
    Apu_Write(0x4005, a);
    uint8_t c = a & 1;                          /* LSR A, ROR A, EOR #$FF */
    a = (uint8_t)((a >> 1) >> 1 | (c << 7));
    Apu_Write(0x4006, (uint8_t)(a ^ 0xFF));
    Apu_Write(0x4007, 0x00);
}

void Sfx_DialogueOpen(void)  { g_sq2_sfx = 53; dialogue_sfx(0x8E); }
void Sfx_DialogueClose(void) { g_sq2_sfx = 37; dialogue_sfx(0x95); }

void Sfx_PoisonStep(void) {                     /* MapPoisonDamage */
    Apu_Write(0x4004, 0x3A);                    /* 12.5% duty, volume $A */
    Apu_Write(0x4005, 0x81);                    /* sweep down */
    Apu_Write(0x4006, 0x60);
    Apu_Write(0x4007, 0x60);
    g_sq2_sfx = 6;
}

void Sfx_DamageTile(void) {                     /* MapTileDamage: "ksssshhh" */
    Apu_Write(0x400C, 0x0F);
    Apu_Write(0x400E, 0x0D);
    Apu_Write(0x400F, 0x00);
}

void Sfx_VehicleFrame(int vehicle, uint8_t fc) {
    uint8_t freq;
    if (vehicle == 8) {                         /* the airship: as AirshipTransitionFrame, twice as fast */
        Apu_Write(0x400C, 0x38);
        freq = (uint8_t)(fc << 1);
    } else if (vehicle == 4) {                  /* the ship: waves fading in and out */
        uint8_t v = (fc & 0x80) ? (uint8_t)(fc ^ 0xFF) : fc;
        Apu_Write(0x400C, (uint8_t)((v >> 4) | 0x30));
        freq = 0x0A;
    } else {
        return;
    }
    Apu_Write(0x400E, freq);
    Apu_Write(0x400F, 0x00);
}

void Sfx_AirshipTransition(uint8_t fc) {        /* AirshipTransitionFrame */
    Apu_Write(0x400C, 0x38);
    Apu_Write(0x400E, fc);
    Apu_Write(0x400F, 0x00);
}

void Sfx_NoiseOff(void)     { Apu_Write(0x400C, 0x00); }
void Sfx_NoiseSilence(void) { Apu_Write(0x400C, 0x30); }

void Sfx_MinimapDraw(void) {                    /* Minimap_DrawSFX */
    Apu_Write(0x4015, 0x02);                    /* only square 2 */
    Apu_Write(0x4004, 0x36);                    /* 12.5% duty, volume 6 */
    Apu_Write(0x4005, 0x7F);
    Apu_Write(0x4006, 0x60);
    Apu_Write(0x4007, 0x00);
}

/* ---- Battle ---- */

/* The btlsfx variables: the square and noise data pointers (offsets into
 * data_BattleSoundEffects), their remaining blocks and frame counters, and
 * the frames left of the whole effect. */
static int bsq_ptr, bnse_ptr;
static uint8_t bsq_len, bnse_len, bsq_ctr, bnse_ctr, bsfx_ctr;

/* UpdateBattleSFX_Square: the next block when its frames run out. */
static void battle_sfx_square(void) {
    if (!bsq_len || --bsq_ctr) return;
    const uint8_t* b = &data_BattleSoundEffects[bsq_ptr];
    bsq_ctr = b[4];
    Apu_Write(0x4004, b[0]);
    Apu_Write(0x4005, b[1]);
    Apu_Write(0x4006, b[2]);
    Apu_Write(0x4007, b[3]);
    bsq_ptr += 8;
    bsq_len--;
}

/* UpdateBattleSFX_Noise */
static void battle_sfx_noise(void) {
    if (!bnse_len || --bnse_ctr) return;
    const uint8_t* b = &data_BattleSoundEffects[bnse_ptr];
    bnse_ctr = b[2];
    Apu_Write(0x400C, b[0]);
    Apu_Write(0x400E, b[1]);
    Apu_Write(0x400F, 0xFF);                    /* a long length */
    bnse_ptr += 8;
    bnse_len--;
}

void Sfx_BattlePlay(int id) {
    int base = lut_BattleSfxOffset[id];
    const uint8_t* h = &data_BattleSoundEffects[base];
    bsq_ptr = base + 3;                         /* past the header */
    bnse_ptr = base + 8;                        /* past the header and the square bytes */
    g_sq2_sfx = bsfx_ctr = h[0];
    bsq_len = h[1];
    bnse_len = h[2];
    bsq_ctr = bnse_ctr = 1;                     /* so the first blocks start now */
    battle_sfx_square();
    battle_sfx_noise();
    Apu_Write(0x4015, 0x0F);
}

void Sfx_BattleUpdate(void) {
    if (!bsfx_ctr) return;
    battle_sfx_square();
    battle_sfx_noise();
    bsfx_ctr--;
}

static uint8_t chaos_vol;                       /* $9C */

static void chaos_noise(void) {
    Apu_Write(0x400C, chaos_vol);
    Apu_Write(0x400D, 0xFF);
    Apu_Write(0x400F, 0xFF);
    Apu_Write(0x400E, 0x0F);                    /* the lowest rumble */
}

void Sfx_ChaosRumbleStart(void) {
    Apu_Write(0x4015, 0x08);
    chaos_vol = 0xFF;                           /* (0 - 1: full volume, constant) */
    chaos_noise();
}

void Sfx_ChaosRumble(void) {
    /* The 17th call makes $EF: full volume with a looping decay, so two
     * more booms follow. */
    chaos_vol--;
    chaos_noise();
}

void Sfx_ChaosRumbleStop(void) { Apu_Write(0x4015, 0x00); }
