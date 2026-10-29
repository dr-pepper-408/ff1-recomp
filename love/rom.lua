-- The player's ROM: finding it, asking for it, and starting the game.
local ffi = require("ffi")
local S = require("state")
local settings = require("settings")
local R = {}

R.ROM_COPY = "rom.nes"             -- the player's ROM, kept in data_dir
R.prompt = nil                     -- asking for the ROM: "" or why the last one failed
R.save_file = nil                  -- the save (love.load)
R.new_game = 0                     -- 1: a new game, skipping the title (--new)

-- The window's icon: the Black Mage (class 5), from the player's ROM, its
-- 16 x 24 sprite centred in a square and scaled up 2x.
local function set_icon()
    local spr = ffi.new("uint8_t[?]", 16 * 24 * 4)
    S.core.ff1_class_sprite(5, spr)
    local icon = love.image.newImageData(48, 48)
    for y = 0, 23 do
        for x = 0, 15 do
            local p = spr + (y * 16 + x) * 4
            if p[3] > 0 then
                for d = 0, 3 do
                    icon:setPixel(8 + x * 2 + d % 2, y * 2 + math.floor(d / 2),
                                  p[0] / 255, p[1] / 255, p[2] / 255, 1)
                end
            end
        end
    end
    love.window.setIcon(icon)
end

function R.start_game()
    local core = S.core
    if core.ff1_init(R.save_file, R.new_game) ~= 0 then error("ff1_init failed") end
    core.ff1_set_encounters(S.opt.encounters and 1 or 0)
    core.ff1_set_reward_mult(S.REWARD_MULTS[S.reward_i])
    S.started, R.prompt = true, nil
    set_icon()
end

-- Start with the first of `paths` that is the ROM; else ask for it.
function R.find(paths, ask)
    for _, path in pairs(paths) do
        if ask then break end                      -- (--ask-rom: show the prompt)
        if path and S.core.ff1_load_rom(path) == 0 then R.start_game(); break end
    end
    if not S.started then R.prompt = "" end
end

-- A file dropped on the window: if it's Final Fantasy (USA), keep a copy
-- and start; else say why not.
function love.filedropped(file)
    if S.started then return end
    file:open("r")
    local data = file:read()
    file:close()
    local path = settings.data_dir .. "/" .. R.ROM_COPY .. ".new"
    settings.write_file(path, data)
    local status = S.core.ff1_load_rom(path)
    os.remove(path)
    if status == 0 then
        settings.write_file(settings.data_dir .. "/" .. R.ROM_COPY, data)
        R.start_game()
    else
        R.prompt = ffi.string(S.core.ff1_rom_message(status))
    end
end

function R.draw_prompt()
    local w, h = love.graphics.getDimensions()
    love.graphics.clear(0.02, 0.03, 0.12, 1)
    local lines = {
        "FINAL FANTASY",
        "",
        "This game needs your copy of Final Fantasy (USA) for the NES, as a ROM file.",
        "Drag it onto this window.",
        "",
        "It is checked, then kept in the game's save folder for next time.",
    }
    if R.prompt ~= "" then
        lines[#lines + 1] = ""
        lines[#lines + 1] = "That file can't be used: " .. R.prompt
    end
    local y = h / 2 - #lines * 12
    for _, l in ipairs(lines) do
        love.graphics.printf(l, 0, y, w, "center")
        y = y + 24
    end
end

return R
