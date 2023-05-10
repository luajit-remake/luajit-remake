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

local towers = {} do
setmetatable(towers, {__index = benchmark})

local function create_disk (size)
    return {size = size, next = nil}
end

function towers:benchmark ()
    self.piles = {}
    self:build_tower_at(1, 13)
    self.moves_done = 0
    self:move_disks(13, 1, 2)
    return self.moves_done
end

function towers:verify_result (result)
    return 8191 == result
end

function towers:push_disk (disk, pile)
    local top = self.piles[pile]
    if top and disk.size >= top.size then
      error 'Cannot put a big disk on a smaller one'
    end
    disk.next = top
    self.piles[pile] = disk
end

function towers:pop_disk_from (pile)
    local top = self.piles[pile]
    assert(top, 'Attempting to remove a disk from an empty pile')
    self.piles[pile] = top.next
    top.next = nil
    return top
end

function towers:move_top_disk (from_pile, to_pile)
    self:push_disk(self:pop_disk_from(from_pile), to_pile)
    self.moves_done = self.moves_done + 1
end

function towers:build_tower_at (pile, disks)
    for i = disks, 1, -1 do
        self:push_disk(create_disk(i), pile)
    end
end

function towers:move_disks (disks, from_pile, to_pile)
    if disks == 1 then
        self:move_top_disk(from_pile, to_pile)
    else
        local other_pile = 6 - from_pile - to_pile
        self:move_disks(disks - 1, from_pile, other_pile)
        self:move_top_disk(from_pile, to_pile)
        self:move_disks(disks - 1, other_pile, to_pile)
    end
end

end -- object towers

local benchmark_iterations = (arg[1] or 2000)
assert(towers:inner_benchmark_loop(benchmark_iterations),
       'Benchmark failed with incorrect result')
