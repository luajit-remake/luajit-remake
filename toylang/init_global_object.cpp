#include "bytecode.h"
#include "deegen_def_lib_func_api.h"

DEEGEN_FORWARD_DECLARE_LIB_FUNC(base_print);
DEEGEN_FORWARD_DECLARE_LIB_FUNC(base_error);
DEEGEN_FORWARD_DECLARE_LIB_FUNC(base_pcall);
DEEGEN_FORWARD_DECLARE_LIB_FUNC(base_xpcall);
DEEGEN_FORWARD_DECLARE_LIB_FUNC(base_getmetatable);
DEEGEN_FORWARD_DECLARE_LIB_FUNC(base_setmetatable);
DEEGEN_FORWARD_DECLARE_LIB_FUNC(base_rawget);
DEEGEN_FORWARD_DECLARE_LIB_FUNC(base_rawset);
DEEGEN_FORWARD_DECLARE_LIB_FUNC(base_pairs);
DEEGEN_FORWARD_DECLARE_LIB_FUNC(base_next);

DEEGEN_FORWARD_DECLARE_LIB_FUNC(math_sqrt);

DEEGEN_FORWARD_DECLARE_LIB_FUNC(io_write);

DEEGEN_FORWARD_DECLARE_LIB_FUNC(debug_getmetatable);
DEEGEN_FORWARD_DECLARE_LIB_FUNC(debug_setmetatable);

UserHeapPointer<TableObject> CreateGlobalObject(VM* vm)
{
#define LIB_FN DEEGEN_CODE_POINTER_FOR_LIB_FUNC

    HeapPtr<TableObject> globalObject = TableObject::CreateEmptyGlobalObject(vm);

    auto insertField = [&](HeapPtr<TableObject> r, const char* propName, TValue value)
    {
        UserHeapPointer<HeapString> hs = vm->CreateStringObjectFromRawString(propName, static_cast<uint32_t>(strlen(propName)));
        PutByIdICInfo icInfo;
        TableObject::PreparePutById(r, hs /*prop*/, icInfo /*out*/);
        TableObject::PutById(r, hs.As<void>(), value, icInfo);
    };

    auto insertCFunc = [&](HeapPtr<TableObject> r, const char* propName, void* func) -> HeapPtr<FunctionObject>
    {
        UserHeapPointer<FunctionObject> funcObj = FunctionObject::CreateCFunc(vm, ExecutableCode::CreateCFunction(vm, func));
        insertField(r, propName, TValue::CreatePointer(funcObj));
        return funcObj.As();
    };

    auto insertObject = [&](HeapPtr<TableObject> r, const char* propName, uint8_t inlineCapacity) -> HeapPtr<TableObject>
    {
        SystemHeapPointer<Structure> initialStructure = Structure::GetInitialStructureForInlineCapacity(vm, inlineCapacity);
        UserHeapPointer<TableObject> o = TableObject::CreateEmptyTableObject(vm, TranslateToRawPointer(vm, initialStructure.As()), 0 /*initialButterflyArrayPartCapacity*/);
        insertField(r, propName, TValue::CreatePointer(o));
        return o.As();
    };

    insertField(globalObject, "_G", TValue::Create<tTable>(globalObject));

    insertCFunc(globalObject, "print", LIB_FN(base_print));
    HeapPtr<FunctionObject> baseDotError = insertCFunc(globalObject, "error", LIB_FN(base_error));
    vm->InitLibBaseDotErrorFunctionObject(TValue::Create<tFunction>(baseDotError));
    insertCFunc(globalObject, "pcall", LIB_FN(base_pcall));
    insertCFunc(globalObject, "xpcall", LIB_FN(base_xpcall));

    insertCFunc(globalObject, "pairs", LIB_FN(base_pairs));
    HeapPtr<FunctionObject> nextFn = insertCFunc(globalObject, "next", LIB_FN(base_next));
    vm->InitLibBaseDotNextFunctionObject(TValue::Create<tFunction>(nextFn));

    insertCFunc(globalObject, "getmetatable", LIB_FN(base_getmetatable));
    insertCFunc(globalObject, "setmetatable", LIB_FN(base_setmetatable));
    insertCFunc(globalObject, "rawget", LIB_FN(base_rawget));
    insertCFunc(globalObject, "rawset", LIB_FN(base_rawset));

    HeapPtr<TableObject> mathObj = insertObject(globalObject, "math", 32);
    insertCFunc(mathObj, "sqrt", LIB_FN(math_sqrt));

    HeapPtr<TableObject> ioObj = insertObject(globalObject, "io", 16);
    insertCFunc(ioObj, "write", LIB_FN(io_write));

    HeapPtr<TableObject> debugObj = insertObject(globalObject, "debug", 16);
    insertCFunc(debugObj, "getmetatable", LIB_FN(debug_getmetatable));
    insertCFunc(debugObj, "setmetatable", LIB_FN(debug_setmetatable));

    return globalObject;
#undef LIB_FN
}
