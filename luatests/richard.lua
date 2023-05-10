-- Origin: https://github.com/smarr/are-we-fast-yet
--
-- The benchmark in its current state is a derivation from the SOM version,
-- which is derived from Mario Wolczko's Smalltalk version of DeltaBlue.
-- Ported on Lua by Francois Perrad <francois.perrad@gadz.org>
--
-- The original license details are availble here:
-- http://web.archive.org/web/20050825101121/http://www.sunlabs.com/people/mario/java_benchmarking/index.html
--
-- This file itself, and its souce control history is however based on the
-- following. It is unclear whether this still bears any relevance since the
-- nature of the code was essentially reverted back to the Smalltalk version.
--
-- Derived from http://pws.prserv.net/dlissett/ben/bench1.htm
-- Licensed CC BY-NC-SA 1.0

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

local NO_TASK = nil
local NO_WORK = nil

local IDLER     = 1
local WORKER    = 2
local HANDLER_A = 3
local HANDLER_B = 4
local DEVICE_A  = 5
local DEVICE_B  = 6

local TRACING = false

local RBObject = {_CLASS = 'RBObject'} do

function RBObject:append (packet, queue_head)
    packet.link = NO_WORK
    if NO_WORK == queue_head then
        return packet
    end

    local mouse = queue_head

    local link = mouse.link
    while NO_WORK ~= link do
        mouse = link
        link = mouse.link
    end

    mouse.link = packet
    return queue_head
end

end -- abstract RBObject

local DeviceTaskDataRecord = {_CLASS = 'DeviceTaskDataRecord'} do
setmetatable(DeviceTaskDataRecord, {__index = RBObject})

function DeviceTaskDataRecord.new ()
    local obj = {
        pending = NO_WORK,
    }
    return setmetatable(obj, {__index = RBObject})
end

end -- class DeviceTaskDataRecord

local HandlerTaskDataRecord = {_CLASS = 'HandlerTaskDataRecord'} do
setmetatable(HandlerTaskDataRecord, {__index = RBObject})

function HandlerTaskDataRecord.new ()
    local obj = {
        work_in   = NO_WORK,
        device_in = NO_WORK,
        append = RBObject.append,
    }
    for k, v in pairs(HandlerTaskDataRecord) do
        obj[k] = v
    end
    return obj
end

function HandlerTaskDataRecord:device_in_add (packet)
    self.device_in = self:append(packet, self.device_in)
end

function HandlerTaskDataRecord:work_in_add (packet)
    self.work_in = self:append(packet, self.work_in)
end

end -- class HandlerTaskDataRecord

local IdleTaskDataRecord = {_CLASS = 'IdleTaskDataRecord'} do
setmetatable(IdleTaskDataRecord, {__index = RBObject})

function IdleTaskDataRecord.new ()
    local obj = {
        control = 1,
        count   = 10000,
        append = RBObject.append
    }
    return obj
end

end -- class IdleTaskDataRecord

local WorkerTaskDataRecord = {_CLASS = 'WorkerTaskDataRecord'} do
setmetatable(WorkerTaskDataRecord, {__index = RBObject})

function WorkerTaskDataRecord.new ()
    local obj = {
        destination = HANDLER_A,
        count = 0,
        append = RBObject.append
    }
    return obj
end

end -- class WorkerTaskDataRecord

local Packet = {_CLASS = 'Packet'} do
setmetatable(Packet, {__index = RBObject})

function Packet.new (link, identity, kind)
    local obj = {
        link     = link,
        kind     = kind,
        identity = identity,
        datum    = 1,
        data     = {0, 0, 0, 0},
        append   = RBObject.append
    }
    return obj
end

end -- class Packet

local TaskState = {_CLASS = 'TaskState'} do
setmetatable(TaskState, {__index = RBObject})

function TaskState.new ()
    local obj = {
        task_holding  = false,
        task_waiting  = false,
        packt_pending = false,
        append   = RBObject.append
    }
    for k, v in pairs(TaskState) do
        obj[k] = v
    end
    return obj
end

function TaskState:is_packet_pending ()
    return self.packt_pending
end

function TaskState:is_task_waiting ()
    return self.task_waiting
end

function TaskState:is_task_holding ()
    return self.task_holding
end

function TaskState:packet_pending ()
    self.packt_pending = true
    self.task_waiting  = false
    self.task_holding  = false
    return self
end

function TaskState:running ()
    self.packt_pending = false
    self.task_waiting  = false
    self.task_holding  = false
    return self
end

function TaskState:waiting ()
    self.packt_pending = false
    self.task_holding  = false
    self.task_waiting  = true
    return self
end

function TaskState:waiting_with_packet ()
    self.task_holding  = false
    self.task_waiting  = true
    self.packt_pending = true
    return self
end

function TaskState:is_task_holding_or_waiting ()
    return self.task_holding or (not self.packt_pending and self.task_waiting)
end

function TaskState:is_waiting_with_packet ()
    return self.packt_pending and self.task_waiting and not self.task_holding
end

function TaskState.create_running ()
    return TaskState.new():running()
end

function TaskState.create_waiting ()
    return TaskState.new():waiting()
end

function TaskState.create_waiting_with_packet ()
    return TaskState.new():waiting_with_packet()
end

end -- class TaskState

local TaskControlBlock = {_CLASS = 'TaskControlBlock'} do
setmetatable(TaskControlBlock, {__index = TaskState})

function TaskControlBlock.new (link, identity, priority, initial_work_queue,
                               initial_state, private_data, fn)
    local obj = {
        link = link,
        identity = identity,
        priority = priority,
        input = initial_work_queue,
        handle = private_data,

        packt_pending = initial_state:is_packet_pending(),
        task_waiting  = initial_state:is_task_waiting(),
        task_holding  = initial_state:is_task_holding(),

        fn = fn,
        
        append = RBObject.append,
        add_input_and_check_priority = TaskControlBlock.add_input_and_check_priority,
        run_task = TaskControlBlock.run_task,
    }
    for k, v in pairs(TaskState) do
        obj[k] = v
    end
    return obj
end

function TaskControlBlock:add_input_and_check_priority (packet, old_task)
    if NO_WORK == self.input then
        self.input = packet
        self.packt_pending = true
        if self.priority > old_task.priority then
            return self
        end
    else
        self.input = self:append(packet, self.input)
    end
    return old_task
end

function TaskControlBlock:run_task ()
    local message
    if self:is_waiting_with_packet() then
        message = self.input
        self.input = message.link
        if NO_WORK == self.input then
            self:running()
        else
            self:packet_pending()
        end
    else
        message = NO_WORK
    end
    return self.fn(message, self.handle)
end

end -- class TaskControlBlock

local Scheduler = {_CLASS = 'Scheduler'} do
setmetatable(Scheduler, {__index = RBObject})

local DEVICE_PACKET_KIND = 0
local WORK_PACKET_KIND   = 1

local DATA_SIZE = 4

function Scheduler.new ()
    local obj = {
        -- init tracing
        layout = 0,

        -- init scheduler
        task_list    = NO_TASK,
        current_task = NO_TASK,
        current_task_identity = 0,
        task_table = {NO_TASK, NO_TASK, NO_TASK, NO_TASK, NO_TASK, NO_TASK},

        queue_count = 0,
        hold_count  = 0,
    }
    for k, v in pairs(Scheduler) do
        obj[k] = v
    end
    return obj
end

function Scheduler:create_device (identity, priority, work, state)
    self:create_task(identity, priority, work, state,
                     DeviceTaskDataRecord.new(),
                     function (packet, data)
        if NO_WORK == packet then
            packet = data.pending
            if NO_WORK == packet then
                return self:wait()
            else
                data.pending = NO_WORK
                return self:queue_packet(packet)
            end
        else
            data.pending = packet
            if TRACING then
                self:trace(packet.datum)
            end
            return self:hold_self()
        end
    end)
end

function Scheduler:create_handler (identity, priority, work, state)
    self:create_task(identity, priority, work, state,
                     HandlerTaskDataRecord.new(),
                     function (packet, data)
        if NO_WORK ~= packet then
            if WORK_PACKET_KIND == packet.kind then
                data:work_in_add(packet)
            else
                data:device_in_add(packet)
            end
        end

        local work_packet = data.work_in
        if NO_WORK == work_packet then
            return self:wait()
        else
            local count = work_packet.datum
            if count > DATA_SIZE then
                data.work_in = work_packet.link
                return self:queue_packet(work_packet)
            else
                local device_packet = data.device_in
                if NO_WORK == device_packet then
                    return self:wait()
                else
                    data.device_in = device_packet.link
                    device_packet.datum = work_packet.data[count]
                    work_packet.datum = count + 1
                    return self:queue_packet(device_packet)
                end
            end
        end
    end)
end

function Scheduler:create_idler (identity, priority, work, state)
    self:create_task(identity, priority, work, state,
                     IdleTaskDataRecord.new(),
                     function (_, data)
        data.count = data.count - 1
        if 0 == data.count then
            return self:hold_self()
        else
            if 0 == data.control % 2 then
                data.control = data.control / 2
                return self:release(DEVICE_A)
            else
                -- data.control = bxor((data.control - 1) / 2, 53256)
                local v = (data.control - 1) / 2
                if (v % 65536 >= 32768) then v = v - 32768 else v = v + 32768 end
                if (v % 32768 >= 16384) then v = v - 16384 else v = v + 16384 end
                if (v % 8192 >= 4096) then v = v - 4096 else v = v + 4096 end
                if (v % 16 >= 8) then v = v - 8 else v = v + 8 end
                data.control = v
                return self:release(DEVICE_B)
            end
        end
    end)
end

function Scheduler:create_packet (link, identity, kind)
    return Packet.new(link, identity, kind)
end

function Scheduler:create_task (identity, priority, work, state, data, fn)
    local tcb = TaskControlBlock.new(self.task_list, identity, priority,
                                     work, state, data, fn)
    self.task_list = tcb
    self.task_table[identity] = tcb
end

function Scheduler:create_worker (identity, priority, work, state)
    self:create_task(identity, priority, work, state,
                     WorkerTaskDataRecord.new(),
                     function (packet, data)
        if NO_WORK == packet then
            return self:wait()
        else
            data.destination = (HANDLER_A == data.destination) and HANDLER_B or HANDLER_A
            packet.identity = data.destination
            packet.datum = 1
            for i = 1, DATA_SIZE do
                data.count = data.count + 1
                if data.count > 26 then
                   data.count = 1
                end
                packet.data[i] = 65 + data.count - 1
            end
            return self:queue_packet(packet)
        end
    end)
end

function Scheduler:start ()
    local queue
    self:create_idler(IDLER, 0, NO_WORK, TaskState.create_running())
    queue = self:create_packet(NO_WORK, WORKER, WORK_PACKET_KIND)
    queue = self:create_packet(queue,   WORKER, WORK_PACKET_KIND)

    self:create_worker(WORKER, 1000, queue, TaskState.create_waiting_with_packet())
    queue = self:create_packet(NO_WORK, DEVICE_A, DEVICE_PACKET_KIND)
    queue = self:create_packet(queue,   DEVICE_A, DEVICE_PACKET_KIND)
    queue = self:create_packet(queue,   DEVICE_A, DEVICE_PACKET_KIND)

    self:create_handler(HANDLER_A, 2000, queue, TaskState.create_waiting_with_packet())
    queue = self:create_packet(NO_WORK, DEVICE_B, DEVICE_PACKET_KIND)
    queue = self:create_packet(queue,   DEVICE_B, DEVICE_PACKET_KIND)
    queue = self:create_packet(queue,   DEVICE_B, DEVICE_PACKET_KIND)

    self:create_handler(HANDLER_B, 3000, queue, TaskState.create_waiting_with_packet())
    self:create_device(DEVICE_A, 4000, NO_WORK, TaskState.create_waiting())
    self:create_device(DEVICE_B, 5000, NO_WORK, TaskState.create_waiting())

    self:schedule()

    return self.queue_count == 23246 and self.hold_count == 9297
end

function Scheduler:find_task (identity)
    local task = self.task_table[identity]
    assert(task ~= NO_TASK, 'find_task failed')
    return task
end

function Scheduler:hold_self ()
    self.hold_count = self.hold_count + 1
    local current_task = self.current_task
    current_task.task_holding = true
    return current_task.link
end

function Scheduler:queue_packet (packet)
    local task = self:find_task(packet.identity)
    if NO_TASK == task then
        return NO_TASK
    end

    self.queue_count = self.queue_count + 1

    packet.link     = NO_WORK
    packet.identity = self.current_task_identity
    return task:add_input_and_check_priority(packet, self.current_task)
end

function Scheduler:release (identity)
    local task = self:find_task(identity)
    if NO_TASK == task then
        return NO_TASK
    end

    task.task_holding = false

    if task.priority > self.current_task.priority then
        return task
    else
        return self.current_task
    end
end

function Scheduler:trace (id)
    self.layout = self.layout - 1
    if 0 >= self.layout then
        io.stdout:write'\n'
        self.layout = 50
    end
    io.stdout:write(tostring(id))
end

function Scheduler:wait ()
    local current_task = self.current_task
    current_task.task_waiting = true
    return current_task
end

function Scheduler:schedule ()
    self.current_task = self.task_list
    while self.current_task ~= NO_TASK do
        if self.current_task:is_task_holding_or_waiting() then
            self.current_task = self.current_task.link
        else
            self.current_task_identity = self.current_task.identity
            if TRACING then
                self:trace(self.current_task_identity - 1)
            end
            self.current_task = self.current_task:run_task()
        end
    end
end

end -- class Scheduler

local richards = {} do
setmetatable(richards, {__index = benchmark})

function richards:benchmark ()
    return Scheduler.new():start()
end

function richards:verify_result (result)
    print(result)
    return result
end

end -- object richards


local benchmark_iterations = 1
assert(richards:inner_benchmark_loop(benchmark_iterations),
       'Benchmark failed with incorrect result')
