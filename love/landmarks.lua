-- --landmark-refs: reference pictures of the overworld's landmarks, for
-- modelling them.
local ffi = require("ffi")
local S = require("state")
local L = {}

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
function L.write_refs()
    love.filesystem.createDirectory("landmarks")
    local core = S.core
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

return L
