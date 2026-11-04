-- The options menu (Esc, or Select + Start on a controller): the view, the
-- camera and the extras, and the controls.  The game waits while it's open;
-- every change is saved at once.
local S = require("state")
local settings = require("settings")
local input = require("input")
local M = {}

M.open = false
local page = "options"             -- or "controls"
local sel = 1
local waiting                      -- Controls: the NES button being rebound, waiting for a key / button
local fonts = {}

local function onoff(v) return v and "On" or "Off" end

local function set_encounters()
    S.core.ff1_set_encounters(S.opt.encounters and 1 or 0)
end

local function cycle(i, n, dir) return (i - 1 + dir) % n + 1 end

-- The options: a label, the value shown, and what left / right / A do.
local OPTIONS = {
    { "View", function() return S.view3d and "3D" or "2D" end,
      function() S.view3d = not S.view3d end },
    { "Camera", function() return S.PRESETS[S.preset_i].name:gsub("^%l", string.upper) end,
      function(d) S.preset_i = cycle(S.preset_i, #S.PRESETS, d); S.zoom = 1 end },
    { "View distance", function() return S.VIEWS[S.view_i][1]:gsub("^%l", string.upper) end,
      function(d) S.view_i = cycle(S.view_i, #S.VIEWS, d); S.VIEW = S.VIEWS[S.view_i][2] end },
    { "Tilt-shift blur", function() return onoff(S.opt.tiltshift) end,
      function() S.opt.tiltshift = not S.opt.tiltshift end },
    { "Water shimmer", function() return onoff(S.opt.shimmer) end,
      function() S.opt.shimmer = not S.opt.shimmer end },
    { "Day and night", function() return onoff(S.opt.daynight) end,
      function() S.opt.daynight = not S.opt.daynight end },
    { "Battle camera motion", function() return onoff(S.opt.battlemove) end,
      function() S.opt.battlemove = not S.opt.battlemove end },
    { "Random encounters", function() return onoff(S.opt.encounters) end,
      function() S.opt.encounters = not S.opt.encounters; set_encounters() end },
    { "Battle speed", function() return S.BATTLE_SPEEDS[S.battle_speed_i] .. "x" end,
      function(d) S.battle_speed_i = cycle(S.battle_speed_i, #S.BATTLE_SPEEDS, d) end },
    { "EXP and gold", function() return S.REWARD_MULTS[S.reward_i] .. "x" end,
      function(d)
          S.reward_i = cycle(S.reward_i, #S.REWARD_MULTS, d)
          S.core.ff1_set_reward_mult(S.REWARD_MULTS[S.reward_i])
      end },
    { "Controls", nil, function(d) if d == 0 then page, sel = "controls", 1 end end },
    { "Resume", nil, function(d) if d == 0 then M.open = false end end },
    { "Quit game", nil, function(d) if d == 0 then love.event.quit() end end },
}

local function controls_rows()
    return #input.BUTTONS + 2              -- the buttons, Reset, Back
end

local function back_to_options()
    page = "options"
    for i, o in ipairs(OPTIONS) do if o[1] == "Controls" then sel = i end end
end

function M.toggle()
    M.open, page, sel, waiting = not M.open, "options", 1, nil
end

-- (--menu: open on that page, for pictures)
function M.show(p)
    M.open, page, sel, waiting = true, p or "options", 1, nil
end

-- Menu actions: "up", "down", "left", "right", "ok", "back".
local function act(a)
    if page == "options" then
        local n = #OPTIONS
        if a == "up" then sel = cycle(sel, n, -1)
        elseif a == "down" then sel = cycle(sel, n, 1)
        elseif a == "back" then M.open = false
        else
            local o = OPTIONS[sel]
            local d = a == "left" and -1 or a == "right" and 1 or 0
            if o[2] == nil and d ~= 0 then return end      -- (Controls, Resume, Quit: A only)
            o[3](o[2] and (d == 0 and 1 or d) or 0)
            settings.save()
        end
    else
        local n = controls_rows()
        if a == "up" then sel = cycle(sel, n, -1)
        elseif a == "down" then sel = cycle(sel, n, 1)
        elseif a == "back" then back_to_options()
        elseif a == "ok" then
            if sel <= #input.BUTTONS then waiting = input.BUTTONS[sel][1]
            elseif sel == n - 1 then input.reset(); settings.save()
            else back_to_options() end
        end
    end
end

-- A key: rebinding, else a menu action (the mapped NES buttons, and the
-- arrows / Enter / Esc always, so a bad mapping can't lock the menu).
function M.keypressed(key)
    if waiting then
        if key ~= "escape" then input.keys[waiting] = { key }; settings.save() end
        waiting = nil
        return
    end
    local b = input.key_button(key)
    if key == "escape" then act("back")
    elseif key == "up" or b == "up" then act("up")
    elseif key == "down" or b == "down" then act("down")
    elseif key == "left" or b == "left" then act("left")
    elseif key == "right" or b == "right" then act("right")
    elseif key == "return" or b == "a" or b == "start" then act("ok")
    elseif b == "b" then act("back") end
end

function M.gamepadpressed(button)
    if waiting then
        input.pad[waiting] = button
        settings.save()
        waiting = nil
        return
    end
    local b = input.pad_button(button)
    if b == "up" or b == "down" or b == "left" or b == "right" then act(b)
    elseif b == "a" or b == "start" then act("ok")
    elseif b == "b" or b == "select" then act("back") end
end

local function font(size)
    size = math.max(10, math.floor(size))
    fonts[size] = fonts[size] or love.graphics.newFont(size)
    return fonts[size]
end

function M.draw()
    if not M.open then return end
    local w, h = love.graphics.getDimensions()
    local f = font(h / 34)
    local lh = f:getHeight() * 1.35
    local rows = page == "options" and #OPTIONS or controls_rows()
    local bw, bh = math.min(w - 40, f:getHeight() * 30), lh * (rows + 3.8)
    local x0, y0 = (w - bw) / 2, (h - bh) / 2
    love.graphics.setColor(0, 0, 0, 0.55)
    love.graphics.rectangle("fill", 0, 0, w, h)
    -- An NES-style window: deep blue, a white rim.
    love.graphics.setColor(0.0, 0.08, 0.55, 0.96)
    love.graphics.rectangle("fill", x0, y0, bw, bh, 6, 6)
    love.graphics.setColor(1, 1, 1, 1)
    love.graphics.setLineWidth(3)
    love.graphics.rectangle("line", x0 + 3, y0 + 3, bw - 6, bh - 6, 6, 6)
    love.graphics.setFont(f)
    local title = page == "options" and "OPTIONS" or "CONTROLS"
    love.graphics.printf(title, x0, y0 + lh * 0.5, bw, "center")
    local lx, rx = x0 + f:getHeight() * 2.2, x0 + bw - f:getHeight() * 1.2
    if page == "controls" then
        love.graphics.setColor(0.8, 0.85, 1, 1)
        love.graphics.printf("Keyboard   |   Controller", lx, y0 + lh * 1.2, rx - lx, "right")
        love.graphics.setColor(1, 1, 1, 1)
    end
    for r = 1, rows do
        local y = y0 + lh * (r + 1.4)
        local label, value
        if page == "options" then
            label = OPTIONS[r][1]
            value = OPTIONS[r][2] and OPTIONS[r][2]() or ""
        elseif r <= #input.BUTTONS then
            local name = input.BUTTONS[r][1]
            label = input.BUTTONS[r][3]
            value = waiting == name and "Press a key or button... (Esc: cancel)"
                    or (input.key_name(name) .. "   |   " .. input.pad_name(name))
        else
            label, value = r == rows - 1 and "Reset to defaults" or "Back", ""
        end
        if r == sel then
            love.graphics.setColor(1, 1, 1, 0.15)
            love.graphics.rectangle("fill", x0 + 10, y - lh * 0.15, bw - 20, lh, 4, 4)
            love.graphics.setColor(1, 1, 1, 1)
            love.graphics.print(">", x0 + f:getHeight() * 0.8, y)
        end
        love.graphics.print(label, lx, y)
        love.graphics.printf(value, lx, y, rx - lx, "right")
    end
    local hint = page == "options" and "Up / Down: choose    Left / Right or A: change    B / Esc: close"
                 or "A, then press the new key or controller button    B / Esc: back"
    love.graphics.setColor(0.8, 0.85, 1, 1)
    love.graphics.printf(hint, x0, y0 + bh - lh * 1.1, bw, "center")
    love.graphics.setColor(1, 1, 1, 1)
end

return M
