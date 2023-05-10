-- Origin: https://github.com/smarr/are-we-fast-yet
--
-- This code is derived from the SOM benchmarks, see AUTHORS.md file.
--
-- Copyright (c) 2016 Francois Perrad <francois.perrad@gadz.org>
--
-- Permission is hereby granted, free of charge, to any person obtaining a copy
-- of this software and associated documentation files (the 'Software'), to deal
-- in the Software without restriction, including without limitation the rights
-- to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
-- copies of the Software, and to permit persons to whom the Software is
-- furnished to do so, subject to the following conditions:
--
-- The above copyright notice and this permission notice shall be included in
-- all copies or substantial portions of the Software.
--
-- THE SOFTWARE IS PROVIDED 'AS IS', WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
-- IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
-- FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
-- AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
-- LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
-- OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
-- THE SOFTWARE.

local Random = {_CLASS = 'Random'} do

function Random.new ()
    local obj = {seed = 74755}
    return setmetatable(obj, {__index = Random})
end

function Random:next ()
  self.seed = ((self.seed * 1309) + 13849) % 65536;
  return self.seed;
end

end -- class Random

local benchmark = {} do

function benchmark:inner_benchmark_loop (inner_iterations)
    for _ = 1, inner_iterations do
        if not self:verify_result(self:benchmark()) then
            return false
        end
    end
    return true
end

function benchmark:benchmark ()
    error 'subclass_responsibility'
end

function benchmark:verify_result ()
    error 'subclass_responsibility'
end

end -- class Benchmark

local Ball = {_CLASS = 'Ball'} do

local abs = math.abs

function Ball.new (random)
    local obj = {
        x = random:next() % 500,
        y = random:next() % 500,
        x_vel = (random:next() % 300) - 150,
        y_vel = (random:next() % 300) - 150,
    }
    return setmetatable(obj, {__index = Ball})
end

function Ball:bounce ()
    local x_limit, y_limit = 500, 500
    local bounced = false
    self.x = self.x + self.x_vel
    self.y = self.y + self.y_vel
    if self.x > x_limit then
        self.x = x_limit
        self.x_vel = 0 - abs(self.x_vel)
        bounced = true
    end
    if self.x < 0 then
        self.x = 0
        self.x_vel = abs(self.x_vel)
        bounced = true
    end
    if self.y > y_limit then
        self.y = y_limit
        self.y_vel = 0 - abs(self.y_vel)
        bounced = true
    end
    if self.y < 0 then
        self.y = 0
        self.y_vel = abs(self.y_vel)
        bounced = true
    end
    return bounced
end

end -- class Ball

local bounce = {} do
setmetatable(bounce, {__index = benchmark})

function bounce:benchmark ()
    local random     = Random.new()
    local ball_count = 100
    local bounces    = 0
    local balls      = {}

    for i = 1, ball_count do
        balls[i] = Ball.new(random)
    end

    for _ = 1, 50 do
        for i = 1, #balls do
            local ball = balls[i]
            if ball:bounce() then
                bounces = bounces + 1
            end
        end
    end
    return bounces
end

function bounce:verify_result (result)
    print(result)
    return 1331 == result
end

end -- object bounce

local benchmark_iterations = 1
assert(bounce:inner_benchmark_loop(benchmark_iterations),
       'Benchmark failed with incorrect result')

