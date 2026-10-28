-- Matrices (row-major nested tables, sent with the "row" layout).
local M = {}

function M.perspective(fovy, aspect, near, far)
    local f = 1 / math.tan(fovy / 2)
    return {
        { f / aspect, 0, 0, 0 },
        { 0, f, 0, 0 },
        { 0, 0, (far + near) / (near - far), 2 * far * near / (near - far) },
        { 0, 0, -1, 0 },
    }
end

function M.look_at(ex, ey, ez, tx, ty, tz)
    local fx, fy, fz = tx - ex, ty - ey, tz - ez
    local l = math.sqrt(fx * fx + fy * fy + fz * fz)
    fx, fy, fz = fx / l, fy / l, fz / l
    local sx, sy, sz = -fz, 0, fx                 -- f x (0,1,0)
    l = math.sqrt(sx * sx + sz * sz)
    sx, sz = sx / l, sz / l
    local ux, uy, uz = sy * fz - sz * fy, sz * fx - sx * fz, sx * fy - sy * fx
    return {
        { sx, sy, sz, -(sx * ex + sy * ey + sz * ez) },
        { ux, uy, uz, -(ux * ex + uy * ey + uz * ez) },
        { -fx, -fy, -fz, fx * ex + fy * ey + fz * ez },
        { 0, 0, 0, 1 },
    }
end

return M
