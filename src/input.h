#pragma once
#include <stdint.h>

/*
 * Keyboard input mapped to NES-style buttons.
 *
 * The low nibble mirrors FF1's "facing" convention (used directly by the
 * movement code):  RIGHT=1, LEFT=2, DOWN=4, UP=8.  The high nibble holds the
 * action buttons A/B/Select/Start.
 */
#define BTN_RIGHT  0x01
#define BTN_LEFT   0x02
#define BTN_DOWN   0x04
#define BTN_UP     0x08
#define BTN_A      0x10
#define BTN_B      0x20
#define BTN_SELECT 0x40
#define BTN_START  0x80

/** Per-frame input snapshot. */
typedef struct {
    uint8_t held;     ///< Buttons currently held down.
    uint8_t pressed;  ///< Buttons that transitioned up->down this frame.
    uint8_t released; ///< Buttons that transitioned down->up this frame.
} InputState;

/** Initialize the input system (call once after SDL_Init). */
void Input_Init(void);

/**
 * Poll the keyboard and fill an InputState.
 * Must be called once per frame, after SDL events have been pumped.
 */
void Input_Poll(InputState* out);
