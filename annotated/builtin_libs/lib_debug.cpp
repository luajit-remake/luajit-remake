#include "deegen_api.h"
#include "runtime_utils.h"

// debug.debug -- https://www.lua.org/manual/5.1/manual.html#pdf-debug.debug
//
// debug.debug ()
// Enters an interactive mode with the user, running each string that the user enters. Using simple commands and other debug facilities,
// the user can inspect global and local variables, change their values, evaluate expressions, and so on. A line containing only the word
// cont finishes this function, so that the caller continues its execution.
//
// Note that commands for debug.debug are not lexically nested within any function, and so have no direct access to local variables.
//
DEEGEN_DEFINE_LIB_FUNC(debug_debug)
{
    ThrowError("Library function 'debug.debug' is not implemented yet!");
}

// debug.getfenv -- https://www.lua.org/manual/5.1/manual.html#pdf-debug.getfenv
//
// debug.getfenv (o)
// Returns the environment of object o.
//
DEEGEN_DEFINE_LIB_FUNC(debug_getfenv)
{
    ThrowError("Library function 'debug.getfenv' is not implemented yet!");
}

// debug.gethook -- https://www.lua.org/manual/5.1/manual.html#pdf-debug.gethook
//
// debug.gethook ([thread])
// Returns the current hook settings of the thread, as three values: the current hook function, the current hook mask, and the current
// hook count (as set by the debug.sethook function).
//
DEEGEN_DEFINE_LIB_FUNC(debug_gethook)
{
    ThrowError("Library function 'debug.gethook' is not implemented yet!");
}

// debug.getinfo -- https://www.lua.org/manual/5.1/manual.html#pdf-debug.getinfo
//
// debug.getinfo ([thread,] function [, what])
// Returns a table with information about a function. You can give the function directly, or you can give a number as the value of
// function, which means the function running at level function of the call stack of the given thread: level 0 is the current function
// (getinfo itself); level 1 is the function that called getinfo; and so on. If function is a number larger than the number of active
// functions, then getinfo returns nil.
//
// The returned table can contain all the fields returned by lua_getinfo, with the string what describing which fields to fill in.
// The default for what is to get all information available, except the table of valid lines. If present, the option 'f' adds a field
// named func with the function itself. If present, the option 'L' adds a field named activelines with the table of valid lines.
//
// For instance, the expression debug.getinfo(1,"n").name returns a table with a name for the current function, if a reasonable name
// can be found, and the expression debug.getinfo(print) returns a table with all available information about the print function.
//
DEEGEN_DEFINE_LIB_FUNC(debug_getinfo)
{
    ThrowError("Library function 'debug.getinfo' is not implemented yet!");
}

// debug.getlocal -- https://www.lua.org/manual/5.1/manual.html#pdf-debug.getlocal
//
// debug.getlocal ([thread,] level, local)
// This function returns the name and the value of the local variable with index local of the function at level level of the stack.
// (The first parameter or local variable has index 1, and so on, until the last active local variable.) The function returns nil if
// there is no local variable with the given index, and raises an error when called with a level out of range. (You can call
// debug.getinfo to check whether the level is valid.)
//
DEEGEN_DEFINE_LIB_FUNC(debug_getlocal)
{
    ThrowError("Library function 'debug.getlocal' is not implemented yet!");
}

// debug.getmetatable -- https://www.lua.org/manual/5.1/manual.html#pdf-debug.getmetatable
//
// debug.getmetatable (object)
// Returns the metatable of the given object or nil if it does not have a metatable.
//
DEEGEN_DEFINE_LIB_FUNC(debug_getmetatable)
{
    if (GetNumArgs() == 0)
    {
        ThrowError("bad argument #1 to 'getmetatable' (value expected)");
    }

    TValue value = GetArg(0);
    UserHeapPointer<void> metatableMaybeNull = GetMetatableForValue(value);
    if (metatableMaybeNull.m_value == 0)
    {
        Return(TValue::Create<tNil>());
    }
    else
    {
        Return(TValue::Create<tHeapEntity>(metatableMaybeNull.As<UserHeapGcObjectHeader>()));
    }
}

// debug.getregistry -- https://www.lua.org/manual/5.1/manual.html#pdf-debug.getregistry
//
// debug.getregistry ()
// Returns the registry table (see §3.5).
//
DEEGEN_DEFINE_LIB_FUNC(debug_getregistry)
{
    ThrowError("Library function 'debug.getregistry' is not implemented yet!");
}

// debug.getupvalue -- https://www.lua.org/manual/5.1/manual.html#pdf-debug.getupvalue
//
// debug.getupvalue (func, up)
// This function returns the name and the value of the upvalue with index up of the function func. The function returns nil if there
// is no upvalue with the given index.
//
DEEGEN_DEFINE_LIB_FUNC(debug_getupvalue)
{
    ThrowError("Library function 'debug.getupvalue' is not implemented yet!");
}

// debug.setfenv -- https://www.lua.org/manual/5.1/manual.html#pdf-debug.setfenv
//
// debug.setfenv (object, table)
// Sets the environment of the given object to the given table. Returns object.
//
DEEGEN_DEFINE_LIB_FUNC(debug_setfenv)
{
    ThrowError("Library function 'debug.setfenv' is not implemented yet!");
}

// debug.sethook -- https://www.lua.org/manual/5.1/manual.html#pdf-debug.sethook
//
// debug.sethook ([thread,] hook, mask [, count])
// Sets the given function as a hook. The string mask and the number count describe when the hook will be called. The string mask may
// have the following characters, with the given meaning:
//     "c": the hook is called every time Lua calls a function;
//     "r": the hook is called every time Lua returns from a function;
//     "l": the hook is called every time Lua enters a new line of code.
// With a count different from zero, the hook is called after every count instructions.
//
// When called without arguments, debug.sethook turns off the hook.
//
// When the hook is called, its first parameter is a string describing the event that has triggered its call: "call", "return"
// (or "tail return", when simulating a return from a tail call), "line", and "count". For line events, the hook also gets the new line
// number as its second parameter. Inside a hook, you can call getinfo with level 2 to get more information about the running function
// (level 0 is the getinfo function, and level 1 is the hook function), unless the event is "tail return". In this case, Lua is only
// simulating the return, and a call to getinfo will return invalid data.
//
DEEGEN_DEFINE_LIB_FUNC(debug_sethook)
{
    ThrowError("Library function 'debug.sethook' is not implemented yet!");
}

// debug.setlocal -- https://www.lua.org/manual/5.1/manual.html#pdf-debug.setlocal
//
// debug.setlocal ([thread,] level, local, value)
// This function assigns the value value to the local variable with index local of the function at level level of the stack. The function
// returns nil if there is no local variable with the given index, and raises an error when called with a level out of range. (You can call
// getinfo to check whether the level is valid.) Otherwise, it returns the name of the local variable.
//
DEEGEN_DEFINE_LIB_FUNC(debug_setlocal)
{
    ThrowError("Library function 'debug.setlocal' is not implemented yet!");
}

// debug.setmetatable -- https://www.lua.org/manual/5.1/manual.html#pdf-debug.setmetatable
//
// debug.setmetatable (object, table)
// Sets the metatable for the given object to the given table (which can be nil).
//
DEEGEN_DEFINE_LIB_FUNC(debug_setmetatable)
{
    size_t numArgs = GetNumArgs();
    if (numArgs <= 1)
    {
        ThrowError("bad argument #2 to 'setmetatable' (nil or table expected)");
    }

    TValue value = GetArg(0);
    TValue mt = GetArg(1);
    if (!mt.Is<tNil>() && !mt.Is<tTable>())
    {
        ThrowError("bad argument #2 to 'setmetatable' (nil or table expected)");
    }

    auto setExoticMetatable = [&](UserHeapPointer<void>& metatable)
    {
        if (mt.IsNil())
        {
            metatable = UserHeapPointer<void>();
        }
        else
        {
            metatable = mt.As<tHeapEntity>();
        }
    };

    VM* vm = VM::GetActiveVMForCurrentThread();
    if (value.Is<tHeapEntity>())
    {
        HeapEntityType ty = value.GetHeapEntityType();

        if (ty == HeapEntityType::Table)
        {
            TableObject* obj = TranslateToRawPointer(vm, value.As<tTable>());
            if (!mt.Is<tNil>())
            {
                obj->SetMetatable(vm, mt.As<tTable>());
            }
            else
            {
                obj->RemoveMetatable(vm);
            }
        }
        else if (ty == HeapEntityType::String)
        {
            setExoticMetatable(vm->m_metatableForString);
        }
        else if (ty == HeapEntityType::Function)
        {
            setExoticMetatable(vm->m_metatableForFunction);
        }
        else if (ty == HeapEntityType::Thread)
        {
            setExoticMetatable(vm->m_metatableForCoroutine);
        }
        else
        {
            // TODO: support USERDATA
            ReleaseAssert(false && "unimplemented");
        }
    }
    else if (value.Is<tMIV>())
    {
        if (value.Is<tNil>())
        {
            setExoticMetatable(vm->m_metatableForNil);
        }
        else
        {
            Assert(value.Is<tBool>());
            setExoticMetatable(vm->m_metatableForBoolean);
        }
    }
    else
    {
        Assert(value.Is<tDouble>() || value.Is<tInt32>());
        setExoticMetatable(vm->m_metatableForNumber);
    }

    // The return value of 'debug.setmetatable' is 'true' in Lua 5.1, but the original object in Lua 5.2 and higher
    // For now we implement Lua 5.1 behavior
    //
    Return(TValue::Create<tBool>(true));
}

// debug.setupvalue -- https://www.lua.org/manual/5.1/manual.html#pdf-debug.setupvalue
//
// debug.setupvalue (func, up, value)
// This function assigns the value value to the upvalue with index up of the function func. The function returns nil if there is no
// upvalue with the given index. Otherwise, it returns the name of the upvalue.
//
DEEGEN_DEFINE_LIB_FUNC(debug_setupvalue)
{
    ThrowError("Library function 'debug.setupvalue' is not implemented yet!");
}

// debug.traceback -- https://www.lua.org/manual/5.1/manual.html#pdf-debug.traceback
//
// debug.traceback ([thread,] [message [, level]])
// Returns a string with a traceback of the call stack. An optional message string is appended at the beginning of the traceback.
// An optional level number tells at which level to start the traceback (default is 1, the function calling traceback).
//
DEEGEN_DEFINE_LIB_FUNC(debug_traceback)
{
    ThrowError("Library function 'debug.traceback' is not implemented yet!");
}

DEEGEN_END_LIB_FUNC_DEFINITIONS
