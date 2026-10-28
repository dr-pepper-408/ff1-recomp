-- --record=NAME: a video of the game (for showing it), drawn off screen at
-- 1024 x 960 (the game's 256 x 240, 4x; --rec-size=WxH), one game frame per picture, piped to ffmpeg with
-- the game's sound; NAME.mp4 in the save folder's videos/.  --rec-frames=N
-- game frames long (default 480, 8 s), after --rec-skip=N not kept; every
-- other frame is kept (30 fps).
-- Needs ffmpeg ($FF1_FFMPEG, else on the PATH).
local ffi = require("ffi")
local S = require("state")
local effects = require("effects")
local R = {}

R.on = false
R.W, R.H = 1024, 960                 -- (the game's 256 x 240, 4x)
R.frames = 480
R.skip = 0                         -- --rec-skip=N: the first N frames aren't kept
local name, canvas, pipe, dir
local frame = 0
local audio = {}                   -- the sound, as 16-bit sample strings

local function ffmpeg()
    return os.getenv("FF1_FFMPEG") or "ffmpeg"
end

function R.start(n)
    R.on, name = true, n
end

-- (love.load, after the window) the canvas, and drawing sized to it.
function R.load()
    if not R.on then return end
    canvas = love.graphics.newCanvas(R.W, R.H)
    effects.target = { canvas, depth = true }
    love.graphics.getDimensions = function() return R.W, R.H end
    love.graphics.getWidth = function() return R.W end
    love.graphics.getHeight = function() return R.H end
    dir = love.filesystem.getSaveDirectory() .. "/videos"
    love.filesystem.createDirectory("videos")
    pipe = io.popen(string.format('""%s" -y -loglevel error -f rawvideo -pix_fmt rgba -s %dx%d -framerate %.4f ' ..
        '-i - -c:v libx264 -preset medium -crf 18 -pix_fmt yuv420p "%s/%s_video.mp4""',
        ffmpeg(), R.W, R.H, 60.0988 / 2, dir, name), "wb")
end

-- The game's sound for this frame.
function R.audio(buf, n)
    if R.on and n > 0 and frame >= R.skip then audio[#audio + 1] = ffi.string(buf, n * 2) end
end

-- Drawing goes into the canvas...
function R.begin_draw()
    if not R.on then return end
    love.graphics.setCanvas(effects.target)
    love.graphics.clear(0, 0, 0, 1, true, true)
end

-- ...which is then kept (every other frame) and shown in the window.
function R.end_draw()
    if not R.on then return end
    love.graphics.setCanvas()
    frame = frame + 1
    if frame > R.skip and (frame - R.skip) % 2 == 0 then
        local img = canvas:newImageData()
        pipe:write(img:getString())
        img:release()
    end
    local ww, wh = love.window.getMode()
    local s = math.min(ww / R.W, wh / R.H)
    love.graphics.clear(0, 0, 0, 1)
    love.graphics.draw(canvas, (ww - R.W * s) / 2, 0, 0, s, s)
    if frame >= R.skip + R.frames then R.finish() end
end

local function wav(path, data)
    local n = #data
    local function u32(v) return string.char(v % 256, math.floor(v / 256) % 256, math.floor(v / 65536) % 256, math.floor(v / 16777216) % 256) end
    local function u16(v) return string.char(v % 256, math.floor(v / 256) % 256) end
    local f = io.open(path, "wb")
    f:write("RIFF", u32(36 + n), "WAVE", "fmt ", u32(16), u16(1), u16(1), u32(44100), u32(88200), u16(2), u16(16),
            "data", u32(n), data)
    f:close()
end

function R.finish()
    pipe:close()
    local base = dir .. "/" .. name
    wav(base .. "_sound.wav", table.concat(audio))
    os.execute(string.format('""%s" -y -loglevel error -i "%s_video.mp4" -i "%s_sound.wav" -c:v copy -c:a aac -b:a 192k -shortest "%s.mp4""',
        ffmpeg(), base, base, base))
    os.remove(base .. "_video.mp4")
    os.remove(base .. "_sound.wav")
    print("saved " .. base .. ".mp4")
    love.event.quit()
end

return R
