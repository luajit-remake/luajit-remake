-- The Computer Language Benchmarks Game
-- http://benchmarksgame.alioth.debian.org/
-- contributed by Mike Pall
-- modified by Geoff Leyland
-- modified by Mario Pernici

sun = {}
jupiter = {}
saturn = {}
uranus = {}
neptune = {}

local sqrt = math.sqrt

local PI = 3.141592653589793
local SOLAR_MASS = 4 * PI * PI
local DAYS_PER_YEAR = 365.24
sun.x = 0.0
sun.y = 0.0
sun.z = 0.0
sun.vx = 0.0
sun.vy = 0.0
sun.vz = 0.0
sun.mass = SOLAR_MASS
jupiter.x = 4.84143144246472090e+00
jupiter.y = -1.16032004402742839e+00
jupiter.z = -1.03622044471123109e-01
jupiter.vx = 1.66007664274403694e-03 * DAYS_PER_YEAR
jupiter.vy = 7.69901118419740425e-03 * DAYS_PER_YEAR
jupiter.vz = -6.90460016972063023e-05 * DAYS_PER_YEAR
jupiter.mass = 9.54791938424326609e-04 * SOLAR_MASS
saturn.x = 8.34336671824457987e+00
saturn.y = 4.12479856412430479e+00
saturn.z = -4.03523417114321381e-01
saturn.vx = -2.76742510726862411e-03 * DAYS_PER_YEAR
saturn.vy = 4.99852801234917238e-03 * DAYS_PER_YEAR
saturn.vz = 2.30417297573763929e-05 * DAYS_PER_YEAR
saturn.mass = 2.85885980666130812e-04 * SOLAR_MASS
uranus.x = 1.28943695621391310e+01
uranus.y = -1.51111514016986312e+01
uranus.z = -2.23307578892655734e-01
uranus.vx = 2.96460137564761618e-03 * DAYS_PER_YEAR
uranus.vy = 2.37847173959480950e-03 * DAYS_PER_YEAR
uranus.vz = -2.96589568540237556e-05 * DAYS_PER_YEAR
uranus.mass = 4.36624404335156298e-05 * SOLAR_MASS
neptune.x = 1.53796971148509165e+01
neptune.y = -2.59193146099879641e+01
neptune.z = 1.79258772950371181e-01
neptune.vx = 2.68067772490389322e-03 * DAYS_PER_YEAR
neptune.vy = 1.62824170038242295e-03 * DAYS_PER_YEAR
neptune.vz = -9.51592254519715870e-05 * DAYS_PER_YEAR
neptune.mass = 5.15138902046611451e-05 * SOLAR_MASS

local bodies = {sun,jupiter,saturn,uranus,neptune}

local function advance(bodies, nbody, dt)
  for i=1,nbody do
    local bi = bodies[i]
    local bix, biy, biz, bimass = bi.x, bi.y, bi.z, bi.mass
    local bivx, bivy, bivz = bi.vx, bi.vy, bi.vz
    for j=i+1,nbody do
      local bj = bodies[j]
      local dx, dy, dz = bix-bj.x, biy-bj.y, biz-bj.z
      local dist2 = dx*dx + dy*dy + dz*dz
      local mag = sqrt(dist2)
      mag = dt / (mag * dist2)
      local bm = bj.mass*mag
      bivx = bivx - (dx * bm)
      bivy = bivy - (dy * bm)
      bivz = bivz - (dz * bm)
      bm = bimass*mag
      bj.vx = bj.vx + (dx * bm)
      bj.vy = bj.vy + (dy * bm)
      bj.vz = bj.vz + (dz * bm)
    end
    bi.vx = bivx
    bi.vy = bivy
    bi.vz = bivz
    bi.x = bix + dt * bivx
    bi.y = biy + dt * bivy
    bi.z = biz + dt * bivz
  end
end

local function energy(bodies, nbody)
  local e = 0
  for i=1,nbody do
    local bi = bodies[i]
    local vx, vy, vz, bim = bi.vx, bi.vy, bi.vz, bi.mass
    e = e + (0.5 * bim * (vx*vx + vy*vy + vz*vz))
    for j=i+1,nbody do
      local bj = bodies[j]
      local dx, dy, dz = bi.x-bj.x, bi.y-bj.y, bi.z-bj.z
      local distance = sqrt(dx*dx + dy*dy + dz*dz)
      e = e - ((bim * bj.mass) / distance)
    end
  end
  return e
end

local function offsetMomentum(b, nbody)
  local px, py, pz = 0, 0, 0
  for i=1,nbody do
    local bi = b[i]
    local bim = bi.mass
    px = px + (bi.vx * bim)
    py = py + (bi.vy * bim)
    pz = pz + (bi.vz * bim)
  end
  b[1].vx = -px / SOLAR_MASS
  b[1].vy = -py / SOLAR_MASS
  b[1].vz = -pz / SOLAR_MASS
end

local N = 1000
local nbody = #bodies

offsetMomentum(bodies, nbody)
print(energy(bodies, nbody))
for i=1,N do advance(bodies, nbody, 0.01) end
print(energy(bodies, nbody))

