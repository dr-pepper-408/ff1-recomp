#include <SDL3/SDL.h>
#include <stdio.h>
#include <string.h>
#include "config.h"
#include "overworld_map.h"
#include "game.h"
#include "input.h"
#include "ppu.h"
#include "audio.h"
#include "romdata.h"

/**
 * Global runtime configuration.
 * Initialized at startup with DEFAULT_SCALE and DEFAULT_FULLSCREEN.
 */
Config g_config;

/**
 * Recomputes window dimensions from the current scale factor.
 * Sets cfg->window_w = NES_SCREEN_W * scale,
 *     cfg->window_h = NES_SCREEN_H * scale.
 * Does NOT create or destroy an SDL window.
 *
 * @param cfg  Pointer to the Config struct to update.
 */
void Config_Apply(Config* cfg) {
    cfg->window_w = NES_SCREEN_W * cfg->scale;
    cfg->window_h = NES_SCREEN_H * cfg->scale;
}

/** All game state (large; kept off the stack). */
static GameContext g_ctx;

/** Converts the PPU frame (palette indices) to RGB and shows it, scaled with
 *  nearest-neighbour filtering to fill the window. */
static void present_frame(SDL_Renderer* renderer, SDL_Texture* screen) {
    void* pixels;
    int pitch;
    if (SDL_LockTexture(screen, NULL, &pixels, &pitch)) {
        Ppu_ToRGBA((uint32_t*)pixels, pitch / 4);
        SDL_UnlockTexture(screen);
    }
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);
    SDL_RenderTexture(renderer, screen, NULL, NULL);
    SDL_RenderPresent(renderer);
}

/**
 * Runs the frame loop until the window is closed or Esc is pressed.
 *
 * Fixed timestep: game logic advances in whole NES frames (60.0988 Hz),
 * independent of the display refresh rate; rendering shows the latest state.
 * Steps per loop are capped so a stall can't snowball.
 */
static void run_loop(SDL_Renderer* renderer, SDL_Texture* screen) {
    InputState input = { 0, 0, 0 };
    int running = 1;
    Uint64 prev_ns = SDL_GetTicksNS();
    Uint64 accum_ns = 0;

    while (running) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_EVENT_QUIT) {
                running = 0;
            } else if (ev.type == SDL_EVENT_KEY_DOWN && ev.key.key == SDLK_ESCAPE) {
                running = 0;
            } else if (ev.type == SDL_EVENT_KEY_DOWN && ev.key.key == SDLK_F3) {
                g_ctx.show_walk_overlay = !g_ctx.show_walk_overlay;
            }
        }

        Uint64 now_ns = SDL_GetTicksNS();
        accum_ns += now_ns - prev_ns;
        prev_ns = now_ns;
        if (accum_ns > NES_FRAME_NS * MAX_FRAMES_PER_LOOP)
            accum_ns = NES_FRAME_NS * MAX_FRAMES_PER_LOOP;

        while (accum_ns >= NES_FRAME_NS) {
            accum_ns -= NES_FRAME_NS;
            Input_Poll(&input);
            Game_Update(&g_ctx, &input);
            Audio_Frame();
        }

        Game_Draw(&g_ctx);
        present_frame(renderer, screen);

        /* Without VSync (or on a fast display) don't spin: sleep until the
         * next logic frame is due. */
        Uint64 spent_ns = SDL_GetTicksNS() - prev_ns;
        if (accum_ns + spent_ns < NES_FRAME_NS)
            SDL_DelayNS(NES_FRAME_NS - accum_ns - spent_ns);
    }
}

/**
 * Application entry point.
 *
 * Initializes SDL3 video, creates the window, an accelerated renderer with
 * VSync and the 256x240 screen texture, decompresses the overworld map, then
 * runs the game.  Performs full cleanup on exit.
 *
 * @param argc  Argument count.
 * @param argv  Argument vector: --new skips the title screen and starts a
 *              new game; --rom <file> is the player's ROM.
 * @return      0 on clean exit, 1 on init failure.
 */
int main(int argc, char* argv[]) {
    int new_game = 0;
    const char* rom_arg = NULL;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--new") == 0) new_game = 1;
        else if (strcmp(argv[i], "--rom") == 0 && i + 1 < argc) rom_arg = argv[++i];
    }

    g_config.scale = DEFAULT_SCALE;
    g_config.fullscreen = DEFAULT_FULLSCREEN;
    Config_Apply(&g_config);

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    Uint64 win_flags = 0;
    if (g_config.fullscreen) win_flags |= SDL_WINDOW_FULLSCREEN;

    SDL_Window* window = SDL_CreateWindow(WINDOW_TITLE, g_config.window_w,
                                          g_config.window_h, win_flags);
    if (!window) {
        fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    SDL_Renderer* renderer = SDL_CreateRenderer(window, NULL);
    if (!renderer) {
        fprintf(stderr, "SDL_CreateRenderer failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    SDL_SetRenderVSync(renderer, 1);

    /* The NES picture: RGBA8888 matches Ppu_ToRGBA's 0xRRGGBBAA integers. */
    SDL_Texture* screen = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888,
                                            SDL_TEXTUREACCESS_STREAMING,
                                            PPU_WIDTH, PPU_HEIGHT);
    if (!screen) {
        fprintf(stderr, "SDL_CreateTexture failed: %s\n", SDL_GetError());
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    SDL_SetTextureScaleMode(screen, SDL_SCALEMODE_NEAREST);

    /* The game's data: the player's ROM -- --rom, $FF1_ROM, or a ROM beside
     * the program (FinalFantasy.nes, ff1.nes). */
    {
        const char* base = SDL_GetBasePath();
        static char p1[1024], p2[1024], p3[1024];
        snprintf(p1, sizeof(p1), "%sFinalFantasy.nes", base ? base : "");
        snprintf(p2, sizeof(p2), "%sff1.nes", base ? base : "");
        snprintf(p3, sizeof(p3), "%s../../FinalFantasy.nes", base ? base : "");
        const char* const roms[] = { p1, p2, p3, NULL };
        RomStatus st = RomData_Find(rom_arg, roms);
        if (st != ROM_OK) {
            char msg[512];
            snprintf(msg, sizeof(msg),
                     "This game needs a ROM of Final Fantasy (USA) for the NES.\n\n%s\n\n"
                     "Start it with --rom <file>, or put the ROM beside the program as FinalFantasy.nes.",
                     Rom_StatusText(st));
            fprintf(stderr, "%s\n", msg);
            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, WINDOW_TITLE, msg, window);
            SDL_DestroyTexture(screen);
            SDL_DestroyRenderer(renderer);
            SDL_DestroyWindow(window);
            SDL_Quit();
            return 1;
        }
    }

    if (OWMap_Init() != 0) {
        fprintf(stderr, "OWMap_Init failed\n");
        SDL_DestroyTexture(screen);
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    /* The SRAM file lives next to the executable. */
    char save_path[1024];
    const char* base = SDL_GetBasePath();
    snprintf(save_path, sizeof(save_path), "%sff1.sav", base ? base : "");
    Game_SetSavePath(save_path);
    Input_Init();
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO) || !Audio_Init())
        fprintf(stderr, "No audio: %s\n", SDL_GetError());
    if (new_game) Game_Init(&g_ctx);
    else Game_PowerOn(&g_ctx);
    run_loop(renderer, screen);

    Game_Shutdown();
    Audio_Shutdown();
    OWMap_Shutdown();
    SDL_DestroyTexture(screen);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
