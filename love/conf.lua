function love.conf(t)
    t.identity = "ff1-recomp"
    t.version = "11.5"
    t.console = true
    t.window.title = "Final Fantasy (LOVE)"
    t.window.width = 768
    t.window.height = 720
    t.window.resizable = true
    t.window.vsync = 1
    t.window.depth = 24          -- the 3D view needs a depth buffer
    t.modules.physics = false
    t.modules.joystick = false
end
