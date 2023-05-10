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

local alloc_array = function (n)
    local t = {}
    t.n = n
    return t
end

local Vector = {_CLASS = 'Vector'} do

local floor = math.floor

function Vector.new (size)
    local obj = {
        storage   = alloc_array(size or 50),
        first_idx = 1,
        last_idx  = 1,
    }
    return setmetatable(obj, {__index = Vector})
end

function Vector.with (elem)
    local v = Vector.new(1)
    v:append(elem)
    return v
end

function Vector:at (idx)
    if idx > self.storage.n then
        return nil
    end
    return self.storage[idx]
end

function Vector:at_put (idx, val)
    if idx > self.storage.n then
        local new_n = self.storage.n
        while idx > new_n do
            new_n = new_n * 2
        end

        local new_storage = alloc_array(new_n)
        for i = 1, self.storage.n do
            new_storage[i] = self.storage[i]
        end
        self.storage = new_storage
    end
    self.storage[idx] = val

    if self.last_idx < idx + 1 then
        self.last_idx = idx + 1
    end
end

function Vector:append (elem)
    if self.last_idx > self.storage.n then
        -- Need to expand capacity first
        local new_storage = alloc_array(2 * self.storage.n)
        for i = 1, self.storage.n do
            new_storage[i] = self.storage[i]
        end
        self.storage = new_storage
    end

    self.storage[self.last_idx] = elem
    self.last_idx = self.last_idx + 1
end

function Vector:is_empty ()
    return self.last_idx == self.first_idx
end

function Vector:each (fn)
    for i = self.first_idx, self.last_idx - 1 do
        fn(self.storage[i])
    end
end

function Vector:has_some (fn)
    for i = self.first_idx, self.last_idx - 1 do
        if fn(self.storage[i]) then
            return true
        end
    end
    return false
end

function Vector:get_one (fn)
    for i = self.first_idx, self.last_idx - 1 do
        local e = self.storage[i]
        if fn(e) then
            return e
        end
    end
    return nil
end

function Vector:remove_first ()
    if self:is_empty() then
        return nil
    end

    self.first_idx = self.first_idx + 1
    return self.storage[self.first_idx - 1]
end

function Vector:remove (obj)
    local new_array = alloc_array(self:capacity())
    local new_last = 1
    local found = false

    self:each(function (it)
        if it == obj then
            found = true
        else
            new_array[new_last] = it
            new_last = new_last + 1
        end
    end)

    self.storage   = new_array
    self.last_idx  = new_last
    self.first_idx = 1
    return found
end

function Vector:remove_all ()
    self.first_idx = 1
    self.last_idx = 1
    self.storage = alloc_array(self:capacity())
end

function Vector:size ()
    return self.last_idx - self.first_idx
end

function Vector:capacity ()
    return self.storage.n
end

function Vector:sort (fn)
    -- Make the argument, block, be the criterion for ordering elements of
    -- the receiver.
    -- Sort blocks with side effects may not work right.
    if self:size() > 0 then
        self:sort_range(self.first_idx, self.last_idx - 1, fn)
    end
end

function Vector:sort_range (i, j, fn)
    assert(fn)

    -- The prefix d means the data at that index.
    local n = j + 1 - i
    if n <= 1 then
        -- Nothing to sort
        return
    end

    local storage = self.storage
    -- Sort di, dj
    local di = storage[i]
    local dj = storage[j]

    -- i.e., should di precede dj?
    if not fn(di, dj) then
        local tmp = storage[i]
        storage[i] = storage[j]
        storage[j] = tmp
        local tt = di
        di = dj
        dj = tt
    end

    -- NOTE: For DeltaBlue, this is never reached.
    if n > 2 then               -- More than two elements.
        local ij  = floor((i + j) / 2)  -- ij is the midpoint of i and j.
        local dij = storage[ij]         -- Sort di,dij,dj.  Make dij be their median.

        if fn(di, dij) then             -- i.e. should di precede dij?
            if not fn(dij, dj) then     -- i.e., should dij precede dj?
               local tmp = storage[j]
               storage[j] = storage[ij]
               storage[ij] = tmp
               dij = dj
            end
        else                            -- i.e. di should come after dij
            local tmp = storage[i]
            storage[i] = storage[ij]
            storage[ij] = tmp
            dij = di
        end

        if n > 3 then           -- More than three elements.
            -- Find k>i and l<j such that dk,dij,dl are in reverse order.
            -- Swap k and l.  Repeat this procedure until k and l pass each other.
            local k = i
            local l = j - 1

            while true do
                -- i.e. while dl succeeds dij
                while k <= l and fn(dij, storage[l]) do
                    l = l - 1
                end

                k = k + 1
                -- i.e. while dij succeeds dk
                while k <= l and fn(storage[k], dij) do
                    k = k + 1
                end

                if k > l then
                    break
                end

                local tmp = storage[k]
                storage[k] = storage[l]
                storage[l] = tmp
            end

            -- Now l < k (either 1 or 2 less), and di through dl are all
            -- less than or equal to dk through dj.  Sort those two segments.
            self:sort_range(i, l, fn)
            self:sort_range(k, j, fn)
        end
    end
end

end -- class Vector

local MIN_X = 0.0
local MIN_Y = 0.0
local MAX_X = 1000.0
local MAX_Y = 1000.0
local MIN_Z = 0.0
local MAX_Z = 10.0
local PROXIMITY_RADIUS = 1.0
local GOOD_VOXEL_SIZE  = PROXIMITY_RADIUS * 2.0

local Vector2D = {_CLASS = 'Vector2D'} do

function Vector2D.new (x, y)
    local obj = {x = x, y = y}
    return setmetatable(obj, {__index = Vector2D})
end

function Vector2D:plus (other)
    return Vector2D.new(self.x + other.x, self.y + other.y)
end

function Vector2D:minus (other)
    return Vector2D.new(self.x - other.x, self.y - other.y)
end

local function compare_numbers (a, b)
    if     a == b then
        return 0
    elseif a < b then
        return -1
    elseif a > b then
        return 1
    -- We say that NaN is smaller than non-NaN.
    elseif a == a then
        return 1
    else
        return -1
    end
end

function Vector2D:compare_to (other)
    local result = compare_numbers(self.x, other.x)
    if result ~= 0 then
        return result
    else
        return compare_numbers(self.y, other.y)
    end
end

end -- class Vector2D

local Vector3D = {_CLASS = 'Vector3D'} do

local sqrt = math.sqrt

function Vector3D.new (x, y, z)
    local obj = {x = x, y = y, z = z}
    return setmetatable(obj, {__index = Vector3D})
end

function Vector3D:plus (other)
    return Vector3D.new(self.x + other.x, self.y + other.y, self.z + other.z)
end

function Vector3D:minus (other)
    return Vector3D.new(self.x - other.x, self.y - other.y, self.z - other.z)
end

function Vector3D:dot (other)
    return self.x * other.x + self.y * other.y + self.z * other.z
end

function Vector3D:squared_magnitude ()
    return self:dot(self)
end

function Vector3D:magnitude ()
    return sqrt(self:squared_magnitude())
end

function Vector3D:times (amount)
    return Vector3D.new(self.x * amount, self.y * amount, self.z * amount)
end

end -- class Vector3D

local function tree_minimum (x)
    local current = x
    while current.left do
        current = current.left
    end
    return current
end

local Node = {_CLASS = 'Node'} do

function Node.new (key, value)
    local obj = {
        key    = key,
        value  = value,
        left   = nil,
        right  = nil,
        parent = nil,
        color  = 'red',
    }
    return setmetatable(obj, {__index = Node})
end

function Node:successor ()
    local x = self
    if x.right then
        return tree_minimum(x.right)
    end

    local y = x.parent
    while y and x == y.right do
        x = y
        y = y.parent
    end
    return y
end

end -- class Node

local RbtEntry = {_CLASS = 'RbtEntry'} do

function RbtEntry.new(key, value)
    local obj = {
        key   = key,
        value = value
    }
    return setmetatable(obj, {__index = RbtEntry})
end

end -- class RbtEntry

local InsertResult = {_CLASS = 'InsertResult'} do

function InsertResult.new(is_new_entry, new_node, old_value)
    local obj = {
        is_new_entry = is_new_entry,
        new_node     = new_node,
        old_value    = old_value,
    }
    return setmetatable(obj, {__index = InsertResult})
end

end -- class InsertResult

local RedBlackTree = {_CLASS = 'RedBlackTree'} do

function RedBlackTree.new ()
    local obj = {root = nil}
    return setmetatable(obj, {__index = RedBlackTree})
end

function RedBlackTree:put (key, value)
    local insertion_result = self:tree_insert(key, value)
    if not insertion_result.is_new_entry then
        return insertion_result.old_value
    end

    local x = insertion_result.new_node

    while x ~= self.root and x.parent.color == 'red' do
        if x.parent == x.parent.parent.left then
            local y = x.parent.parent.right
            if y and y.color == 'red' then
                -- Case 1
                x.parent.color = 'black'
                y.color = 'black'
                x.parent.parent.color = 'red'
                x = x.parent.parent
            else
                if x == x.parent.right then
                    -- Case 2
                    x = x.parent
                    self:left_rotate(x)
                end

                -- Case 3
                x.parent.color = 'black'
                x.parent.parent.color = 'red'
                self:right_rotate(x.parent.parent)
            end
        else
            -- Same as "then" clause with "right" and "left" exchanged.
            local y = x.parent.parent.left
            if y and y.color == 'red' then
                -- Case 1
                x.parent.color = 'black'
                y.color = 'black'
                x.parent.parent.color = 'red'
                x = x.parent.parent
            else
                if x == x.parent.left then
                    -- Case 2
                    x = x.parent
                    self:right_rotate(x)
                end

                -- Case 3
                x.parent.color = 'black'
                x.parent.parent.color = 'red'
                self:left_rotate(x.parent.parent)
            end
        end
    end

    self.root.color = 'black'
    return nil
end

function RedBlackTree:remove (key)
    local z = self:find_node(key)
    if not z then
        return nil
    end

    -- Y is the node to be unlinked from the tree.
    local y
    if not z.left or not z.right then
        y = z
    else
        y = z:successor()
    end

    -- Y is guaranteed to be non-null at this point.
    local x
    if y.left then
        x = y.left
    else
        x = y.right
    end

    -- X is the child of y which might potentially replace y
    -- in the tree. X might be null at this point.
    local x_parent
    if x then
        x.parent = y.parent
        x_parent = x.parent
    else
        x_parent = y.parent
    end

    if not y.parent then
        self.root = x
    else
        if y == y.parent.left then
            y.parent.left = x
        else
            y.parent.right = x
        end
    end

    if y ~= z then
        if y.color == 'black' then
            self:remove_fixup(x, x_parent)
        end

        y.parent = z.parent
        y.color  = z.color
        y.left   = z.left
        y.right  = z.right

        if z.left then
            z.left.parent = y
        end
        if z.right then
            z.right.parent = y
        end
        if z.parent then
            if z.parent.left == z then
                z.parent.left = y
            else
                z.parent.right = y
            end
        else
            self.root = y
        end
    elseif y.color == 'black' then
        self:remove_fixup(x, x_parent)
    end

    return z.value
end

function RedBlackTree:get (key)
    local node = self:find_node(key)
    if node then
        return node.value
    end
    return nil
end

function RedBlackTree:for_each (fn)
    if not self.root then
        return
    end
    local current = tree_minimum(self.root)
    while current do
        fn(RbtEntry.new(current.key, current.value))
        current = current:successor()
     end
end

function RedBlackTree:find_node (key)
    local current = self.root
    while current do
        local comparison_result = key:compare_to(current.key)
        if     comparison_result == 0 then
            return current
        elseif comparison_result < 0 then
            current = current.left
        else
            current = current.right
        end
    end
    return nil
end

function RedBlackTree:tree_insert (key, value)
    local y = nil
    local x = self.root

    while x do
        y = x
        local comparison_result = key:compare_to(x.key)
        if comparison_result < 0 then
            x = x.left
        elseif comparison_result > 0 then
            x = x.right
        else
            local old_value = x.value
            x.value = value
            return InsertResult.new(false, nil, old_value)
        end
    end

    local z = Node.new(key, value)
    z.parent = y

    if not y then
        self.root = z
    else
        if key:compare_to(y.key) < 0 then
            y.left = z
        else
            y.right = z
        end
    end
    return InsertResult.new(true, z, nil)
end

function RedBlackTree:left_rotate (x)
    local y = x.right

    -- Turn y's left subtree into x's right subtree.
    x.right = y.left
    if y.left then
        y.left.parent = x
    end

    -- Link x's parent to y.
    y.parent = x.parent
    if not x.parent then
        self.root = y
    else
        if x == x.parent.left then
            x.parent.left = y
        else
            x.parent.right = y
        end
    end

    -- Put x on y's left.
    y.left   = x
    x.parent = y

    return y
end

function RedBlackTree:right_rotate (y)
    local x = y.left

    -- Turn x's right subtree into y's left subtree.
    y.left = x.right
    if x.right then
        x.right.parent = y
    end

    -- Link y's parent to x.
    x.parent = y.parent
    if not y.parent then
        self.root = x
    else
        if y == y.parent.left then
            y.parent.left = x
        else
            y.parent.right = x
        end
    end

    x.right = y
    y.parent = x

    return x
end

function RedBlackTree:remove_fixup (x, x_parent)
    while x ~= self.root and (not x or x.color == 'black') do
        if x == x_parent.left then
            -- Note: the text points out that w cannot be null.
            -- The reason is not obvious from simply looking at the code;
            -- it comes about from the properties of the red-black tree.
            local w = x_parent.right
            if w.color == 'red' then
                 -- Case 1
                w.color = 'black'
                x_parent.color = 'red'
                self:left_rotate(x_parent)
                w = x_parent.right
            end
            if (not w.left or w.left.color == 'black') and
               (not w.right or w.right.color == 'black') then
                -- Case 2
                w.color = 'red'
                x = x_parent
                x_parent = x.parent
            else
                if not w.right or w.right.color == 'black' then
                    -- Case 3
                    w.left.color = 'black'
                    w.color = 'red'
                    self:right_rotate(w)
                    w = x_parent.right
                end
                -- Case 4
                w.color = x_parent.color
                x_parent.color = 'black'
                if w.right then
                    w.right.color = 'black'
                end
                self:left_rotate(x_parent)
                x = self.root
                x_parent = x.parent
            end
        else
            -- Same as "then" clause with "right" and "left" exchanged.
            local w = x_parent.left
            if w.color == 'red' then
                -- Case 1
                w.color = 'black'
                x_parent.color = 'red'
                self:right_rotate(x_parent)
                w = x_parent.left
            end
            if (not w.right or w.right.color == 'black') and
               (not w.left or w.left.color == 'black') then
                -- Case 2
                w.color = 'red'
                x = x_parent
                x_parent = x.parent
            else
                if not w.left or w.left.color == 'black' then
                    -- Case 3
                    w.right.color = 'black'
                    w.color = 'red'
                    self:left_rotate(w)
                    w = x_parent.left
                end
                -- Case 4
                w.color = x_parent.color
                x_parent.color = 'black'
                if w.left then
                    w.left.color = 'black'
                end
                self:right_rotate(x_parent)
                x = self.root
                x_parent = x.parent
            end
        end
    end
    if x then
        x.color = 'black'
    end
end

end -- class RedBlackTree

local CallSign = {_CLASS = 'CallSign'} do

function CallSign.new (value)
    local obj = {value  = value}
    return setmetatable(obj, {__index = CallSign})
end

function CallSign:compare_to (other)
    return (self.value == other.value) and 0 or ((self.value < other.value) and -1 or 1)
end

end -- class CallSign

local Collision = {_CLASS = 'Collision'} do

function Collision.new (aircraft_a, aircraft_b, position)
    local obj = {
        aircraft_a = aircraft_a,
        aircraft_b = aircraft_b,
        position   = position
    }
    return setmetatable(obj, {__index = Collision})
end

end -- class Collision

local Motion = {_CLASS = 'Motion'} do

local sqrt = math.sqrt

function Motion.new (callsign, pos_one, pos_two)
    local obj = {
        callsign = callsign,
        pos_one = pos_one,
        pos_two = pos_two,
    }
    return setmetatable(obj, {__index = Motion})
end

function Motion:delta ()
    return self.pos_two:minus(self.pos_one)
end

function Motion:find_intersection (other)
    local init1 = self.pos_one
    local init2 = other.pos_one
    local vec1 = self:delta()
    local vec2 = other:delta()
    local radius = PROXIMITY_RADIUS

    -- this test is not geometrical 3-d intersection test,
    -- it takes the fact that the aircraft move
    -- into account; so it is more like a 4d test
    -- (it assumes that both of the aircraft have a constant speed
    -- over the tested interval)

    -- we thus have two points,
    -- each of them moving on its line segment at constant speed;
    -- we are looking for times when the distance between
    -- these two points is smaller than r

    -- vec1 is vector of aircraft 1
    -- vec2 is vector of aircraft 2

    -- a = (V2 - V1)^T * (V2 - V1)
    local a = vec2:minus(vec1):squared_magnitude()

    if a ~= 0.0 then
        -- we are first looking for instances
        -- of time when the planes are exactly r from each other
        -- at least one plane is moving;
        -- if the planes are moving in parallel, they do not have constant speed

        -- if the planes are moving in parallel, then
        --   if the faster starts behind the slower,
        --     we can have 2, 1, or 0 solutions
        --   if the faster plane starts in front of the slower,
        --     we can have 0 or 1 solutions

        -- if the planes are not moving in parallel, then

        -- point P1 = I1 + vV1
        -- point P2 = I2 + vV2
        --   - looking for v, such that dist(P1,P2) = || P1 - P2 || = r

        -- it follows that || P1 - P2 || = sqrt( < P1-P2, P1-P2 > )
        --   0 = -r^2 + < P1 - P2, P1 - P2 >
        --  from properties of dot product
        --   0 = -r^2 + <I1-I2,I1-I2> + v * 2<I1-I2, V1-V2> + v^2 *<V1-V2,V1-V2>
        --   so we calculate a, b, c - and solve the quadratic equation
        --   0 = c + bv + av^2

        -- b = 2 * <I1-I2, V1-V2>
        local b = 2.0 * init1:minus(init2):dot(vec1:minus(vec2))

        -- c = -r^2 + (I2 - I1)^T * (I2 - I1)
        local c = -radius * radius + init2:minus(init1):squared_magnitude()

        local discr = b * b - 4.0 * a * c
        if discr < 0.0 then
            return nil
        end

        local v1 = (-b - sqrt(discr)) / (2.0 * a)
        local v2 = (-b + sqrt(discr)) / (2.0 * a)

        if v1 <= v2 and ((v1  <= 1.0 and 1.0 <= v2) or
            (v1  <= 0.0 and 0.0 <= v2) or
            (0.0 <= v1  and v2  <= 1.0)) then
            -- Pick a good "time" at which to report the collision.
            local v
            if v1 <= 0.0 then
                -- The collision started before this frame.
                -- Report it at the start of the frame.
                v = 0.0
            else
                -- The collision started during this frame. Report it at that moment.
                v = v1
            end

            local result1 = init1:plus(vec1:times(v))
            local result2 = init2:plus(vec2:times(v))

            local result = result1:plus(result2):times(0.5)
            if result.x >= MIN_X and
               result.x <= MAX_X and
               result.y >= MIN_Y and
               result.y <= MAX_Y and
               result.z >= MIN_Z and
               result.z <= MAX_Z then
                return result
            end
        end

        return nil
    end

    -- the planes have the same speeds and are moving in parallel
    -- (or they are not moving at all)
    -- they  thus have the same distance all the time;
    -- we calculate it from the initial point

    -- dist = || i2 - i1 || = sqrt(  ( i2 - i1 )^T * ( i2 - i1 ) )
    local dist = init2:minus(init1):magnitude()
    if dist <= radius then
        return init1:plus(init2):times(0.5)
    end
    return nil
end

end -- class Motion

local CollisionDetector = {_CLASS = 'CollisionDetector'} do

local floor = math.floor
local HORIZONTAL = Vector2D.new(GOOD_VOXEL_SIZE, 0.0)
local VERTICAL   = Vector2D.new(0.0, GOOD_VOXEL_SIZE)

function CollisionDetector.new ()
    local obj = {state = RedBlackTree.new()}
    return setmetatable(obj, {__index = CollisionDetector})
end

function CollisionDetector:handle_new_frame (frame)
    local motions = Vector.new()
    local seen    = RedBlackTree.new()

    frame:each(function (aircraft)
        local old_position = self.state:put(aircraft.callsign, aircraft.position)
        local new_position = aircraft.position
        seen:put(aircraft.callsign, true)

        if not old_position then
            -- Treat newly introduced aircraft as if they were stationary.
            old_position = new_position
        end

        motions:append(Motion.new(aircraft.callsign, old_position, new_position))
    end)

    -- Remove aircraft that are no longer present.
    local to_remove = Vector.new()
    self.state:for_each(function (e)
        if not seen:get(e.key) then
            to_remove:append(e.key)
        end
    end)

    to_remove:each(function (e)
        self.state:remove(e)
    end)

    local all_reduced = self:reduce_collision_set(motions)
    local collisions = Vector.new()
    all_reduced:each(function (reduced)
        for i = 1, reduced:size() do
            local motion1 = reduced:at(i)
            for j = i + 1, reduced:size() do
                local motion2 = reduced:at(j)
                local collision = motion1:find_intersection(motion2)
                if collision then
                    collisions:append(Collision.new(motion1.callsign,
                                                    motion2.callsign,
                                                    collision))
                end
            end
        end
    end)

    return collisions
end

function CollisionDetector:is_in_voxel (voxel, motion)
    if voxel.x > MAX_X or
       voxel.x < MIN_X or
       voxel.y > MAX_Y or
       voxel.y < MIN_Y then
        return false
    end

    local init = motion.pos_one
    local fin  = motion.pos_two

    local v_s = GOOD_VOXEL_SIZE
    local r   = PROXIMITY_RADIUS / 2.0

    local v_x = voxel.x
    local x0 = init.x
    local xv = fin.x - init.x

    local v_y = voxel.y
    local y0 = init.y
    local yv = fin.y - init.y

    local low_x  = (v_x - r - x0) / xv
    local high_x = (v_x + v_s + r - x0) / xv

    if xv < 0.0 then
        local tmp = low_x
        low_x = high_x
        high_x = tmp
    end

    local low_y  = (v_y - r - y0) / yv
    local high_y = (v_y + v_s + r - y0) / yv

    if yv < 0.0 then
        local tmp = low_y
        low_y = high_y
        high_y = tmp
    end

    return (((xv == 0.0 and v_x <= x0 + r and x0 - r <= v_x + v_s) or -- no motion in x
            (low_x <= 1.0 and 1.0 <= high_x) or (low_x <= 0.0 and 0.0 <= high_x) or
             (0.0 <= low_x and high_x <= 1.0)) and
           ((yv == 0.0 and v_y <= y0 + r and y0 - r <= v_y + v_s) or -- no motion in y
            ((low_y <= 1.0 and 1.0 <= high_y) or (low_y <= 0.0 and 0.0 <= high_y) or
             (0.0 <= low_y and high_y <= 1.0))) and
           (xv == 0.0 or yv == 0.0 or -- no motion in x or y or both
            (low_y <= high_x and high_x <= high_y) or
            (low_y <= low_x and low_x <= high_y) or
            (low_x <= low_y and high_y <= high_x)))
end

function CollisionDetector:put_into_map (voxel_map, voxel, motion)
    local array = voxel_map:get(voxel)
    if not array then
        array = Vector.new()
        voxel_map:put(voxel, array)
    end
    array:append(motion)
end

function CollisionDetector:recurse (voxel_map, seen, next_voxel, motion)
    if not self:is_in_voxel(next_voxel, motion) then
        return
    end
    if seen:put(next_voxel, true) then
        return
    end

    self:put_into_map(voxel_map, next_voxel, motion)

    self:recurse(voxel_map, seen, next_voxel:minus(HORIZONTAL), motion)
    self:recurse(voxel_map, seen, next_voxel:plus(HORIZONTAL),  motion)
    self:recurse(voxel_map, seen, next_voxel:minus(VERTICAL),   motion)
    self:recurse(voxel_map, seen, next_voxel:plus(VERTICAL),    motion)
    self:recurse(voxel_map, seen, next_voxel:minus(HORIZONTAL):minus(VERTICAL), motion)
    self:recurse(voxel_map, seen, next_voxel:minus(HORIZONTAL):plus(VERTICAL),  motion)
    self:recurse(voxel_map, seen, next_voxel:plus(HORIZONTAL):minus(VERTICAL),  motion)
    self:recurse(voxel_map, seen, next_voxel:plus(HORIZONTAL):plus(VERTICAL),   motion)
end

function CollisionDetector:reduce_collision_set (motions)
    local voxel_map = RedBlackTree.new()
    motions:each(function (motion)
        self:draw_motion_on_voxel_map(voxel_map, motion)
    end)

    local result = Vector.new()
    voxel_map:for_each(function (e)
        if e.value:size() > 1 then
            result:append(e.value)
        end
    end)
    return result
end

function CollisionDetector:voxel_hash (position)
    local x_div = floor(position.x / GOOD_VOXEL_SIZE)
    local y_div = floor(position.y / GOOD_VOXEL_SIZE)

    local x = GOOD_VOXEL_SIZE * x_div
    local y = GOOD_VOXEL_SIZE * y_div

    if position.x < 0 then
        x = x - GOOD_VOXEL_SIZE
    end
    if position.y < 0 then
        y = y - GOOD_VOXEL_SIZE
    end

    return Vector2D.new(x, y)
end

function CollisionDetector:draw_motion_on_voxel_map (voxel_map, motion)
    local seen = RedBlackTree.new()
    return self:recurse(voxel_map, seen, self:voxel_hash(motion.pos_one), motion)
end

end -- class CollisionDetector

local Aircraft = {_CLASS = 'Aircraft'} do

function Aircraft.new (callsign, position)
    local obj = {
        callsign = callsign,
        position = position
    }
    return setmetatable(obj, {__index = Aircraft})
end

end -- class Collision

local Simulator = {_CLASS = 'Simulator'} do

local cos = math.cos
local sin = math.sin

function Simulator.new (num_aircrafts)
    local aircraft = Vector.new()
    for i = 1, num_aircrafts do
        aircraft:append(CallSign.new(i))
    end
    local obj = {aircraft = aircraft}
    return setmetatable(obj, {__index = Simulator})
end

function Simulator:simulate (time)
    local frame = Vector.new()
    for i = 1, self.aircraft:size() - 1, 2 do
        frame:append(Aircraft.new(self.aircraft:at(i),
                                  Vector3D.new(time,
                                               cos(time) * 2 + (i - 1) * 3,
                                               10.0)))
        frame:append(Aircraft.new(self.aircraft:at(i + 1),
                                  Vector3D.new(time,
                                               sin(time) * 2 + (i - 1) * 3,
                                               10.0)))
    end
    return frame
end

end -- class Simulator

local cd = {} do
setmetatable(cd, {__index = benchmark})

function cd:benchmark (num_aircrafts)
    local num_frames = 200
    local simulator  = Simulator.new(num_aircrafts)
    local detector   = CollisionDetector.new()

    local actual_collisions = 0
    for i = 0, num_frames - 1 do
        local time = i / 10.0
        local collisions = detector:handle_new_frame(simulator:simulate(time))
        actual_collisions = actual_collisions + collisions:size()
    end
    return actual_collisions
end

function cd:inner_benchmark_loop (inner_iterations)
    return self:verify_result(self:benchmark(inner_iterations), inner_iterations)
end

function cd:verify_result (actual_collisions, num_aircrafts)
    print(('Result is: %d'):format(actual_collisions))
    if     num_aircrafts == 1000 then
        return actual_collisions == 14484
    elseif num_aircrafts ==  500 then
        return actual_collisions == 14484
    elseif num_aircrafts ==  250 then
        return actual_collisions == 10830
    elseif num_aircrafts ==  200 then
        return actual_collisions ==  8655
    elseif num_aircrafts ==  100 then
        return actual_collisions ==  4305
    elseif num_aircrafts ==   10 then
        return actual_collisions ==   390
    elseif num_aircrafts ==    2 then
        return actual_collisions ==    42
    else
        print(('No verification result for %d found'):format(num_aircrafts))
        print(('Result is: %d'):format(actual_collisions))
        return false
    end
end

end -- object cd

local benchmark_iterations = 2
assert(cd:inner_benchmark_loop(benchmark_iterations),
       'Benchmark failed with incorrect result')

