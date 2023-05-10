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

local Entry = {_CLASS = 'Entry'} do

function Entry.new (hash, key, value, next)
    local obj = {
        hash  = hash,
        key   = key,
        value = value,
        next  = next,
    }
    return setmetatable(obj, {__index = Entry})
end

function Entry:match (hash, key)
    return self.hash == hash and self.key == key
end

end -- class Entry

local Dictionary = {_CLASS = 'Dictionary'} do

local INITIAL_CAPACITY = 16

function Dictionary.new (size)
    local obj = {
        buckets = alloc_array(size or INITIAL_CAPACITY),
        size    = 0,
    }
    return setmetatable(obj, {__index = Dictionary})
end

function Dictionary:hash (key)
    if not key then
        return 0
    end
    local hash = key:custom_hash()
    return hash
end

function Dictionary:is_empty ()
    return self.size == 0
end

function Dictionary:get_bucket_idx (hash)
    return hash % self.buckets.n + 1
end

function Dictionary:get_bucket (hash)
    return self.buckets[self:get_bucket_idx(hash)]
end

function Dictionary:at (key)
    local hash = self:hash(key)
    local e = self:get_bucket(hash)

    while e do
        if e:match(hash, key) then
            return e.value
        end
        e = e.next
    end
    return nil
end

function Dictionary:contains_key (key)
    local hash = self:hash(key)
    local e = self:get_bucket(hash)

    while e do
        if e.match(hash, key) then
            return true
        end
        e = e.next
    end
    return false
end

function Dictionary:at_put (key, value)
    local hash = self:hash(key)
    local i = self:get_bucket_idx(hash)
    local current = self.buckets[i]

    if not current then
        self.buckets[i] = self:new_entry(key, value, hash)
        self.size = self.size + 1
    else
        self:insert_bucket_entry(key, value, hash, current)
    end

    if self.size > self.buckets.n then
        self:resize()
    end
end

function Dictionary:new_entry (key, value, hash)
    return Entry.new(hash, key, value, nil)
end

function Dictionary:insert_bucket_entry (key, value, hash, head)
    local current = head

    while true do
        if current:match(hash, key) then
            current.value = value
            return
        end
        if not current.next then
            self.size = self.size + 1
            current.next = self:new_entry(key, value, hash)
            return
        end
        current = current.next
    end
end

function Dictionary:resize ()
    local old_storage = self.buckets
    self.buckets = alloc_array(old_storage.n * 2)
    self:transfer_entries(old_storage)
end

function Dictionary:transfer_entries (old_storage)
    local buckets = self.buckets
    for i = 1, old_storage.n do
        local current = old_storage[i]

        if current then
            old_storage[i] = nil
            if not current.next then
                local hash = current.hash %  buckets.n + 1
                buckets[hash] = current
            else
                self:split_bucket(old_storage, i, current)
            end
        end
    end
end

function Dictionary:split_bucket (old_storage, i, head)
    local lo_head, lo_tail = nil, nil
    local hi_head, hi_tail = nil, nil
    local current = head

    while current do
        if current.hash % (old_storage.n * 2) < old_storage.n then
            if not lo_tail then
               lo_head = current
            else
                lo_tail.next = current
            end
            lo_tail = current
        else
            if not hi_tail then
                hi_head = current
            else
                hi_tail.next = current
            end
            hi_tail = current
        end
       current = current.next
    end

    if lo_tail then
        lo_tail.next = nil
        self.buckets[i] = lo_head
    end
    if hi_tail then
        hi_tail.next = nil
        self.buckets[i + old_storage.n] = hi_head
    end
end

function Dictionary:remove_all ()
    self.buckets = alloc_array(self.buckets.n)
    self.size = 0
end

function Dictionary:keys ()
    local keys = Vector.new(self.size)
    local buckets = self.buckets
    for i = 1, buckets.n do
        local current = buckets[i]
        while current do
            keys:append(current.key)
            current = current.next
        end
    end
    return keys
end

function Dictionary:values ()
    local vals = Vector.new(self.size)
    local buckets = self.buckets
    for i = 1, buckets.n do
        local current = buckets[i]
        while current do
            vals:append(current.value)
            current = current.next
        end
    end
    return vals
end

end -- class Dictionary

local IdEntry = {_CLASS = 'IdEntry'} do
setmetatable(IdEntry, {__index = Entry})

function IdEntry.new (hash, key, value, next)
    local obj = Entry.new (hash, key, value, next)
    return setmetatable(obj, {__index = IdEntry})
end

function IdEntry:match (hash, key)
    return self.hash == hash and self.key == key
end

end -- class IdEntry

local IdentityDictionary = {_CLASS = 'IdentityDictionary'} do
setmetatable(IdentityDictionary, {__index = Dictionary})

function IdentityDictionary.new (size)
    local obj = Dictionary.new (size)
    return setmetatable(obj, {__index = Dictionary})
end

function IdentityDictionary:new_entry (key, value, hash)
    return IdEntry.new(hash, key, value, nil)
end

end -- class IdentityDictionary

local Sym = {_CLASS = 'Sym'} do

function Sym.new (hash)
    local obj = {hash = hash}
    return setmetatable(obj, {__index = Sym})
end

function Sym:custom_hash ()
    return self.hash
end

end -- class Sym

local ABSOLUTE_STRONGEST = Sym.new(0)
local REQUIRED           = Sym.new(1)
local STRONG_PREFERRED   = Sym.new(2)
local PREFERRED          = Sym.new(3)
local STRONG_DEFAULT     = Sym.new(4)
local DEFAULT            = Sym.new(5)
local WEAK_DEFAULT       = Sym.new(6)
local ABSOLUTE_WEAKEST   = Sym.new(7)

local Strength = {_CLASS = 'Strength'} do

local function create_strength_table ()
    local dict = IdentityDictionary.new()
    dict:at_put(ABSOLUTE_STRONGEST,  -10000)
    dict:at_put(REQUIRED,              -800)
    dict:at_put(STRONG_PREFERRED,      -600)
    dict:at_put(PREFERRED,             -400)
    dict:at_put(STRONG_DEFAULT,        -200)
    dict:at_put(DEFAULT,                  0)
    dict:at_put(WEAK_DEFAULT,           500)
    dict:at_put(ABSOLUTE_WEAKEST,     10000)
    return dict
end

local STRENGHT_TABLE = create_strength_table()

function Strength.new (strength_sym)
    local obj = {
        symbolic_value   = strength_sym,
        arithmetic_value = STRENGHT_TABLE:at(strength_sym),
    }
    return setmetatable(obj, {__index = Strength})
end

local function create_strength_constants ()
    local dict = IdentityDictionary.new()
    STRENGHT_TABLE:keys():each(function (key)
        dict:at_put(key, Strength.new(key))
    end)
    return dict
end

local STRENGHT_CONSTANTS = create_strength_constants()

function Strength.of (sym)
    return STRENGHT_CONSTANTS:at(sym)
end

Strength.ABSOLUTE_STRONGEST = Strength.of(ABSOLUTE_STRONGEST)
Strength.ABSOLUTE_WEAKEST   = Strength.of(ABSOLUTE_WEAKEST)
Strength.REQUIRED           = Strength.of(REQUIRED)

function Strength:same_as (strength)
    return self.arithmetic_value == strength.arithmetic_value
end

function Strength:stronger (strength)
    return self.arithmetic_value < strength.arithmetic_value
end

function Strength:weaker (strength)
    return self.arithmetic_value > strength.arithmetic_value
end

function Strength:strongest (strength)
    if strength:stronger(self) then
        return strength
    else
        return self
    end
end

function Strength:weakest (strength)
    if strength:weaker(self) then
        return strength
    else
        return self
    end
end

end -- class Strength

local AbstractConstraint = {_CLASS = 'AbstractConstraint'} do

function AbstractConstraint:build (strength_sym)
    self.strength = Strength.of(strength_sym)
end

function AbstractConstraint:is_input ()
    return false
end

function AbstractConstraint:add_constraint (planner)
    self:add_to_graph()
    planner:incremental_add(self)
end

function AbstractConstraint:destroy_constraint (planner)
    if self:is_satisfied() then
        planner:incremental_remove(self)
    end
    self:remove_from_graph()
end

function AbstractConstraint:inputs_known (mark)
    return not self:inputs_has_one(function (v)
        return not ((v.mark == mark) or v.stay or (v.determined_by == nil))
    end)
end

function AbstractConstraint:satisfy (mark, planner)
    local overridden
    self:choose_method(mark)

    if self:is_satisfied() then
        -- constraint can be satisfied
        -- mark inputs to allow cycle detection in addPropagate
        self:inputs_do(function (i)
            i.mark = mark
        end)

        local out = self:output()
        overridden = out.determined_by
        if overridden then
            overridden:mark_unsatisfied()
        end
        out.determined_by = self
        assert(planner:add_propagate(self, mark),
               'Cycle encountered adding: Constraint removed')
        out.mark = mark
    else
        overridden = nil
        assert(not self.strength:same_as(Strength.REQUIRED),
               'Failed to satisfy a required constraint')
    end
    return overridden
end

end -- abstract class AbstractConstraint

local BinaryConstraint = {_CLASS = 'BinaryConstraint'} do
setmetatable(BinaryConstraint, {__index = AbstractConstraint})

function BinaryConstraint:build (v1, v2, strength)
    AbstractConstraint.build(self, strength)
    self.v1 = v1
    self.v2 = v2
    self.direction = nil
end

function BinaryConstraint:is_satisfied ()
    return self.direction ~= nil
end

function BinaryConstraint:add_to_graph ()
    self.v1:add_constraint(self)
    self.v2:add_constraint(self)
    self.direction = nil
end

function BinaryConstraint:remove_from_graph ()
    if self.v1 then
        self.v1:remove_constraint(self)
    end
    if self.v2 then
        self.v2:remove_constraint(self)
    end
    self.direction = nil
end

function BinaryConstraint:choose_method (mark)
    if self.v1.mark == mark then
        if (self.v2.mark ~= mark) and self.strength:stronger(self.v2.walk_strength) then
            self.direction = 'forward'
            return self.direction
        else
            self.direction = nil
            return nil
        end
    end

    if self.v2.mark == mark then
        if (self.v1.mark ~= mark) and self.strength:stronger(self.v1.walk_strength) then
            self.direction = 'backward'
            return self.direction
        else
            self.direction = nil
            return nil
        end
    end

    -- If we get here, neither variable is marked, so we have a choice.
    if self.v1.walk_strength:weaker(self.v2.walk_strength) then
        if self.strength:stronger(self.v1.walk_strength) then
            self.direction = 'backward'
            return self.direction
        else
            self.direction = nil
            return nil
        end
    else
        if self.strength:stronger(self.v2.walk_strength) then
            self.direction = 'forward'
            return self.direction
        else
            self.direction = nil
            return nil
        end
    end
end

function BinaryConstraint:inputs_do (fn)
    if self.direction == 'forward' then
        fn(self.v1)
    else
        fn(self.v2)
    end
end

function BinaryConstraint:inputs_has_one (fn)
    if self.direction == 'forward' then
        return fn(self.v1)
    else
        return fn(self.v2)
    end
end

function BinaryConstraint:mark_unsatisfied ()
    self.direction = nil
end

function BinaryConstraint:output ()
    return self.direction == 'forward' and self.v2 or self.v1
end

function BinaryConstraint:recalculate ()
    local ihn, out
    if self.direction == 'forward' then
        ihn = self.v1
        out = self.v2
    else
        ihn = self.v2
        out = self.v1
    end
    out.walk_strength = self.strength:weakest(ihn.walk_strength)
    out.stay = ihn.stay
    if out.stay then
        self:execute()
    end
end

end -- abstract class BinaryConstraint

local UnaryConstraint = {_CLASS = 'UnaryConstraint'} do
setmetatable(UnaryConstraint, {__index = AbstractConstraint})

function UnaryConstraint:build (v, strength, planner)
    AbstractConstraint.build(self, strength)
    self.output_   = v
    self.satisfied = false
    self:add_constraint(planner)
end

function UnaryConstraint:is_satisfied ()
    return self.satisfied
end

function UnaryConstraint:add_to_graph ()
    self.output_:add_constraint(self)
    self.satisfied = false
end

function UnaryConstraint:remove_from_graph ()
    if self.output_ then
        self.output_:remove_constraint(self)
    end
    self.satisfied = false
end

function UnaryConstraint:choose_method (mark)
    self.satisfied = (self.output_.mark ~= mark) and
                     self.strength:stronger(self.output_.walk_strength)
    return nil
end

function UnaryConstraint:inputs_do ()
    -- No-op. I have no input variable.
end

function UnaryConstraint:inputs_has_one ()
    return false
end

function UnaryConstraint:mark_unsatisfied ()
    self.satisfied = false
end

function UnaryConstraint:output ()
    return self.output_
end

function UnaryConstraint:recalculate ()
    self.output_.walk_strength = self.strength
    self.output_.stay          = not self.is_input()
    if self.output_.stay then
        self:execute()  -- stay optimization
    end
end

end -- abstract class UnaryConstraint

local EditConstraint = {_CLASS = 'EditConstraint'} do
setmetatable(EditConstraint, {__index = UnaryConstraint})

function EditConstraint.new (v, strength, planner)
    local obj = setmetatable({}, {__index = EditConstraint})
    UnaryConstraint.build(obj, v, strength, planner)
    return obj
end

function EditConstraint:is_input ()
    return true
end

function EditConstraint:execute ()
    -- Edit constraints does nothing.
end

end -- class EditConstraint

local EqualityConstraint = {_CLASS = 'EqualityConstraint'} do
setmetatable(EqualityConstraint, {__index = BinaryConstraint})

function EqualityConstraint.new (var1, var2, strength, planner)
    local obj = setmetatable({}, {__index = EqualityConstraint})
    BinaryConstraint.build(obj, var1, var2, strength)
    obj:add_constraint(planner)
    return obj
end

function EqualityConstraint:execute ()
    if self.direction == 'forward' then
        self.v2.value = self.v1.value
    else
        self.v1.value = self.v2.value
    end
end

end -- class EqualityConstraint

local ScaleConstraint = {_CLASS = 'ScaleConstraint'} do
setmetatable(ScaleConstraint, {__index = BinaryConstraint})

function ScaleConstraint.new (src, scale, offset, dest, strength, planner)
    local obj = {
        scale = scale,
        offset = offset,
    }
    setmetatable(obj, {__index = ScaleConstraint})
    BinaryConstraint.build(obj, src, dest, strength)
    obj:add_constraint(planner)
    return obj
end

function ScaleConstraint:add_to_graph ()
    self.v1:add_constraint(self)
    self.v2:add_constraint(self)
    self.scale:add_constraint(self)
    self.offset:add_constraint(self)
    self.direction = nil
end

function ScaleConstraint:remove_from_graph ()
    if self.v1 then
        self.v1:remove_constraint(self)
    end
    if self.v2 then
        self.v2:remove_constraint(self)
    end
    if self.scale then
        self.scale:remove_constraint(self)
    end
    if self.offset then
        self.offset:remove_constraint(self)
    end
    self.direction = nil
end

function ScaleConstraint:execute ()
    if self.direction == 'forward' then
        self.v2.value = self.v1.value * self.scale.value + self.offset.value
    else
        self.v1.value = (self.v2.value - self.offset.value) / self.scale.value
    end
end

function ScaleConstraint:inputs_do (fn)
    if self.direction == 'forward' then
        fn(self.v1)
        fn(self.scale)
        fn(self.offset)
    else
        fn(self.v2)
        fn(self.scale)
        fn(self.offset)
    end
end

function ScaleConstraint:recalculate ()
    local ihn, out
    if self.direction == 'forward' then
        ihn = self.v1
        out = self.v2
    else
        out = self.v1
        ihn = self.v2
    end
    out.walk_strength = self.strength:weakest(ihn.walk_strength)
    out.stay = ihn.stay and self.scale.stay and self.offset.stay
    if out.stay then
        self:execute()  -- stay optimization
    end
end

end -- class ScaleConstraint

local StayConstraint = {_CLASS = 'StayConstraint'} do
setmetatable(StayConstraint, {__index = UnaryConstraint})

function StayConstraint.new (v, strength, planner)
    local obj = setmetatable({}, {__index = StayConstraint})
    UnaryConstraint.build(obj, v, strength, planner)
    return obj
end

function StayConstraint:execute ()
    -- Stay Constraints do nothing
end

end -- class StayConstraint

local Variable = {_CLASS = 'Variable'} do

function Variable.new (initial_value)
    local obj = {
        value         = initial_value or 0,
        constraints   = Vector.new(2),
        determined_by = nil,
        walk_strength = Strength.ABSOLUTE_WEAKEST,
        stay          = true,
        mark          = 0,
    }
    return setmetatable(obj, {__index = Variable})
end

function Variable:add_constraint (constraint)
    self.constraints:append(constraint)
end

function Variable:remove_constraint (constraint)
    self.constraints:remove(constraint)
    if  self.determined_by == constraint then
        self.determined_by = nil
    end
end

end -- class Variable

local Plan = {_CLASS = 'Plan'} do
setmetatable(Plan, {__index = Vector})

function Plan.new ()
    local obj = Vector.new(15)
    return setmetatable(obj, {__index = Plan})
end

function Plan:execute ()
    self:each(function (c)
        c:execute()
    end)
end

end -- class Plan

local Planner = {_CLASS = 'Planner'} do

function Planner.new ()
    local obj = {current_mark = 1}
    return setmetatable(obj, {__index = Planner})
end

function Planner:incremental_add (constraint)
    local mark = self:new_mark()
    local overridden = constraint:satisfy(mark, self)
    while overridden do
        overridden = overridden:satisfy(mark, self)
    end
end

function Planner:incremental_remove (constraint)
    local out = constraint:output()
    constraint:mark_unsatisfied()
    constraint:remove_from_graph()
    local unsatisfied = self:remove_propagate_from(out)
    unsatisfied:each(function (u)
        self:incremental_add(u)
    end)
end

function Planner:extract_plan_from_constraints (constraints)
    local sources = Vector.new()
    constraints:each(function (c)
        if c:is_input() and c:is_satisfied() then
            sources:append(c)
        end
    end)
    return self:make_plan(sources)
end

function Planner:make_plan (sources)
    local mark = self:new_mark()
    local plan = Plan.new()
    local todo = sources
    while not todo:is_empty () do
        local c = todo:remove_first()
        if (c:output().mark ~= mark) and c:inputs_known(mark) then
            -- not in plan already and eligible for inclusion
            plan:append(c)
            c:output().mark = mark
            self:add_constraints_consuming_to(c:output(), todo)
        end
    end
    return plan
end

function Planner:propagate_from (v)
    local todo = Vector.new()
    self:add_constraints_consuming_to(v, todo)
    while not todo:is_empty() do
        local c = todo:remove_first()
        c:execute()
        self:add_constraints_consuming_to(c:output(), todo)
    end
end

function Planner:add_constraints_consuming_to (v, coll)
    local determining_c = v.determined_by
    v.constraints:each(function (c)
        if (c ~= determining_c) and c:is_satisfied() then
            coll:append(c)
        end
    end)
end

function Planner:add_propagate (c, mark)
    local todo = Vector.with(c)
    while not todo:is_empty() do
        local d = todo:remove_first()
        if d:output().mark == mark then
            self:incremental_remove(c)
            return false
        end
        d:recalculate()
        self:add_constraints_consuming_to(d:output(), todo)
    end
    return true
end

function Planner:change_var (var, val)
    local edit_constraint = EditConstraint.new(var, PREFERRED, self)
    local plan = self:extract_plan_from_constraints(Vector.with(edit_constraint))
    for _ = 1, 10 do
        var.value = val
        plan:execute()
    end
    edit_constraint:destroy_constraint(self)
end

function Planner:constraints_consuming (v, fn)
    local determining_c = v.determined_by
    v.constraints:each(function (c)
        if (c ~= determining_c) and c:is_satisfied() then
            fn(c)
        end
    end)
end

function Planner:new_mark ()
    local current_mark = self.current_mark
    self.current_mark = current_mark + 1
    return current_mark
end

function Planner:remove_propagate_from (out)
    local unsatisfied = Vector.new()

    out.determined_by = nil
    out.walk_strength = Strength.ABSOLUTE_WEAKEST
    out.stay = true

    local todo = Vector.with(out)
    while not todo:is_empty() do
        local v = todo:remove_first()

        v.constraints:each(function (c)
            if not c:is_satisfied() then
                unsatisfied:append(c)
            end
        end)

        self:constraints_consuming(v, function (c)
            c:recalculate()
            todo:append(c:output())
        end)
    end

    unsatisfied:sort(function (c1, c2)
        return c1.strength:stronger(c2.strength)
    end)
    return unsatisfied
end

function Planner.chain_test (n)
    -- This is the standard DeltaBlue benchmark. A long chain of equality
    -- constraints is constructed with a stay constraint on one end. An
    -- edit constraint is then added to the opposite end and the time is
    -- measured for adding and removing this constraint, and extracting
    -- and executing a constraint satisfaction plan. There are two cases.
    -- In case 1, the added constraint is stronger than the stay
    -- constraint and values must propagate down the entire length of the
    -- chain. In case 2, the added constraint is weaker than the stay
    -- constraint so it cannot be accomodated. The cost in this case is,
    -- of course, very low. Typical situations lie somewhere between these
    -- two extremes.

    local planner = Planner.new()
    local vars = {}
    for i = 1, n + 1 do
        vars[i] = Variable.new()
    end

    -- thread a chain of equality constraints through the variables
    for i = 1, n do
        local v1, v2 = vars[i], vars[i + 1]
        EqualityConstraint.new(v1, v2, REQUIRED, planner)
    end

    StayConstraint.new(vars[n + 1], STRONG_DEFAULT, planner)
    local edit = EditConstraint.new(vars[1], PREFERRED, planner)
    local plan = planner:extract_plan_from_constraints(Vector.with(edit))

    for v = 1, 100 do
        vars[1].value = v
        plan:execute()
        print(vars[n + 1].value)
        assert(vars[n + 1].value == v, 'Chain test failed!')
    end

    edit:destroy_constraint(planner)
end

function Planner.projection_test (n)
    -- This test constructs a two sets of variables related to each
    -- other by a simple linear transformation (scale and offset). The
    -- time is measured to change a variable on either side of the
    -- mapping and to change the scale and offset factors.

    local planner = Planner.new()
    local dests   = Vector.new()
    local scale   = Variable.new(10)
    local offset  = Variable.new(1000)

    local src = nil
    local dst = nil

    for i = 1, n do
        src = Variable.new(i)
        dst = Variable.new(i)
        dests:append(dst)
        StayConstraint.new(src, DEFAULT, planner)
        ScaleConstraint.new(src, scale, offset, dst, REQUIRED, planner)
    end

    planner:change_var(src, 17)
    print(dst.value)
    assert(dst.value == 1170, 'Projection 1 failed')

    planner:change_var(dst, 1050)
    print(src.value)
    assert(src.value == 5, 'Projection 2 failed')

    planner:change_var(scale, 5)
    for i = 1, n - 1 do
        print(dests:at(i).value)
        assert(dests:at(i).value == i * 5 + 1000, 'Projection 3 failed')
    end

    planner:change_var(offset, 2000)
    for i = 1, n - 1 do
        print(dests:at(i).value)
        assert(dests:at(i).value == i * 5 + 2000, 'Projection 4 failed')
    end
end

end -- class Planner

local deltablue = {} do
setmetatable(deltablue, {__index = benchmark})

function deltablue:inner_benchmark_loop (inner_iterations)
    Planner.chain_test(inner_iterations)
    Planner.projection_test(inner_iterations)
    return true
end

end -- object deltablue


local benchmark_iterations = 1
assert(deltablue:inner_benchmark_loop(benchmark_iterations),
       'Benchmark failed with incorrect result')

