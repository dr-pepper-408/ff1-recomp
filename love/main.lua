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
  --teleport=N (take normal teleport N: a dungeon's floors; see --tour),
  --inroom (with --enter: as if through a door, the rooms' insides shown),
  --dialogue=N (show dialogue N, as talking does), --2d (start in 2D),
  --nohud (no status lines at the top, for pictures),
  --tour (every standard map in turn: ] / [ next / previous),
  --tour-shots (a picture of each map in the save folder's tour/, then quit),
  --record=NAME (a 1024 x 960 video, videos/NAME.mp4; --rec-frames=N long,
  --rec-size=WxH, --rec-skip=N frames not kept first),
  --walk=U32,W60,R16,A1... (buttons held for N frames: U D L R A B, W waits),
  --spin=DEG (the map camera turns DEG degrees a second),
  --classes=A,B,C,D (the party's classes: 0-5, 6-11 after the class change),
  --level=N (the party raised to level N, as battles' EXP would),
  --kit=N (stage N's equipment and spells: 1 Lich ... 5 Chaos),
  --cast=A,B,C,D / --heal=A,B,C,D (battle commands chosen for the party:
  each one's healing spell on an ally under half HP, else its attack
  spell, else FIGHT; spell ids, -1 none),
  --ask-rom (ask for the ROM even if one is found), --drop=FILE (as if
  FILE were dropped on the ROM prompt), --landmark-refs (write
  reference pictures of the overworld's landmarks to the save folder's
  landmarks/, for modelling them, then quit).

  The files: state.lua (what they share), ff1core.lua (the library's
  functions), settings.lua (where files live, the settings), rom.lua (the
  ROM prompt, starting the game), world3d.lua and battle3d.lua (the 3D
  view), effects.lua (day / night, tilt-shift), shaders.lua, matrix.lua,
  landmarks.lua (--landmark-refs), tour.lua (--tour).
]]

local ffi = require("ffi")
local load_core = require("ff1core")
local S = require("state")
local settings = require("settings")
local rom = require("rom")
local effects = require("effects")
local world3d = require("world3d")
local battle3d = require("battle3d")
local landmarks = require("landmarks")
local tour = require("tour")
local record = require("record")

local NES_FRAME = 16639267 / 1e9   -- 60.0988 Hz (config.h NES_FRAME_NS)

local source, audio_tmp
local acc = 0

-- The test options (see above).
local start_battle                         -- --battle=N
local battle_wait                          -- (frames before it, after --enter)
local no_hud                               -- --nohud: no status lines (for pictures)
local set_classes                          -- --classes=A,B,C,D
local set_level                            -- --level=N
local set_kit                              -- --kit=N
local pilot                                -- --cast / --heal: { attack = {..}, heal = {..} }
local enter_id                             -- --enter=N
local teleport_id                          -- --teleport=N
local inroom_at                            -- --inroom: frames until the party is "in a room"
local dialogue_at, dialogue_id             -- --dialogue=N
local refs_at                              -- --landmark-refs: frames until they're written
local shot_frames                          -- --shot: capture the screen, then quit
local place                                -- --at=X,Y
local shot_t0
local auto_a                               -- --auto: tap A every 16 frames
local walk                                 -- --walk: { button, frames } still to press
local spin                                 -- --spin: degrees a second

function love.load(args)
    local src = love.filesystem.getSource()
    -- The game library: beside the program in a release (fused), else the
    -- development build next to the project.
    local release = love.filesystem.getSourceBaseDirectory() .. "/ff1core.dll"
    local build = src .. "/../build/"
    local dev = love.filesystem.getInfo ~= nil and io.open(build .. "ff1core.dll", "rb")
    if dev then dev:close() end
    S.core = load_core(dev and (build .. "ff1core.dll") or release)
    settings.pick_data_dir(dev)
    settings.load()
    local cam, PRESETS = S.cam, S.PRESETS
    local ask_rom, drop_path
    for _, a in ipairs(args) do
        if a == "--new" then rom.new_game = 1 end
        if a == "--shot" then rom.new_game, shot_frames = 1, shot_frames or 150 end
        local fr = a:match("^%-%-frames=(%d+)$")
        if fr then shot_frames = tonumber(fr) end
        if a == "--auto" then auto_a = 0 end
        if a == "--inroom" then inroom_at = 120 end
        if a == "--2d" then S.view3d = false end
        if a == "--nohud" then no_hud = true end
        local rn = a:match("^%-%-record=([%w_%-]+)$")
        if rn then record.start(rn); no_hud = true end
        local rf = a:match("^%-%-rec%-frames=(%d+)$")
        if rf then record.frames = tonumber(rf) end
        local rs = a:match("^%-%-rec%-skip=(%d+)$")
        if rs then record.skip = tonumber(rs) end
        local rw, rh = a:match("^%-%-rec%-size=(%d+)x(%d+)$")
        if rw then record.W, record.H = tonumber(rw), tonumber(rh) end
        local wk = a:match("^%-%-walk=([%w,]+)$")
        if wk then
            walk = {}
            for b, f in wk:gmatch("(%a)(%d+)") do walk[#walk + 1] = { b:upper(), tonumber(f) } end
        end
        local sp = a:match("^%-%-spin=(%-?[%d%.]+)$")
        if sp then spin = math.rad(tonumber(sp)) end
        if a == "--tour" then rom.new_game = 1; tour.start(false) end
        if a == "--tour-shots" then                -- (in daylight; the setting isn't saved)
            rom.new_game, no_hud, S.opt.daynight = 1, true, false
            tour.start(true)
        end
        local kt = a:match("^%-%-kit=(%d)$")
        if kt then set_kit = tonumber(kt) end
        local ck, cl4 = a:match("^%-%-(%a+)=(%-?%d+,%-?%d+,%-?%d+,%-?%d+)$")
        if ck == "cast" or ck == "heal" then
            pilot = pilot or { cast = { -1, -1, -1, -1 }, heal = { -1, -1, -1, -1 } }
            local k = 0
            for v in cl4:gmatch("%-?%d+") do k = k + 1; pilot[ck][k] = tonumber(v) end
        end
        local lv = a:match("^%-%-level=(%d+)$")
        if lv then set_level = tonumber(lv) end
        local cl = a:match("^%-%-classes=([%d,]+)$")
        if cl then set_classes = {}; for c in cl:gmatch("%d+") do set_classes[#set_classes + 1] = tonumber(c) end end
        if a == "--ask-rom" then ask_rom = true end
        local dp = a:match("^%-%-drop=(.+)$")              -- (as if this file were dropped)
        if dp then ask_rom, drop_path = true, dp end
        if a == "--landmark-refs" then rom.new_game, refs_at = 1, 90 end
        local dl = a:match("^%-%-dialogue=(%w+)$")
        if dl then dialogue_at, dialogue_id = 120, tonumber(dl) end
        local x, y = a:match("^%-%-at=(%w+),(%w+)$")
        if x then place = place or {}; place[1], place[2] = tonumber(x), tonumber(y) end
        local v = a:match("^%-%-vehicle=(%d+)$")
        if v then place = place or {}; place[3] = tonumber(v) end
        local tp = a:match("^%-%-teleport=(%w+)$")
        if tp then teleport_id = tonumber(tp) end
        local en = a:match("^%-%-enter=(%w+)$")
        if en then enter_id = tonumber(en) end
        local bc = a:match("^%-%-battlecam=(%d)$")
        if bc then battle3d.force_cam = tonumber(bc) end
        local bn = a:match("^%-%-battle=(%w+)$")
        if bn then start_battle = tonumber(bn) end
        local cm = a:match("^%-%-camera=(%d)$")
        if cm and PRESETS[tonumber(cm)] then
            S.preset_i = tonumber(cm)
            local pr = PRESETS[S.preset_i]
            cam.dist, cam.pitch, cam.fov = pr.dist, pr.pitch, pr.fov
        end
        local dt_ = a:match("^%-%-daytime=([%d%.]+)$")
        if dt_ then effects.day_offset = tonumber(dt_); S.opt.daynight = true end
        local dd = a:match("^%-%-dist=(%d+)$")
        if dd then S.zoom = tonumber(dd) / PRESETS[S.preset_i].dist; cam.dist = tonumber(dd) end
        local deg = a:match("^%-%-yaw=(%-?%d+)$")
        if deg then S.yaw = math.rad(tonumber(deg)) end
    end
    -- The game's data comes from the player's ROM: the copy kept from an
    -- earlier run, $FF1_ROM, or the development one beside the project;
    -- else the player is asked for it (love.filedropped).
    -- The save: the development build's (shared with the SDL build), or
    -- data_dir in a release.
    rom.save_file = dev and (build .. "ff1.sav") or (settings.data_dir .. "/ff1.sav")
    rom.find({ settings.data_dir .. "/" .. rom.ROM_COPY, os.getenv("FF1_ROM"),
               src .. "/../../FinalFantasy.nes", src .. "/../FinalFantasy.nes" }, ask_rom)
    if drop_path then                              -- (--drop: the same as dropping it)
        local fh = io.open(drop_path, "rb")
        local data = fh and fh:read("*a") or ""
        if fh then fh:close() end
        love.filedropped({ open = function() end, read = function() return data end, close = function() end })
    end

    love.graphics.setDefaultFilter("nearest", "nearest")
    S.screen_data = love.image.newImageData(256, 240, "rgba8")
    S.screen = love.graphics.newImage(S.screen_data)
    world3d.load()
    battle3d.load()
    effects.load()
    record.load()

    source = love.audio.newQueueableSource(44100, 16, 1, 8)
    audio_tmp = ffi.new("int16_t[8192]")
end

function love.quit()
    if S.core then S.core.ff1_quit() end
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
    if walk and walk[1] then                     -- (--walk: the script's button, held)
        local step = walk[1]
        local bits = { U = 0x08, D = 0x04, L = 0x02, R = 0x01, A = 0x10, B = 0x20, W = 0 }
        b = b + (bits[step[1]] or 0)
        step[2] = step[2] - 1
        if step[2] <= 0 then table.remove(walk, 1) end
    end
    if auto_a then
        auto_a = auto_a + 1
        if auto_a % 16 < 2 then b = b + 0x10 end
    end
    return b
end

function love.update(dt)
    if not S.started then return end
    local core = S.core
    acc = record.on and NES_FRAME or math.min(acc + dt, NES_FRAME * 4)   -- (--record: one frame a picture)
    local n = 0
    while acc >= NES_FRAME do
        acc = acc - NES_FRAME
        -- (F10) Battles run faster: several game frames to a frame; only
        -- the last one's sound is kept, so the music stays in tune.
        local extra = core.ff1_battle3d() == 1 and S.BATTLE_SPEEDS[S.battle_speed_i] - 1 or 0
        local b = buttons()
        for _ = 1, extra do core.ff1_frame(b) end
        core.ff1_frame(b)
        if place then core.ff1_debug_place(place[1], place[2], place[3] or 0); place = nil end
        if set_classes then
            for i, c in ipairs(set_classes) do core.ff1_debug_class(i - 1, c) end
            set_classes = nil
        end
        if set_level then core.ff1_debug_level(set_level); set_level = nil end   -- (after --classes)
        if set_kit then core.ff1_debug_kit(set_kit); set_kit = nil end          -- (after --level)
        if pilot then
            core.ff1_debug_autopilot(1, ffi.new("int[4]", pilot.cast), ffi.new("int[4]", pilot.heal))
            pilot = nil
        end
        if start_battle then                       -- (with --enter: once the party is in)
            battle_wait = (battle_wait or (enter_id and 120 or 0)) - 1
            if battle_wait < 0 then core.ff1_debug_battle(start_battle); start_battle = nil end
        end
        if enter_id then core.ff1_debug_enter(enter_id); enter_id = nil end
        if teleport_id and core.ff1_debug_teleport(teleport_id) ~= 0 then teleport_id = nil end   -- (once walking)
        if inroom_at then
            inroom_at = inroom_at - 1
            if inroom_at == 0 then core.ff1_debug_inroom(); inroom_at = nil end
        end
        if refs_at then
            refs_at = refs_at - 1
            if refs_at == 0 then landmarks.write_refs(); love.event.quit() end
        end
        if dialogue_at then
            dialogue_at = dialogue_at - 1
            if dialogue_at == 0 then core.ff1_debug_dialogue(dialogue_id); dialogue_at = nil end
        end
        tour.frame()
        local got = core.ff1_audio(audio_tmp + n, 8192 - n)
        record.audio(audio_tmp + n, got)
        n = n + got
    end
    -- One buffer per update; dropped when the queue is full (the game ran
    -- ahead of the device), as audio.c does.
    if n > 0 and source:getFreeBufferCount() > 0 then
        local sd = love.sound.newSoundData(n, 44100, 16, 1)
        ffi.copy(sd:getFFIPointer(), audio_tmp, n * 2)
        source:queue(sd)
        source:play()
    end

    if spin then S.yaw = S.yaw + spin * (record.on and NES_FRAME or dt) end
    if love.keyboard.isDown("q") then S.yaw = S.yaw - dt * 1.5 end   -- (the map's camera only)
    if love.keyboard.isDown("e") then S.yaw = S.yaw + dt * 1.5 end
    if love.keyboard.isDown("pageup") then S.zoom = math.max(0.4, S.zoom - dt * 0.8) end
    if love.keyboard.isDown("pagedown") then S.zoom = math.min(2.0, S.zoom + dt * 0.8) end
    local cam = S.cam
    local pr, k = S.PRESETS[S.preset_i], 1 - math.exp(-dt * 6)   -- (ease towards the preset)
    cam.dist = cam.dist + (pr.dist * S.zoom - cam.dist) * k
    cam.pitch = cam.pitch + (pr.pitch - cam.pitch) * k
    cam.fov = cam.fov + (pr.fov - cam.fov) * k
end

function love.wheelmoved(_, y)
    S.zoom = math.min(2.0, math.max(0.4, S.zoom - y * 0.08))
end

function love.keypressed(key)
    local opt = S.opt
    if key == "f2" then S.view3d = not S.view3d end
    if key == "c" then
        S.preset_i = S.preset_i % #S.PRESETS + 1
        S.zoom = 1
        settings.save()
    end
    if key == "f5" then opt.tiltshift = not opt.tiltshift; settings.save() end
    if key == "f6" then opt.shimmer = not opt.shimmer; settings.save() end
    if key == "f7" then opt.daynight = not opt.daynight; settings.save() end
    if key == "f8" then opt.battlemove = not opt.battlemove; settings.save() end
    if key == "f9" then
        opt.encounters = not opt.encounters
        S.core.ff1_set_encounters(opt.encounters and 1 or 0)
        settings.save()
    end
    if key == "f10" then
        S.battle_speed_i = S.battle_speed_i % #S.BATTLE_SPEEDS + 1
        settings.save()
    end
    if key == "v" then
        S.view_i = S.view_i % #S.VIEWS + 1
        S.VIEW = S.VIEWS[S.view_i][2]
        settings.save()
    end
    tour.keypressed(key)
    if key == "escape" then love.event.quit() end
end

local function draw2d()
    local w, h = love.graphics.getDimensions()
    local s = math.min(w / 256, h / 240)
    love.graphics.clear(0, 0, 0, 1)
    love.graphics.draw(S.screen, (w - 256 * s) / 2, (h - 240 * s) / 2, 0, s, s)
end

function love.draw()
    if not S.started then
        rom.draw_prompt()
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
    local core = S.core
    record.begin_draw()
    core.ff1_draw(ffi.cast("uint8_t*", S.screen_data:getFFIPointer()))
    S.screen:replacePixels(S.screen_data)
    if S.view3d and core.ff1_view3d() == 1 then world3d.draw()
    elseif S.view3d and core.ff1_battle3d() == 1 then battle3d.draw()
    else draw2d() end
    if core.ff1_battle3d() ~= 1 then battle3d.leave() end
    tour.after_draw()
    tour.draw_label()
    record.end_draw()
    if shot_frames then
        if shot_frames == 60 then shot_t0 = love.timer.getTime() end
        shot_frames = shot_frames - 1
        if shot_frames == 1 and shot_t0 then
            local nc, verts = world3d.stats()
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
    local opt = S.opt
    love.graphics.print(string.format("%s  %s camera, %s view  %d fps  (F2 2D/3D, C camera, V view, Q/E turn, PgUp/PgDn zoom)",
        S.view3d and "3D" or "2D", S.PRESETS[S.preset_i].name, S.VIEWS[S.view_i][1], love.timer.getFPS()), 8, 8)
    love.graphics.print(string.format("extras: F5 tilt-shift %s, F6 water %s, F7 day/night %s, F8 battle camera move %s, F9 encounters %s, F10 battle speed %dx",
        opt.tiltshift and "on" or "off", opt.shimmer and "on" or "off", opt.daynight and "on" or "off",
        opt.battlemove and "on" or "off", opt.encounters and "on" or "off", S.BATTLE_SPEEDS[S.battle_speed_i]), 8, 24)
end
