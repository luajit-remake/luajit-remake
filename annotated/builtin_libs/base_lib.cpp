#include "deegen_api.h"
#include "bytecode.h"

// Lua standard library base.print
//
DEEGEN_DEFINE_LIB_FUNC(base_print)
{
    FILE* fp = VM::GetActiveVMForCurrentThread()->GetStdout();
    size_t numArgs = GetNumArgs();
    for (size_t i = 0; i < numArgs; i++)
    {
        if (i > 0)
        {
            fprintf(fp, "\t");
        }
        PrintTValue(fp, GetArg(i));
    }
    fprintf(fp, "\n");
    Return();
}

// Lua standard library base.getmetatable
//
DEEGEN_DEFINE_LIB_FUNC(base_getmetatable)
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

// Lua standard library base.setmetatable
//
DEEGEN_DEFINE_LIB_FUNC(base_setmetatable)
{
    size_t numArgs = GetNumArgs();
    if (numArgs == 0)
    {
        ThrowError("bad argument #1 to 'setmetatable' (table expected, got no value)");
    }

    TValue value = GetArg(0);
    if (!value.Is<tTable>())
    {
        // TODO: make this error message consistent with Lua
        //
        ThrowError("bad argument #1 to 'setmetatable' (table expected)");
    }

    if (numArgs == 1)
    {
        ThrowError("bad argument #2 to 'setmetatable' (nil or table expected)");
    }

    TValue mt = GetArg(1);
    if (!mt.Is<tNil>() && !mt.Is<tTable>())
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
        if (!result.Is<tNil>())
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

// Lua standard library base.rawget
//
DEEGEN_DEFINE_LIB_FUNC(base_rawget)
{
    size_t numArgs = GetNumArgs();
    if (numArgs < 2)
    {
        ThrowError("bad argument #2 to 'rawget' (value expected)");
    }

    TValue base = GetArg(0);
    TValue index = GetArg(1);
    if (!base.Is<tTable>())
    {
        ThrowError("bad argument #1 to 'rawget' (table expected)");
    }

    HeapPtr<TableObject> tableObj = base.As<tTable>();
    TValue result;

    if (index.Is<tInt32>())
    {
        GetByIntegerIndexICInfo icInfo;
        TableObject::PrepareGetByIntegerIndex(tableObj, icInfo /*out*/);
        result = TableObject::GetByIntegerIndex(tableObj, index.As<tInt32>(), icInfo);
    }
    else if (index.Is<tDouble>())
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

// Lua standard library base.rawset
//
DEEGEN_DEFINE_LIB_FUNC(base_rawset)
{
    if (GetNumArgs() < 3)
    {
        ThrowError("bad argument #3 to 'rawset' (value expected)");
    }

    TValue base = GetArg(0);
    TValue index = GetArg(1);
    TValue newValue = GetArg(2);
    if (!base.Is<tTable>())
    {
        ThrowError("bad argument #1 to 'rawset' (table expected)");
    }

    HeapPtr<TableObject> tableObj = base.As<tTable>();

    if (index.Is<tInt32>())
    {
        TableObject::RawPutByValIntegerIndex(tableObj, index.As<tInt32>(), newValue);
    }
    else if (index.Is<tDouble>())
    {
        double indexDouble = index.As<tDouble>();
        if (IsNaN(indexDouble))
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
        if (index.Is<tNil>())
        {
            ThrowError("table index is nil");
        }
        assert(index.Is<tBool>());
        UserHeapPointer<HeapString> specialKey = VM_GetSpecialKeyForBoolean(index.As<tBool>());

        PutByIdICInfo icInfo;
        TableObject::PreparePutById(tableObj, specialKey, icInfo /*out*/);
        TableObject::PutById(tableObj, specialKey.As<void>(), newValue, icInfo);
    }

    Return();
}

DEEGEN_END_LIB_FUNC_DEFINITIONS
