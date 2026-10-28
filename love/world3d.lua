-- The 3D view of the overworld and the standard maps: the world in chunks
-- (meshed in C), the sprites and the maps' objects as cards, soft shadows.
local ffi = require("ffi")
local S = require("state")
local shaders = require("shaders")
local matrix = require("matrix")
local effects = require("effects")
local W = {}

local CHUNK = 256                  -- world pixels per chunk side
local MAP = 4096                   -- the world wraps every MAP pixels (ff1_world)
local NCHUNK = 16                  -- chunks per side
local MAX_SPRITES = 64 * 6
local MAX_PROPS = 49 * 49 * 6
local MAX_SHADOWS = 16
local VERTEX_SIZE, VERTEX_FORMAT = shaders.VERTEX_SIZE, shaders.VERTEX_FORMAT

local world_key
local world_tiles = ffi.new("int[1]")
local atlas_data, atlas                    -- overworld metatiles
local spr_data, spr_atlas                  -- the frame's sprites
local scratch, scratch_verts               -- chunk meshing output
local chunks = {}                          -- [cz * 16 + cx] = { key, mesh }
local sprite_buf, sprite_mesh
local prop_buf, prop_mesh                  -- a standard map's objects as cards
local shader
local shadow_shader, shadow_mesh, shadow_buf
local proj_m, view_m
local fx = ffi.new("float[12]")            -- the frame's screen effects (ff1_view3d_fx)
local fx_mono = 0
local overlay_data, overlay                -- the dialogue box, over the world

function W.load()
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
    shader = love.graphics.newShader(shaders.SCENE)

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
    shadow_shader = love.graphics.newShader(shaders.SHADOW)
    shadow_mesh = love.graphics.newMesh(VERTEX_FORMAT, MAX_SHADOWS * 6, "triangles", "stream")
    shadow_mesh:setTexture(shadow_img)
    shadow_buf = ffi.new("float[?]", MAX_SHADOWS * 4)

    overlay_data = love.image.newImageData(256, 240, "rgba8")
    overlay = love.graphics.newImage(overlay_data)
end

-- The mesh of chunk (cx, cz), rebuilt when its tiles change.  Meshing a
-- chunk takes a few milliseconds, so beyond the nearest chunks only
-- `budget` are built per frame (what isn't ready yet is out in the fog).
local budget
local function chunk_mesh(cx, cz, near)
    local core = S.core
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
    local core, cam, yaw, VIEW = S.core, S.cam, S.yaw, S.VIEW
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

    local light, day_sky = effects.time_of_day()
    local sky = day_sky or effects.SKY
    if fx_mono > 0 then local l = sky[1] * 0.3 + sky[2] * 0.59 + sky[3] * 0.11; sky = { l, l, l } end
    love.graphics.clear(sky[1] * fx[2], sky[2] * fx[3], sky[3] * fx[4], 1, true, true)
    love.graphics.setShader(shader)
    shader:send("tint", { fx[2], fx[3], fx[4] })
    shader:send("mono", fx_mono)
    shader:send("time", love.timer.getTime() % 1000)
    shader:send("water", S.opt.shimmer and 1 or 0)
    shader:send("daylight", light)
    shader:send("outline", 0)
    proj_m = matrix.perspective(math.rad(cam.fov), w / h, 2, 3000)
    if love.graphics.getCanvas() then              -- (an image is drawn upside down: flip y back)
        for c = 1, 4 do proj_m[2][c] = -proj_m[2][c] end
    end
    view_m = matrix.look_at(ex, ey, ez, tx, ty, tz)
    shader:send("proj", "row", proj_m)
    shader:send("view", "row", view_m)
    shader:send("focus", { tx, ty, tz })
    shader:send("sky", effects.SKY)
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

-- The frame in 3D: the world, the screen effects, the dialogue box.
function W.draw()
    local core = S.core
    core.ff1_view3d_fx(fx)
    fx_mono = fx[5]
    local w, h = love.graphics.getDimensions()
    local s = math.min(w / 256, h / 240)
    local ox, oy = (w - 256 * s) / 2, (h - 240 * s) / 2
    if S.opt.tiltshift then                        -- (into the scene image, blurred onto the screen)
        effects.begin_tilt_shift(w, h)
    end
    draw3d()
    if fx[8] > 0 then                              -- the altar's beam: that part again, in grey
        love.graphics.setScissor(ox + fx[6] * s, oy + fx[7] * s, (fx[8] - fx[6]) * s, (fx[9] - fx[7]) * s)
        fx_mono = 1
        draw3d()
        love.graphics.setScissor()
    end
    if S.opt.tiltshift then
        effects.end_tilt_shift(w, h)
    end
    if fx[0] > 0 or fx[1] < 240 then               -- the screen wipe: only these rows show
        love.graphics.setColor(0, 0, 0, 1)
        love.graphics.rectangle("fill", 0, 0, w, oy + fx[0] * s)
        love.graphics.rectangle("fill", 0, oy + (fx[0] + fx[1]) * s, w, h)
        love.graphics.setColor(1, 1, 1, 1)
    end
    -- The dialogue box, as drawn, over the world.
    ffi.copy(overlay_data:getFFIPointer(), S.screen_data:getFFIPointer(), 256 * 240 * 4)
    if core.ff1_view3d_overlay(ffi.cast("uint8_t*", overlay_data:getFFIPointer())) ~= 0 then
        overlay:replacePixels(overlay_data)
        love.graphics.draw(overlay, (w - 256 * s) / 2, (h - 240 * s) / 2, 0, s, s)
    end
end

-- The chunks built and their vertices (--shot's report).
function W.stats()
    local verts, nc = 0, 0
    for _, c in pairs(chunks) do verts = verts + c.verts; nc = nc + 1 end
    return nc, verts
end

return W
