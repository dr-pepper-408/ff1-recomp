-- The game library's functions (src/ff1core.h), as LuaJIT's FFI sees them.
local ffi = require("ffi")

ffi.cdef [[
typedef struct { float x, y, z, u, v; uint8_t r, g, b, a; } Ff1Vertex;
int ff1_load_rom(const char* path);
const char* ff1_rom_message(int status);
int ff1_init(const char* save_path, int new_game);
void ff1_class_sprite(int cls, uint8_t* rgba);
void ff1_quit(void);
void ff1_frame(int buttons);
int ff1_audio(int16_t* out, int max);
void ff1_draw(uint8_t* rgba);
int ff1_view3d(void);
int ff1_view3d_overlay(uint8_t* rgba);
void ff1_view3d_fx(float* out);
uint32_t ff1_world(int* tiles);
uint32_t ff1_chunk_key(int cx, int cz);
int ff1_chunk(int cx, int cz, Ff1Vertex* out, int max);
void ff1_terrain_atlas(uint8_t* rgba);
void ff1_focus(float* xyz);
int ff1_sprites(Ff1Vertex* out, int max, uint8_t* atlas, float yaw);
void ff1_debug_place(int tx, int ty, int vehicle);
void ff1_set_encounters(int on);
void ff1_set_reward_mult(int mult);
int ff1_shadows(float* out, int max);
int ff1_props(Ff1Vertex* out, int max, float yaw);
int ff1_battle3d(void);
int ff1_battle_scene(Ff1Vertex* out, int max, uint8_t* atlas, float rx, float rz, float* info);
void ff1_battle_overlay(uint8_t* rgba);
void ff1_battle_stagger(float s);
void ff1_battle_flip(int on);
void ff1_debug_battle(int formation);
void ff1_debug_enter(int id);
void ff1_debug_inroom(void);
void ff1_debug_dialogue(int id);
void ff1_debug_class(int i, int cls);
void ff1_debug_level(int level);
void ff1_debug_kit(int stage);
int ff1_debug_teleport(int id);
void ff1_debug_autopilot(int on, const int* attack, const int* heal);
int ff1_tour_count(void);
int ff1_tour_go(int i, int* info);
int ff1_landmark_count(void);
void ff1_landmark_info(int i, int* out);
void ff1_landmark_pixels(int i, uint8_t* rgba);
]]

-- The library at `path`.
return function(path) return ffi.load(path) end
