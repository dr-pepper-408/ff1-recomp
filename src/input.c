/**
 * @file input.c
 * @brief Keyboard -> NES-style button mapping with edge detection.
 */

#include "input.h"
#include <SDL3/SDL.h>

/** SDL keyboard state pointer (valid for the lifetime of the app). */
static const bool* g_keys = NULL;

/** Held-button bitmask from the previous frame (for edge detection). */
static uint8_t g_prev_held = 0;

void Input_Init(void) {
    g_keys = SDL_GetKeyboardState(NULL);
    g_prev_held = 0;
}

/**
 * Reads the currently-held buttons from SDL's keyboard state.
 *
 * Key mapping:
 *   Arrows      -> directions
 *   Z / Space   -> A
 *   X / Backsp. -> B
 *   Shift / Tab -> Select
 *   Enter       -> Start
 */
static uint8_t read_buttons(void) {
    uint8_t b = 0;

    if (g_keys[SDL_SCANCODE_RIGHT]) b |= BTN_RIGHT;
    if (g_keys[SDL_SCANCODE_LEFT])  b |= BTN_LEFT;
    if (g_keys[SDL_SCANCODE_DOWN])  b |= BTN_DOWN;
    if (g_keys[SDL_SCANCODE_UP])    b |= BTN_UP;

    if (g_keys[SDL_SCANCODE_Z] || g_keys[SDL_SCANCODE_SPACE]) b |= BTN_A;
    if (g_keys[SDL_SCANCODE_X] || g_keys[SDL_SCANCODE_BACKSPACE]) b |= BTN_B;
    if (g_keys[SDL_SCANCODE_LSHIFT] || g_keys[SDL_SCANCODE_RSHIFT] ||
        g_keys[SDL_SCANCODE_TAB]) b |= BTN_SELECT;
    if (g_keys[SDL_SCANCODE_RETURN] || g_keys[SDL_SCANCODE_KP_ENTER]) b |= BTN_START;

    return b;
}

void Input_Poll(InputState* out) {
    if (!out) return;

    uint8_t held = g_keys ? read_buttons() : 0;
    uint8_t changed = (uint8_t)(held ^ g_prev_held);

    out->held     = held;
    out->pressed  = (uint8_t)(changed & held);        // up -> down this frame
    out->released = (uint8_t)(changed & ~held);       // down -> up this frame

    g_prev_held = held;
}
