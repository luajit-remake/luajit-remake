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

local Element = {_CLASS = 'Element'} do

function Element.new (v)
    local obj = {val = v, next = nil}
    return setmetatable(obj, {__index = Element})
end

function Element:length ()
    if not self.next then
        return 1
    else
        return 1 + self.next:length()
    end
end

end -- class Element

local list = {} do
setmetatable(list, {__index = benchmark})

function list:benchmark ()
    local result = self:tail(self:make_list(15),
                             self:make_list(10),
                             self:make_list(6))
    return result:length()
end

function list:make_list (length)
    if length == 0 then
        return nil
    else
        local e = Element.new(length)
        e.next = self:make_list(length - 1)
        return e
    end
end

function list:is_shorter_than (x, y)
    local x_tail, y_tail = x, y
    while y_tail do
        if not x_tail then
            return true
        end
        x_tail = x_tail.next
        y_tail = y_tail.next
    end
    return false
end

function list:tail (x, y, z)
    if self:is_shorter_than(y, x) then
        return self:tail(self:tail(x.next, y, z),
                         self:tail(y.next, z, x),
                         self:tail(z.next, x, y))
    else
        return z
    end
end

function list:verify_result (result)
    return 10 == result
end

end -- object list

local benchmark_iterations = (arg[1] or 5000)
assert(list:inner_benchmark_loop(benchmark_iterations),
       'Benchmark failed with incorrect result')

