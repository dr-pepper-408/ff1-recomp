-- Where the save, settings and ROM copy live, and the settings themselves.
local S = require("state")
local input = require("input")
local Set = {}

local SETTINGS = "settings.txt"

-- Where the save, settings and ROM copy live (data_dir): LOVE's save folder
-- (%APPDATA%\ff1-recomp for the release), which every release finds,
-- wherever it was unzipped.  A portable.txt beside the program keeps them
-- in the program's folder instead (when it can be written to).  Read and
-- written with io, as LOVE can only write to its save folder.
Set.data_dir = nil
Set.PLAYER_FILES = { "ff1.sav", SETTINGS, "rom.nes" }

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
    Set.data_dir = saves
    -- (LOVE makes its save folder on its first write; io can't)
    if love.filesystem.write(".created", "") then love.filesystem.remove(".created") end
    if dev then return end
    local base = love.filesystem.getSourceBaseDirectory()
    if read_file(base .. "/portable.txt") and write_file(base .. "/.write-test", "") then
        os.remove(base .. "/.write-test")
        Set.data_dir = base                        -- (portable)
        return
    end
    -- An earlier release kept them beside the program: each one that's newer
    -- than the save folder's copy (or the only one) comes here, the copy it
    -- replaces kept as NAME.bak.  (The dates: the program's folder mounted
    -- as "base"; without it, only missing files come.)
    local mounted = love.filesystem.mount(base, "base")
    for _, name in ipairs(Set.PLAYER_FILES) do
        local there = read_file(base .. "/" .. name)
        if there then
            local here = read_file(saves .. "/" .. name)
            local t_there = mounted and love.filesystem.getInfo("base/" .. name)
            local t_here = love.filesystem.getInfo(name)
            local newer = here == nil or (t_there and t_here and here ~= there and
                                          (t_there.modtime or 0) > (t_here.modtime or 0))
            if newer then
                if here then write_file(saves .. "/" .. name .. ".bak", here) end
                write_file(saves .. "/" .. name, there)
            end
        end
    end
    if mounted then love.filesystem.unmount(base) end
end

function Set.save()
    local opt = S.opt
    write_file(Set.data_dir .. "/" .. SETTINGS,
          string.format("preset=%d\nview=%d\ntiltshift=%d\nshimmer=%d\ndaynight=%d\nbattlemove=%d\nencounters=%d\nbattlespeed=%d\nrewards=%d\nview3d=%d\n",
                        S.preset_i, S.view_i, opt.tiltshift and 1 or 0, opt.shimmer and 1 or 0,
                        opt.daynight and 1 or 0, opt.battlemove and 1 or 0, opt.encounters and 1 or 0,
                        S.battle_speed_i, S.reward_i, S.view3d and 1 or 0) .. input.serialize())
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
    local v3 = text:match("view3d=(%d)")
    if v3 then S.view3d = v3 == "1" end
    input.parse(text)
    S.VIEW = S.VIEWS[S.view_i][2]
    local pr = S.PRESETS[S.preset_i]
    S.cam.dist, S.cam.pitch, S.cam.fov = pr.dist, pr.pitch, pr.fov
end

return Set
