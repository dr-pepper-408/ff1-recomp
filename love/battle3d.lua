-- The 3D view of a battle: the diorama (built in C) and its camera.
local ffi = require("ffi")
local S = require("state")
local shaders = require("shaders")
local matrix = require("matrix")
local B = {}

local BATTLE_VERTS = 1200
local BATTLE_FOV = 40
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
local BOSS_CAM = { name = "boss", tx = 62, ty = 36, tz = 64, d = 400, a = 36, ey = 30 }
-- Where the party stands on the stage (draw_character's rows and x, the
-- stage's axes): rows across at 60 + 24 * i, depth about $B8, 24 tall.
local PARTY_ACROSS0, PARTY_ROW, PARTY_Z, PARTY_H = 60, 24, 0xB8, 24

B.force_cam = nil                          -- --battlecam=N: always that shot (5: the boss's)
local battle_cam                           -- this battle's shot (nil between battles)
local battle_t0 = 0                        -- when this battle's shot began (its camera move)
local battle_data, battle_atlas, battle_buf, battle_mesh, overlay_data, overlay
local battle_info = ffi.new("float[11]")
local shader

function B.load()
    battle_data = love.image.newImageData(512, 512, "rgba8")
    battle_atlas = love.graphics.newImage(battle_data)
    battle_buf = love.data.newByteData(BATTLE_VERTS * shaders.VERTEX_SIZE)
    battle_mesh = love.graphics.newMesh(shaders.VERTEX_FORMAT, BATTLE_VERTS, "triangles", "stream")
    battle_mesh:setTexture(battle_atlas)
    overlay_data = love.image.newImageData(256, 240, "rgba8")
    overlay = love.graphics.newImage(overlay_data)
    shader = love.graphics.newShader(shaders.SCENE)
end

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
    local proj = matrix.perspective(math.rad(BATTLE_FOV), aspect, 4, 3000)   -- (centred: the shift is added later)
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
            local v = mul(proj, mul(matrix.look_at(ex, ey, ez, tx, ty, tz), { pcx, PARTY_H / 2, PARTY_Z, 1 }))
            local nx = v[1] / v[4]
            local fx, fz = tx - ex, tz - ez
            local l = math.sqrt(fx * fx + fz * fz)
            local rx, rz = -fz / l, fx / l
            local dist = math.sqrt((pcx - ex) ^ 2 + (PARTY_Z - ez) ^ 2)
            local shift = nx * dist * math.tan(math.rad(BATTLE_FOV) / 2) * aspect
            tx, tz = tx + rx * shift, tz + rz * shift
        end
        local ex, ey, ez = battle_camera(c, tx, ty, tz, d)
        local view = matrix.look_at(ex, ey, ez, tx, ty, tz)
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

-- A battle begins: pick its shot.
local function begin()
    local core = S.core
    core.ff1_battle_scene(ffi.cast("Ff1Vertex*", battle_buf:getFFIPointer()), BATTLE_VERTS,
                          ffi.cast("uint8_t*", battle_data:getFFIPointer()), 1, 0, battle_info)
    local shot
    if B.force_cam then shot = BATTLE_CAMS[B.force_cam] or BOSS_CAM
    elseif battle_info[5] > 0 then shot = BOSS_CAM
    else shot = BATTLE_CAMS[love.math.random(#BATTLE_CAMS)] end
    -- The party on a diagonal that fans out from this side; then
    -- the shot framed round this party and these enemies.
    local stagger = shot.a > 4 and -12 or shot.a < -4 and 12 or 0
    core.ff1_battle_stagger(stagger)
    local w, h = love.graphics.getDimensions()
    battle_cam = frame_battle(shot, battle_info, stagger, w / h)
    battle_t0 = love.timer.getTime()
    -- Seen from this shot, are the enemies to the party's right?
    -- Then the cutouts are mirrored, so the two sides face each
    -- other.  (The camera's right is (cos a, -sin a) on the stage.)
    local a = math.rad(shot.a)
    local da = (battle_info[6] + battle_info[7]) / 2 - (PARTY_ACROSS0 + 1.5 * PARTY_ROW)
    local dz = (battle_info[8] + battle_info[9]) / 2 - PARTY_Z
    core.ff1_battle_flip(math.cos(a) * da - math.sin(a) * dz > 0 and 1 or 0)
end

-- The frame in 3D: the stage, then the menus and the status column.
function B.draw()
    if not battle_cam then begin() end
    local core = S.core
    -- Behind the party, looking at the enemies.  Stage axes: x across
    -- (screen y), z depth (screen x), y up.
    local c = battle_cam
    local tx, ty, tz = c.tx, c.ty, c.tz
    -- (F8) The camera glides into its shot as the battle starts, then
    -- drifts a little: a slow sway, in and out, up and down.
    local d, sway, bob = c.d, 0, 0
    if S.opt.battlemove then
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
        local p = matrix.perspective(math.rad(BATTLE_FOV), w / h, 4, 3000)
        p[1][3] = -STAGE_NDC_X                     -- (off-centre: x' = x + s * w)
        p[2][3] = -STAGE_NDC_Y
        love.graphics.setShader(shader)
        shader:send("proj", "row", p)
        shader:send("view", "row", matrix.look_at(ex, ey, ez, tx, ty, tz))
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
    ffi.copy(overlay_data:getFFIPointer(), S.screen_data:getFFIPointer(), 256 * 240 * 4)
    core.ff1_battle_overlay(ffi.cast("uint8_t*", overlay_data:getFFIPointer()))
    overlay:replacePixels(overlay_data)
    local s = math.min(w / 256, h / 240)
    love.graphics.draw(overlay, (w - 256 * s) / 2, (h - 240 * s) / 2, 0, s, s)
end

-- Out of battle: the next one picks a new shot.
function B.leave()
    battle_cam = nil
end

return B
