-- Where the save, settings and ROM copy live, and the settings themselves.
local S = require("state")
local Set = {}

local SETTINGS = "settings.txt"

-- Where the save, settings and ROM copy live (data_dir): in a release, the
-- program's own folder when it can be written to (a portable folder), else
-- LOVE's save folder.  Read and written with io, as LOVE can only write to
-- its save folder.
Set.data_dir = nil

function Set.read_file(path)
    local f = io.open(path, "rb")
    if not f then return nil end
    local s = f:read("*a")
    f:close()
    return s
end

function Set.write_file(path, s)
    local f = io.open(path, "wb")
    if not f then return false end
    f:write(s)
    f:close()
    return true
end

local read_file, write_file = Set.read_file, Set.write_file

function Set.pick_data_dir(dev)
    local saves = love.filesystem.getSaveDirectory()
    if dev then Set.data_dir = saves; return end
    local base = love.filesystem.getSourceBaseDirectory()
    if not write_file(base .. "/.write-test", "") then Set.data_dir = saves; return end
    os.remove(base .. "/.write-test")
    -- (from an earlier release that kept them in the save folder)
    for _, name in ipairs({ "ff1.sav", SETTINGS, "rom.nes" }) do
        local old = read_file(saves .. "/" .. name)
        if old and not read_file(base .. "/" .. name) then write_file(base .. "/" .. name, old) end
    end
    Set.data_dir = base
end

function Set.save()
    local opt = S.opt
    write_file(Set.data_dir .. "/" .. SETTINGS,
          string.format("preset=%d\nview=%d\ntiltshift=%d\nshimmer=%d\ndaynight=%d\nbattlemove=%d\nencounters=%d\nbattlespeed=%d\nrewards=%d\n",
                        S.preset_i, S.view_i, opt.tiltshift and 1 or 0, opt.shimmer and 1 or 0,
                        opt.daynight and 1 or 0, opt.battlemove and 1 or 0, opt.encounters and 1 or 0,
                        S.battle_speed_i, S.reward_i))
end

function Set.load()
    local text = read_file(Set.data_dir .. "/" .. SETTINGS)
    if not text then return end
    local p = tonumber(text:match("preset=(%d+)"))
    local v = tonumber(text:match("view=(%d+)"))
    if p and S.PRESETS[p] then S.preset_i = p end
    if v and S.VIEWS[v] then S.view_i = v end
    local bs = tonumber(text:match("battlespeed=(%d)"))
    if bs and S.BATTLE_SPEEDS[bs] then S.battle_speed_i = bs end
    local rw = tonumber(text:match("rewards=(%d)"))
    if rw and S.REWARD_MULTS[rw] then S.reward_i = rw end
    for _, k in ipairs({ "tiltshift", "shimmer", "daynight", "battlemove", "encounters" }) do
        local x = text:match(k .. "=(%d)")
        if x then S.opt[k] = x == "1" end
    end
    S.VIEW = S.VIEWS[S.view_i][2]
    local pr = S.PRESETS[S.preset_i]
    S.cam.dist, S.cam.pitch, S.cam.fov = pr.dist, pr.pitch, pr.fov
end

return Set
