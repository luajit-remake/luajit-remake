#include "deegen_api.h"
#include "lualib_tonumber_util.h"
#include "runtime_utils.h"
#include "lj_parser_wrapper.h"

// base.assert -- https://www.lua.org/manual/5.1/manual.html#pdf-assert
//
// assert (v [, message])
// Issues an error when the value of its argument v is false (i.e., nil or false);
// otherwise, returns all its arguments. message is an error message;
// when absent, it defaults to "assertion failed!"
//
DEEGEN_DEFINE_LIB_FUNC(base_assert)
{
    size_t numArgs = GetNumArgs();
    if (unlikely(numArgs == 0))
    {
        ThrowError("bad argument #1 to 'assert' (value expected)");
    }

    TValue v = GetArg(0);
    if (unlikely(!v.IsTruthy()))
    {
        if (numArgs == 1)
        {
            ThrowError("assertion failed!");
        }
        TValue msg = GetArg(1);
        if (msg.Is<tString>())
        {
            ThrowError(msg);
        }
        else if (msg.Is<tDouble>())
        {
            char buf[x_default_tostring_buffersize_double];
            uint32_t msgLen = static_cast<uint32_t>(StringifyDoubleUsingDefaultLuaFormattingOptions(buf, msg.As<tDouble>()) - buf);
            TValue errObj = TValue::Create<tString>(VM::GetActiveVMForCurrentThread()->CreateStringObjectFromRawString(buf, msgLen).As());
            ThrowError(errObj);
        }
        else
        {
            // TODO: make error message consistent with Lua
            //
            ThrowError("bad argument #2 to 'assert' (string expected)");
        }
    }

    ReturnValueRange(GetStackBase(), numArgs);
}

// base.collectgarbage -- https://www.lua.org/manual/5.1/manual.html#pdf-collectgarbage
//
// collectgarbage ([opt [, arg]])
// This function is a generic interface to the garbage collector. It performs different functions according to its first argument, opt:
//     "collect": performs a full garbage-collection cycle. This is the default option.
//     "stop": stops the garbage collector.
//     "restart": restarts the garbage collector.
//     "count": returns the total memory in use by Lua (in Kbytes).
//     "step": performs a garbage-collection step. The step "size" is controlled by arg (larger values mean more steps)
//             in a non-specified way. If you want to control the step size you must experimentally tune the value of arg.
//             Returns true if the step finished a collection cycle.
//     "setpause": sets arg as the new value for the pause of the collector (see §2.10). Returns the previous value for pause.
//     "setstepmul": sets arg as the new value for the step multiplier of the collector (see §2.10). Returns the previous value for step.
//
DEEGEN_DEFINE_LIB_FUNC(base_collectgarbage)
{
    ThrowError("Library function 'collectgarbage' is not implemented yet!");
}

DEEGEN_DEFINE_LIB_FUNC_CONTINUATION(base_dofile_continuation)
{
    ReturnValueRange(GetReturnValuesBegin(), GetNumReturnValues());
}

DEEGEN_DEFINE_LIB_FUNC_CONTINUATION(base_dofile_read_stdin_continuation)
{
    // We need to execute the result returned by 'load', or throw out error if 'load' failed.
    //
    assert(GetNumReturnValues() >= 1);
    TValue r1 = GetReturnValuesBegin()[0];
    if (r1.Is<tNil>())
    {
        assert(GetNumReturnValues() == 2);
        TValue errMsg = GetReturnValuesBegin()[1];
        ThrowError(errMsg);
    }

    assert(r1.Is<tFunction>());
    TValue* callFrame = GetStackBase();
    reinterpret_cast<void**>(callFrame)[0] = TranslateToRawPointer(r1.As<tFunction>());
    MakeInPlaceCall(callFrame + x_numSlotsForStackFrameHeader, 0 /*numArgs*/, DEEGEN_LIB_FUNC_RETURN_CONTINUATION(base_dofile_continuation));
}

// base.dofile -- https://www.lua.org/manual/5.1/manual.html#pdf-dofile
//
// dofile ([filename])
// Opens the named file and executes its contents as a Lua chunk. When called without arguments, dofile executes the contents of the
// standard input (stdin). Returns all values returned by the chunk. In case of errors, dofile propagates the error to its caller
// (that is, dofile does not run in protected mode).
//
DEEGEN_DEFINE_LIB_FUNC(base_dofile)
{
    if (GetNumArgs() < 1 || GetArg(0).Is<tNil>())
    {
        // Read from stdin, in this case we can just use io_lines_iter as the iterator function and do a load
        //
        VM* vm = VM::GetActiveVMForCurrentThread();
        TValue* callframe = GetStackBase();
        StorePtrToCallframe(callframe, TranslateToRawPointer(vm->GetLibFn<VM::LibFn::BaseLoad>().As<tFunction>()));
        callframe[x_numSlotsForStackFrameHeader] = vm->GetLibFn<VM::LibFn::IoLinesIter>();
        MakeInPlaceCall(callframe + x_numSlotsForStackFrameHeader, 1 /*numArgs*/, DEEGEN_LIB_FUNC_RETURN_CONTINUATION(base_dofile_read_stdin_continuation));
    }

    GET_ARG_AS_STRING(dofile, 1, fileNamePtr, fileNameLen);
    std::ignore = fileNameLen;

    FunctionObject* entryPoint;
    {
        ParseResult res = ParseLuaScriptFromFile(GetCurrentCoroutine(), fileNamePtr);
        if (res.m_scriptModule.get() == nullptr)
        {
            ThrowError(res.errMsg);
        }
        entryPoint = TranslateToRawPointer(res.m_scriptModule->m_defaultEntryPoint.As());
    }

    TValue* callFrame = GetStackBase();
    StorePtrToCallframe(callFrame, entryPoint);
    MakeInPlaceCall(callFrame + x_numSlotsForStackFrameHeader, 0 /*numArgs*/, DEEGEN_LIB_FUNC_RETURN_CONTINUATION(base_dofile_continuation));
}

// base.getfenv -- https://www.lua.org/manual/5.1/manual.html#pdf-getfenv
//
// getfenv ([f])
// Returns the current environment in use by the function. f can be a Lua function or a number that specifies the function at that stack
// level: Level 1 is the function calling getfenv. If the given function is not a Lua function, or if f is 0, getfenv returns the global
// environment. The default for f is 1.
///
DEEGEN_DEFINE_LIB_FUNC(base_getfenv)
{
    ThrowError("Library function 'getfenv' is not implemented yet!");
}

// base.getmetatable -- https://www.lua.org/manual/5.1/manual.html#pdf-getmetatable
//
// getmetatable (object)
// If object does not have a metatable, returns nil. Otherwise, if the object's metatable has a "__metatable" field, returns the
// associated value. Otherwise, returns the metatable of the given object.
//
DEEGEN_DEFINE_LIB_FUNC(base_getmetatable)
{
    if (unlikely(GetNumArgs() == 0))
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
        HeapPtr<TableObject> tableObj = metatableMaybeNull.As<TableObject>();
        UserHeapPointer<HeapString> prop = VM_GetStringNameForMetatableKind(LuaMetamethodKind::ProtectedMt);
        GetByIdICInfo icInfo;
        TableObject::PrepareGetById(tableObj, prop, icInfo /*out*/);
        TValue result = TableObject::GetById(tableObj, prop.As<void>(), icInfo);
        if (result.Is<tNil>())
        {
            Return(TValue::Create<tTable>(tableObj));
        }
        else
        {
            Return(result);
        }
    }
}

// The iterator returned by ipairs
//
DEEGEN_DEFINE_LIB_FUNC(base_ipairs_iterator)
{
    if (unlikely(GetNumArgs() < 2))
    {
        ThrowError("bad argument #2 to 'ipairs iterator' (number expected, got no value)");
    }
    TValue arg1 = GetArg(0);
    if (unlikely(!arg1.Is<tTable>()))
    {
        ThrowError("bad argument #1 to 'ipairs iterator' (table expected)");
    }
    HeapPtr<TableObject> tableObj = arg1.As<tTable>();
    TValue arg2 = GetArg(1);
    if (unlikely(!arg2.Is<tDouble>()))
    {
        ThrowError("bad argument #2 to 'ipairs iterator' (number expected)");
    }
    double idxDouble = arg2.As<tDouble>();
    // Lua standard only enforce behavior on one use of this function, which is as the iterator for iterative for loop.
    // So we may safely assume that 'idx' is a small and valid array index. If not, we can exhibit any behavior as long
    // as we don't crash. Lua official implementation doesn't seem to error out in this case either.
    //
    int32_t idx = static_cast<int32_t>(idxDouble);
    idx++;

    GetByIntegerIndexICInfo icInfo;
    TableObject::PrepareGetByIntegerIndex(tableObj, icInfo /*out*/);
    TValue res = TableObject::GetByIntegerIndex(tableObj, idx, icInfo);
    if (res.Is<tNil>())
    {
        Return();
    }
    else
    {
        Return(TValue::Create<tDouble>(idx), res);
    }
}

// base.ipairs -- https://www.lua.org/manual/5.1/manual.html#pdf-ipairs
//
// ipairs (t)
// Returns three values: an iterator function, the table t, and 0, so that the construction
//     for i,v in ipairs(t) do body end
// will iterate over the pairs (1,t[1]), (2,t[2]), ···, up to the first integer key absent from the table.
//
DEEGEN_DEFINE_LIB_FUNC(base_ipairs)
{
    if (unlikely(GetNumArgs() < 1))
    {
        ThrowError("bad argument #1 to 'ipairs' (table expected, got no value)");
    }
    TValue arg1 = GetArg(0);
    if (unlikely(!arg1.Is<tTable>()))
    {
        ThrowError("bad argument #1 to 'ipairs' (table expected)");
    }
    TValue iterFn = VM_GetLibFunctionObject<VM::LibFn::BaseIPairsIter>();
    Return(iterFn, arg1, TValue::Create<tDouble>(0));
}

DEEGEN_DEFINE_LIB_FUNC_CONTINUATION(base_load_continuation)
{
    if (GetNumReturnValues() == 0)
    {
        goto iter_end;
    }
    else
    {
        VM* vm = VM::GetActiveVMForCurrentThread();
        TValue v = GetReturnValuesBegin()[0];
        if (v.Is<tNil>())
        {
            goto iter_end;
        }
        else if (!v.Is<tString>())
        {
            if (v.Is<tInt32>())
            {
                char buf[x_default_tostring_buffersize_int];
                StringifyInt32UsingDefaultLuaFormattingOptions(buf, v.As<tInt32>());
                v = TValue::Create<tString>(TranslateToHeapPtr(vm->CreateStringObjectFromRawCString(buf)));
            }
            else if (v.Is<tDouble>())
            {
                char buf[x_default_tostring_buffersize_double];
                StringifyDoubleUsingDefaultLuaFormattingOptions(buf, v.As<tDouble>());
                v = TValue::Create<tString>(TranslateToHeapPtr(vm->CreateStringObjectFromRawCString(buf)));
            }
            else
            {
                TValue errMsg = TValue::Create<tString>(TranslateToHeapPtr(vm->CreateStringObjectFromRawCString("reader function must return a string")));
                Return(TValue::Create<tNil>(), errMsg);
            }
        }

        assert(v.Is<tString>());
        if (v.As<tString>()->m_length == 0)
        {
            goto iter_end;
        }

        TValue* sb = GetStackBase();
        assert(sb[2].Is<tTable>());
        HeapPtr<TableObject> tab = sb[2].As<tTable>();
        assert(sb[3].Is<tInt32>());
        int32_t ord = sb[3].As<tInt32>();
        assert(ord >= 0);
        if (ord >= 1000000)
        {
            TValue errMsg = TValue::Create<tString>(TranslateToHeapPtr(vm->CreateStringObjectFromRawCString("'load' returned too many (>1000000) code pieces")));
            Return(TValue::Create<tNil>(), errMsg);
        }
        ord++;
        TableObject::RawPutByValIntegerIndex(tab, ord, v);
        sb[3] = TValue::Create<tInt32>(ord);

        TValue* callFrame = sb + 4;
        assert(sb[0].Is<tFunction>());
        StorePtrToCallframe(callFrame, TranslateToRawPointer(sb[0].As<tFunction>()));
        MakeInPlaceCall(callFrame + x_numSlotsForStackFrameHeader, 0 /*numArgs*/, DEEGEN_LIB_FUNC_RETURN_CONTINUATION(base_load_continuation));
    }

iter_end:
    HeapPtr<FunctionObject> entryPoint;

    {
        TValue* sb = GetStackBase();
        assert(sb[2].Is<tTable>());
        HeapPtr<TableObject> tab = sb[2].As<tTable>();
        assert(sb[3].Is<tInt32>());
        int32_t len = sb[3].As<tInt32>();
        assert(len >= 0);

        ParseResult res = ParseLuaScript(GetCurrentCoroutine(), tab, static_cast<uint32_t>(len));
        if (res.m_scriptModule.get() == nullptr)
        {
            Return(TValue::Create<tNil>(), res.errMsg);
        }
        entryPoint = res.m_scriptModule->m_defaultEntryPoint.As();
    }

    Return(TValue::Create<tFunction>(entryPoint));
}

// base.load -- https://www.lua.org/manual/5.1/manual.html#pdf-load
//
// load (func [, chunkname])
// Loads a chunk using function func to get its pieces. Each call to func must return a string that concatenates with previous results.
// A return of an empty string, nil, or no value signals the end of the chunk.
//
// If there are no errors, returns the compiled chunk as a function; otherwise, returns nil plus the error message. The environment of
// the returned function is the global environment.
//
// chunkname is used as the chunk name for error messages and debug information. When absent, it defaults to "=(load)".
//
DEEGEN_DEFINE_LIB_FUNC(base_load)
{
    if (unlikely(GetNumArgs() < 1))
    {
        ThrowError("bad argument #1 to 'load' (function expected, got no value)");
    }
    if (!GetArg(0).Is<tFunction>())
    {
        ThrowError("bad argument #1 to 'load' (function expected)");
    }
    FunctionObject* func = TranslateToRawPointer(GetArg(0).As<tFunction>());

    // Put all the string pieces into a table
    //
    TValue* sb = GetStackBase();
    VM* vm = VM::GetActiveVMForCurrentThread();
    HeapPtr<TableObject> tab = TableObject::CreateEmptyTableObject(vm, 0U /*inlineCap*/, 1 /*initialButterfly*/);
    sb[2] = TValue::Create<tTable>(tab);
    sb[3] = TValue::Create<tInt32>(0);

    TValue* callFrame = sb + 4;
    StorePtrToCallframe(callFrame, func);
    MakeInPlaceCall(callFrame + x_numSlotsForStackFrameHeader, 0 /*numArgs*/, DEEGEN_LIB_FUNC_RETURN_CONTINUATION(base_load_continuation));
}

DEEGEN_DEFINE_LIB_FUNC_CONTINUATION(base_loadfile_continuation)
{
    ReturnValueRange(GetReturnValuesBegin(), GetNumReturnValues());
}

// base.loadfile -- https://www.lua.org/manual/5.1/manual.html#pdf-loadfile
//
// loadfile ([filename])
// Similar to load, but gets the chunk from file filename or from the standard input, if no file name is given.
//
DEEGEN_DEFINE_LIB_FUNC(base_loadfile)
{
    if (GetNumArgs() < 1 || GetArg(0).Is<tNil>())
    {
        // Read from stdin, in this case we can just use io_lines_iter as the iterator function and do a load
        //
        VM* vm = VM::GetActiveVMForCurrentThread();
        TValue* callframe = GetStackBase();
        StorePtrToCallframe(callframe, TranslateToRawPointer(vm->GetLibFn<VM::LibFn::BaseLoad>().As<tFunction>()));
        callframe[x_numSlotsForStackFrameHeader] = vm->GetLibFn<VM::LibFn::IoLinesIter>();
        MakeInPlaceCall(callframe + x_numSlotsForStackFrameHeader, 1 /*numArgs*/, DEEGEN_LIB_FUNC_RETURN_CONTINUATION(base_loadfile_continuation));
    }

    GET_ARG_AS_STRING(loadfile, 1, fileNamePtr, fileNameLen);
    std::ignore = fileNameLen;

    HeapPtr<FunctionObject> entryPoint;
    {
        ParseResult res = ParseLuaScriptFromFile(GetCurrentCoroutine(), fileNamePtr);
        if (res.m_scriptModule.get() == nullptr)
        {
            Return(TValue::Create<tNil>(), res.errMsg);
        }
        entryPoint = res.m_scriptModule->m_defaultEntryPoint.As();
    }
    Return(TValue::Create<tFunction>(entryPoint));
}

// base.loadstring -- https://www.lua.org/manual/5.1/manual.html#pdf-loadstring
//
// loadstring (string [, chunkname])
// Similar to load, but gets the chunk from the given string.
// To load and run a given string, use the idiom
//     assert(loadstring(s))()
// When absent, chunkname defaults to the given string.
//
DEEGEN_DEFINE_LIB_FUNC(base_loadstring)
{
    if (unlikely(GetNumArgs() < 1))
    {
        ThrowError("bad argument #1 to 'loadstring' (string expected, got no value)");
    }
    GET_ARG_AS_STRING(loadstring, 1, ptr, len);

    HeapPtr<FunctionObject> entryPoint;

    {
        ParseResult res = ParseLuaScript(GetCurrentCoroutine(), ptr, len);
        if (res.m_scriptModule.get() == nullptr)
        {
            Return(TValue::Create<tNil>(), res.errMsg);
        }
        entryPoint = res.m_scriptModule->m_defaultEntryPoint.As();
    }

    Return(TValue::Create<tFunction>(entryPoint));
}

// base.module -- https://www.lua.org/manual/5.1/manual.html#pdf-module
//
// module (name [, ···])
//
// Creates a module. If there is a table in package.loaded[name], this table is the module. Otherwise, if there is a global table t with
// the given name, this table is the module. Otherwise creates a new table t and sets it as the value of the global name and the value of
// package.loaded[name]. This function also initializes t._NAME with the given name, t._M with the module (t itself), and t._PACKAGE with
// the package name (the full module name minus last component; see below). Finally, module sets t as the new environment of the current
// function and the new value of package.loaded[name], so that require returns t.
//
// If name is a compound name (that is, one with components separated by dots), module creates (or reuses, if they already exist) tables
// for each component. For instance, if name is a.b.c, then module stores the module table in field c of field b of global a.
//
// This function can receive optional options after the module name, where each option is a function to be applied over the module.
//
DEEGEN_DEFINE_LIB_FUNC(base_module)
{
    ThrowError("Library function 'module' is not implemented yet!");
}

// base.next -- https://www.lua.org/manual/5.1/manual.html#pdf-next
//
// next (table [, index])
// Allows a program to traverse all fields of a table. Its first argument is a table and its second argument is an index in this table.
// next returns the next index of the table and its associated value. When called with nil as its second argument, next returns an initial
// index and its associated value. When called with the last index, or with nil in an empty table, next returns nil. If the second argument
// is absent, then it is interpreted as nil. In particular, you can use next(t) to check whether a table is empty.
//
// The order in which the indices are enumerated is not specified, even for numeric indices. (To traverse a table in numeric order, use a
// numerical for or the ipairs function.)
//
// The behavior of next is undefined if, during the traversal, you assign any value to a non-existent field in the table. You may however
// modify existing fields. In particular, you may clear existing fields.
//
DEEGEN_DEFINE_LIB_FUNC(base_next)
{
    if (unlikely(GetNumArgs() == 0))
    {
        ThrowError("bad argument #1 to 'next' (table expected, got no value)");
    }

    TValue tab = GetArg(0);
    if (unlikely(!tab.Is<tTable>()))
    {
        ThrowError("bad argument #1 to 'next' (table expected)");
    }
    HeapPtr<TableObject> tableObj = tab.As<tTable>();

    TValue key;
    if (GetNumArgs() >= 2)
    {
        key = GetArg(1);
    }
    else
    {
        key = TValue::Create<tNil>();
    }

    TableObjectIterator::KeyValuePair kv;
    bool success = TableObjectIterator::GetNextFromKey(tableObj, key, kv /*out*/);
    if (unlikely(!success))
    {
        ThrowError("invalid key to 'next'");
    }

    // Lua manual states:
    //     "When called with the last index, or with 'nil' in an empty table, 'next' returns 'nil'."
    // So when end of table is reached, this should return "nil", not "nil, nil"...
    //
    if (kv.m_key.Is<tNil>())
    {
        assert(kv.m_value.Is<tNil>());
        Return(TValue::Create<tNil>());
    }
    else
    {
        Return(kv.m_key, kv.m_value);
    }
}

// base.pairs -- https://www.lua.org/manual/5.1/manual.html#pdf-pairs
//
// pairs (t)
// Returns three values: the next function, the table t, and nil, so that the construction
//     for k,v in pairs(t) do body end
// will iterate over all key–value pairs of table t.
// See function next for the caveats of modifying the table during its traversal.
//
DEEGEN_DEFINE_LIB_FUNC(base_pairs)
{
    if (unlikely(GetNumArgs() == 0))
    {
        ThrowError("bad argument #1 to 'pairs' (table expected, got no value)");
    }
    TValue input = GetArg(0);
    if (unlikely(!input.Is<tTable>()))
    {
        ThrowError("bad argument #1 to 'pairs' (table expected)");
    }

    Return(VM_GetLibFunctionObject<VM::LibFn::BaseNext>(), input, TValue::Create<tNil>());
}

static std::pair<bool, TValue> WARN_UNUSED IsGlobalToStringFunctionUnchanged(VM* vm, HeapPtr<TableObject> globalObject)
{
    GetByIdICInfo info;
    TableObject::PrepareGetByIdForGlobalObject(globalObject, vm->m_toStringString, info /*out*/);
    TValue globalToString = TableObject::GetById(globalObject, UserHeapPointer<void> { vm->m_toStringString.As() }, info);
    TValue expected = VM_GetLibFunctionObject<VM::LibFn::BaseToString>();
    return std::make_pair(globalToString.m_value == expected.m_value, globalToString);
}

static bool WARN_UNUSED HasNoExoticToStringMetamethodForStringType(VM* vm)
{
    if (vm->m_metatableForString.m_value == 0)
    {
        return true;
    }
    HeapPtr<TableObject> tab = vm->m_metatableForString.As<TableObject>();
    assert(tab->m_type == HeapEntityType::Table);
    SystemHeapPointer<void> hiddenClass = TCGet(tab->m_hiddenClass);
    return hiddenClass.m_value == vm->m_initialHiddenClassOfMetatableForString.m_value;
}

// This function may only be called if both the 'IsGlobalToStringFunctionUnchanged' check and
// the 'HasNoExoticToStringMetamethodForStringType' check have passed.
//
static bool WARN_UNUSED TryPrintUsingFastPath(VM* vm, FILE* fp, TValue tv)
{
    if (tv.Is<tDouble>())
    {
        if (unlikely(vm->m_metatableForNumber.m_value != 0))
        {
            return false;
        }
        double dbl = tv.As<tDouble>();
        char buf[x_default_tostring_buffersize_double];
        StringifyDoubleUsingDefaultLuaFormattingOptions(buf /*out*/, dbl);
        fprintf(fp, "%s", buf);
        return true;
    }

    if (tv.Is<tMIV>())
    {
        MiscImmediateValue miv = tv.AsMIV();
        if (miv.IsNil())
        {
            if (unlikely(vm->m_metatableForNil.m_value != 0))
            {
                return false;
            }
            fprintf(fp, "nil");
        }
        else
        {
            assert(miv.IsBoolean());
            if (unlikely(vm->m_metatableForBoolean.m_value != 0))
            {
                return false;
            }
            fprintf(fp, "%s", (miv.GetBooleanValue() ? "true" : "false"));
        }
        return true;
    }

    assert(tv.Is<tHeapEntity>());
    HeapEntityType ty = tv.GetHeapEntityType();
    void* p = TranslateToRawPointer(vm, tv.As<tHeapEntity>());
    if (ty == HeapEntityType::String)
    {
        HeapString* hs = reinterpret_cast<HeapString*>(p);
        fwrite(hs->m_string, sizeof(char), hs->m_length /*length*/, fp);
        return true;
    }

    if (ty == HeapEntityType::Function)
    {
        if (unlikely(vm->m_metatableForFunction.m_value != 0))
        {
            return false;
        }
        fprintf(fp, "function: %p", static_cast<void*>(p));
        return true;
    }

    if (ty == HeapEntityType::Thread)
    {
        if (unlikely(vm->m_metatableForCoroutine.m_value != 0))
        {
            return false;
        }
        fprintf(fp, "thread: %p", static_cast<void*>(p));
        return true;
    }

    if (ty == HeapEntityType::Userdata)
    {
        // TODO: support userdata
        //
        assert(false && "unimplemented");
        __builtin_unreachable();
    }

    assert(ty == HeapEntityType::Table);
    HeapPtr<TableObject> tableObj = tv.As<tTable>();
    UserHeapPointer<void> mt = TableObject::GetMetatable(tableObj).m_result;
    if (unlikely(mt.m_value != 0))
    {
        return false;
    }
    fprintf(fp, "table: %p", static_cast<void*>(p));
    return true;
}

DEEGEN_DEFINE_LIB_FUNC_CONTINUATION(base_print_continuation)
{
    if (unlikely(GetNumReturnValues() == 0))
    {
        ThrowError("'tostring' must return a string to 'print'");
    }

    VM* vm = VM::GetActiveVMForCurrentThread();
    FILE* fp = vm->GetStdout();

    TValue valueToPrint = GetReturnValuesBegin()[0];
    // Print the value returned from the 'tostring' function
    // Lua only allows this value to be number or string, otherwise it will error out
    //
    if (valueToPrint.Is<tString>())
    {
        HeapString* hs = TranslateToRawPointer(vm, valueToPrint.As<tString>());
        fwrite(hs->m_string, sizeof(char), hs->m_length /*length*/, fp);
    }
    else if (valueToPrint.Is<tDouble>())
    {
        double dbl = valueToPrint.As<tDouble>();
        char buf[x_default_tostring_buffersize_double];
        StringifyDoubleUsingDefaultLuaFormattingOptions(buf /*out*/, dbl);
        fprintf(fp, "%s", buf);
    }
    else
    {
        ThrowError("'tostring' must return a string to 'print'");
    }

    // Having printed the current element, now we should continue to print the remaining elements.
    // Load the information from the call frame to figure out what we should print next.
    //
    TValue* sb = GetStackBase();
    assert(sb[0].Is<tInt32>());
    size_t numElementsToPrint = static_cast<size_t>(sb[0].As<tInt32>());
    assert(sb[numElementsToPrint].Is<tInt32>());
    size_t curElementToPrint = static_cast<size_t>(sb[numElementsToPrint].As<tInt32>());
    assert(0 < curElementToPrint && curElementToPrint <= numElementsToPrint);
    if (curElementToPrint == numElementsToPrint)
    {
        fprintf(fp, "\n");
        Return();
    }

    fprintf(fp, "\t");

    // Having returned from a call (which can do anything), we need to re-validate the fast path conditions.
    // However, we should not lookup the global 'tostring' again. Instead we should retrieve its cached value
    // from the call frame. This is required to exhibit identical behavior as official Lua implementation.
    //
    TValue toStringFn = sb[numElementsToPrint + 1];
    uint64_t toStringFnU64 = reinterpret_cast<uint64_t>(TranslateToRawPointer(toStringFn.As<tFunction>()));
    if (unlikely(toStringFn.m_value != vm->GetLibFn<VM::LibFn::BaseToString>().m_value))
    {
        goto make_call_slowpath;
    }
    if (unlikely(!HasNoExoticToStringMetamethodForStringType(vm)))
    {
        goto make_call_slowpath;
    }

    while (curElementToPrint < numElementsToPrint)
    {
        if (unlikely(!TryPrintUsingFastPath(vm, fp, sb[curElementToPrint])))
        {
            goto make_call_slowpath;
        }
        curElementToPrint++;
        if (curElementToPrint < numElementsToPrint)
        {
            fprintf(fp, "\t");
        }
    }
    fprintf(fp, "\n");
    Return();

make_call_slowpath:
    valueToPrint = sb[curElementToPrint];
    sb[numElementsToPrint] = TValue::Create<tInt32>(static_cast<int32_t>(curElementToPrint + 1));

    TValue* callFrame = sb + numElementsToPrint + 2;
    if (likely(toStringFn.Is<tFunction>()))
    {
        reinterpret_cast<uint64_t*>(callFrame)[0] = toStringFnU64;
        callFrame[x_numSlotsForStackFrameHeader] = valueToPrint;
        MakeInPlaceCall(callFrame + x_numSlotsForStackFrameHeader, 1 /*numArgs*/, DEEGEN_LIB_FUNC_RETURN_CONTINUATION(base_print_continuation));
    }
    else
    {
        FunctionObject* callTarget = GetCallTargetViaMetatable(toStringFn);
        if (unlikely(callTarget == nullptr))
        {
            ThrowError(MakeErrorMessageForUnableToCall(toStringFn));
        }
        StorePtrToCallframe(callFrame, callTarget);
        callFrame[x_numSlotsForStackFrameHeader] = toStringFn;
        callFrame[x_numSlotsForStackFrameHeader + 1] = valueToPrint;
        MakeInPlaceCall(callFrame + x_numSlotsForStackFrameHeader, 2 /*numArgs*/, DEEGEN_LIB_FUNC_RETURN_CONTINUATION(base_print_continuation));
    }
}

// base.print -- https://www.lua.org/manual/5.1/manual.html#pdf-print
//
// print (···)
// Receives any number of arguments, and prints their values to stdout, using the tostring function to convert them to strings.
// print is not intended for formatted output, but only as a quick way to show a value, typically for debugging. For formatted
// output, use string.format.
//
// DEVNOTE: Lua 5.1-5.3 actually requires dynamically looking up the global value 'tostring', and invoke that function for
// each value to print. And the 'tostring' function always needs to check for '__tostring' metamethod, even for primitive data type.
// Therefore, we provide a fastpath for the common case that:
// (1) The global 'tostring' function is not changed
// (2) No exotic '__tostring' metamethod exists for non-table/userdata types.
// We can stay on the fastpath until we make a call (for a table with __tostring metamethod). Since the call can do anything,
// we need to recheck the fastpath condition after we get back from the call.
//
DEEGEN_DEFINE_LIB_FUNC(base_print)
{
    VM* vm = VM::GetActiveVMForCurrentThread();
    FILE* fp = vm->GetStdout();

    size_t numArgs = GetNumArgs();
    if (numArgs == 0)
    {
        fprintf(fp, "\n");
        Return();
    }

    size_t cur = 0;
    CoroutineRuntimeContext* currentCoro = GetCurrentCoroutine();
    auto [isToStringUnchanged, toStringFn] = IsGlobalToStringFunctionUnchanged(vm, currentCoro->m_globalObject.As());
    auto toStringFnU64 = reinterpret_cast<uint64_t>(TranslateToRawPointer(toStringFn.As<tFunction>()));
    if (unlikely(!isToStringUnchanged))
    {
        goto make_call_slowpath;
    }
    if (unlikely(!HasNoExoticToStringMetamethodForStringType(vm)))
    {
        goto make_call_slowpath;
    }

    while (cur < numArgs)
    {
        if (cur > 0)
        {
            fprintf(fp, "\t");
        }
        if (unlikely(!TryPrintUsingFastPath(vm, fp, GetArg(cur))))
        {
            goto make_call_slowpath;
        }
        cur++;
    }
    fprintf(fp, "\n");
    Return();

make_call_slowpath:
    TValue* sb = GetStackBase();
    TValue valueToPrint = sb[cur];
    // We store the # of arguments at slot 0 (this is fine since we don't need slot 0's value at this moment)
    // the ordinal of the next value to print at slot numArg, the toString function at slot numArg + 1,
    // so that these information can survive the call.
    //
    sb[0] = TValue::Create<tInt32>(static_cast<int32_t>(numArgs));
    sb[numArgs] = TValue::Create<tInt32>(static_cast<int32_t>(cur + 1));
    // This is required to exhibit the same behavior as official Lua. Official Lua will only lookup the global 'tostring'
    // once, and cache it to print all values in this 'print' call, even if the global 'tostring' value is modified by
    // user metamethod logic in between.
    //
    sb[numArgs + 1] = toStringFn;

    TValue* callFrame = sb + numArgs + 2;
    if (likely(toStringFn.Is<tFunction>()))
    {
        reinterpret_cast<uint64_t*>(callFrame)[0] = toStringFnU64;
        callFrame[x_numSlotsForStackFrameHeader] = valueToPrint;
        MakeInPlaceCall(callFrame + x_numSlotsForStackFrameHeader, 1 /*numArgs*/, DEEGEN_LIB_FUNC_RETURN_CONTINUATION(base_print_continuation));
    }
    else
    {
        FunctionObject* callTarget = GetCallTargetViaMetatable(toStringFn);
        if (unlikely(callTarget == nullptr))
        {
            ThrowError(MakeErrorMessageForUnableToCall(toStringFn));
        }
        reinterpret_cast<void**>(callFrame)[0] = callTarget;
        callFrame[x_numSlotsForStackFrameHeader] = toStringFn;
        callFrame[x_numSlotsForStackFrameHeader + 1] = valueToPrint;
        MakeInPlaceCall(callFrame + x_numSlotsForStackFrameHeader, 2 /*numArgs*/, DEEGEN_LIB_FUNC_RETURN_CONTINUATION(base_print_continuation));
    }
}

// base.rawequal -- https://www.lua.org/manual/5.1/manual.html#pdf-rawequal
//
// rawequal (v1, v2)
// Checks whether v1 is equal to v2, without invoking any metamethod. Returns a boolean.
//
DEEGEN_DEFINE_LIB_FUNC(base_rawequal)
{
    if (unlikely(GetNumArgs() < 2))
    {
        ThrowError("bad argument #2 to 'rawequal' (value expected)");
    }
    TValue lhs = GetArg(0);
    TValue rhs = GetArg(1);
    bool result;
    // See equality_bytecodes.cpp for explanation
    //
    if (rhs.Is<tDouble>())
    {
        result = UnsafeFloatEqual(lhs.ViewAsDouble(), rhs.As<tDouble>());
    }
    else
    {
        result = (lhs.m_value == rhs.m_value);
    }
    Return(TValue::Create<tBool>(result));
}

// base.rawget -- https://www.lua.org/manual/5.1/manual.html#pdf-rawget
//
// rawget (table, index)
// Gets the real value of table[index], without invoking any metamethod. table must be a table; index may be any value.
//
DEEGEN_DEFINE_LIB_FUNC(base_rawget)
{
    size_t numArgs = GetNumArgs();
    if (unlikely(numArgs < 2))
    {
        ThrowError("bad argument #2 to 'rawget' (value expected)");
    }

    TValue base = GetArg(0);
    TValue index = GetArg(1);
    if (unlikely(!base.Is<tTable>()))
    {
        ThrowError("bad argument #1 to 'rawget' (table expected)");
    }

    HeapPtr<TableObject> tableObj = base.As<tTable>();
    TValue result;

#if 0
    if (index.Is<tInt32>())
    {
        GetByIntegerIndexICInfo icInfo;
        TableObject::PrepareGetByIntegerIndex(tableObj, icInfo /*out*/);
        result = TableObject::GetByIntegerIndex(tableObj, index.As<tInt32>(), icInfo);
    }
    else
#endif
    if (index.Is<tDouble>())
    {
        double indexDouble = index.As<tDouble>();
        if (unlikely(IsNaN(indexDouble)))
        {
            // Indexing a table by 'NaN' for read is not an error, but always results in nil,
            // because indexing a table by 'NaN' for write is an error
            //
            result = TValue::Create<tNil>();
        }
        else
        {
            GetByIntegerIndexICInfo icInfo;
            TableObject::PrepareGetByIntegerIndex(tableObj, icInfo /*out*/);
            result = TableObject::GetByDoubleVal(tableObj, indexDouble, icInfo);
        }
    }
    else if (index.Is<tHeapEntity>())
    {
        GetByIdICInfo icInfo;
        TableObject::PrepareGetById(tableObj, UserHeapPointer<void> { index.As<tHeapEntity>() }, icInfo /*out*/);
        result = TableObject::GetById(tableObj, index.As<tHeapEntity>(), icInfo);
    }
    else
    {
        assert(index.Is<tMIV>());
        if (index.Is<tNil>())
        {
            // Indexing a table by 'nil' for read is not an error, but always results in nil,
            // because indexing a table by 'nil' for write is an error
            //
            result = TValue::Create<tNil>();
        }
        else
        {
            assert(index.Is<tBool>());
            UserHeapPointer<HeapString> specialKey = VM_GetSpecialKeyForBoolean(index.As<tBool>());

            GetByIdICInfo icInfo;
            TableObject::PrepareGetById(tableObj, specialKey, icInfo /*out*/);
            result = TableObject::GetById(tableObj, specialKey.As<void>(), icInfo);
        }
    }

    Return(result);
}

// base.rawset -- https://www.lua.org/manual/5.1/manual.html#pdf-rawset
//
// rawset (table, index, value)
// Sets the real value of table[index] to value, without invoking any metamethod. table must be a table, index any value
// different from nil, and value any Lua value.
//
// This function returns table.
//
DEEGEN_DEFINE_LIB_FUNC(base_rawset)
{
    if (unlikely(GetNumArgs() < 3))
    {
        ThrowError("bad argument #3 to 'rawset' (value expected)");
    }

    TValue base = GetArg(0);
    TValue index = GetArg(1);
    TValue newValue = GetArg(2);
    if (unlikely(!base.Is<tTable>()))
    {
        ThrowError("bad argument #1 to 'rawset' (table expected)");
    }

    HeapPtr<TableObject> tableObj = base.As<tTable>();

#if 0
    if (index.Is<tInt32>())
    {
        TableObject::RawPutByValIntegerIndex(tableObj, index.As<tInt32>(), newValue);
    }
    else
#endif
    if (index.Is<tDouble>())
    {
        double indexDouble = index.As<tDouble>();
        if (unlikely(IsNaN(indexDouble)))
        {
            ThrowError("table index is NaN");
        }
        TableObject::RawPutByValDoubleIndex(tableObj, indexDouble, newValue);
    }
    else if (index.Is<tHeapEntity>())
    {
        PutByIdICInfo icInfo;
        TableObject::PreparePutById(tableObj, UserHeapPointer<void> { index.As<tHeapEntity>() }, icInfo /*out*/);
        TableObject::PutById(tableObj, index.As<tHeapEntity>(), newValue, icInfo);
    }
    else
    {
        assert(index.Is<tMIV>());
        if (unlikely(index.Is<tNil>()))
        {
            ThrowError("table index is nil");
        }
        assert(index.Is<tBool>());
        UserHeapPointer<HeapString> specialKey = VM_GetSpecialKeyForBoolean(index.As<tBool>());

        PutByIdICInfo icInfo;
        TableObject::PreparePutById(tableObj, specialKey, icInfo /*out*/);
        TableObject::PutById(tableObj, specialKey.As<void>(), newValue, icInfo);
    }

    Return(base);
}

// base.require -- https://www.lua.org/manual/5.1/manual.html#pdf-require
//
// require (modname)
// Loads the given module. The function starts by looking into the package.loaded table to determine whether modname is already loaded.
// If it is, then require returns the value stored at package.loaded[modname]. Otherwise, it tries to find a loader for the module.
//
// To find a loader, require is guided by the package.loaders array. By changing this array, we can change how require looks for a module.
// The following explanation is based on the default configuration for package.loaders.
//
// First require queries package.preload[modname]. If it has a value, this value (which should be a function) is the loader. Otherwise
// require searches for a Lua loader using the path stored in package.path. If that also fails, it searches for a C loader using the path
// stored in package.cpath. If that also fails, it tries an all-in-one loader (see package.loaders).
//
// Once a loader is found, require calls the loader with a single argument, modname. If the loader returns any value, require assigns the
// returned value to package.loaded[modname]. If the loader returns no value and has not assigned any value to package.loaded[modname],
// then require assigns true to this entry. In any case, require returns the final value of package.loaded[modname].
//
// If there is any error loading or running the module, or if it cannot find any loader for the module, then require signals an error.
//
DEEGEN_DEFINE_LIB_FUNC(base_require)
{
    ThrowError("Library function 'require' is not implemented yet!");
}

// base.select -- https://www.lua.org/manual/5.1/manual.html#pdf-select
//
// select (index, ···)
// If index is a number, returns all arguments after argument number index. Otherwise, index must be the string "#", and select returns
// the total number of extra arguments it received.
//
DEEGEN_DEFINE_LIB_FUNC(base_select)
{
    size_t numArgs = GetNumArgs();
    if (unlikely(numArgs == 0))
    {
        ThrowError("bad argument #1 to 'select' (number expected, got no value)");
    }

    TValue selector = GetArg(0);
    if (selector.Is<tDouble>())
    {
        // This is exactly official Lua's logic. This is required, otherwise we could exhibit
        // different behavior regarding the "index out of range" error.
        //
        int64_t ord = static_cast<int64_t>(selector.As<tDouble>());
        int64_t range = static_cast<int64_t>(numArgs);
        if (ord < 0)
        {
            ord += range;
        }
        else if (ord > range)
        {
            ord = range;
        }
        if (unlikely(ord < 1))
        {
            ThrowError("bad argument #1 to 'select' (index out of range)");
        }
        ReturnValueRange(GetStackBase() + ord, static_cast<size_t>(range - ord));
    }

    bool isCountSelector = false;
    if (likely(selector.Is<tString>()))
    {
        HeapPtr<HeapString> str = selector.As<tString>();
        if (likely(str->m_length == 1 && str->m_string[0] == static_cast<uint8_t>('#')))
        {
            isCountSelector = true;
        }
    }
    if (unlikely(!isCountSelector))
    {
        ThrowError("bad argument #1 to 'select' (number expected)");
    }

    Return(TValue::Create<tDouble>(static_cast<double>(numArgs - 1)));
}

// base.setfenv -- https://www.lua.org/manual/5.1/manual.html#pdf-setfenv
//
// setfenv (f, table)
// Sets the environment to be used by the given function. f can be a Lua function or a number that specifies the function at that stack
// level: Level 1 is the function calling setfenv. setfenv returns the given function.
//
// As a special case, when f is 0 setfenv changes the environment of the running thread. In this case, setfenv returns no values.
//
DEEGEN_DEFINE_LIB_FUNC(base_setfenv)
{
    ThrowError("Library function 'setfenv' is not implemented yet!");
}

// base.setmetatable -- https://www.lua.org/manual/5.1/manual.html#pdf-setmetatable
//
// setmetatable (table, metatable)
// Sets the metatable for the given table. (You cannot change the metatable of other types from Lua, only from C.) If metatable is nil,
// removes the metatable of the given table. If the original metatable has a "__metatable" field, raises an error.
//
// This function returns table.
//
DEEGEN_DEFINE_LIB_FUNC(base_setmetatable)
{
    size_t numArgs = GetNumArgs();
    if (unlikely(numArgs == 0))
    {
        ThrowError("bad argument #1 to 'setmetatable' (table expected, got no value)");
    }

    TValue value = GetArg(0);
    if (unlikely(!value.Is<tTable>()))
    {
        // TODO: make this error message consistent with Lua
        //
        ThrowError("bad argument #1 to 'setmetatable' (table expected)");
    }

    if (unlikely(numArgs == 1))
    {
        ThrowError("bad argument #2 to 'setmetatable' (nil or table expected)");
    }

    TValue mt = GetArg(1);
    if (unlikely(!mt.Is<tNil>() && !mt.Is<tTable>()))
    {
        ThrowError("bad argument #2 to 'setmetatable' (nil or table expected)");
    }

    VM* vm = VM::GetActiveVMForCurrentThread();
    TableObject* obj = TranslateToRawPointer(vm, value.As<tTable>());

    UserHeapPointer<void> metatableMaybeNull = TableObject::GetMetatable(obj).m_result;
    if (metatableMaybeNull.m_value != 0)
    {
        HeapPtr<TableObject> existingMetatable = metatableMaybeNull.As<TableObject>();
        UserHeapPointer<HeapString> prop = VM_GetStringNameForMetatableKind(LuaMetamethodKind::ProtectedMt);
        GetByIdICInfo icInfo;
        TableObject::PrepareGetById(existingMetatable, prop, icInfo /*out*/);
        TValue result = TableObject::GetById(existingMetatable, prop.As<void>(), icInfo);
        if (unlikely(!result.Is<tNil>()))
        {
            ThrowError("cannot change a protected metatable");
        }
    }

    if (!mt.Is<tNil>())
    {
        obj->SetMetatable(vm, mt.As<tTable>());
    }
    else
    {
        obj->RemoveMetatable(vm);
    }

    // The return value of 'setmetatable' is the original object
    //
    Return(value);
}

// base.tonumber -- https://www.lua.org/manual/5.1/manual.html#pdf-tonumber
//
// tonumber (e [, base])
// Tries to convert its argument to a number. If the argument is already a number or a string convertible to a number, then tonumber
// returns this number; otherwise, it returns nil.
//
// An optional argument specifies the base to interpret the numeral. The base may be any integer between 2 and 36, inclusive. In bases
// above 10, the letter 'A' (in either upper or lower case) represents 10, 'B' represents 11, and so forth, with 'Z' representing 35.
// In base 10 (the default), the number can have a decimal part, as well as an optional exponent part (see §2.1). In other bases, only
// unsigned integers are accepted.
//
DEEGEN_DEFINE_LIB_FUNC(base_tonumber)
{
    if (unlikely(GetNumArgs() == 0))
    {
        ThrowError("bad argument #1 to 'tonumber' (value expected)");
    }
    TValue input = GetArg(0);
    if (likely(GetNumArgs() == 1))
    {
base_10_conversion:
        auto [success, value] = LuaLib_ToNumber(input);
        if (success)
        {
            Return(TValue::Create<tDouble>(value));
        }
        else
        {
            Return(TValue::Create<tNil>());
        }
    }

    TValue tvBase = GetArg(1);
    if (tvBase.Is<tNil>())
    {
        goto base_10_conversion;
    }

    auto [success, baseValueDouble] = LuaLib_ToNumber(tvBase);
    if (unlikely(!success))
    {
        ThrowError("bad argument #2 to 'tonumber' (number expected)");
    }
    int32_t baseValue = static_cast<int32_t>(baseValueDouble);
    if (baseValue == 10)
    {
        goto base_10_conversion;
    }

    if (unlikely(baseValue < 2 || baseValue > 36))
    {
        ThrowError("bad argument #2 to 'tonumber' (base out of range)");
    }

    // When a non-10 base is explicitly given, even if a number is passed in, it is treated as a string
    //
    if (input.Is<tDouble>())
    {
        double dbl = input.As<tDouble>();
        char buf[x_default_tostring_buffersize_double];
        StringifyDoubleUsingDefaultLuaFormattingOptions(buf /*out*/, dbl);
        StrScanResult res = TryConvertStringWithBaseToDoubleWithLuaSemantics(baseValue, buf);
        if (res.fmt == STRSCAN_ERROR)
        {
            Return(TValue::Create<tNil>());
        }
        else
        {
            assert(res.fmt == STRSCAN_NUM);
            Return(TValue::Create<tDouble>(res.d));
        }
    }
    else
    {
        // Yes this is Lua behavior: if input is not string, and if base is not provided, is nil, or is 10,
        // no error is thrown and the function returns nil.
        // But if the base is explicitly provided to be a valid non-10 base, then an error is thrown if the input is not string.
        //
        if (!input.Is<tString>())
        {
            ThrowError("bad argument #1 to 'tonumber' (string expected)");
        }
        HeapString* str = TranslateToRawPointer(input.As<tString>());
        StrScanResult res = TryConvertStringWithBaseToDoubleWithLuaSemantics(baseValue, str->m_string);
        if (res.fmt == STRSCAN_ERROR)
        {
            Return(TValue::Create<tNil>());
        }
        else
        {
            assert(res.fmt == STRSCAN_NUM);
            Return(TValue::Create<tDouble>(res.d));
        }
    }
}

static TValue WARN_UNUSED LuaDefaultStringifyValue(VM* vm, TValue value)
{
    if (value.Is<tDouble>())
    {
        double dbl = value.AsDouble();
        char buf[x_default_tostring_buffersize_double];
        StringifyDoubleUsingDefaultLuaFormattingOptions(buf /*out*/, dbl);
        return TValue::Create<tString>(TranslateToHeapPtr(vm->CreateStringObjectFromRawCString(buf)));
    }
    else if (value.Is<tMIV>())
    {
        MiscImmediateValue miv = value.AsMIV();
        if (miv.IsNil())
        {
            return TValue::Create<tString>(TranslateToHeapPtr(vm->CreateStringObjectFromRawCString("nil")));
        }
        else
        {
            assert(miv.IsBoolean());
            if (miv.GetBooleanValue())
            {
                return TValue::Create<tString>(TranslateToHeapPtr(vm->CreateStringObjectFromRawCString("true")));
            }
            else
            {
                return TValue::Create<tString>(TranslateToHeapPtr(vm->CreateStringObjectFromRawCString("false")));
            }
        }
    }
    else
    {
        assert(value.Is<tHeapEntity>());
        if (value.Is<tString>())
        {
            return value;
        }

        UserHeapGcObjectHeader* p = TranslateToRawPointer(vm, value.As<tHeapEntity>());
        char buf[100];
        if (p->m_type == HeapEntityType::Function)
        {
            sprintf(buf, "function: %p", static_cast<void*>(p));
        }
        else if (p->m_type == HeapEntityType::Table)
        {
            sprintf(buf, "table: %p", static_cast<void*>(p));
        }
        else if (p->m_type == HeapEntityType::Thread)
        {
            sprintf(buf, "thread: %p", static_cast<void*>(p));
        }
        else
        {
            // TODO: support userdata
            //
            assert(p->m_type == HeapEntityType::Userdata);
            assert(false && "unimplemented");
            __builtin_unreachable();
        }

        return TValue::Create<tString>(TranslateToHeapPtr(vm->CreateStringObjectFromRawCString(buf)));
    }
}

DEEGEN_DEFINE_LIB_FUNC_CONTINUATION(base_tostring_continuation)
{
    if (GetNumReturnValues() == 0)
    {
        Return(TValue::Create<tNil>());
    }
    else
    {
        Return(GetReturnValuesBegin()[0]);
    }
}

// base.tostring -- https://www.lua.org/manual/5.1/manual.html#pdf-tostring
//
// tostring (e)
// Receives an argument of any type and converts it to a string in a reasonable format. For complete control of how numbers are
// converted, use string.format.
//
// If the metatable of e has a "__tostring" field, then tostring calls the corresponding value with e as argument, and uses the
// result of the call as its result.
//
DEEGEN_DEFINE_LIB_FUNC(base_tostring)
{
    if (unlikely(GetNumArgs() == 0))
    {
        ThrowError("bad argument #1 to 'tostring' (value expected)");
    }
    VM* vm = VM::GetActiveVMForCurrentThread();
    TValue value = GetArg(0);
    UserHeapPointer<void> mt = GetMetatableForValue(value);
    if (likely(mt.m_value == 0))
    {
        Return(LuaDefaultStringifyValue(vm, value));
    }

    HeapPtr<TableObject> metatable = mt.As<TableObject>();
    assert(metatable->m_type == HeapEntityType::Table);
    GetByIdICInfo info;
    TableObject::PrepareGetById(metatable, vm->m_stringNameForToStringMetamethod, info /*out*/);
    TValue metamethod = TableObject::GetById(metatable, vm->m_stringNameForToStringMetamethod.As(), info);

    if (likely(metamethod.Is<tNil>()))
    {
        Return(LuaDefaultStringifyValue(vm, value));
    }

    TValue* callFrame = GetStackBase();
    if (likely(metamethod.Is<tFunction>()))
    {
        reinterpret_cast<void**>(callFrame)[0] = TranslateToRawPointer(metamethod.As<tFunction>());
        callFrame[x_numSlotsForStackFrameHeader] = value;
        MakeInPlaceCall(callFrame + x_numSlotsForStackFrameHeader, 1 /*numArgs*/, DEEGEN_LIB_FUNC_RETURN_CONTINUATION(base_tostring_continuation));
    }
    else
    {
        FunctionObject* callTarget = GetCallTargetViaMetatable(metamethod);
        if (unlikely(callTarget == nullptr))
        {
            ThrowError(MakeErrorMessageForUnableToCall(metamethod));
        }
        reinterpret_cast<void**>(callFrame)[0] = callTarget;
        callFrame[x_numSlotsForStackFrameHeader] = metamethod;
        callFrame[x_numSlotsForStackFrameHeader + 1] = value;
        MakeInPlaceCall(callFrame + x_numSlotsForStackFrameHeader, 2 /*numArgs*/, DEEGEN_LIB_FUNC_RETURN_CONTINUATION(base_tostring_continuation));
    }
}

// base.type -- https://www.lua.org/manual/5.1/manual.html#pdf-type
//
// type (v)
// Returns the type of its only argument, coded as a string. The possible results of this function are "nil" (a string, not the value nil),
// "number", "string", "boolean", "table", "function", "thread", and "userdata".
//
DEEGEN_DEFINE_LIB_FUNC(base_type)
{
    if (unlikely(GetNumArgs() == 0))
    {
        ThrowError("bad argument #1 to 'type' (value expected)");
    }
    VM* vm = VM::GetActiveVMForCurrentThread();
    TValue arg = GetArg(0);
    if (arg.Is<tNil>())
    {
        Return(TValue::Create<tString>(TranslateToHeapPtr(vm->CreateStringObjectFromRawCString("nil"))));
    }
    else if (arg.Is<tBool>())
    {
        Return(TValue::Create<tString>(TranslateToHeapPtr(vm->CreateStringObjectFromRawCString("boolean"))));
    }
    else if (arg.Is<tDouble>())
    {
        Return(TValue::Create<tString>(TranslateToHeapPtr(vm->CreateStringObjectFromRawCString("number"))));
    }
    else
    {
        assert(arg.Is<tHeapEntity>());
        HeapEntityType ty = arg.GetHeapEntityType();
        switch (ty)
        {
        case HeapEntityType::Table:
        {
            Return(TValue::Create<tString>(TranslateToHeapPtr(vm->CreateStringObjectFromRawCString("table"))));
        }
        case HeapEntityType::Function:
        {
            Return(TValue::Create<tString>(TranslateToHeapPtr(vm->CreateStringObjectFromRawCString("function"))));
        }
        case HeapEntityType::String:
        {
            Return(TValue::Create<tString>(TranslateToHeapPtr(vm->CreateStringObjectFromRawCString("string"))));
        }
        case HeapEntityType::Thread:
        {
            Return(TValue::Create<tString>(TranslateToHeapPtr(vm->CreateStringObjectFromRawCString("thread"))));
        }
        case HeapEntityType::Userdata:
        {
            Return(TValue::Create<tString>(TranslateToHeapPtr(vm->CreateStringObjectFromRawCString("userdata"))));
        }
        default:
        {
            assert(false);
            __builtin_unreachable();
        }
        }   /* switch ty*/
    }
}

// base.unpack -- https://www.lua.org/manual/5.1/manual.html#pdf-unpack
//
// unpack (list [, i [, j]])
// Returns the elements from the given table. This function is equivalent to
//     return list[i], list[i+1], ···, list[j]
// except that the above code can be written only for a fixed number of elements. By default, i is 1 and j is the length of the list,
// as defined by the length operator (see §2.5.5).
//
DEEGEN_DEFINE_LIB_FUNC(base_unpack)
{
    size_t numArgs = GetNumArgs();
    if (unlikely(numArgs == 0))
    {
        ThrowError("bad argument #1 to 'unpack' (table expected, got no value)");
    }
    TValue tvList = GetArg(0);
    if (unlikely(!tvList.Is<tTable>()))
    {
        ThrowError("bad argument #1 to 'unpack' (table expected)");
    }
    HeapPtr<TableObject> tableObj = tvList.As<tTable>();

    int64_t lb;
    if (numArgs == 1)
    {
        lb = 1;
    }
    else
    {
        TValue tvLb = GetArg(1);
        auto [success, val] = LuaLib_ToNumber(tvLb);
        if (unlikely(!success))
        {
            ThrowError("bad argument #2 to 'unpack' (number expected)");
        }
        lb = static_cast<int64_t>(val);
    }

    int64_t ub;
    if (numArgs < 3)
    {
        ub = TableObject::GetTableLengthWithLuaSemantics(tableObj);
    }
    else
    {
        TValue tvUb = GetArg(2);
        auto [success, val] = LuaLib_ToNumber(tvUb);
        if (unlikely(!success))
        {
            ThrowError("bad argument #3 to 'unpack' (number expected)");
        }
        ub = static_cast<int64_t>(val);
    }

    if (unlikely(lb > ub))
    {
        Return();
    }

    size_t numValues = static_cast<size_t>(ub - lb + 1);

    GetByIntegerIndexICInfo info;
    TableObject::PrepareGetByIntegerIndex(tableObj, info /*out*/);

    TValue* sb = GetStackBase();
    if (likely(info.m_isContinuous))
    {
        // Value exists iff index is in [1, endIdx]
        //
        TValue* butterfly = reinterpret_cast<TValue*>(tableObj->m_butterfly);
        int64_t endIdx = tableObj->m_butterfly->GetHeader()->m_arrayLengthIfContinuous + ArrayGrowthPolicy::x_arrayBaseOrd - 1;

        // Find the intersection interval between [1, endIdx] and [lb, ub]
        //
        int64_t intersectionStart = std::max(lb, static_cast<int64_t>(1));
        int64_t intersectionEnd = std::min(ub, endIdx);

        if (intersectionStart <= intersectionEnd)
        {
            // Populate the non-nil area by memcpy, and fill everything else with nil
            //
            for (int64_t i = 0; i < intersectionStart - lb; i++)
            {
                sb[i] = TValue::Create<tNil>();
            }
            for (int64_t i = intersectionEnd - lb + 1; i <= ub - lb; i++)
            {
                sb[i] = TValue::Create<tNil>();
            }
            memcpy(sb + intersectionStart - lb, butterfly + intersectionStart, sizeof(TValue) * static_cast<size_t>(intersectionEnd - intersectionStart + 1));
        }
        else
        {
            // The requested range has no intersection with the array, all nil
            //
            for (int64_t i = 0; i <= ub - lb; i++)
            {
                sb[i] = TValue::Create<tNil>();
            }
        }
        ReturnValueRange(sb, numValues);
    }

    // For the non-continuous cases, for now we just use a naive implementation
    // TODO: we could have done better
    //
    for (int64_t i = lb; i <= ub; i++)
    {
        sb[i - lb] = TableObject::GetByIntegerIndex(tableObj, i, info);
    }
    ReturnValueRange(sb, numValues);
}

// gcinfo -- deprecated in 5.1, removed in 5.2
//
// gcinfo ()
// Returns two results: the number of Kbytes of dynamic memory that Lua is using and the current garbage collector threshold (also in Kbytes).
//
DEEGEN_DEFINE_LIB_FUNC(base_gcinfo)
{
    ThrowError("Library function 'gcinfo' is not implemented yet!");
}

// newproxy -- undocumented feature, removed in 5.2
//
DEEGEN_DEFINE_LIB_FUNC(base_newproxy)
{
    ThrowError("Library function 'newproxy' is not implemented yet!");
}

DEEGEN_END_LIB_FUNC_DEFINITIONS
