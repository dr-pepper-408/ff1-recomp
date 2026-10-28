-- The location tour (--tour): every standard map in turn, to check them.
-- ] / [ go to the next / previous one.  --tour-shots takes a picture of
-- each (the save folder's tour/, 3D or with --2d 2D) and quits.
local ffi = require("ffi")
local S = require("state")
local T = {}

local SETTLE = 240                 -- game frames from the jump to the picture (wipes, meshing)

T.on, T.shots = false, false
local count                        -- stops (one per map)
local stop = -1                    -- the current one
local want                         -- the one to go to (when the party is walking)
local settle                       -- --tour-shots: frames until the picture
local info = ffi.new("int[3]")
local label = ""

function T.start(shots)
    T.on, T.shots, want = true, shots, 0
end

function T.keypressed(key)
    if not T.on or not count then return end
    if key == "]" then want = (stop + 1) % count end
    if key == "[" then want = (stop - 1) % count end
end

-- Each game frame: jump when asked and the party is walking.
function T.frame()
    if not T.on then return end
    count = count or S.core.ff1_tour_count()
    if want and S.core.ff1_tour_go(want, info) ~= 0 then
        stop, want = want, nil
        label = string.format("tour %d / %d: map $%02X, by %s $%02X   ([ ] previous / next)",
            stop + 1, count, info[0], info[1] == 1 and "teleport" or "entrance", info[2])
        print(label)
        if T.shots then settle = SETTLE end
    end
    if settle then settle = settle - 1 end
end

-- After the frame is drawn: --tour-shots' picture, then the next stop (or quit).
function T.after_draw()
    if not (T.shots and settle and settle <= 0) then return end
    settle = nil
    local name = string.format("tour/%02d_map_%02X%s.png", stop + 1, info[0], S.view3d and "" or "_2d")
    local last = stop + 1 >= count
    love.filesystem.createDirectory("tour")
    love.graphics.captureScreenshot(function(img)
        img:encode("png", name)
        if last then
            print("saved " .. count .. " pictures to " .. love.filesystem.getSaveDirectory() .. "/tour")
            love.event.quit()
        end
    end)
    if not last then want = stop + 1 end
end

function T.draw_label()
    if not T.on or T.shots then return end
    local h = love.graphics.getHeight()
    love.graphics.setColor(0, 0, 0, 0.6)
    love.graphics.rectangle("fill", 0, h - 26, love.graphics.getWidth(), 26)
    love.graphics.setColor(1, 1, 1, 1)
    love.graphics.print(label, 8, h - 20)
end

return T
