-- The pad: the keyboard and game controllers, mapped to the NES buttons.
-- The mapping is changed in the options menu (Controls) and remembered
-- with the settings.  A controller's D-pad and left stick always move too;
-- its right stick turns and zooms the map camera.
local I = {}

-- The NES buttons: name, the pad bit (ff1_frame), the menu's label.
I.BUTTONS = {
    { "up", 0x08, "Up" }, { "down", 0x04, "Down" }, { "left", 0x02, "Left" }, { "right", 0x01, "Right" },
    { "a", 0x10, "A" }, { "b", 0x20, "B" }, { "select", 0x40, "Select" }, { "start", 0x80, "Start" },
}
I.DEFAULT_KEYS = {
    up = { "up" }, down = { "down" }, left = { "left" }, right = { "right" },
    a = { "z", "space" }, b = { "x", "backspace" }, select = { "tab", "rshift" }, start = { "return", "kpenter" },
}
I.DEFAULT_PAD = {
    up = "dpup", down = "dpdown", left = "dpleft", right = "dpright",
    a = "a", b = "b", select = "back", start = "start",
}

local function copy(t)
    local c = {}
    for k, v in pairs(t) do c[k] = type(v) == "table" and copy(v) or v end
    return c
end

I.keys, I.pad = copy(I.DEFAULT_KEYS), copy(I.DEFAULT_PAD)

function I.reset()
    I.keys, I.pad = copy(I.DEFAULT_KEYS), copy(I.DEFAULT_PAD)
end

local STICK = 0.5                  -- how far a stick goes before it counts

local function pads()
    local list = {}
    for _, j in ipairs(love.joystick and love.joystick.getJoysticks() or {}) do
        if j:isGamepad() then list[#list + 1] = j end
    end
    return list
end

-- The NES buttons held now (ff1_frame's bits).
function I.buttons()
    local b = 0
    for _, def in ipairs(I.BUTTONS) do
        local name, bit = def[1], def[2]
        local down = love.keyboard.isDown(unpack(I.keys[name]))
        for _, j in ipairs(pads()) do
            if j:isGamepadDown(I.pad[name]) then down = true end
            local x, y = j:getGamepadAxis("leftx"), j:getGamepadAxis("lefty")
            if (name == "left" and x < -STICK) or (name == "right" and x > STICK) or
               (name == "up" and y < -STICK) or (name == "down" and y > STICK) then down = true end
        end
        if down then b = b + bit end
    end
    return b
end

-- The right stick: the map camera's turn and zoom (-1 .. 1 each).
function I.camera_stick()
    local turn, zoom = 0, 0
    for _, j in ipairs(pads()) do
        local x, y = j:getGamepadAxis("rightx"), j:getGamepadAxis("righty")
        if math.abs(x) > 0.2 then turn = turn + x end
        if math.abs(y) > 0.2 then zoom = zoom + y end
    end
    return turn, zoom
end

-- Which NES button a key or a controller button is mapped to (nil: none).
function I.key_button(key)
    for _, def in ipairs(I.BUTTONS) do
        for _, k in ipairs(I.keys[def[1]]) do if k == key then return def[1] end end
    end
end

function I.pad_button(button)
    for _, def in ipairs(I.BUTTONS) do
        if I.pad[def[1]] == button then return def[1] end
    end
    if button == "dpup" then return "up" end         -- (the D-pad always moves)
    if button == "dpdown" then return "down" end
    if button == "dpleft" then return "left" end
    if button == "dpright" then return "right" end
end

-- Names for the menu.
local PAD_NAMES = {
    a = "A (bottom)", b = "B (right)", x = "X (left)", y = "Y (top)",
    back = "Back / Select", start = "Start", guide = "Guide",
    leftshoulder = "Left shoulder", rightshoulder = "Right shoulder",
    leftstick = "Left stick press", rightstick = "Right stick press",
    dpup = "D-pad up", dpdown = "D-pad down", dpleft = "D-pad left", dpright = "D-pad right",
}

local KEY_NAMES = {
    ["return"] = "Enter", kpenter = "Keypad Enter", rshift = "Right Shift", lshift = "Left Shift",
    rctrl = "Right Ctrl", lctrl = "Left Ctrl", ralt = "Right Alt", lalt = "Left Alt",
}

function I.key_name(name)
    local list = {}
    for _, k in ipairs(I.keys[name]) do
        list[#list + 1] = KEY_NAMES[k] or (k:sub(1, 1):upper() .. k:sub(2))
    end
    return table.concat(list, " / ")
end

function I.pad_name(name)
    return PAD_NAMES[I.pad[name]] or I.pad[name]
end

-- For the settings file: one line per button and device.
function I.serialize()
    local lines = {}
    for _, def in ipairs(I.BUTTONS) do
        lines[#lines + 1] = "key_" .. def[1] .. "=" .. table.concat(I.keys[def[1]], " ")   -- (no key is named with a space)
        lines[#lines + 1] = "pad_" .. def[1] .. "=" .. I.pad[def[1]]
    end
    return table.concat(lines, "\n") .. "\n"
end

function I.parse(text)
    for _, def in ipairs(I.BUTTONS) do
        local k = text:match("\nkey_" .. def[1] .. "=([^\n]+)") or text:match("^key_" .. def[1] .. "=([^\n]+)")
        if k then
            local list = {}
            for key in k:gmatch("%S+") do list[#list + 1] = key end
            if #list > 0 then I.keys[def[1]] = list end
        end
        local p = text:match("\npad_" .. def[1] .. "=([%w]+)")
        if p then I.pad[def[1]] = p end
    end
end

return I
