-- The front end's shared state: what more than one file reads or changes.
-- Everything else stays local to its file.
local S = {}

S.core = nil                       -- ff1core.dll, through the FFI (love.load)
S.started = false                  -- the game is running (a ROM was found)
S.screen_data, S.screen = nil, nil -- the 256x240 picture (the 3D views draw parts of it over theirs)
S.view3d = true                    -- F2: the 3D view, else the 2D picture
S.yaw = 0                          -- the map camera's turn (Q / E)

-- The extras (not in the original), each on its own key, remembered.
S.opt = { tiltshift = true, shimmer = true, daynight = false, battlemove = true, encounters = true }
S.BATTLE_SPEEDS = { 1, 2, 4 }      -- F10: frames of battle per frame
S.battle_speed_i = 1
S.REWARD_MULTS = { 1, 2, 4 }       -- F11: battles' EXP and gold times this
S.reward_i = 1

S.VIEW = 560                       -- draw distance (pixels); fog ends there (V cycles)
S.VIEWS = { { "near", 420 }, { "normal", 560 }, { "far", 760 } }
S.view_i = 2

-- Camera presets (C cycles): distance from the party, pitch above the
-- ground, field of view.  The camera eases into a new one.
S.PRESETS = {
    { name = "classic", dist = 250, pitch = 47, fov = 45 },
    { name = "close",   dist = 125, pitch = 32, fov = 50 },
    { name = "high",    dist = 420, pitch = 66, fov = 40 },
    { name = "low",     dist = 165, pitch = 17, fov = 55 },   -- third person, HD-2D style
}
S.preset_i = 1
S.zoom = 1                         -- PgUp / PgDn / wheel, on top of the preset
S.cam = { dist = 250, pitch = 47, fov = 45 }   -- where the camera is now (easing)

return S
