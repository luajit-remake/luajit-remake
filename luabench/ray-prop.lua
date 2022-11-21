-- Author: Mike Pall
-- https://github.com/LuaJIT/LuaJIT-test-cleanup
-- 
-- modified to represent the vector as <x,y,z> instead of numeric index <1,2,3>
--
local sqrt = math.sqrt
local huge = math.huge

local delta = 1
while delta * delta + 1 ~= 1 do
  delta = delta * 0.5
end

local function length(x, y, z)  return sqrt(x*x + y*y + z*z) end
local function vlen(v)          return length(v.x, v.y, v.z) end
local function mul(c, x, y, z)  return { x = c*x, y = c*y, z = c*z } end
local function unitise(x, y, z) return mul(1/length(x, y, z), x, y, z) end
local function dot(x1, y1, z1, x2, y2, z2)
  return x1*x2 + y1*y2 + z1*z2
end

local function vsub(a, b)        return a.x - b.x, a.y - b.y, a.z - b.z end
local function vdot(a, b)        return dot(a.x, a.y, a.z, b.x, b.y, b.z) end


local sphere = {}
function sphere:new(centre, radius)
  return {centre=centre, radius=radius, new=self.new, intersect=self.intersect }
end

local function sphere_distance(self, origin, dir)
  local vx, vy, vz = vsub(self.centre, origin)
  local b = dot(vx, vy, vz, dir.x, dir.y, dir.z)
  local r = self.radius
  local disc = r*r + b*b - vx*vx-vy*vy-vz*vz
  if disc < 0 then return huge end
  local d = sqrt(disc)
  local t2 = b + d
  if t2 < 0 then return huge end
  local t1 = b - d
  return t1 > 0 and t1 or t2
end

function sphere:intersect(origin, dir, best)
  local lambda = sphere_distance(self, origin, dir)
  if lambda < best[1] then
    local c = self.centre
    best[1] = lambda
    best[2] =
      unitise(
        origin.x - c.x + lambda * dir.x,
        origin.y - c.y + lambda * dir.y,
        origin.z - c.z + lambda * dir.z)
  end
end

local group = {}
function group:new(bound)
  return {bound=bound, children={}, new=self.new, add=self.add, intersect=self.intersect }
end

function group:add(s)
  self.children[#self.children+1] = s
end

function group:intersect(origin, dir, best)
  local lambda = sphere_distance(self.bound, origin, dir)
  if lambda < best[1] then
    for _, c in ipairs(self.children) do
      c:intersect(origin, dir, best)
    end
  end
end

local hit = { x = 0, y = 0, z = 0 }
local ilight
local best = { huge, { x = 0, y = 0, z = 0 } }

local function ray_trace(light, camera, dir, scene)
  best[1] = huge
  scene:intersect(camera, dir, best)
  local b1 = best[1]
  if b1 == huge then return 0 end
  local b2 = best[2]
  local g = vdot(b2, light)
  if g >= 0 then return 0 end
  hit.x = camera.x + b1*dir.x + delta*b2.x
  hit.y = camera.y + b1*dir.y + delta*b2.y
  hit.z = camera.z + b1*dir.z + delta*b2.z
  best[1] = huge
  scene:intersect(hit, ilight, best)
  if best[1] == huge then
    return -g
  else
    return 0
  end
end

local function create(level, centre, radius)
  local s = sphere:new(centre, radius)
  if level == 1 then return s end
  local gr = group:new(sphere:new(centre, 3*radius))
  gr:add(s)
  local rn = 3*radius/sqrt(12)
  for dz = -1,1,2 do
    for dx = -1,1,2 do
      gr:add(create(level-1, { x = centre.x + rn*dx, y = centre.y + rn, z = centre.z + rn*dz }, radius*0.5))
    end
  end
  return gr
end


local level, n, ss = tonumber(arg[1]) or 9, tonumber(arg[2]) or 256, 4
local iss = 1/ss
local gf = 255/(ss*ss)

io.write(("P5\n%d %d\n255\n"):format(n, n))
local light = unitise(-1, -3, 2)
ilight = { x = -light.x, y = -light.y, z = -light.z }
local camera = { x = 0, y = 0, z = -4 }
local dir = { x = 0, y = 0, z = 0 }

local scene = create(level, {x = 0, y = -1, z = 0}, 1)

for y = n/2-1, -n/2, -1 do
  for x = -n/2, n/2-1 do
    local g = 0
    for d = y, y+.99, iss do
      for e = x, x+.99, iss do
        dir = unitise(e, d, n)
        g = g + ray_trace(light, camera, dir, scene) 
      end
    end
    io.write(string.char(math.floor(0.5 + g*gf)))
  end
end

