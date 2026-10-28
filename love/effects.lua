-- Two of the extras: day / night (F7) and the tilt-shift blur (F5).
local S = require("state")
local shaders = require("shaders")
local E = {}

E.SKY = { 0.42, 0.62, 0.95 }

-- Day / night: the light and the sky through a six-minute day.
local DAY_PERIOD = 360
E.day_offset = 0                   -- --daytime=F: start at that time of day (0..1)
local DAY_KEYS = {
    { 0.00, { 1, 1, 1 },          { 0.42, 0.62, 0.95 } },
    { 0.45, { 1, 1, 1 },          { 0.42, 0.62, 0.95 } },   -- day
    { 0.55, { 1.0, 0.78, 0.62 },  { 0.95, 0.55, 0.35 } },   -- dusk
    { 0.65, { 0.42, 0.48, 0.78 }, { 0.05, 0.07, 0.20 } },   -- night
    { 0.88, { 0.42, 0.48, 0.78 }, { 0.05, 0.07, 0.20 } },
    { 0.95, { 1.0, 0.86, 0.76 },  { 0.90, 0.66, 0.56 } },   -- dawn
    { 1.00, { 1, 1, 1 },          { 0.42, 0.62, 0.95 } },
}

-- The light and the sky now (the sky nil: the usual one).
function E.time_of_day()
    if not S.opt.daynight then return { 1, 1, 1 }, nil end
    local t = (love.timer.getTime() / DAY_PERIOD + E.day_offset) % 1
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

local scene_canvas, blur_canvas, blur_shader
E.target = nil                     -- where the frame goes (nil: the window; --record: its canvas)

function E.load()
    blur_shader = love.graphics.newShader(shaders.BLUR)
end

local function ensure_canvases(w, h)
    if scene_canvas and scene_canvas:getWidth() == w and scene_canvas:getHeight() == h then return end
    if scene_canvas then scene_canvas:release(); blur_canvas:release() end
    scene_canvas = love.graphics.newCanvas(w, h)
    blur_canvas = love.graphics.newCanvas(w, h)
end

-- Tilt-shift: the scene is drawn into an image (begin)...
function E.begin_tilt_shift(w, h)
    ensure_canvases(w, h)
    love.graphics.setCanvas({ scene_canvas, depth = true })
end

-- ...then drawn to the screen through the two blur passes.
function E.end_tilt_shift(w, h)
    love.graphics.setCanvas(E.target)
    love.graphics.setCanvas(blur_canvas)
    love.graphics.setShader(blur_shader)
    blur_shader:send("strength", 1)
    blur_shader:send("dir", { 1 / w, 0 })
    love.graphics.draw(scene_canvas)
    love.graphics.setCanvas(E.target)
    blur_shader:send("dir", { 0, 1 / h })
    love.graphics.draw(blur_canvas)
    love.graphics.setShader()
end

return E
