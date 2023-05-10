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

local storage = {} do
setmetatable(storage, {__index = benchmark})

function storage:benchmark ()
    local random = Random.new()
    self.count = 0
    self:build_tree_depth(7, random)
    return self.count
end

function storage:verify_result (result)
    print(result)
    return 5461 == result
end

function storage:build_tree_depth (depth, random)
    self.count = self.count + 1
    if depth == 1 then
        -- With Lua, an array cannot be pre-allocated.
        -- We just compute the size like in others languages.
        return {n = random:next() % 10 + 1}
    else
        local arr = {n = 4}
        for i = 1, 4 do
            arr[i] = self:build_tree_depth(depth - 1, random)
        end
        return arr
    end
end

end -- object storage

local benchmark_iterations = 1
assert(storage:inner_benchmark_loop(benchmark_iterations),
       'Benchmark failed with incorrect result')
