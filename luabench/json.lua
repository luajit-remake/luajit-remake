-- Origin: https://github.com/smarr/are-we-fast-yet
--
-- This code is derived from the SOM benchmarks, see AUTHORS.md file.
-- This benchmark is based on the minimal-json Java library maintained at:
-- https://github.com/ralfstx/minimal-json
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

local JsonValue = {_CLASS = 'JsonValue'} do

function JsonValue:is_object ()
    return false
end

function JsonValue:is_array ()
    return false
end

function JsonValue:is_number ()
    return false
end

function JsonValue:is_string ()
    return false
end

function JsonValue:is_boolean ()
    return false
end

function JsonValue:is_true ()
    return false
end

function JsonValue:is_false ()
    return false
end

function JsonValue:is_null ()
    return false
end

function JsonValue:as_object ()
    error('Unsupported operation, not an object: ' .. self:as_string())
end

function JsonValue:as_array ()
    error('Unsupported operation, not an array: ' .. self:as_string())
end

end -- abstract JsonValue

local JsonArray = {_CLASS = 'JsonArray'} do
setmetatable(JsonArray, {__index = JsonValue})

function JsonArray.new ()
    local obj = {values = Vector.new()}
    return setmetatable(obj, {__index = JsonArray})
end

function JsonArray:add (value)
    assert(value, 'value is null')
    self.values:append(value)
    return self
end

function JsonArray:size ()
    return self.values:size()
end

function JsonArray:get (index)
    return self.values:at(index)
end

function JsonArray:is_array ()
    return true
end

function JsonArray:as_array ()
    return self
end

end -- class JsonArray

local JsonLiteral = {_CLASS = 'JsonLiteral'} do
setmetatable(JsonLiteral, {__index = JsonValue})

function JsonLiteral.new (value)
    local obj = {
        value    = value,
        is_null  = 'null'  == value,
        is_true  = 'true'  == value,
        is_false = 'false' == value,
    }
    return setmetatable(obj, {__index = JsonLiteral})
end

function JsonLiteral:as_string ()
    return self.value
end

function JsonLiteral:is_boolean ()
    return self.is_true or self.is_false
end

JsonLiteral.NULL  = JsonLiteral.new('null')
JsonLiteral.TRUE  = JsonLiteral.new('true')
JsonLiteral.FALSE = JsonLiteral.new('false')

end -- class JsonLiteral

local JsonNumber = {_CLASS = 'JsonNumber'} do
setmetatable(JsonNumber, {__index = JsonValue})

function JsonNumber.new (string)
    assert(string, 'string is null')
    local obj = {string = string}
    return setmetatable(obj, {__index = JsonNumber})
end

function JsonNumber:as_string ()
    return self.string
end

function JsonNumber:is_number ()
    return true
end

end -- class JsonNumber

local JsonString = {_CLASS = 'JsonString'} do
setmetatable(JsonString, {__index = JsonValue})

function JsonString.new (string)
    assert(string, 'string is null')
    local obj = {string = string}
    return setmetatable(obj, {__index = JsonString})
end

function JsonString:is_string ()
    return true
end

function JsonString:as_string ()
    return self.string
end

end -- class JsonString

local HashIndexTable = {_CLASS = 'HashIndexTable'} do

function HashIndexTable.new ()
    local obj = {
        hash_table = {length = 32;
                      0, 0, 0, 0, 0, 0, 0, 0,
                      0, 0, 0, 0, 0, 0, 0, 0,
                      0, 0, 0, 0, 0, 0, 0, 0,
                      0, 0, 0, 0, 0, 0, 0, 0},
    }
    return setmetatable(obj, {__index = HashIndexTable})
end

function HashIndexTable:add (name, index)
    local slot = self:hash_slot_for(name)
    if index < 255 then
        -- increment by 1, 0 stands for empty
        self.hash_table[slot] = (index + 1) % 256
    else
        self.hash_table[slot] = 0
    end
end

function HashIndexTable:get (name)
    local slot = self:hash_slot_for(name)
    -- subtract 1, 0 stands for empty
    return self.hash_table[slot] % 256 - 1
end

function HashIndexTable:string_hash (s)
    -- this is not a proper hash, but sufficient for the benchmark,
    -- and very portable!
    return #s * 1402589
end

function HashIndexTable:hash_slot_for (element)
    return self:string_hash(element) % self.hash_table.length + 1
end

end -- class HashIndexTable

local JsonObject = {_CLASS = 'JsonObject'} do
setmetatable(JsonObject, {__index = JsonValue})

function JsonObject.new ()
    local obj = {
        names = Vector.new(),
        values = Vector.new(),
        table = HashIndexTable.new(),
    }
    return setmetatable(obj, {__index = JsonObject})
end

function JsonObject:add (name, value)
    assert(name, 'name is null')
    assert(value, 'value is null')

    self.names:append(name)
    self.values:append(value)
    self.table:add(name, self.names:size())
    return self
end

function JsonObject:get (name)
    assert(name, 'name is null')
    local index = self:index_of(name)
    if index == -1 then
        return nil
    else
        return self.values:at(index)
    end
end

function JsonObject:size ()
    return self.names:size()
end

function JsonObject:is_empty ()
    return self.names:is_empty()
end

function JsonObject:is_object ()
    return true
end

function JsonObject:as_object ()
    return self
end

function JsonObject:index_of (name)
    local index = self.table:get(name)
    if index ~= -1 and name == self.names:at(index) then
        return index
    end
    error('NotImplemented')
end

end -- class JsonObject

local Parser = {_CLASS = 'Parser'} do

local function ParseException (message, offset, line, column)
    return ('JSON:%d:%d (%d): %s'):format(line, column, offset, message)
end

function Parser.new (str)
    local obj = {
        input   = str,
        index   = 0,
        line    = 1,
        capture_start = -1,
        column  = 0,
        current = nil,
        capture_buffer = '',
    }
    return setmetatable(obj, {__index = Parser})
end

function Parser:parse ()
    self:read()
    self:skip_white_space()
    local result = self:read_value()
    self:skip_white_space()
    assert(self:is_end_of_text(), self:error('Unexpected character'))
    return result
end

function Parser:read_value ()
    local current = self.current
    if     current == 'n' then
        return self:read_null()
    elseif current == 't' then
        return self:read_true()
    elseif current == 'f' then
        return self:read_false()
    elseif current == '"' then
        return self:read_string()
    elseif current == '[' then
        return self:read_array()
    elseif current == '{' then
        return self:read_object()
    elseif current == '-' or
           current == '0' or
           current == '1' or
           current == '2' or
           current == '3' or
           current == '4' or
           current == '5' or
           current == '6' or
           current == '7' or
           current == '8' or
           current == '9' then
        return self:read_number()
    else
        error(self:expected('value'))
    end
end

function Parser:read_array ()
    self:read()
    local array = JsonArray.new()
    self:skip_white_space()
    if self:read_char(']') then
        return array
    end

    repeat
        self:skip_white_space()
        array:add(self:read_value())
        self:skip_white_space()
    until not self:read_char(',')

    if not self:read_char(']') then
        error(self:expected("',' or ']'"))
    end
    return array
end

function Parser:read_object ()
    self:read()
    local object = JsonObject.new()
    self:skip_white_space()
    if self:read_char('}') then
        return object
    end

    repeat
        self:skip_white_space()
        local name = self:read_name()
        self:skip_white_space()
        if not self:read_char(':') then
            error(self:expected("':'"))
        end

        self:skip_white_space()
        object:add(name, self:read_value())
        self:skip_white_space()
    until not self:read_char(',')

    if not self:read_char('}') then
        error(self:expected("',' or '}'"))
    end
    return object
end

function Parser:read_name ()
    if self.current ~= '"' then
        error(self:expected('name'))
    end
    return self:read_string_internal()
end

function Parser:read_null ()
    self:read()
    self:read_required_char('u')
    self:read_required_char('l')
    self:read_required_char('l')
    return JsonLiteral.NULL
end

function Parser:read_true ()
    self:read()
    self:read_required_char('r')
    self:read_required_char('u')
    self:read_required_char('e')
    return JsonLiteral.TRUE
end

function Parser:read_false ()
    self:read()
    self:read_required_char('a')
    self:read_required_char('l')
    self:read_required_char('s')
    self:read_required_char('e')
    return JsonLiteral.FALSE
end

function Parser:read_required_char (ch)
    if not self:read_char(ch) then
        error(self:expected("'" .. ch .. "'"))
    end
end

function Parser:read_string ()
    return JsonString.new(self:read_string_internal())
end

function Parser:read_string_internal ()
    self:read()
    self:start_capture()
    while self.current ~= '"' do
        if self.current == '\\' then
            self:pause_capture()
            self:read_escape()
            self:start_capture()
        else
            self:read()
        end
    end
    local str = self:end_capture()
    self:read()
    return str
end

function Parser:read_escape ()
    self:read()
    local current = self.current
    if     current == '"' or
           current == '/' or
           current == '\\' then
        self.capture_buffer = self.capture_buffer .. current
    elseif current == 'b' then
        self.capture_buffer = self.capture_buffer .. "\b"
    elseif current == 'f' then
        self.capture_buffer = self.capture_buffer .. "\f"
    elseif current == 'n' then
        self.capture_buffer = self.capture_buffer .. "\n"
    elseif current == 'r' then
        self.capture_buffer = self.capture_buffer .. "\r"
    elseif current == 't' then
        self.capture_buffer = self.capture_buffer .. "\t"
    else
        error(self:expected('valid escape sequence'))
    end
    self:read()
end

function Parser:read_number ()
    self:start_capture()
    self:read_char('-')
    local first_digit = self.current
    if not self:read_digit() then
        error(self:expected('digit'))
    end

    if first_digit ~= '0' then
        while self:read_digit() do
        end
    end
    self:read_fraction()
    self:read_exponent()
    return JsonNumber.new(self:end_capture())
end

function Parser:read_fraction ()
    if not self:read_char('.') then
        return false
    end
    if not self:read_digit() then
        error(self:expected('digit'))
    end

    while self:read_digit() do
    end
    return true
end

function Parser:read_exponent ()
    if not self:read_char('e') and not self:read_char('E') then
        return false
    end

    if not self:read_char('+') then
        self:read_char('-')
    end

    if not self:read_digit() then
        error(self:expected('digit'))
    end

    while self:read_digit() do
    end

    return true
end

function Parser:read_char (ch)
    if self.current ~= ch then
        return false
    end
    self:read()
    return true
end

function Parser:read_digit ()
    if not self:is_digit() then
        return false
    end
    self:read()
    return true
end

function Parser:skip_white_space ()
    while self:is_white_space() do
        self:read()
    end
end

function Parser:read ()
    if '\n' == self.current then
        self.line   = self.line + 1
        self.column = 0
    end

    self.index = self.index + 1

    if self.index <= #self.input then
        self.current = self.input:sub(self.index, self.index)
    else
        self.current = nil
    end
end

function Parser:start_capture ()
    self.capture_start = self.index
end

function Parser:pause_capture ()
    local end_ = not self.current and self.index or (self.index - 1)
    self.capture_buffer = self.capture_buffer .. self.input:sub(self.capture_start, end_)
    self.capture_start = -1
end

function Parser:end_capture ()
    local end_ = not self.current and self.index or (self.index - 1)

    local captured
    if '' == self.capture_buffer then
        captured = self.input:sub(self.capture_start, end_)
    else
        self.capture_buffer = self.capture_buffer .. self.input:sub(self.capture_start, end_)
        captured = self.capture_buffer
        self.capture_buffer = ''
    end
    self.capture_start = -1
    return captured
end

function Parser:expected (expected)
    if self:is_end_of_text() then
        return self:error('Unexpected end of input')
    else
        return self:error('Expected ' .. expected)
    end
end

function Parser:error (message)
    return ParseException(message, self.index, self.line, self.column - 1)
end

function Parser:is_white_space ()
    local current = self.current
    return ' ' == current or "\t" == current or "\n" == current or "\r" == current
end

function Parser:is_digit ()
    local current = self.current
    return '0' == current or '1' == current or '2' == current or '3' == current or
           '4' == current or '5' == current or '6' == current or
           '7' == current or '8' == current or '9' == current
end

function Parser:is_end_of_text ()
    return self.current == nil
end

end -- class Parser

local json = {} do
setmetatable(json, {__index = benchmark})

local RAP_BENCHMARK_MINIFIED = "{\"head\":{\"requestCounter\":4},\"operations\":[[\"destroy\",\"w54\"],[\"set\",\"w2\",{\"activeControl\":\"w99\"}],[\"set\",\"w21\",{\"customVariant\":\"variant_navigation\"}],[\"set\",\"w28\",{\"customVariant\":\"variant_selected\"}],[\"set\",\"w53\",{\"children\":[\"w95\"]}],[\"create\",\"w95\",\"rwt.widgets.Composite\",{\"parent\":\"w53\",\"style\":[\"NONE\"],\"bounds\":[0,0,1008,586],\"children\":[\"w96\",\"w97\"],\"tabIndex\":-1,\"clientArea\":[0,0,1008,586]}],[\"create\",\"w96\",\"rwt.widgets.Label\",{\"parent\":\"w95\",\"style\":[\"NONE\"],\"bounds\":[10,30,112,26],\"tabIndex\":-1,\"customVariant\":\"variant_pageHeadline\",\"text\":\"TableViewer\"}],[\"create\",\"w97\",\"rwt.widgets.Composite\",{\"parent\":\"w95\",\"style\":[\"NONE\"],\"bounds\":[0,61,1008,525],\"children\":[\"w98\",\"w99\",\"w226\",\"w228\"],\"tabIndex\":-1,\"clientArea\":[0,0,1008,525]}],[\"create\",\"w98\",\"rwt.widgets.Text\",{\"parent\":\"w97\",\"style\":[\"LEFT\",\"SINGLE\",\"BORDER\"],\"bounds\":[10,10,988,32],\"tabIndex\":22,\"activeKeys\":[\"#13\",\"#27\",\"#40\"]}],[\"listen\",\"w98\",{\"KeyDown\":true,\"Modify\":true}],[\"create\",\"w99\",\"rwt.widgets.Grid\",{\"parent\":\"w97\",\"style\":[\"SINGLE\",\"BORDER\"],\"appearance\":\"table\",\"indentionWidth\":0,\"treeColumn\":-1,\"markupEnabled\":false}],[\"create\",\"w100\",\"rwt.widgets.ScrollBar\",{\"parent\":\"w99\",\"style\":[\"HORIZONTAL\"]}],[\"create\",\"w101\",\"rwt.widgets.ScrollBar\",{\"parent\":\"w99\",\"style\":[\"VERTICAL\"]}],[\"set\",\"w99\",{\"bounds\":[10,52,988,402],\"children\":[],\"tabIndex\":23,\"activeKeys\":[\"CTRL+#70\",\"CTRL+#78\",\"CTRL+#82\",\"CTRL+#89\",\"CTRL+#83\",\"CTRL+#71\",\"CTRL+#69\"],\"cancelKeys\":[\"CTRL+#70\",\"CTRL+#78\",\"CTRL+#82\",\"CTRL+#89\",\"CTRL+#83\",\"CTRL+#71\",\"CTRL+#69\"]}],[\"listen\",\"w99\",{\"MouseDown\":true,\"MouseUp\":true,\"MouseDoubleClick\":true,\"KeyDown\":true}],[\"set\",\"w99\",{\"itemCount\":118,\"itemHeight\":28,\"itemMetrics\":[[0,0,50,3,0,3,44],[1,50,50,53,0,53,44],[2,100,140,103,0,103,134],[3,240,180,243,0,243,174],[4,420,50,423,0,423,44],[5,470,50,473,0,473,44]],\"columnCount\":6,\"headerHeight\":35,\"headerVisible\":true,\"linesVisible\":true,\"focusItem\":\"w108\",\"selection\":[\"w108\"]}],[\"listen\",\"w99\",{\"Selection\":true,\"DefaultSelection\":true}],[\"set\",\"w99\",{\"enableCellToolTip\":true}],[\"listen\",\"w100\",{\"Selection\":true}],[\"set\",\"w101\",{\"visibility\":true}],[\"listen\",\"w101\",{\"Selection\":true}],[\"create\",\"w102\",\"rwt.widgets.GridColumn\",{\"parent\":\"w99\",\"text\":\"Nr.\",\"width\":50,\"moveable\":true}],[\"listen\",\"w102\",{\"Selection\":true}],[\"create\",\"w103\",\"rwt.widgets.GridColumn\",{\"parent\":\"w99\",\"text\":\"Sym.\",\"index\":1,\"left\":50,\"width\":50,\"moveable\":true}],[\"listen\",\"w103\",{\"Selection\":true}],[\"create\",\"w104\",\"rwt.widgets.GridColumn\",{\"parent\":\"w99\",\"text\":\"Name\",\"index\":2,\"left\":100,\"width\":140,\"moveable\":true}],[\"listen\",\"w104\",{\"Selection\":true}],[\"create\",\"w105\",\"rwt.widgets.GridColumn\",{\"parent\":\"w99\",\"text\":\"Series\",\"index\":3,\"left\":240,\"width\":180,\"moveable\":true}],[\"listen\",\"w105\",{\"Selection\":true}],[\"create\",\"w106\",\"rwt.widgets.GridColumn\",{\"parent\":\"w99\",\"text\":\"Group\",\"index\":4,\"left\":420,\"width\":50,\"moveable\":true}],[\"listen\",\"w106\",{\"Selection\":true}],[\"create\",\"w107\",\"rwt.widgets.GridColumn\",{\"parent\":\"w99\",\"text\":\"Period\",\"index\":5,\"left\":470,\"width\":50,\"moveable\":true}],[\"listen\",\"w107\",{\"Selection\":true}],[\"create\",\"w108\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":0,\"texts\":[\"1\",\"H\",\"Hydrogen\",\"Nonmetal\",\"1\",\"1\"],\"cellBackgrounds\":[null,null,null,[138,226,52,255],null,null]}],[\"create\",\"w109\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":1,\"texts\":[\"2\",\"He\",\"Helium\",\"Noble gas\",\"18\",\"1\"],\"cellBackgrounds\":[null,null,null,[114,159,207,255],null,null]}],[\"create\",\"w110\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":2,\"texts\":[\"3\",\"Li\",\"Lithium\",\"Alkali metal\",\"1\",\"2\"],\"cellBackgrounds\":[null,null,null,[239,41,41,255],null,null]}],[\"create\",\"w111\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":3,\"texts\":[\"4\",\"Be\",\"Beryllium\",\"Alkaline earth metal\",\"2\",\"2\"],\"cellBackgrounds\":[null,null,null,[233,185,110,255],null,null]}],[\"create\",\"w112\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":4,\"texts\":[\"5\",\"B\",\"Boron\",\"Metalloid\",\"13\",\"2\"],\"cellBackgrounds\":[null,null,null,[156,159,153,255],null,null]}],[\"create\",\"w113\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":5,\"texts\":[\"6\",\"C\",\"Carbon\",\"Nonmetal\",\"14\",\"2\"],\"cellBackgrounds\":[null,null,null,[138,226,52,255],null,null]}],[\"create\",\"w114\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":6,\"texts\":[\"7\",\"N\",\"Nitrogen\",\"Nonmetal\",\"15\",\"2\"],\"cellBackgrounds\":[null,null,null,[138,226,52,255],null,null]}],[\"create\",\"w115\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":7,\"texts\":[\"8\",\"O\",\"Oxygen\",\"Nonmetal\",\"16\",\"2\"],\"cellBackgrounds\":[null,null,null,[138,226,52,255],null,null]}],[\"create\",\"w116\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":8,\"texts\":[\"9\",\"F\",\"Fluorine\",\"Halogen\",\"17\",\"2\"],\"cellBackgrounds\":[null,null,null,[252,233,79,255],null,null]}],[\"create\",\"w117\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":9,\"texts\":[\"10\",\"Ne\",\"Neon\",\"Noble gas\",\"18\",\"2\"],\"cellBackgrounds\":[null,null,null,[114,159,207,255],null,null]}],[\"create\",\"w118\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":10,\"texts\":[\"11\",\"Na\",\"Sodium\",\"Alkali metal\",\"1\",\"3\"],\"cellBackgrounds\":[null,null,null,[239,41,41,255],null,null]}],[\"create\",\"w119\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":11,\"texts\":[\"12\",\"Mg\",\"Magnesium\",\"Alkaline earth metal\",\"2\",\"3\"],\"cellBackgrounds\":[null,null,null,[233,185,110,255],null,null]}],[\"create\",\"w120\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":12,\"texts\":[\"13\",\"Al\",\"Aluminium\",\"Poor metal\",\"13\",\"3\"],\"cellBackgrounds\":[null,null,null,[238,238,236,255],null,null]}],[\"create\",\"w121\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":13,\"texts\":[\"14\",\"Si\",\"Silicon\",\"Metalloid\",\"14\",\"3\"],\"cellBackgrounds\":[null,null,null,[156,159,153,255],null,null]}],[\"create\",\"w122\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":14,\"texts\":[\"15\",\"P\",\"Phosphorus\",\"Nonmetal\",\"15\",\"3\"],\"cellBackgrounds\":[null,null,null,[138,226,52,255],null,null]}],[\"create\",\"w123\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":15,\"texts\":[\"16\",\"S\",\"Sulfur\",\"Nonmetal\",\"16\",\"3\"],\"cellBackgrounds\":[null,null,null,[138,226,52,255],null,null]}],[\"create\",\"w124\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":16,\"texts\":[\"17\",\"Cl\",\"Chlorine\",\"Halogen\",\"17\",\"3\"],\"cellBackgrounds\":[null,null,null,[252,233,79,255],null,null]}],[\"create\",\"w125\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":17,\"texts\":[\"18\",\"Ar\",\"Argon\",\"Noble gas\",\"18\",\"3\"],\"cellBackgrounds\":[null,null,null,[114,159,207,255],null,null]}],[\"create\",\"w126\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":18,\"texts\":[\"19\",\"K\",\"Potassium\",\"Alkali metal\",\"1\",\"4\"],\"cellBackgrounds\":[null,null,null,[239,41,41,255],null,null]}],[\"create\",\"w127\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":19,\"texts\":[\"20\",\"Ca\",\"Calcium\",\"Alkaline earth metal\",\"2\",\"4\"],\"cellBackgrounds\":[null,null,null,[233,185,110,255],null,null]}],[\"create\",\"w128\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":20,\"texts\":[\"21\",\"Sc\",\"Scandium\",\"Transition metal\",\"3\",\"4\"],\"cellBackgrounds\":[null,null,null,[252,175,62,255],null,null]}],[\"create\",\"w129\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":21,\"texts\":[\"22\",\"Ti\",\"Titanium\",\"Transition metal\",\"4\",\"4\"],\"cellBackgrounds\":[null,null,null,[252,175,62,255],null,null]}],[\"create\",\"w130\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":22,\"texts\":[\"23\",\"V\",\"Vanadium\",\"Transition metal\",\"5\",\"4\"],\"cellBackgrounds\":[null,null,null,[252,175,62,255],null,null]}],[\"create\",\"w131\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":23,\"texts\":[\"24\",\"Cr\",\"Chromium\",\"Transition metal\",\"6\",\"4\"],\"cellBackgrounds\":[null,null,null,[252,175,62,255],null,null]}],[\"create\",\"w132\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":24,\"texts\":[\"25\",\"Mn\",\"Manganese\",\"Transition metal\",\"7\",\"4\"],\"cellBackgrounds\":[null,null,null,[252,175,62,255],null,null]}],[\"create\",\"w133\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":25,\"texts\":[\"26\",\"Fe\",\"Iron\",\"Transition metal\",\"8\",\"4\"],\"cellBackgrounds\":[null,null,null,[252,175,62,255],null,null]}],[\"create\",\"w134\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":26,\"texts\":[\"27\",\"Co\",\"Cobalt\",\"Transition metal\",\"9\",\"4\"],\"cellBackgrounds\":[null,null,null,[252,175,62,255],null,null]}],[\"create\",\"w135\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":27,\"texts\":[\"28\",\"Ni\",\"Nickel\",\"Transition metal\",\"10\",\"4\"],\"cellBackgrounds\":[null,null,null,[252,175,62,255],null,null]}],[\"create\",\"w136\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":28,\"texts\":[\"29\",\"Cu\",\"Copper\",\"Transition metal\",\"11\",\"4\"],\"cellBackgrounds\":[null,null,null,[252,175,62,255],null,null]}],[\"create\",\"w137\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":29,\"texts\":[\"30\",\"Zn\",\"Zinc\",\"Transition metal\",\"12\",\"4\"],\"cellBackgrounds\":[null,null,null,[252,175,62,255],null,null]}],[\"create\",\"w138\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":30,\"texts\":[\"31\",\"Ga\",\"Gallium\",\"Poor metal\",\"13\",\"4\"],\"cellBackgrounds\":[null,null,null,[238,238,236,255],null,null]}],[\"create\",\"w139\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":31,\"texts\":[\"32\",\"Ge\",\"Germanium\",\"Metalloid\",\"14\",\"4\"],\"cellBackgrounds\":[null,null,null,[156,159,153,255],null,null]}],[\"create\",\"w140\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":32,\"texts\":[\"33\",\"As\",\"Arsenic\",\"Metalloid\",\"15\",\"4\"],\"cellBackgrounds\":[null,null,null,[156,159,153,255],null,null]}],[\"create\",\"w141\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":33,\"texts\":[\"34\",\"Se\",\"Selenium\",\"Nonmetal\",\"16\",\"4\"],\"cellBackgrounds\":[null,null,null,[138,226,52,255],null,null]}],[\"create\",\"w142\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":34,\"texts\":[\"35\",\"Br\",\"Bromine\",\"Halogen\",\"17\",\"4\"],\"cellBackgrounds\":[null,null,null,[252,233,79,255],null,null]}],[\"create\",\"w143\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":35,\"texts\":[\"36\",\"Kr\",\"Krypton\",\"Noble gas\",\"18\",\"4\"],\"cellBackgrounds\":[null,null,null,[114,159,207,255],null,null]}],[\"create\",\"w144\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":36,\"texts\":[\"37\",\"Rb\",\"Rubidium\",\"Alkali metal\",\"1\",\"5\"],\"cellBackgrounds\":[null,null,null,[239,41,41,255],null,null]}],[\"create\",\"w145\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":37,\"texts\":[\"38\",\"Sr\",\"Strontium\",\"Alkaline earth metal\",\"2\",\"5\"],\"cellBackgrounds\":[null,null,null,[233,185,110,255],null,null]}],[\"create\",\"w146\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":38,\"texts\":[\"39\",\"Y\",\"Yttrium\",\"Transition metal\",\"3\",\"5\"],\"cellBackgrounds\":[null,null,null,[252,175,62,255],null,null]}],[\"create\",\"w147\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":39,\"texts\":[\"40\",\"Zr\",\"Zirconium\",\"Transition metal\",\"4\",\"5\"],\"cellBackgrounds\":[null,null,null,[252,175,62,255],null,null]}],[\"create\",\"w148\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":40,\"texts\":[\"41\",\"Nb\",\"Niobium\",\"Transition metal\",\"5\",\"5\"],\"cellBackgrounds\":[null,null,null,[252,175,62,255],null,null]}],[\"create\",\"w149\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":41,\"texts\":[\"42\",\"Mo\",\"Molybdenum\",\"Transition metal\",\"6\",\"5\"],\"cellBackgrounds\":[null,null,null,[252,175,62,255],null,null]}],[\"create\",\"w150\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":42,\"texts\":[\"43\",\"Tc\",\"Technetium\",\"Transition metal\",\"7\",\"5\"],\"cellBackgrounds\":[null,null,null,[252,175,62,255],null,null]}],[\"create\",\"w151\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":43,\"texts\":[\"44\",\"Ru\",\"Ruthenium\",\"Transition metal\",\"8\",\"5\"],\"cellBackgrounds\":[null,null,null,[252,175,62,255],null,null]}],[\"create\",\"w152\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":44,\"texts\":[\"45\",\"Rh\",\"Rhodium\",\"Transition metal\",\"9\",\"5\"],\"cellBackgrounds\":[null,null,null,[252,175,62,255],null,null]}],[\"create\",\"w153\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":45,\"texts\":[\"46\",\"Pd\",\"Palladium\",\"Transition metal\",\"10\",\"5\"],\"cellBackgrounds\":[null,null,null,[252,175,62,255],null,null]}],[\"create\",\"w154\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":46,\"texts\":[\"47\",\"Ag\",\"Silver\",\"Transition metal\",\"11\",\"5\"],\"cellBackgrounds\":[null,null,null,[252,175,62,255],null,null]}],[\"create\",\"w155\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":47,\"texts\":[\"48\",\"Cd\",\"Cadmium\",\"Transition metal\",\"12\",\"5\"],\"cellBackgrounds\":[null,null,null,[252,175,62,255],null,null]}],[\"create\",\"w156\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":48,\"texts\":[\"49\",\"In\",\"Indium\",\"Poor metal\",\"13\",\"5\"],\"cellBackgrounds\":[null,null,null,[238,238,236,255],null,null]}],[\"create\",\"w157\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":49,\"texts\":[\"50\",\"Sn\",\"Tin\",\"Poor metal\",\"14\",\"5\"],\"cellBackgrounds\":[null,null,null,[238,238,236,255],null,null]}],[\"create\",\"w158\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":50,\"texts\":[\"51\",\"Sb\",\"Antimony\",\"Metalloid\",\"15\",\"5\"],\"cellBackgrounds\":[null,null,null,[156,159,153,255],null,null]}],[\"create\",\"w159\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":51,\"texts\":[\"52\",\"Te\",\"Tellurium\",\"Metalloid\",\"16\",\"5\"],\"cellBackgrounds\":[null,null,null,[156,159,153,255],null,null]}],[\"create\",\"w160\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":52,\"texts\":[\"53\",\"I\",\"Iodine\",\"Halogen\",\"17\",\"5\"],\"cellBackgrounds\":[null,null,null,[252,233,79,255],null,null]}],[\"create\",\"w161\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":53,\"texts\":[\"54\",\"Xe\",\"Xenon\",\"Noble gas\",\"18\",\"5\"],\"cellBackgrounds\":[null,null,null,[114,159,207,255],null,null]}],[\"create\",\"w162\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":54,\"texts\":[\"55\",\"Cs\",\"Caesium\",\"Alkali metal\",\"1\",\"6\"],\"cellBackgrounds\":[null,null,null,[239,41,41,255],null,null]}],[\"create\",\"w163\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":55,\"texts\":[\"56\",\"Ba\",\"Barium\",\"Alkaline earth metal\",\"2\",\"6\"],\"cellBackgrounds\":[null,null,null,[233,185,110,255],null,null]}],[\"create\",\"w164\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":56,\"texts\":[\"57\",\"La\",\"Lanthanum\",\"Lanthanide\",\"3\",\"6\"],\"cellBackgrounds\":[null,null,null,[173,127,168,255],null,null]}],[\"create\",\"w165\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":57,\"texts\":[\"58\",\"Ce\",\"Cerium\",\"Lanthanide\",\"3\",\"6\"],\"cellBackgrounds\":[null,null,null,[173,127,168,255],null,null]}],[\"create\",\"w166\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":58,\"texts\":[\"59\",\"Pr\",\"Praseodymium\",\"Lanthanide\",\"3\",\"6\"],\"cellBackgrounds\":[null,null,null,[173,127,168,255],null,null]}],[\"create\",\"w167\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":59,\"texts\":[\"60\",\"Nd\",\"Neodymium\",\"Lanthanide\",\"3\",\"6\"],\"cellBackgrounds\":[null,null,null,[173,127,168,255],null,null]}],[\"create\",\"w168\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":60,\"texts\":[\"61\",\"Pm\",\"Promethium\",\"Lanthanide\",\"3\",\"6\"],\"cellBackgrounds\":[null,null,null,[173,127,168,255],null,null]}],[\"create\",\"w169\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":61,\"texts\":[\"62\",\"Sm\",\"Samarium\",\"Lanthanide\",\"3\",\"6\"],\"cellBackgrounds\":[null,null,null,[173,127,168,255],null,null]}],[\"create\",\"w170\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":62,\"texts\":[\"63\",\"Eu\",\"Europium\",\"Lanthanide\",\"3\",\"6\"],\"cellBackgrounds\":[null,null,null,[173,127,168,255],null,null]}],[\"create\",\"w171\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":63,\"texts\":[\"64\",\"Gd\",\"Gadolinium\",\"Lanthanide\",\"3\",\"6\"],\"cellBackgrounds\":[null,null,null,[173,127,168,255],null,null]}],[\"create\",\"w172\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":64,\"texts\":[\"65\",\"Tb\",\"Terbium\",\"Lanthanide\",\"3\",\"6\"],\"cellBackgrounds\":[null,null,null,[173,127,168,255],null,null]}],[\"create\",\"w173\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":65,\"texts\":[\"66\",\"Dy\",\"Dysprosium\",\"Lanthanide\",\"3\",\"6\"],\"cellBackgrounds\":[null,null,null,[173,127,168,255],null,null]}],[\"create\",\"w174\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":66,\"texts\":[\"67\",\"Ho\",\"Holmium\",\"Lanthanide\",\"3\",\"6\"],\"cellBackgrounds\":[null,null,null,[173,127,168,255],null,null]}],[\"create\",\"w175\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":67,\"texts\":[\"68\",\"Er\",\"Erbium\",\"Lanthanide\",\"3\",\"6\"],\"cellBackgrounds\":[null,null,null,[173,127,168,255],null,null]}],[\"create\",\"w176\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":68,\"texts\":[\"69\",\"Tm\",\"Thulium\",\"Lanthanide\",\"3\",\"6\"],\"cellBackgrounds\":[null,null,null,[173,127,168,255],null,null]}],[\"create\",\"w177\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":69,\"texts\":[\"70\",\"Yb\",\"Ytterbium\",\"Lanthanide\",\"3\",\"6\"],\"cellBackgrounds\":[null,null,null,[173,127,168,255],null,null]}],[\"create\",\"w178\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":70,\"texts\":[\"71\",\"Lu\",\"Lutetium\",\"Lanthanide\",\"3\",\"6\"],\"cellBackgrounds\":[null,null,null,[173,127,168,255],null,null]}],[\"create\",\"w179\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":71,\"texts\":[\"72\",\"Hf\",\"Hafnium\",\"Transition metal\",\"4\",\"6\"],\"cellBackgrounds\":[null,null,null,[252,175,62,255],null,null]}],[\"create\",\"w180\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":72,\"texts\":[\"73\",\"Ta\",\"Tantalum\",\"Transition metal\",\"5\",\"6\"],\"cellBackgrounds\":[null,null,null,[252,175,62,255],null,null]}],[\"create\",\"w181\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":73,\"texts\":[\"74\",\"W\",\"Tungsten\",\"Transition metal\",\"6\",\"6\"],\"cellBackgrounds\":[null,null,null,[252,175,62,255],null,null]}],[\"create\",\"w182\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":74,\"texts\":[\"75\",\"Re\",\"Rhenium\",\"Transition metal\",\"7\",\"6\"],\"cellBackgrounds\":[null,null,null,[252,175,62,255],null,null]}],[\"create\",\"w183\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":75,\"texts\":[\"76\",\"Os\",\"Osmium\",\"Transition metal\",\"8\",\"6\"],\"cellBackgrounds\":[null,null,null,[252,175,62,255],null,null]}],[\"create\",\"w184\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":76,\"texts\":[\"77\",\"Ir\",\"Iridium\",\"Transition metal\",\"9\",\"6\"],\"cellBackgrounds\":[null,null,null,[252,175,62,255],null,null]}],[\"create\",\"w185\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":77,\"texts\":[\"78\",\"Pt\",\"Platinum\",\"Transition metal\",\"10\",\"6\"],\"cellBackgrounds\":[null,null,null,[252,175,62,255],null,null]}],[\"create\",\"w186\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":78,\"texts\":[\"79\",\"Au\",\"Gold\",\"Transition metal\",\"11\",\"6\"],\"cellBackgrounds\":[null,null,null,[252,175,62,255],null,null]}],[\"create\",\"w187\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":79,\"texts\":[\"80\",\"Hg\",\"Mercury\",\"Transition metal\",\"12\",\"6\"],\"cellBackgrounds\":[null,null,null,[252,175,62,255],null,null]}],[\"create\",\"w188\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":80,\"texts\":[\"81\",\"Tl\",\"Thallium\",\"Poor metal\",\"13\",\"6\"],\"cellBackgrounds\":[null,null,null,[238,238,236,255],null,null]}],[\"create\",\"w189\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":81,\"texts\":[\"82\",\"Pb\",\"Lead\",\"Poor metal\",\"14\",\"6\"],\"cellBackgrounds\":[null,null,null,[238,238,236,255],null,null]}],[\"create\",\"w190\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":82,\"texts\":[\"83\",\"Bi\",\"Bismuth\",\"Poor metal\",\"15\",\"6\"],\"cellBackgrounds\":[null,null,null,[238,238,236,255],null,null]}],[\"create\",\"w191\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":83,\"texts\":[\"84\",\"Po\",\"Polonium\",\"Metalloid\",\"16\",\"6\"],\"cellBackgrounds\":[null,null,null,[156,159,153,255],null,null]}],[\"create\",\"w192\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":84,\"texts\":[\"85\",\"At\",\"Astatine\",\"Halogen\",\"17\",\"6\"],\"cellBackgrounds\":[null,null,null,[252,233,79,255],null,null]}],[\"create\",\"w193\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":85,\"texts\":[\"86\",\"Rn\",\"Radon\",\"Noble gas\",\"18\",\"6\"],\"cellBackgrounds\":[null,null,null,[114,159,207,255],null,null]}],[\"create\",\"w194\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":86,\"texts\":[\"87\",\"Fr\",\"Francium\",\"Alkali metal\",\"1\",\"7\"],\"cellBackgrounds\":[null,null,null,[239,41,41,255],null,null]}],[\"create\",\"w195\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":87,\"texts\":[\"88\",\"Ra\",\"Radium\",\"Alkaline earth metal\",\"2\",\"7\"],\"cellBackgrounds\":[null,null,null,[233,185,110,255],null,null]}],[\"create\",\"w196\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":88,\"texts\":[\"89\",\"Ac\",\"Actinium\",\"Actinide\",\"3\",\"7\"],\"cellBackgrounds\":[null,null,null,[173,127,168,255],null,null]}],[\"create\",\"w197\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":89,\"texts\":[\"90\",\"Th\",\"Thorium\",\"Actinide\",\"3\",\"7\"],\"cellBackgrounds\":[null,null,null,[173,127,168,255],null,null]}],[\"create\",\"w198\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":90,\"texts\":[\"91\",\"Pa\",\"Protactinium\",\"Actinide\",\"3\",\"7\"],\"cellBackgrounds\":[null,null,null,[173,127,168,255],null,null]}],[\"create\",\"w199\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":91,\"texts\":[\"92\",\"U\",\"Uranium\",\"Actinide\",\"3\",\"7\"],\"cellBackgrounds\":[null,null,null,[173,127,168,255],null,null]}],[\"create\",\"w200\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":92,\"texts\":[\"93\",\"Np\",\"Neptunium\",\"Actinide\",\"3\",\"7\"],\"cellBackgrounds\":[null,null,null,[173,127,168,255],null,null]}],[\"create\",\"w201\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":93,\"texts\":[\"94\",\"Pu\",\"Plutonium\",\"Actinide\",\"3\",\"7\"],\"cellBackgrounds\":[null,null,null,[173,127,168,255],null,null]}],[\"create\",\"w202\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":94,\"texts\":[\"95\",\"Am\",\"Americium\",\"Actinide\",\"3\",\"7\"],\"cellBackgrounds\":[null,null,null,[173,127,168,255],null,null]}],[\"create\",\"w203\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":95,\"texts\":[\"96\",\"Cm\",\"Curium\",\"Actinide\",\"3\",\"7\"],\"cellBackgrounds\":[null,null,null,[173,127,168,255],null,null]}],[\"create\",\"w204\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":96,\"texts\":[\"97\",\"Bk\",\"Berkelium\",\"Actinide\",\"3\",\"7\"],\"cellBackgrounds\":[null,null,null,[173,127,168,255],null,null]}],[\"create\",\"w205\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":97,\"texts\":[\"98\",\"Cf\",\"Californium\",\"Actinide\",\"3\",\"7\"],\"cellBackgrounds\":[null,null,null,[173,127,168,255],null,null]}],[\"create\",\"w206\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":98,\"texts\":[\"99\",\"Es\",\"Einsteinium\",\"Actinide\",\"3\",\"7\"],\"cellBackgrounds\":[null,null,null,[173,127,168,255],null,null]}],[\"create\",\"w207\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":99,\"texts\":[\"100\",\"Fm\",\"Fermium\",\"Actinide\",\"3\",\"7\"],\"cellBackgrounds\":[null,null,null,[173,127,168,255],null,null]}],[\"create\",\"w208\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":100,\"texts\":[\"101\",\"Md\",\"Mendelevium\",\"Actinide\",\"3\",\"7\"],\"cellBackgrounds\":[null,null,null,[173,127,168,255],null,null]}],[\"create\",\"w209\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":101,\"texts\":[\"102\",\"No\",\"Nobelium\",\"Actinide\",\"3\",\"7\"],\"cellBackgrounds\":[null,null,null,[173,127,168,255],null,null]}],[\"create\",\"w210\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":102,\"texts\":[\"103\",\"Lr\",\"Lawrencium\",\"Actinide\",\"3\",\"7\"],\"cellBackgrounds\":[null,null,null,[173,127,168,255],null,null]}],[\"create\",\"w211\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":103,\"texts\":[\"104\",\"Rf\",\"Rutherfordium\",\"Transition metal\",\"4\",\"7\"],\"cellBackgrounds\":[null,null,null,[252,175,62,255],null,null]}],[\"create\",\"w212\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":104,\"texts\":[\"105\",\"Db\",\"Dubnium\",\"Transition metal\",\"5\",\"7\"],\"cellBackgrounds\":[null,null,null,[252,175,62,255],null,null]}],[\"create\",\"w213\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":105,\"texts\":[\"106\",\"Sg\",\"Seaborgium\",\"Transition metal\",\"6\",\"7\"],\"cellBackgrounds\":[null,null,null,[252,175,62,255],null,null]}],[\"create\",\"w214\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":106,\"texts\":[\"107\",\"Bh\",\"Bohrium\",\"Transition metal\",\"7\",\"7\"],\"cellBackgrounds\":[null,null,null,[252,175,62,255],null,null]}],[\"create\",\"w215\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":107,\"texts\":[\"108\",\"Hs\",\"Hassium\",\"Transition metal\",\"8\",\"7\"],\"cellBackgrounds\":[null,null,null,[252,175,62,255],null,null]}],[\"create\",\"w216\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":108,\"texts\":[\"109\",\"Mt\",\"Meitnerium\",\"Transition metal\",\"9\",\"7\"],\"cellBackgrounds\":[null,null,null,[252,175,62,255],null,null]}],[\"create\",\"w217\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":109,\"texts\":[\"110\",\"Ds\",\"Darmstadtium\",\"Transition metal\",\"10\",\"7\"],\"cellBackgrounds\":[null,null,null,[252,175,62,255],null,null]}],[\"create\",\"w218\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":110,\"texts\":[\"111\",\"Rg\",\"Roentgenium\",\"Transition metal\",\"11\",\"7\"],\"cellBackgrounds\":[null,null,null,[252,175,62,255],null,null]}],[\"create\",\"w219\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":111,\"texts\":[\"112\",\"Uub\",\"Ununbium\",\"Transition metal\",\"12\",\"7\"],\"cellBackgrounds\":[null,null,null,[252,175,62,255],null,null]}],[\"create\",\"w220\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":112,\"texts\":[\"113\",\"Uut\",\"Ununtrium\",\"Poor metal\",\"13\",\"7\"],\"cellBackgrounds\":[null,null,null,[238,238,236,255],null,null]}],[\"create\",\"w221\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":113,\"texts\":[\"114\",\"Uuq\",\"Ununquadium\",\"Poor metal\",\"14\",\"7\"],\"cellBackgrounds\":[null,null,null,[238,238,236,255],null,null]}],[\"create\",\"w222\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":114,\"texts\":[\"115\",\"Uup\",\"Ununpentium\",\"Poor metal\",\"15\",\"7\"],\"cellBackgrounds\":[null,null,null,[238,238,236,255],null,null]}],[\"create\",\"w223\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":115,\"texts\":[\"116\",\"Uuh\",\"Ununhexium\",\"Poor metal\",\"16\",\"7\"],\"cellBackgrounds\":[null,null,null,[238,238,236,255],null,null]}],[\"create\",\"w224\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":116,\"texts\":[\"117\",\"Uus\",\"Ununseptium\",\"Halogen\",\"17\",\"7\"],\"cellBackgrounds\":[null,null,null,[252,233,79,255],null,null]}],[\"create\",\"w225\",\"rwt.widgets.GridItem\",{\"parent\":\"w99\",\"index\":117,\"texts\":[\"118\",\"Uuo\",\"Ununoctium\",\"Noble gas\",\"18\",\"7\"],\"cellBackgrounds\":[null,null,null,[114,159,207,255],null,null]}],[\"create\",\"w226\",\"rwt.widgets.Composite\",{\"parent\":\"w97\",\"style\":[\"BORDER\"],\"bounds\":[10,464,988,25],\"children\":[\"w227\"],\"tabIndex\":-1,\"clientArea\":[0,0,986,23]}],[\"create\",\"w227\",\"rwt.widgets.Label\",{\"parent\":\"w226\",\"style\":[\"NONE\"],\"bounds\":[10,10,966,3],\"tabIndex\":-1,\"text\":\"Hydrogen (H)\"}],[\"create\",\"w228\",\"rwt.widgets.Label\",{\"parent\":\"w97\",\"style\":[\"WRAP\"],\"bounds\":[10,499,988,16],\"tabIndex\":-1,\"foreground\":[150,150,150,255],\"font\":[[\"Verdana\",\"Lucida Sans\",\"Arial\",\"Helvetica\",\"sans-serif\"],10,false,false],\"text\":\"Shortcuts: [CTRL+F] - Filter | Sort by: [CTRL+R] - Number, [CTRL+Y] - Symbol, [CTRL+N] - Name, [CTRL+S] - Series, [CTRL+G] - Group, [CTRL+E] - Period\"}],[\"set\",\"w1\",{\"focusControl\":\"w99\"}],[\"call\",\"rwt.client.BrowserNavigation\",\"addToHistory\",{\"entries\":[[\"tableviewer\",\"TableViewer\"]]}]]}"

function json:benchmark ()
    return Parser.new(RAP_BENCHMARK_MINIFIED):parse()
end

function json:verify_result (result)
    if not result:is_object() then
        return false
    end
    if not result:as_object():get('head'):is_object() then
        return false
    end
    if not result:as_object():get('operations'):is_array() then
        return false
    end
    return result:as_object():get('operations'):as_array():size() == 156
end

end -- object json


local benchmark_iterations = (arg[1] or 500)
assert(json:inner_benchmark_loop(benchmark_iterations),
       'Benchmark failed with incorrect result')

