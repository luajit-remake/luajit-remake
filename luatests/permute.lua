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

local permute = {} do
setmetatable(permute, {__index = benchmark})

function permute:benchmark ()
    self.count = 0
    self.v = {0, 0, 0, 0, 0, 0}
    self:permute(6)
    return self.count
end

function permute:verify_result (result)
    print(result)
    return 8660 == result
end

function permute:permute (n)
    self.count = self.count + 1
    if n ~= 0 then
        local n1 = n - 1
        self:permute(n1)
        for i = n, 1, -1 do
            self:swap(n, i)
            self:permute(n1)
            self:swap(n, i)
        end
    end
end

function permute:swap (i, j)
    local tmp = self.v[i]
    self.v[i] = self.v[j]
    self.v[j] = tmp
end

end -- object permute

local benchmark_iterations = 1
assert(permute:inner_benchmark_loop(benchmark_iterations),
       'Benchmark failed with incorrect result')

