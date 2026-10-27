--[[
  A LOVE front end for the C port: the game runs in ff1core.dll (built by
  CMake into ../build), loaded through LuaJIT's FFI.  This does what
  main.c, input.c and audio.c do for the SDL build, and adds a 3D view of
  the overworld (F2).

  Keys: arrows, Z = A, X = B, Tab = Select, Enter = Start (as the SDL build).
  F2 2D / 3D,  C camera preset,  V view distance,  Q / E turn the camera,
  PgUp / PgDn or the wheel zoom.  Extras, not in the original: F5 tilt-shift
  blur (HD-2D's miniature look), F6 water shimmer, F7 day / night, F8 the
  battle camera's glide in and drift, F9 random encounters on / off, F10
  battle speed (1x, 2x, 4x).  (The
  camera, distance and extras are remembered.)

  Test options: --new (skip the title), --shot (new game, screenshot of
  the 3D view, quit), --at=X,Y (put the party on that overworld tile),
  --vehicle=N (with --at: 1 foot, 2 canoe, 4 ship, 8 airship),
  --yaw=DEGREES (camera angle), --dist=PIXELS (camera distance),
  --camera=N (preset 1 classic, 2 close, 3 high, 4 low; not remembered),
  --daytime=F (day / night on, at that time of day: 0.5 dusk, 0.75 night),
  --battle=N (start formation N at once), --battlecam=N (that battle shot,
  1-4; 5 the boss's), --frames=N (when --shot
  captures), --auto (tap A every 16 frames: fight through menus),
  --enter=N (go through overworld entrance N: 1 Coneria, 2 Pravoka...),
  --inroom (with --enter: as if through a door, the rooms' insides shown),
  --dialogue=N (show dialogue N, as talking does), --2d (start in 2D),
  --nohud (no status lines at the top, for pictures),
  --classes=A,B,C,D (the party's classes: 0-5, 6-11 after the class change),
  --ask-rom (ask for the ROM even if one is found), --drop=FILE (as if
  FILE were dropped on the ROM prompt), --landmark-refs (write
  reference pictures of the overworld's landmarks to the save folder's
  landmarks/, for modelling them, then quit).
]]

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
int ff1_shadows(float* out, int max);
int ff1_props(Ff1Vertex* out, int max, float yaw);
int ff1_battle3d(void);
int ff1_battle_scene(Ff1Vertex* out, int max, uint8_t* atlas, float rx, float rz, float* info);
void ff1_battle_overlay(uint8_t* rgba);
void ff1_battle_stagger(float s);
void ff1_debug_battle(int formation);
void ff1_debug_enter(int id);
void ff1_debug_inroom(void);
void ff1_debug_dialogue(int id);
void ff1_debug_class(int i, int cls);
int ff1_landmark_count(void);
void ff1_landmark_info(int i, int* out);
void ff1_landmark_pixels(int i, uint8_t* rgba);
]]

local NES_FRAME = 16639267 / 1e9   -- 60.0988 Hz (config.h NES_FRAME_NS)
local CHUNK = 256                  -- world pixels per chunk side
local MAP = 4096                   -- the world wraps every MAP pixels (ff1_world)
local NCHUNK = 16                  -- chunks per side
local world_key
local world_tiles = ffi.new("int[1]")
local VIEW = 560                   -- draw distance (pixels); fog ends there (V cycles)
local VIEWS = { { "near", 420 }, { "normal", 560 }, { "far", 760 } }
local view_i = 2

-- Camera presets (C cycles): distance from the party, pitch above the
-- ground, field of view.  The camera eases into a new one.
local PRESETS = {
    { name = "classic", dist = 250, pitch = 47, fov = 45 },
    { name = "close",   dist = 125, pitch = 32, fov = 50 },
    { name = "high",    dist = 420, pitch = 66, fov = 40 },
    { name = "low",     dist = 165, pitch = 17, fov = 55 },   -- third person, HD-2D style
}
local preset_i = 1
local zoom = 1                     -- PgUp / PgDn / wheel, on top of the preset
local cam = { dist = 250, pitch = 47, fov = 45 }   -- where the camera is now (easing)
local SETTINGS = "settings.txt"
local MAX_SPRITES = 64 * 6
local VERTEX_SIZE = ffi.sizeof("Ff1Vertex")

local core
local screen_data, screen                  -- the 256x240 picture
local atlas_data, atlas                    -- overworld metatiles
local spr_data, spr_atlas                  -- the frame's sprites
local scratch, scratch_verts               -- chunk meshing output
local chunks = {}                          -- [cz * 16 + cx] = { key, mesh }
local sprite_buf, sprite_mesh
local prop_buf, prop_mesh                  -- a standard map's objects as cards
local MAX_PROPS = 49 * 49 * 6
local shader
local shadow_shader, shadow_mesh, shadow_buf
local proj_m, view_m
local fx = ffi.new("float[12]")            -- the frame's screen effects (ff1_view3d_fx)
local fx_mono = 0
local battle_data, battle_atlas, battle_buf, battle_mesh, overlay_data, overlay
local battle_info = ffi.new("float[11]")
local BATTLE_VERTS = 1200
-- The battle cameras: fixed shots, one picked at random as a battle
-- starts; bosses always get the boss shot.  (Stage axes: x across, z
-- depth, y up; the target is among the enemies; `a` the angle round it
-- from straight behind the party, `ey` the camera's height.)
local BATTLE_CAMS = {
    { name = "right shoulder", tx = 88, ty = 12, tz = 72, d = 340, a = 13,  ey = 64 },
    { name = "left shoulder",  tx = 112, ty = 12, tz = 72, d = 370, a = -14, ey = 64 },
    { name = "high",           tx = 92, ty = 8,  tz = 72, d = 360, a = 4,   ey = 150 },
    { name = "low side",       tx = 72, ty = 20, tz = 72, d = 370, a = 28,  ey = 42 },
}
-- Where the party stands on the stage (draw_character's rows and x, the
-- stage's axes): rows across at 60 + 24 * i, depth about $B8, 24 tall.
local PARTY_ACROSS0, PARTY_ROW, PARTY_Z, PARTY_H = 60, 24, 0xB8, 24
local BATTLE_FOV = 40
local BOSS_CAM = { name = "boss", tx = 62, ty = 36, tz = 64, d = 400, a = 36, ey = 30 }
local battle_cam                           -- this battle's shot (nil between battles)
local force_battlecam                      -- --battlecam=N
local battle_t0 = 0                        -- when this battle's shot began (its camera move)
local start_battle                         -- --battle=N
local battle_wait                          -- (frames before it, after --enter)
local no_hud                               -- --nohud: no status lines (for pictures)
local set_classes                          -- --classes=A,B,C,D
local enter_id                             -- --enter=N
local inroom_at                            -- --inroom: frames until the party is "in a room"
local dialogue_at, dialogue_id             -- --dialogue=N
local refs_at                              -- --landmark-refs: frames until they're written
local write_landmark_refs                  -- (below)
local source, audio_tmp
local acc = 0
local view3d = true
local yaw = 0
local shot_frames                          -- --shot: capture the screen, then quit
local place                                -- --at=X,Y
local shot_t0
local auto_a                               -- --auto: tap A every 16 frames

local VERTEX_FORMAT = {
    { "VertexPosition", "float", 3 },
    { "VertexTexCoord", "float", 2 },
    { "VertexColor", "byte", 4 },
}

local SHADER = [[
uniform mat4 proj;
uniform mat4 view;
uniform vec3 offset;
uniform vec3 focus;
uniform vec3 sky;
uniform float view_dist;
uniform vec3 tint;
uniform float mono;
uniform float time;
uniform float water;
uniform vec3 daylight;
uniform float outline;     // > 0: cutouts get a dark rim (this many texels of atlas: 1 / size)
varying vec3 wpos;
#ifdef VERTEX
vec4 position(mat4 transform_projection, vec4 vertex_position) {
    wpos = vertex_position.xyz + offset;
    return proj * view * vec4(wpos, 1.0);
}
#endif
#ifdef PIXEL
vec4 effect(vec4 color, Image tex, vec2 uv, vec2 screen_coords) {
    vec4 c = Texel(tex, uv) * color;
    if (c.a < 0.5) {
        // A dark rim round the cutouts: air next to a drawn pixel.
        if (outline <= 0.0) discard;
        float n = Texel(tex, uv + vec2(outline, 0.0)).a + Texel(tex, uv - vec2(outline, 0.0)).a
                + Texel(tex, uv + vec2(0.0, outline)).a + Texel(tex, uv - vec2(0.0, outline)).a;
        if (n < 0.5) discard;
        return vec4(0.04, 0.04, 0.07, 1.0);
    }
    float fog = smoothstep(view_dist * 0.7, view_dist, distance(wpos.xz, focus.xz));
    vec3 rgb = mix(c.rgb, sky, fog);
    if (water > 0.5 && wpos.y < 1.5) {                              // (the water: the lowest ground)
        float s = sin(time * 1.7 + wpos.x * 0.21 + wpos.z * 0.13) * sin(time * 1.1 - wpos.x * 0.07 + wpos.z * 0.19);
        rgb *= 1.0 + 0.12 * s;
    }
    rgb = mix(rgb, vec3(dot(rgb, vec3(0.3, 0.59, 0.11))), mono);   // (monochrome: $2001 bit 0)
    return vec4(rgb * tint * daylight, 1.0);                        // (colour emphasis, time of day)
}
#endif
]]

-- Soft shadows under the sprites (blended, so a shader of their own).
local SHADOW_SHADER = [[
uniform mat4 proj;
uniform mat4 view;
uniform vec3 focus;
uniform float view_dist;
varying vec3 wpos;
#ifdef VERTEX
vec4 position(mat4 transform_projection, vec4 vertex_position) {
    wpos = vertex_position.xyz;
    return proj * view * vertex_position;
}
#endif
#ifdef PIXEL
vec4 effect(vec4 color, Image tex, vec2 uv, vec2 screen_coords) {
    float fog = smoothstep(view_dist * 0.7, view_dist, distance(wpos.xz, focus.xz));
    return vec4(0.0, 0.0, 0.0, Texel(tex, uv).a * 0.45 * (1.0 - fog));
}
#endif
]]
local MAX_SHADOWS = 16

-- Tilt-shift: a separable blur that grows with the distance from the
-- screen's middle row (where the party is), HD-2D's miniature look.
local BLUR_SHADER = [[
uniform vec2 dir;          // one texel along the pass
uniform float strength;
vec4 effect(vec4 color, Image tex, vec2 uv, vec2 screen_coords) {
    float a = smoothstep(0.12, 0.48, abs(uv.y - 0.5)) * strength;
    vec4 sum = vec4(0.0);
    float wsum = 0.0;
    for (int i = -4; i <= 4; i++) {
        float w = exp(-float(i * i) / 8.0);
        sum += Texel(tex, uv + dir * float(i) * a * 1.6) * w;
        wsum += w;
    }
    return sum / wsum * color;
}
]]

-- The extras (not in the original), each on its own key, remembered.
local opt = { tiltshift = true, shimmer = true, daynight = false, battlemove = true, encounters = true }
local BATTLE_SPEEDS = { 1, 2, 4 }          -- F10: frames of battle per frame
local battle_speed_i = 1
local scene_canvas, blur_canvas, blur_shader

-- Day / night: the light and the sky through a six-minute day.
local DAY_PERIOD = 360
local day_offset = 0               -- --daytime=F: start at that time of day (0..1)
local DAY_KEYS = {
    { 0.00, { 1, 1, 1 },          { 0.42, 0.62, 0.95 } },
    { 0.45, { 1, 1, 1 },          { 0.42, 0.62, 0.95 } },   -- day
    { 0.55, { 1.0, 0.78, 0.62 },  { 0.95, 0.55, 0.35 } },   -- dusk
    { 0.65, { 0.42, 0.48, 0.78 }, { 0.05, 0.07, 0.20 } },   -- night
    { 0.88, { 0.42, 0.48, 0.78 }, { 0.05, 0.07, 0.20 } },
    { 0.95, { 1.0, 0.86, 0.76 },  { 0.90, 0.66, 0.56 } },   -- dawn
    { 1.00, { 1, 1, 1 },          { 0.42, 0.62, 0.95 } },
}

local function time_of_day()
    if not opt.daynight then return { 1, 1, 1 }, nil end
    local t = (love.timer.getTime() / DAY_PERIOD + day_offset) % 1
    for i = 1, #DAY_KEYS - 1 do
        local a, b = DAY_KEYS[i], DAY_KEYS[i + 1]
        if t <= b[1] then
            local k = (t - a[1]) / (b[1] - a[1])
            local function mix(x, y) return { x[1] + (y[1] - x[1]) * k, x[2] + (y[2] - x[2]) * k, x[3] + (y[3] - x[3]) * k } end
            return mix(a[2], b[2]), mix(a[3], b[3])
        end
    end
    return { 1, 1, 1 }, nil
end

local function ensure_canvases(w, h)
    if scene_canvas and scene_canvas:getWidth() == w and scene_canvas:getHeight() == h then return end
    if scene_canvas then scene_canvas:release(); blur_canvas:release() end
    scene_canvas = love.graphics.newCanvas(w, h)
    blur_canvas = love.graphics.newCanvas(w, h)
end

-- Draw the scene image to the screen through the two blur passes.
local function tilt_shift(w, h)
    love.graphics.setCanvas(blur_canvas)
    love.graphics.setShader(blur_shader)
    blur_shader:send("strength", 1)
    blur_shader:send("dir", { 1 / w, 0 })
    love.graphics.draw(scene_canvas)
    love.graphics.setCanvas()
    blur_shader:send("dir", { 0, 1 / h })
    love.graphics.draw(blur_canvas)
    love.graphics.setShader()
end

local SKY = { 0.42, 0.62, 0.95 }

-- Where the save, settings and ROM copy live (data_dir): in a release, the
-- program's own folder when it can be written to (a portable folder), else
-- LOVE's save folder.  Read and written with io, as LOVE can only write to
-- its save folder.
data_dir = nil

local function read_file(path)
    local f = io.open(path, "rb")
    if not f then return nil end
    local s = f:read("*a")
    f:close()
    return s
end

local function write_file(path, s)
    local f = io.open(path, "wb")
    if not f then return false end
    f:write(s)
    f:close()
    return true
end

local function pick_data_dir(dev)
    local saves = love.filesystem.getSaveDirectory()
    if dev then return saves end
    local base = love.filesystem.getSourceBaseDirectory()
    if not write_file(base .. "/.write-test", "") then return saves end
    os.remove(base .. "/.write-test")
    -- (from an earlier release that kept them in the save folder)
    for _, name in ipairs({ "ff1.sav", SETTINGS, "rom.nes" }) do
        local old = read_file(saves .. "/" .. name)
        if old and not read_file(base .. "/" .. name) then write_file(base .. "/" .. name, old) end
    end
    return base
end

local function save_settings()
    write_file(data_dir .. "/" .. SETTINGS,
          string.format("preset=%d\nview=%d\ntiltshift=%d\nshimmer=%d\ndaynight=%d\nbattlemove=%d\nencounters=%d\nbattlespeed=%d\n",
                        preset_i, view_i, opt.tiltshift and 1 or 0, opt.shimmer and 1 or 0,
                        opt.daynight and 1 or 0, opt.battlemove and 1 or 0, opt.encounters and 1 or 0,
                        battle_speed_i))
end

local function load_settings()
    local text = read_file(data_dir .. "/" .. SETTINGS)
    if not text then return end
    local p = tonumber(text:match("preset=(%d+)"))
    local v = tonumber(text:match("view=(%d+)"))
    if p and PRESETS[p] then preset_i = p end
    if v and VIEWS[v] then view_i = v end
    local bs = tonumber(text:match("battlespeed=(%d)"))
    if bs and BATTLE_SPEEDS[bs] then battle_speed_i = bs end
    for _, k in ipairs({ "tiltshift", "shimmer", "daynight", "battlemove", "encounters" }) do
        local x = text:match(k .. "=(%d)")
        if x then opt[k] = x == "1" end
    end
    VIEW = VIEWS[view_i][2]
    local pr = PRESETS[preset_i]
    cam.dist, cam.pitch, cam.fov = pr.dist, pr.pitch, pr.fov
end

-- ---- The ROM ----

ROM_COPY = "rom.nes"               -- the player's ROM, kept in data_dir
started = false                    -- the game is running (a ROM was found)
rom_prompt = nil                   -- asking for the ROM: "" or why the last one failed

function start_game()
    if core.ff1_init(save_file, start_new) ~= 0 then error("ff1_init failed") end
    core.ff1_set_encounters(opt.encounters and 1 or 0)
    started, rom_prompt = true, nil
    set_icon()
end

-- The window's icon: the Black Mage (class 5), from the player's ROM, its
-- 16 x 24 sprite centred in a square and scaled up 2x.
function set_icon()
    local spr = ffi.new("uint8_t[?]", 16 * 24 * 4)
    core.ff1_class_sprite(5, spr)
    local icon = love.image.newImageData(48, 48)
    for y = 0, 23 do
        for x = 0, 15 do
            local p = spr + (y * 16 + x) * 4
            if p[3] > 0 then
                for d = 0, 3 do
                    icon:setPixel(8 + x * 2 + d % 2, y * 2 + math.floor(d / 2),
                                  p[0] / 255, p[1] / 255, p[2] / 255, 1)
                end
            end
        end
    end
    love.window.setIcon(icon)
end

-- A file dropped on the window: if it's Final Fantasy (USA), keep a copy
-- and start; else say why not.
function love.filedropped(file)
    if started then return end
    file:open("r")
    local data = file:read()
    file:close()
    local path = data_dir .. "/" .. ROM_COPY .. ".new"
    write_file(path, data)
    local status = core.ff1_load_rom(path)
    os.remove(path)
    if status == 0 then
        write_file(data_dir .. "/" .. ROM_COPY, data)
        start_game()
    else
        rom_prompt = ffi.string(core.ff1_rom_message(status))
    end
end

local function draw_rom_prompt()
    local w, h = love.graphics.getDimensions()
    love.graphics.clear(0.02, 0.03, 0.12, 1)
    local lines = {
        "FINAL FANTASY",
        "",
        "This game needs your copy of Final Fantasy (USA) for the NES, as a ROM file.",
        "Drag it onto this window.",
        "",
        "It is checked, then kept in the game's save folder for next time.",
    }
    if rom_prompt ~= "" then
        lines[#lines + 1] = ""
        lines[#lines + 1] = "That file can't be used: " .. rom_prompt
    end
    local y = h / 2 - #lines * 12
    for _, l in ipairs(lines) do
        love.graphics.printf(l, 0, y, w, "center")
        y = y + 24
    end
end

function love.load(args)
    local src = love.filesystem.getSource()
    -- The game library: beside the program in a release (fused), else the
    -- development build next to the project.
    local release = love.filesystem.getSourceBaseDirectory() .. "/ff1core.dll"
    local build = src .. "/../build/"
    local dev = love.filesystem.getInfo ~= nil and io.open(build .. "ff1core.dll", "rb")
    if dev then dev:close() end
    core = ffi.load(dev and (build .. "ff1core.dll") or release)
    data_dir = pick_data_dir(dev)
    load_settings()
    local new_game = 0
    for _, a in ipairs(args) do
        if a == "--new" then new_game = 1 end
        if a == "--shot" then new_game, shot_frames = 1, shot_frames or 150 end
        local fr = a:match("^%-%-frames=(%d+)$")
        if fr then shot_frames = tonumber(fr) end
        if a == "--auto" then auto_a = 0 end
        if a == "--inroom" then inroom_at = 120 end
        if a == "--2d" then view3d = false end
        if a == "--nohud" then no_hud = true end
        local cl = a:match("^%-%-classes=([%d,]+)$")
        if cl then set_classes = {}; for c in cl:gmatch("%d+") do set_classes[#set_classes + 1] = tonumber(c) end end
        if a == "--ask-rom" then ask_rom = true end
        local dp = a:match("^%-%-drop=(.+)$")              -- (as if this file were dropped)
        if dp then ask_rom, drop_path = true, dp end
        if a == "--landmark-refs" then new_game, refs_at = 1, 90 end
        local dl = a:match("^%-%-dialogue=(%w+)$")
        if dl then dialogue_at, dialogue_id = 120, tonumber(dl) end
        local x, y = a:match("^%-%-at=(%w+),(%w+)$")
        if x then place = place or {}; place[1], place[2] = tonumber(x), tonumber(y) end
        local v = a:match("^%-%-vehicle=(%d+)$")
        if v then place = place or {}; place[3] = tonumber(v) end
        local en = a:match("^%-%-enter=(%w+)$")
        if en then enter_id = tonumber(en) end
        local bc = a:match("^%-%-battlecam=(%d)$")
        if bc then force_battlecam = tonumber(bc) end
        local bn = a:match("^%-%-battle=(%w+)$")
        if bn then start_battle = tonumber(bn) end
        local cm = a:match("^%-%-camera=(%d)$")
        if cm and PRESETS[tonumber(cm)] then
            preset_i = tonumber(cm)
            local pr = PRESETS[preset_i]
            cam.dist, cam.pitch, cam.fov = pr.dist, pr.pitch, pr.fov
        end
        local dt_ = a:match("^%-%-daytime=([%d%.]+)$")
        if dt_ then day_offset = tonumber(dt_); opt.daynight = true end
        local dd = a:match("^%-%-dist=(%d+)$")
        if dd then zoom = tonumber(dd) / PRESETS[preset_i].dist; cam.dist = tonumber(dd) end
        local deg = a:match("^%-%-yaw=(%-?%d+)$")
        if deg then yaw = math.rad(tonumber(deg)) end
    end
    -- The game's data comes from the player's ROM: the copy kept from an
    -- earlier run, $FF1_ROM, or the development one beside the project;
    -- else the player is asked for it (love.filedropped).
    -- The save: the development build's (shared with the SDL build), or
    -- data_dir in a release.
    save_file = dev and (build .. "ff1.sav") or (data_dir .. "/ff1.sav")
    start_new = new_game
    local tries = { data_dir .. "/" .. ROM_COPY, os.getenv("FF1_ROM"),
                    src .. "/../../FinalFantasy.nes", src .. "/../FinalFantasy.nes" }
    for _, path in pairs(tries) do
        if ask_rom then break end                  -- (--ask-rom: show the prompt)
        if path and core.ff1_load_rom(path) == 0 then start_game(); break end
    end
    if not started then rom_prompt = "" end
    if drop_path then                              -- (--drop: the same as dropping it)
        local fh = io.open(drop_path, "rb")
        local data = fh and fh:read("*a") or ""
        if fh then fh:close() end
        love.filedropped({ open = function() end, read = function() return data end, close = function() end })
    end

    love.graphics.setDefaultFilter("nearest", "nearest")
    screen_data = love.image.newImageData(256, 240, "rgba8")
    screen = love.graphics.newImage(screen_data)
    atlas_data = love.image.newImageData(256, 256, "rgba8")   -- (FF1_ATLAS_W x FF1_ATLAS_H)
    atlas = love.graphics.newImage(atlas_data)
    spr_data = love.image.newImageData(64, 64, "rgba8")
    spr_atlas = love.graphics.newImage(spr_data)

    scratch_verts = 200000
    scratch = love.data.newByteData(scratch_verts * VERTEX_SIZE)
    sprite_buf = love.data.newByteData(MAX_SPRITES * VERTEX_SIZE)
    sprite_mesh = love.graphics.newMesh(VERTEX_FORMAT, MAX_SPRITES, "triangles", "dynamic")
    sprite_mesh:setTexture(spr_atlas)
    prop_buf = love.data.newByteData(MAX_PROPS * VERTEX_SIZE)
    prop_mesh = love.graphics.newMesh(VERTEX_FORMAT, MAX_PROPS, "triangles", "dynamic")
    prop_mesh:setTexture(atlas)
    shader = love.graphics.newShader(SHADER)
    blur_shader = love.graphics.newShader(BLUR_SHADER)

    -- A round soft shadow, darkest in the middle.
    local sd = love.image.newImageData(32, 32)
    sd:mapPixel(function(x, y)
        local dx, dy = (x + 0.5 - 16) / 16, (y + 0.5 - 16) / 16
        local a = math.max(0, 1 - math.sqrt(dx * dx + dy * dy))
        return 0, 0, 0, a ^ 0.8
    end)
    love.graphics.setDefaultFilter("linear", "linear")
    local shadow_img = love.graphics.newImage(sd)
    love.graphics.setDefaultFilter("nearest", "nearest")
    shadow_shader = love.graphics.newShader(SHADOW_SHADER)
    shadow_mesh = love.graphics.newMesh(VERTEX_FORMAT, MAX_SHADOWS * 6, "triangles", "stream")
    shadow_mesh:setTexture(shadow_img)
    shadow_buf = ffi.new("float[?]", MAX_SHADOWS * 4)

    battle_data = love.image.newImageData(512, 512, "rgba8")
    battle_atlas = love.graphics.newImage(battle_data)
    battle_buf = love.data.newByteData(BATTLE_VERTS * VERTEX_SIZE)
    battle_mesh = love.graphics.newMesh(VERTEX_FORMAT, BATTLE_VERTS, "triangles", "stream")
    battle_mesh:setTexture(battle_atlas)
    overlay_data = love.image.newImageData(256, 240, "rgba8")
    overlay = love.graphics.newImage(overlay_data)

    source = love.audio.newQueueableSource(44100, 16, 1, 8)
    audio_tmp = ffi.new("int16_t[8192]")
end

function love.quit()
    if core then core.ff1_quit() end
end

local function buttons()
    local k, b = love.keyboard.isDown, 0
    if k("right") then b = b + 0x01 end
    if k("left") then b = b + 0x02 end
    if k("down") then b = b + 0x04 end
    if k("up") then b = b + 0x08 end
    if k("z", "space") then b = b + 0x10 end
    if k("x", "backspace") then b = b + 0x20 end
    if k("tab", "rshift") then b = b + 0x40 end
    if k("return", "kpenter") then b = b + 0x80 end
    if auto_a then
        auto_a = auto_a + 1
        if auto_a % 16 < 2 then b = b + 0x10 end
    end
    return b
end

function love.update(dt)
    if not started then return end
    acc = math.min(acc + dt, NES_FRAME * 4)
    local n = 0
    while acc >= NES_FRAME do
        acc = acc - NES_FRAME
        -- (F10) Battles run faster: several game frames to a frame; only
        -- the last one's sound is kept, so the music stays in tune.
        local extra = core.ff1_battle3d() == 1 and BATTLE_SPEEDS[battle_speed_i] - 1 or 0
        local b = buttons()
        for _ = 1, extra do core.ff1_frame(b) end
        core.ff1_frame(b)
        if place then core.ff1_debug_place(place[1], place[2], place[3] or 0); place = nil end
        if set_classes then
            for i, c in ipairs(set_classes) do core.ff1_debug_class(i - 1, c) end
            set_classes = nil
        end
        if start_battle then                       -- (with --enter: once the party is in)
            battle_wait = (battle_wait or (enter_id and 120 or 0)) - 1
            if battle_wait < 0 then core.ff1_debug_battle(start_battle); start_battle = nil end
        end
        if enter_id then core.ff1_debug_enter(enter_id); enter_id = nil end
        if inroom_at then
            inroom_at = inroom_at - 1
            if inroom_at == 0 then core.ff1_debug_inroom(); inroom_at = nil end
        end
        if refs_at then
            refs_at = refs_at - 1
            if refs_at == 0 then write_landmark_refs(); love.event.quit() end
        end
        if dialogue_at then
            dialogue_at = dialogue_at - 1
            if dialogue_at == 0 then core.ff1_debug_dialogue(dialogue_id); dialogue_at = nil end
        end
        n = n + core.ff1_audio(audio_tmp + n, 8192 - n)
    end
    -- One buffer per update; dropped when the queue is full (the game ran
    -- ahead of the device), as audio.c does.
    if n > 0 and source:getFreeBufferCount() > 0 then
        local sd = love.sound.newSoundData(n, 44100, 16, 1)
        ffi.copy(sd:getFFIPointer(), audio_tmp, n * 2)
        source:queue(sd)
        source:play()
    end

    if love.keyboard.isDown("q") then yaw = yaw - dt * 1.5 end   -- (the map's camera only)
    if love.keyboard.isDown("e") then yaw = yaw + dt * 1.5 end
    if love.keyboard.isDown("pageup") then zoom = math.max(0.4, zoom - dt * 0.8) end
    if love.keyboard.isDown("pagedown") then zoom = math.min(2.0, zoom + dt * 0.8) end
    local pr, k = PRESETS[preset_i], 1 - math.exp(-dt * 6)   -- (ease towards the preset)
    cam.dist = cam.dist + (pr.dist * zoom - cam.dist) * k
    cam.pitch = cam.pitch + (pr.pitch - cam.pitch) * k
    cam.fov = cam.fov + (pr.fov - cam.fov) * k
end

function love.wheelmoved(_, y)
    zoom = math.min(2.0, math.max(0.4, zoom - y * 0.08))
end

function love.keypressed(key)
    if key == "f2" then view3d = not view3d end
    if key == "c" then
        preset_i = preset_i % #PRESETS + 1
        zoom = 1
        save_settings()
    end
    if key == "f5" then opt.tiltshift = not opt.tiltshift; save_settings() end
    if key == "f6" then opt.shimmer = not opt.shimmer; save_settings() end
    if key == "f7" then opt.daynight = not opt.daynight; save_settings() end
    if key == "f8" then opt.battlemove = not opt.battlemove; save_settings() end
    if key == "f9" then
        opt.encounters = not opt.encounters
        core.ff1_set_encounters(opt.encounters and 1 or 0)
        save_settings()
    end
    if key == "f10" then
        battle_speed_i = battle_speed_i % #BATTLE_SPEEDS + 1
        save_settings()
    end
    if key == "v" then
        view_i = view_i % #VIEWS + 1
        VIEW = VIEWS[view_i][2]
        save_settings()
    end
    if key == "escape" then love.event.quit() end
end

-- ---- Matrices (row-major nested tables, sent with the "row" layout) ----

local function perspective(fovy, aspect, near, far)
    local f = 1 / math.tan(fovy / 2)
    return {
        { f / aspect, 0, 0, 0 },
        { 0, f, 0, 0 },
        { 0, 0, (far + near) / (near - far), 2 * far * near / (near - far) },
        { 0, 0, -1, 0 },
    }
end

local function look_at(ex, ey, ez, tx, ty, tz)
    local fx, fy, fz = tx - ex, ty - ey, tz - ez
    local l = math.sqrt(fx * fx + fy * fy + fz * fz)
    fx, fy, fz = fx / l, fy / l, fz / l
    local sx, sy, sz = -fz, 0, fx                 -- f x (0,1,0)
    l = math.sqrt(sx * sx + sz * sz)
    sx, sz = sx / l, sz / l
    local ux, uy, uz = sy * fz - sz * fy, sz * fx - sx * fz, sx * fy - sy * fx
    return {
        { sx, sy, sz, -(sx * ex + sy * ey + sz * ez) },
        { ux, uy, uz, -(ux * ex + uy * ey + uz * ez) },
        { -fx, -fy, -fz, fx * ex + fy * ey + fz * ez },
        { 0, 0, 0, 1 },
    }
end

-- ---- Chunks ----

-- The mesh of chunk (cx, cz), rebuilt when its tiles change.  Meshing a
-- chunk takes a few milliseconds, so beyond the nearest chunks only
-- `budget` are built per frame (what isn't ready yet is out in the fog).
local budget
local function chunk_mesh(cx, cz, near)
    local id = cz * 16 + cx
    local key = core.ff1_chunk_key(cx, cz)
    local c = chunks[id]
    if c and c.key == key then return c.mesh end
    if not near then
        if budget <= 0 then return c and c.mesh end
        budget = budget - 1
    end
    local n = core.ff1_chunk(cx, cz, ffi.cast("Ff1Vertex*", scratch:getFFIPointer()), scratch_verts)
    if n > scratch_verts then                     -- grow the scratch buffer and redo
        scratch_verts = n
        scratch = love.data.newByteData(scratch_verts * VERTEX_SIZE)
        n = core.ff1_chunk(cx, cz, ffi.cast("Ff1Vertex*", scratch:getFFIPointer()), scratch_verts)
    end
    local mesh
    if n > 0 then
        local data = love.data.newByteData(n * VERTEX_SIZE)
        ffi.copy(data:getFFIPointer(), scratch:getFFIPointer(), n * VERTEX_SIZE)
        mesh = love.graphics.newMesh(VERTEX_FORMAT, n, "triangles", "static")
        mesh:setVertices(data)
        mesh:setTexture(atlas)
    end
    chunks[id] = { key = key, mesh = mesh, verts = n }
    return mesh
end

-- The multiple of the map size that brings `v` closest to `near`.
local function wrap_shift(v, near)
    return math.floor((near - v) / MAP + 0.5) * MAP
end

local function draw3d()
    -- The metatile and sprite pixels are taken every frame (palette
    -- changes, animation).
    core.ff1_terrain_atlas(ffi.cast("uint8_t*", atlas_data:getFFIPointer()))
    atlas:replacePixels(atlas_data)
    local ns = core.ff1_sprites(ffi.cast("Ff1Vertex*", sprite_buf:getFFIPointer()), MAX_SPRITES,
                                ffi.cast("uint8_t*", spr_data:getFFIPointer()), yaw)
    spr_atlas:replacePixels(spr_data)
    if ns > 0 then
        sprite_mesh:setVertices(sprite_buf)
        sprite_mesh:setDrawRange(1, ns)
    end

    -- Another world (the overworld, a map, a room's palette): new chunks.
    local wk = core.ff1_world(world_tiles)
    if wk ~= world_key then
        for _, c in pairs(chunks) do if c.mesh then c.mesh:release() end end
        chunks = {}
        world_key = wk
        MAP = world_tiles[0] * 16
        NCHUNK = world_tiles[0] / 16
    end

    local f = ffi.new("float[3]")
    core.ff1_focus(f)
    local tx, ty, tz = f[0] + fx[10], f[1] + 8 + fx[11], f[2]     -- (the shake)
    local p = math.rad(cam.pitch)
    local ex = tx + math.sin(yaw) * cam.dist * math.cos(p)
    local ez = tz + math.cos(yaw) * cam.dist * math.cos(p)
    local ey = ty + cam.dist * math.sin(p)
    local w, h = love.graphics.getDimensions()

    local light, day_sky = time_of_day()
    local sky = day_sky or SKY
    if fx_mono > 0 then local l = sky[1] * 0.3 + sky[2] * 0.59 + sky[3] * 0.11; sky = { l, l, l } end
    love.graphics.clear(sky[1] * fx[2], sky[2] * fx[3], sky[3] * fx[4], 1, true, true)
    love.graphics.setShader(shader)
    shader:send("tint", { fx[2], fx[3], fx[4] })
    shader:send("mono", fx_mono)
    shader:send("time", love.timer.getTime() % 1000)
    shader:send("water", opt.shimmer and 1 or 0)
    shader:send("daylight", light)
    shader:send("outline", 0)
    proj_m = perspective(math.rad(cam.fov), w / h, 2, 3000)
    if love.graphics.getCanvas() then              -- (an image is drawn upside down: flip y back)
        for c = 1, 4 do proj_m[2][c] = -proj_m[2][c] end
    end
    view_m = look_at(ex, ey, ez, tx, ty, tz)
    shader:send("proj", "row", proj_m)
    shader:send("view", "row", view_m)
    shader:send("focus", { tx, ty, tz })
    shader:send("sky", SKY)
    shader:send("view_dist", VIEW)
    love.graphics.setDepthMode("lequal", true)

    -- The chunks within view, each placed at the copy of the map nearest
    -- the party; one more ring is built ahead of time.
    budget = 1
    local ahead = VIEW + CHUNK * 0.75
    for cz = 0, NCHUNK - 1 do
        local oz = wrap_shift(cz * CHUNK + CHUNK / 2, tz)
        local dz = math.abs(cz * CHUNK + CHUNK / 2 + oz - tz)
        do                                          -- (every row: far chunks are freed)
            for cx = 0, NCHUNK - 1 do
                local ox = wrap_shift(cx * CHUNK + CHUNK / 2, tx)
                local dx = math.abs(cx * CHUNK + CHUNK / 2 + ox - tx)
                local d = math.max(dx, dz)
                local mesh = d < ahead and chunk_mesh(cx, cz, d < CHUNK)
                local c = chunks[cz * 16 + cx]
                if d > ahead + CHUNK and c then             -- far behind: free it
                    if c.mesh then c.mesh:release() end
                    chunks[cz * 16 + cx] = nil
                end
                if mesh and d < VIEW + CHUNK / 2 then
                    shader:send("offset", { ox, 0, oz })
                    love.graphics.draw(mesh)
                end
            end
        end
    end
    -- A standard map's objects (trees, chests, doors...): 2D cards like
    -- the characters.
    local np = core.ff1_props(ffi.cast("Ff1Vertex*", prop_buf:getFFIPointer()), MAX_PROPS, yaw)
    if np > 0 then
        prop_mesh:setVertices(prop_buf)
        prop_mesh:setDrawRange(1, np)
        shader:send("offset", { 0, 0, 0 })
        love.graphics.draw(prop_mesh)
    end

    -- The soft shadows: on the ground, blended, not hiding anything.
    local nsh = core.ff1_shadows(shadow_buf, MAX_SHADOWS)
    if nsh > 0 then
        local verts = {}
        for i = 0, nsh - 1 do
            local x, y, z, r = shadow_buf[i * 4], shadow_buf[i * 4 + 1], shadow_buf[i * 4 + 2], shadow_buf[i * 4 + 3]
            local rz = r * 0.6                        -- an oval, flatter front to back
            local a = { x - r, y, z - rz, 0, 0 }
            local b = { x + r, y, z - rz, 1, 0 }
            local c = { x + r, y, z + rz, 1, 1 }
            local d = { x - r, y, z + rz, 0, 1 }
            for _, v in ipairs({ a, b, c, a, c, d }) do
                verts[#verts + 1] = { v[1], v[2], v[3], v[4], v[5], 1, 1, 1, 1 }
            end
        end
        shadow_mesh:setVertices(verts)
        shadow_mesh:setDrawRange(1, #verts)
        love.graphics.setShader(shadow_shader)
        shadow_shader:send("proj", "row", proj_m)
        shadow_shader:send("view", "row", view_m)
        shadow_shader:send("focus", { tx, ty, tz })
        shadow_shader:send("view_dist", VIEW)
        love.graphics.setDepthMode("lequal", false)
        love.graphics.draw(shadow_mesh)
        love.graphics.setShader(shader)
        love.graphics.setDepthMode("lequal", true)
    end
    if ns > 0 then
        shader:send("offset", { 0, 0, 0 })
        love.graphics.draw(sprite_mesh)
    end
    love.graphics.setDepthMode()
    love.graphics.setShader()
end

-- ---- Reference pictures of the landmarks (--landmark-refs) ----

-- The overworld's landmarks by their (lowest) entrance id.
local LANDMARK_NAMES = {
    [0x00] = "cardia_south_cave", [0x01] = "coneria", [0x02] = "pravoka", [0x03] = "elfland",
    [0x04] = "melmond", [0x05] = "crescent_lake", [0x06] = "gaia", [0x07] = "onrac", [0x08] = "lefein",
    [0x09] = "coneria_castle", [0x0A] = "elfland_castle", [0x0B] = "northwest_castle",
    [0x0C] = "castle_of_ordeals", [0x0D] = "temple_of_fiends", [0x0E] = "earth_cave",
    [0x0F] = "gurgu_volcano", [0x10] = "ice_cave", [0x11] = "cardia_cave", [0x12] = "bahamuts_cave",
    [0x14] = "dwarf_cave", [0x15] = "matoyas_cave", [0x16] = "sardas_cave", [0x17] = "marsh_cave",
    [0x18] = "mirage_tower", [0x19] = "titans_tunnel_west", [0x1A] = "titans_tunnel_east",
    [0x1B] = "cardia_cave_2", [0x1C] = "cardia_cave_3", [0x1D] = "cardia_cave_4",
}

-- Each landmark's tiles as the game draws them, 1 pixel = 1 voxel, and an
-- 8x copy with a line every map tile; and a list of them.
function write_landmark_refs()               -- (the local declared above)
    love.filesystem.createDirectory("landmarks")
    local n = core.ff1_landmark_count()
    local info = ffi.new("int[5]")
    local lines = {
        "Overworld landmarks: reference pictures, 1 pixel = 1 voxel (a map tile is 16 x 16).",
        "name: first tile (x, y), size in tiles, size in pixels / voxels, entrance id",
        "",
    }
    local used = {}
    for i = 0, n - 1 do
        core.ff1_landmark_info(i, info)
        local tx, ty, w, h, ent = info[0], info[1], info[2], info[3], info[4]
        local name = LANDMARK_NAMES[ent] or string.format("landmark_%02X_%02X", tx, ty)
        if used[name] then name = name .. string.format("_%02X_%02X", tx, ty) end
        used[name] = true
        local img = love.image.newImageData(w * 16, h * 16, "rgba8")
        core.ff1_landmark_pixels(i, ffi.cast("uint8_t*", img:getFFIPointer()))
        img:encode("png", "landmarks/" .. name .. ".png")
        local big = love.image.newImageData(w * 128, h * 128, "rgba8")
        big:mapPixel(function(x, y)
            local r, g, b, a = img:getPixel(math.floor(x / 8), math.floor(y / 8))
            if x % 128 == 0 or y % 128 == 0 then         -- the map's tile lines
                return r * 0.4 + 0.6, g * 0.4, b * 0.4, 1
            end
            return r, g, b, a
        end)
        big:encode("png", "landmarks/" .. name .. "_x8.png")
        lines[#lines + 1] = string.format("%-20s tile (%3d, %3d) = ($%02X, $%02X), %d x %d tiles, %d x %d voxels, entrance $%02X",
            name, tx, ty, tx, ty, w, h, w * 16, h * 16, ent < 0 and 0 or ent)
    end
    love.filesystem.write("landmarks/landmarks.txt", table.concat(lines, "\n") .. "\n")
    print(string.format("wrote %d landmarks to %s/landmarks", n, love.filesystem.getSaveDirectory()))
end

-- ---- The battle diorama ----

-- The 3D stage fills where the enemy and party boxes were (the top-left
-- 200 x 144 of the 256 x 240 picture); the menus and status are drawn
-- over the rest.  This shifts the projection's centre into that area.
local STAGE_NDC_X, STAGE_NDC_Y = (100 / 256) * 2 - 1, 1 - (72 / 240) * 2

-- The camera of shot `c` looking at target (tx, ty, tz) from distance d.
local function battle_camera(c, tx, ty, tz, d)
    local a = math.rad(c.a)
    return tx + math.sin(a) * d, c.ey, tz + math.cos(a) * d
end

-- The stage area on screen, in normalised device coordinates: its middle
-- (where the projection is centred) and half sizes.
local STAGE_HW, STAGE_HH = 200 / 256, 144 / 240

-- Frame shot `c` for this battle: slide the target across until the
-- party's middle is in the middle of the stage area, then pull back until
-- the party and the enemies (info: their extent) all fit, with a margin.
local function frame_battle(c, info, stagger, aspect)
    local function mul(m, v)
        return { m[1][1] * v[1] + m[1][2] * v[2] + m[1][3] * v[3] + m[1][4] * v[4],
                 m[2][1] * v[1] + m[2][2] * v[2] + m[2][3] * v[3] + m[2][4] * v[4],
                 m[3][1] * v[1] + m[3][2] * v[2] + m[3][3] * v[3] + m[3][4] * v[4],
                 m[4][1] * v[1] + m[4][2] * v[2] + m[4][3] * v[3] + m[4][4] * v[4] }
    end
    local proj = perspective(math.rad(BATTLE_FOV), aspect, 4, 3000)   -- (centred: the shift is added later)
    local party = {}
    for b = 0, 3 do
        local x, z = PARTY_ACROSS0 + b * PARTY_ROW, PARTY_Z + (b - 1.5) * stagger
        for _, p in ipairs({ { x - 9, 0, z }, { x + 9, 0, z }, { x - 9, PARTY_H, z }, { x + 9, PARTY_H, z } }) do
            party[#party + 1] = p
        end
    end
    -- (the inner 70% of the enemies' extent: a formation rarely fills its corners)
    local enemies = {}
    local cx, cz = (info[6] + info[7]) / 2, (info[8] + info[9]) / 2
    for _, x in ipairs({ info[6], info[7] }) do
        for _, z in ipairs({ info[8], info[9] }) do
            for _, y in ipairs({ 0, info[10] * 0.85 }) do
                enemies[#enemies + 1] = { cx + (x - cx) * 0.7, y, cz + (z - cz) * 0.7 }
            end
        end
    end
    local pcx = PARTY_ACROSS0 + 1.5 * PARTY_ROW
    local tx, ty, tz, d = c.tx, c.ty, c.tz, c.d
    for _ = 1, 12 do
        for _ = 1, 4 do                            -- the party's middle to the middle
            local ex, ey, ez = battle_camera(c, tx, ty, tz, d)
            local v = mul(proj, mul(look_at(ex, ey, ez, tx, ty, tz), { pcx, PARTY_H / 2, PARTY_Z, 1 }))
            local nx = v[1] / v[4]
            local fx, fz = tx - ex, tz - ez
            local l = math.sqrt(fx * fx + fz * fz)
            local rx, rz = -fz / l, fx / l
            local dist = math.sqrt((pcx - ex) ^ 2 + (PARTY_Z - ez) ^ 2)
            local shift = nx * dist * math.tan(math.rad(BATTLE_FOV) / 2) * aspect
            tx, tz = tx + rx * shift, tz + rz * shift
        end
        local ex, ey, ez = battle_camera(c, tx, ty, tz, d)
        local view = look_at(ex, ey, ez, tx, ty, tz)
        local fits = true
        local function check(p, mx, my)
            local v = mul(proj, mul(view, { p[1], p[2], p[3], 1 }))
            if v[4] <= 0 then fits = false; return end
            local nx, ny = v[1] / v[4], v[2] / v[4]
            if math.abs(nx) > STAGE_HW * mx or math.abs(ny) > STAGE_HH * my then fits = false end
        end
        for _, p in ipairs(party) do check(p, 0.8, 0.85) end
        for _, p in ipairs(enemies) do check(p, 0.95, 0.95) end
        if fits then break end
        d = d * 1.08
    end
    return { name = c.name, a = c.a, ey = c.ey, tx = tx, ty = ty, tz = tz, d = d }
end

local function draw_battle3d()
    -- Behind the party, looking at the enemies.  Stage axes: x across
    -- (screen y), z depth (screen x), y up.
    local c = battle_cam
    local tx, ty, tz = c.tx, c.ty, c.tz
    -- (F8) The camera glides into its shot as the battle starts, then
    -- drifts a little: a slow sway, in and out, up and down.
    local d, sway, bob = c.d, 0, 0
    if opt.battlemove then
        local t = love.timer.getTime() - battle_t0
        local k = math.min(1, t / 1.5)
        k = k * k * (3 - 2 * k)                    -- (ease in and out)
        sway = (1 - k) * 16 + 2.5 * math.sin(t * 0.45) + 1.0 * math.sin(t * 0.17)
        d = c.d * (1 + (1 - k) * 0.22 + 0.025 * math.sin(t * 0.31))
        bob = 2.5 * math.sin(t * 0.37)
    end
    local ex, ey, ez = battle_camera({ a = c.a + sway, ey = c.ey + bob }, tx, ty, tz, d)
    local fx, fz = tx - ex, tz - ez
    local l = math.sqrt(fx * fx + fz * fz)
    local rx, rz = -fz / l, fx / l
    local n = core.ff1_battle_scene(ffi.cast("Ff1Vertex*", battle_buf:getFFIPointer()), BATTLE_VERTS,
                                    ffi.cast("uint8_t*", battle_data:getFFIPointer()), rx, rz, battle_info)
    battle_atlas:replacePixels(battle_data)
    ex = ex + battle_info[0] * 0.6                -- BattleScreenShake shakes the camera
    ey = ey + battle_info[1] * 0.6

    local w, h = love.graphics.getDimensions()
    local bg = battle_info[2] >= 0 and { battle_info[2], battle_info[3], battle_info[4] } or { 0, 0, 0 }
    love.graphics.clear(bg[1], bg[2], bg[3], 1, true, true)
    if n > 0 then
        battle_mesh:setVertices(battle_buf)
        battle_mesh:setDrawRange(1, n)
        local p = perspective(math.rad(BATTLE_FOV), w / h, 4, 3000)
        p[1][3] = -STAGE_NDC_X                     -- (off-centre: x' = x + s * w)
        p[2][3] = -STAGE_NDC_Y
        love.graphics.setShader(shader)
        shader:send("proj", "row", p)
        shader:send("view", "row", look_at(ex, ey, ez, tx, ty, tz))
        shader:send("offset", { 0, 0, 0 })
        shader:send("focus", { tx, ty, tz })
        shader:send("sky", bg)
        shader:send("view_dist", 100000)
        shader:send("tint", { 1, 1, 1 })
        shader:send("mono", 0)
        shader:send("time", 0)
        shader:send("water", 0)
        shader:send("daylight", { 1, 1, 1 })
        shader:send("outline", 1 / 512)
        love.graphics.setDepthMode("lequal", true)
        love.graphics.draw(battle_mesh)
        love.graphics.setDepthMode()
        love.graphics.setShader()
    end

    -- The menus and the status column, as drawn.
    ffi.copy(overlay_data:getFFIPointer(), screen_data:getFFIPointer(), 256 * 240 * 4)
    core.ff1_battle_overlay(ffi.cast("uint8_t*", overlay_data:getFFIPointer()))
    overlay:replacePixels(overlay_data)
    local s = math.min(w / 256, h / 240)
    love.graphics.draw(overlay, (w - 256 * s) / 2, (h - 240 * s) / 2, 0, s, s)
end

local function draw2d()
    local w, h = love.graphics.getDimensions()
    local s = math.min(w / 256, h / 240)
    love.graphics.clear(0, 0, 0, 1)
    love.graphics.draw(screen, (w - 256 * s) / 2, (h - 240 * s) / 2, 0, s, s)
end

function love.draw()
    if not started then
        draw_rom_prompt()
        if shot_frames then                        -- (--shot: the prompt is the picture)
            shot_frames = shot_frames - 1
            if shot_frames <= 0 then
                love.graphics.captureScreenshot(function(img)
                    img:encode("png", "shot.png")
                    print("saved " .. love.filesystem.getSaveDirectory() .. "/shot.png")
                    love.event.quit()
                end)
                shot_frames = nil
            end
        end
        return
    end
    core.ff1_draw(ffi.cast("uint8_t*", screen_data:getFFIPointer()))
    screen:replacePixels(screen_data)
    if view3d and core.ff1_view3d() == 1 then
        core.ff1_view3d_fx(fx)
        fx_mono = fx[5]
        local w, h = love.graphics.getDimensions()
        local s = math.min(w / 256, h / 240)
        local ox, oy = (w - 256 * s) / 2, (h - 240 * s) / 2
        if opt.tiltshift then                      -- (into the scene image, blurred onto the screen)
            ensure_canvases(w, h)
            love.graphics.setCanvas({ scene_canvas, depth = true })
        end
        draw3d()
        if fx[8] > 0 then                          -- the altar's beam: that part again, in grey
            love.graphics.setScissor(ox + fx[6] * s, oy + fx[7] * s, (fx[8] - fx[6]) * s, (fx[9] - fx[7]) * s)
            fx_mono = 1
            draw3d()
            love.graphics.setScissor()
        end
        if opt.tiltshift then
            love.graphics.setCanvas()
            tilt_shift(w, h)
        end
        if fx[0] > 0 or fx[1] < 240 then           -- the screen wipe: only these rows show
            love.graphics.setColor(0, 0, 0, 1)
            love.graphics.rectangle("fill", 0, 0, w, oy + fx[0] * s)
            love.graphics.rectangle("fill", 0, oy + (fx[0] + fx[1]) * s, w, h)
            love.graphics.setColor(1, 1, 1, 1)
        end
        -- The dialogue box, as drawn, over the world.
        ffi.copy(overlay_data:getFFIPointer(), screen_data:getFFIPointer(), 256 * 240 * 4)
        if core.ff1_view3d_overlay(ffi.cast("uint8_t*", overlay_data:getFFIPointer())) ~= 0 then
            overlay:replacePixels(overlay_data)
            local w, h = love.graphics.getDimensions()
            local s = math.min(w / 256, h / 240)
            love.graphics.draw(overlay, (w - 256 * s) / 2, (h - 240 * s) / 2, 0, s, s)
        end
    elseif view3d and core.ff1_battle3d() == 1 then
        if not battle_cam then                     -- a battle begins: pick its shot
            local n = core.ff1_battle_scene(ffi.cast("Ff1Vertex*", battle_buf:getFFIPointer()), BATTLE_VERTS,
                                            ffi.cast("uint8_t*", battle_data:getFFIPointer()), 1, 0, battle_info)
            local shot
            if force_battlecam then shot = BATTLE_CAMS[force_battlecam] or BOSS_CAM
            elseif battle_info[5] > 0 then shot = BOSS_CAM
            else shot = BATTLE_CAMS[love.math.random(#BATTLE_CAMS)] end
            -- The party on a diagonal that fans out from this side; then
            -- the shot framed round this party and these enemies.
            local stagger = shot.a > 4 and -12 or shot.a < -4 and 12 or 0
            core.ff1_battle_stagger(stagger)
            local w, h = love.graphics.getDimensions()
            battle_cam = frame_battle(shot, battle_info, stagger, w / h)
            battle_t0 = love.timer.getTime()
        end
        draw_battle3d()
    else draw2d() end
    if core.ff1_battle3d() ~= 1 then battle_cam = nil end
    if shot_frames then
        if shot_frames == 60 then shot_t0 = love.timer.getTime() end
        shot_frames = shot_frames - 1
        if shot_frames == 1 and shot_t0 then
            local verts, nc = 0, 0
            for _, c in pairs(chunks) do verts = verts + c.verts; nc = nc + 1 end
            print(string.format("last 59 frames: %.1f ms/frame; %d chunks, %d vertices",
                (love.timer.getTime() - shot_t0) * 1000 / 59, nc, verts))
        end
        if shot_frames == 0 then
            love.graphics.captureScreenshot(function(img)
                img:encode("png", "shot.png")
                print("saved " .. love.filesystem.getSaveDirectory() .. "/shot.png")
                love.event.quit()
            end)
        end
    end
    if no_hud then return end
    love.graphics.print(string.format("%s  %s camera, %s view  %d fps  (F2 2D/3D, C camera, V view, Q/E turn, PgUp/PgDn zoom)",
        view3d and "3D" or "2D", PRESETS[preset_i].name, VIEWS[view_i][1], love.timer.getFPS()), 8, 8)
    love.graphics.print(string.format("extras: F5 tilt-shift %s, F6 water %s, F7 day/night %s, F8 battle camera move %s, F9 encounters %s, F10 battle speed %dx",
        opt.tiltshift and "on" or "off", opt.shimmer and "on" or "off", opt.daynight and "on" or "off",
        opt.battlemove and "on" or "off", opt.encounters and "on" or "off", BATTLE_SPEEDS[battle_speed_i]), 8, 24)
end
