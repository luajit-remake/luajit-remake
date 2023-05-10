-- Origin: https://github.com/smarr/are-we-fast-yet
--
-- Adapted based on SOM benchmark.
-- Ported on Lua by Francois Perrad <francois.perrad@gadz.org>
--
-- Copyright 2011 Google Inc.
--
--     Licensed under the Apache License, Version 2.0 (the "License");
-- you may not use this file except in compliance with the License.
--     You may obtain a copy of the License at
--
-- http://www.apache.org/licenses/LICENSE-2.0
--
-- Unless required by applicable law or agreed to in writing, software
-- distributed under the License is distributed on an "AS IS" BASIS,
--     WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
--     See the License for the specific language governing permissions and
--         limitations under the License.

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

local Set = {_CLASS = 'Set'} do

local INITIAL_SIZE = 10

function Set.new (size)
    local obj = {
        items = Vector.new(size or INITIAL_SIZE)
    }
    return setmetatable(obj, {__index = Set})
end

function Set:size ()
    return self.items:size()
end

function Set:each (fn)
    self.items:each(fn)
end

function Set:has_some (fn)
    return self.items:has_some(fn)
end

function Set:get_one (fn)
    return self.items:get_one(fn)
end

function Set:add (obj)
    if not self:contains(obj) then
        self.items:append(obj)
    end
end

function Set:remove_all ()
    self.items:remove_all()
end

function Set:collect (fn)
    local coll = Vector.new()
    self:each(function (it)
        coll:append(fn(it))
    end)
    return coll
end

function Set:contains (obj)
    return self:has_some(function (it) return it == obj end)
end

end -- class Set

local IdentitySet = {_CLASS = 'IdentitySet'} do
setmetatable(IdentitySet, {__index = Set})

function IdentitySet.new (size)
    local obj = Set.new(size)
    return setmetatable(obj, {__index = IdentitySet})
end

function IdentitySet:contains (obj)
    return self:has_some(function (it) return it == obj end)
end

end -- class IdentitySet

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

local BasicBlock = {_CLASS = 'BasicBlock'} do

function BasicBlock.new (name)
    local obj = {
        name = name,
        in_edges  = Vector.new(2),
        out_edges = Vector.new(2),
    }
    return setmetatable(obj, {__index = BasicBlock})
end

function BasicBlock:num_pred ()
    return self.in_edges:size()
end

function BasicBlock:add_out_edge (to)
    self.out_edges:append(to)
end

function BasicBlock:add_in_edge (from)
    self.in_edges:append(from)
end

function BasicBlock:custom_hash ()
    return self.name
end

end -- class BasicBlock

local BasicBlockEdge = {_CLASS = 'BasicBlockEdge'} do

function BasicBlockEdge.new (cfg, from_name, to_name)
    local from = cfg:create_node(from_name)
    local to   = cfg:create_node(to_name)

    from:add_out_edge(to)
    to:add_in_edge(from)
    local obj = {
        from = from,
        to   = to,
    }
    setmetatable(obj, {__index = BasicBlockEdge})

    cfg:add_edge(obj)
    return obj
end

end -- class BasicBlockEdge

local ControlFlowGraph = {_CLASS = 'ControlFlowGraph'} do

function ControlFlowGraph.new ()
    local obj = {
        start_node = nil,
        basic_block_map = Vector.new(),
        edge_list = Vector.new(),
    }
    return setmetatable(obj, {__index = ControlFlowGraph})
end

function ControlFlowGraph:create_node (name)
    local node
    if self.basic_block_map:at(name) then
        node = self.basic_block_map:at(name)
    else
        node = BasicBlock.new(name)
        self.basic_block_map:at_put(name, node)
    end
    if self:num_nodes() == 1 then
        self.start_node = node
    end
    return node
end

function ControlFlowGraph:add_edge (edge)
    self.edge_list:append(edge)
end

function ControlFlowGraph:num_nodes ()
    return self.basic_block_map:size()
end

function ControlFlowGraph:get_start_basic_block ()
    return self.start_node
end

function ControlFlowGraph:get_basic_blocks ()
    return self.basic_block_map
end

end -- class ControlFlowGraph

local SimpleLoop = {_CLASS = 'SimpleLoop'} do

function SimpleLoop.new (bb, is_reducible)
    local obj = {
        header        = bb,
        is_reducible  = is_reducible,
        parent        = nil,
        is_root       = false,
        nesting_level = 0,
        depth_level   = 0,
        counter       = 0,
        basic_blocks  = IdentitySet.new(),
        children      = IdentitySet.new(),
    }
    if bb then
        obj.basic_blocks:add(bb)
    end
    return setmetatable(obj, {__index = SimpleLoop})
end

function SimpleLoop:add_node (bb)
    self.basic_blocks:add(bb)
end

function SimpleLoop:add_child_loop (loop)
    self.children:add(loop)
end

function SimpleLoop:set_parent (parent)
    self.parent = parent
    self.parent:add_child_loop(self)
end

function SimpleLoop:set_is_root ()
    self.is_root = true
end

function SimpleLoop:set_nesting_level (level)
    self.nesting_level = level
    if level == 0 then
        self:set_is_root()
    end
end

end -- class SimpleLoop

local LoopStructureGraph = {_CLASS = 'LoopStructureGraph'} do

function LoopStructureGraph.new ()
    local loops = Vector.new()
    local root = SimpleLoop.new(nil, true)
    local obj = {
        loop_counter = 0,
        loops = loops,
        root = root,
    }
    root:set_nesting_level(0)
    root.counter = obj.loop_counter
    obj.loop_counter = obj.loop_counter + 1
    loops:append(root)
    return setmetatable(obj, {__index = LoopStructureGraph})
end

function LoopStructureGraph:create_new_loop (bb, is_reducible)
    local loop = SimpleLoop.new(bb, is_reducible)
    loop.counter = self.loop_counter
    self.loop_counter = self.loop_counter + 1
    self.loops:append(loop)
    return loop
end

function LoopStructureGraph:calculate_nesting_level ()
    -- link up all 1st level loops to artificial root node.
    self.loops:each(function (it)
        if not it.is_root then
            if not it.parent then
                it:set_parent(self.root)
            end
        end
    end)
    -- recursively traverse the tree and assign levels.
    self:calculate_nesting_level_rec(self.root, 0)
end

local function max (a, b)
    return (a < b) and b or a
end

function LoopStructureGraph:calculate_nesting_level_rec (loop, depth)
    loop.depth_level = depth
    loop.children:each(function (it)
        self:calculate_nesting_level_rec(it, depth + 1)
        loop:set_nesting_level(max(loop.nesting_level, 1 + it.nesting_level))
    end)
end

function LoopStructureGraph:num_loops ()
    return self.loops:size()
end

end -- class LoopStructureGraph

local UnionFindNode = {_CLASS = 'UnionFindNode'} do

function UnionFindNode.new ()
    local obj = {
        dfs_number = 0,
        parent = nil,
        bb     = nil,
        loop   = nil,
    }
    return setmetatable(obj, {__index = UnionFindNode})
end

function UnionFindNode:init_node (bb, dfs_number)
    self.parent     = self
    self.bb         = bb
    self.dfs_number = dfs_number
    self.loop       = nil
end

function UnionFindNode:find_set ()
    local node_list = Vector.new()

    local node = self
    while node ~= node.parent do
        if node.parent ~= node.parent.parent then
            node_list:append(node)
        end
        node = node.parent
    end

    -- Path Compression, all nodes' parents point to the 1st level parent.
    node_list:each(function (it)
        it:union(self.parent)
    end)
    return node
end

function UnionFindNode:union (basic_block)
    self.parent = basic_block
end

end -- class UnionFindNode

local HavlakLoopFinder = {_CLASS = 'HavlakLoopFinder'} do

local UNVISITED = 2147483647            -- Marker for uninitialized nodes.
local MAXNONBACKPREDS = 32 * 1024       -- Safeguard against pathological algorithm behavior.

function HavlakLoopFinder.new (cfg, lsg)
    local obj = {
        cfg = cfg,
        lsg = lsg,
        non_back_preds = Vector.new(),
        back_preds     = Vector.new(),
        number         = IdentityDictionary.new(),
        max_size = 0,
        header = nil,
        type   = nil,
        last   = nil,
        nodes  = nil,
    }
    return setmetatable(obj, {__index = HavlakLoopFinder})
end

-- As described in the paper, determine whether a node 'w' is a
-- "true" ancestor for node 'v'.
--
-- Dominance can be tested quickly using a pre-order trick
-- for depth-first spanning trees. This is why DFS is the first
-- thing we run below.
function HavlakLoopFinder:is_ancestor (w, v)
    return (w <= v) and (v <= self.last[w])
end

-- DFS - Depth-First-Search
--
-- DESCRIPTION:
-- Simple depth first traversal along out edges with node numbering.
function HavlakLoopFinder:do_dfs (current_node, current)
    self.nodes[current]:init_node(current_node, current)
    self.number:at_put(current_node, current)

    local last_id = current
    local outer_blocks = current_node.out_edges

    outer_blocks:each(function (target)
        if self.number:at(target) == UNVISITED then
            last_id = self:do_dfs(target, last_id + 1)
        end
    end)

    self.last[current] = last_id
    return last_id
end

function HavlakLoopFinder:init_all_nodes ()
    -- Step a:
    --   - initialize all nodes as unvisited.
    --   - depth-first traversal and numbering.
    --   - unreached BB's are marked as dead.
    self.cfg:get_basic_blocks():each(function (bb)
        self.number:at_put(bb, UNVISITED)
    end)
    self:do_dfs(self.cfg:get_start_basic_block(), 1)
end

function HavlakLoopFinder:identify_edges (size)
    -- Step b:
    --   - iterate over all nodes.
    --
    --   A backedge comes from a descendant in the DFS tree, and non-backedges
    --   from non-descendants (following Tarjan).
    --
    --   - check incoming edges 'v' and add them to either
    --     - the list of backedges (backPreds) or
    --     - the list of non-backedges (nonBackPreds)
    for w = 1, size do
        self.header[w] = 1
        self.type[w] = 'BB_NONHEADER'

        local node_w = self.nodes[w].bb
        if not node_w then
            self.type[w] = 'BB_DEAD'
        else
            self:process_edges(node_w, w)
        end
    end
end

function HavlakLoopFinder:process_edges (node_w, w)
    local number = self.number
    if node_w:num_pred() > 0 then
        node_w.in_edges:each(function (node_v)
            local v = number:at(node_v)
            if v ~= UNVISITED then
                if self:is_ancestor(w, v) then
                    self.back_preds:at(w):append(v)
                else
                    self.non_back_preds:at(w):add(v)
                end
            end
        end)
    end
end

-- Find loops and build loop forest using Havlak's algorithm, which
-- is derived from Tarjan. Variable names and step numbering has
-- been chosen to be identical to the nomenclature in Havlak's
-- paper (which, in turn, is similar to the one used by Tarjan).
function HavlakLoopFinder:find_loops ()
    if not self.cfg:get_start_basic_block() then
        return
    end

    local size = self.cfg:num_nodes()
    self.non_back_preds:remove_all()
    self.back_preds:remove_all()
    self.number:remove_all()

    if size > self.max_size then
        self.header   = {}
        self.type     = {}
        self.last     = {}
        self.nodes    = {}
        self.max_size = size
    end

    for i = 1, size do
        self.non_back_preds:append(Set.new())
        self.back_preds:append(Vector.new())
        self.nodes[i] = UnionFindNode.new()
    end

    self:init_all_nodes()
    self:identify_edges(size)

    -- Start node is root of all other loops.
    self.header[0] = 0

    -- Step c:
    --
    -- The outer loop, unchanged from Tarjan. It does nothing except
    -- for those nodes which are the destinations of backedges.
    -- For a header node w, we chase backward from the sources of the
    -- backedges adding nodes to the set P, representing the body of
    -- the loop headed by w.
    --
    -- By running through the nodes in reverse of the DFST preorder,
    -- we ensure that inner loop headers will be processed before the
    -- headers for surrounding loops.
    for w = size, 1, -1 do
        -- this is 'P' in Havlak's paper
        local node_pool = Vector.new()
        local node_w = self.nodes[w].bb
        if node_w then
            self:step_d(w, node_pool)

            -- Copy nodePool to workList.
            local work_list = Vector.new()
            node_pool:each(function (it)
                work_list:append(it)
            end)

            if node_pool:size() ~= 0 then
                self.type[w] = 'BB_REDUCIBLE'
            end

            -- work the list...
            while not work_list:is_empty() do
                local x = work_list:remove_first()

                -- Step e:
                --
                -- Step e represents the main difference from Tarjan's method.
                -- Chasing upwards from the sources of a node w's backedges. If
                -- there is a node y' that is not a descendant of w, w is marked
                -- the header of an irreducible loop, there is another entry
                -- into this loop that avoids w.

                -- The algorithm has degenerated. Break and
                -- return in this case.
                local non_back_size = self.non_back_preds:at(x.dfs_number):size()
                if non_back_size > MAXNONBACKPREDS then
                    return
                end
                self:step_e_process_non_back_preds(w, node_pool, work_list, x)
            end
        end

        -- Collapse/Unionize nodes in a SCC to a single node
        -- For every SCC found, create a loop descriptor and link it in.
        if (node_pool:size() > 0) or (self.type[w] == 'BB_SELF') then
            local loop = self.lsg:create_new_loop(node_w, self.type[w] ~= 'BB_IRREDUCIBLE')
            self:set_loop_attributes(w, node_pool, loop)
        end
    end
end

function HavlakLoopFinder:step_e_process_non_back_preds (w, node_pool, work_list, x)
    self.non_back_preds:at(x.dfs_number):each(function (it)
        local y = self.nodes[it]
        local ydash = y:find_set()

        if not self:is_ancestor(w, ydash.dfs_number) then
            self.type[w] = 'BB_IRREDUCIBLE'
            self.non_back_preds:at(w):add(ydash.dfs_number)
        else
            if ydash.dfs_number ~= w then
                if not node_pool:has_some(function (e) return e == ydash end) then
                    work_list:append(ydash)
                    node_pool:append(ydash)
                end
            end
        end
    end)
end

function HavlakLoopFinder:set_loop_attributes (w, node_pool, loop)
    -- At this point, one can set attributes to the loop, such as:
    --
    -- the bottom node:
    --    iter  = backPreds[w].begin();
    --    loop bottom is: nodes[iter].node);
    --
    -- the number of backedges:
    --    backPreds[w].size()
    --
    -- whether this loop is reducible:
    --    type[w] != BasicBlockClass.BB_IRREDUCIBLE
    self.nodes[w].loop = loop
    node_pool:each(function (node)
        -- Add nodes to loop descriptor.
        self.header[node.dfs_number] = w
        node:union(self.nodes[w])

        -- Nested loops are not added, but linked together.
        if node.loop then
            node.loop:set_parent(loop)
        else
            loop:add_node(node.bb)
        end
    end)
end

function HavlakLoopFinder:step_d (w, node_pool)
    self.back_preds:at(w):each(function (v)
        if v ~= w then
            node_pool:append(self.nodes[v]:find_set())
        else
            self.type[w] = 'BB_SELF'
        end
    end)
end

end -- class HavlakLoopFinder

local LoopTesterApp = {_CLASS = 'LoopTesterApp'} do

function LoopTesterApp.new ()
    local cfg = ControlFlowGraph.new()
    local lsg = LoopStructureGraph.new()
    local obj = {
        cfg = cfg,
        lsg = lsg,
    }
    cfg:create_node(1)
    return setmetatable(obj, {__index = LoopTesterApp})
end

-- Create 4 basic blocks, corresponding to and if/then/else clause
-- with a CFG that looks like a diamond
function LoopTesterApp:build_diamond (start)
    local bb0 = start
    BasicBlockEdge.new(self.cfg, bb0, bb0 + 1)
    BasicBlockEdge.new(self.cfg, bb0, bb0 + 2)
    BasicBlockEdge.new(self.cfg, bb0 + 1, bb0 + 3)
    BasicBlockEdge.new(self.cfg, bb0 + 2, bb0 + 3)
    return bb0 + 3
end

-- Connect two existing nodes
function LoopTesterApp:build_connect (start, end_)
    BasicBlockEdge.new(self.cfg, start, end_)
end

-- Form a straight connected sequence of n basic blocks
function LoopTesterApp:build_straight (start, n)
    for i = 1, n do
        self:build_connect(start + i - 1, start + i)
    end
    return start + n
end

-- Construct a simple loop with two diamonds in it
function LoopTesterApp:build_base_loop (from)
    local header   = self:build_straight(from, 1)
    local diamond1 = self:build_diamond(header)
    local d11      = self:build_straight(diamond1, 1)
    local diamond2 = self:build_diamond(d11)
    local footer   = self:build_straight(diamond2, 1)
    self:build_connect(diamond2, d11)
    self:build_connect(diamond1, header)
    self:build_connect(footer, from)
    return self:build_straight(footer, 1)
end

function LoopTesterApp:main (num_dummy_loops, find_loop_iterations, par_loops,
                             ppar_loops, pppar_loops)
    self:construct_simple_cfg()
    self:add_dummy_loops(num_dummy_loops)
    self:construct_cfg(par_loops, ppar_loops, pppar_loops)

    -- Performing Loop Recognition, 1 Iteration, then findLoopIteration
    self:find_loops(self.lsg)

    for _ = 0, find_loop_iterations do
        self:find_loops(LoopStructureGraph.new())
    end

    self.lsg:calculate_nesting_level()
    return {self.lsg:num_loops(), self.cfg:num_nodes()}
end

function LoopTesterApp:construct_cfg (par_loops, ppar_loops, pppar_loops)
    local n = 3

    for _ = 1, par_loops do
        self.cfg:create_node(n + 1)
        self:build_connect(3, n + 1)
        n = n + 1

        for _ = 1, ppar_loops do
            local top = n
            n = self:build_straight(n, 1)
            for _ = 1, pppar_loops do
                n = self:build_base_loop(n)
            end
            local bottom = self:build_straight(n, 1)
            self:build_connect(n, top)
            n = bottom
        end
        self:build_connect(n, 1)
    end
end

function LoopTesterApp:add_dummy_loops (num_dummy_loops)
    for _ = 1, num_dummy_loops do
        self:find_loops(self.lsg)
    end
end

function LoopTesterApp:find_loops (loop_structure)
    local finder = HavlakLoopFinder.new(self.cfg, loop_structure)
    finder:find_loops()
end

function LoopTesterApp:construct_simple_cfg ()
    self.cfg:create_node(1)
    self:build_base_loop(1)
    self.cfg:create_node(2)
    BasicBlockEdge.new(self.cfg, 1, 3)
end

end -- class LoopTesterApp


local havlak = {} do
setmetatable(havlak, {__index = benchmark})

function havlak:inner_benchmark_loop (inner_iterations)
    local result = LoopTesterApp.new():main(inner_iterations, 50, 10, 10, 5)
    return self:verify_result(result, inner_iterations)
end

function havlak:verify_result (result, inner_iterations)
    if     inner_iterations == 15000 then
        return result[1] == 46602 and result[2] == 5213
    elseif inner_iterations ==  1500 then
        return result[1] ==  6102 and result[2] == 5213
    elseif inner_iterations ==   150 then
        return result[1] ==  2052 and result[2] == 5213
    elseif inner_iterations ==    15 then
        return result[1] ==  1647 and result[2] == 5213
    elseif inner_iterations ==     1 then
        return result[1] ==  1605 and result[2] == 5213
    else
        print(('No verification result for %d found'):format(inner_iterations))
        print(('Result is: %d, %d'):format(result[0], result[1]))
        return false
    end
end

end -- object havlak

local benchmark_iterations = (arg[1] or 15)
assert(havlak:inner_benchmark_loop(benchmark_iterations),
       'Benchmark failed with incorrect result')

