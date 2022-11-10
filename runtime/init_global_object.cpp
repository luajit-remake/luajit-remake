#include "runtime_utils.h"
#include "api_define_lib_function.h"

#define LUA_LIB_BASE_FUNCTION_LIST  \
    assert                          \
  , collectgarbage                  \
  , dofile                          \
  , error                           \
  , getmetatable                    \
  , next                            \
  , pairs                           \
  , pcall                           \
  , print                           \
  , rawget                          \
  , rawset                          \
  , setmetatable                    \
  , xpcall                          \

#define LUA_LIB_MATH_FUNCTION_LIST  \
    sqrt                            \

#define LUA_LIB_IO_FUNCTION_LIST    \
    write                           \

#define LUA_LIB_DEBUG_FUNCTION_LIST \
    getmetatable                    \
  , setmetatable                    \

// Create forward declaration for all the library functions
//
#define macro(libName, fnName) DEEGEN_FORWARD_DECLARE_LIB_FUNC(libName ## _ ## fnName);
PP_FOR_EACH_CARTESIAN_PRODUCT(macro, (base), (LUA_LIB_BASE_FUNCTION_LIST))
PP_FOR_EACH_CARTESIAN_PRODUCT(macro, (math), (LUA_LIB_MATH_FUNCTION_LIST))
PP_FOR_EACH_CARTESIAN_PRODUCT(macro, (io), (LUA_LIB_IO_FUNCTION_LIST))
PP_FOR_EACH_CARTESIAN_PRODUCT(macro, (debug), (LUA_LIB_DEBUG_FUNCTION_LIST))
#undef macro

struct CreateGlobalObjectHelper
{
    CreateGlobalObjectHelper(VM* vm_) : vm(vm_) { }

    void InsertField(HeapPtr<TableObject> r, const char* propName, TValue value)
    {
        UserHeapPointer<HeapString> hs = vm->CreateStringObjectFromRawString(propName, static_cast<uint32_t>(strlen(propName)));
        PutByIdICInfo icInfo;
        TableObject::PreparePutById(r, hs /*prop*/, icInfo /*out*/);
        TableObject::PutById(r, hs.As<void>(), value, icInfo);
    }

    HeapPtr<FunctionObject> InsertCFunc(HeapPtr<TableObject> r, const char* propName, void* func)
    {
        UserHeapPointer<FunctionObject> funcObj = FunctionObject::CreateCFunc(vm, ExecutableCode::CreateCFunction(vm, func));
        InsertField(r, propName, TValue::CreatePointer(funcObj));
        return funcObj.As();
    }

    HeapPtr<TableObject> InsertObject(HeapPtr<TableObject> r, const char* propName, uint8_t inlineCapacity)
    {
        SystemHeapPointer<Structure> initialStructure = Structure::GetInitialStructureForInlineCapacity(vm, inlineCapacity);
        UserHeapPointer<TableObject> o = TableObject::CreateEmptyTableObject(vm, TranslateToRawPointer(vm, initialStructure.As()), 0 /*initialButterflyArrayPartCapacity*/);
        InsertField(r, propName, TValue::CreatePointer(o));
        return o.As();
    }

    VM* vm;
};

UserHeapPointer<TableObject> CreateGlobalObject(VM* vm)
{
    CreateGlobalObjectHelper h(vm);
    HeapPtr<TableObject> globalObject = TableObject::CreateEmptyGlobalObject(vm);
    h.InsertField(globalObject, "_G", TValue::Create<tTable>(globalObject));

    HeapPtr<TableObject> libobj_base = globalObject;
    HeapPtr<TableObject> libobj_math = h.InsertObject(globalObject, "math", 32 /*inlineCapacity*/);
    HeapPtr<TableObject> libobj_io = h.InsertObject(globalObject, "io", 16 /*inlineCapacity*/);
    HeapPtr<TableObject> libobj_debug = h.InsertObject(globalObject, "debug", 16 /*inlineCapacity*/);

#define macro(libName, fnName)                                                      \
    [[maybe_unused]] HeapPtr<FunctionObject> libfn_ ## libName ##_ ## fnName =      \
        h.InsertCFunc(                                                              \
            libobj_ ## libName /*object*/,                                          \
            PP_STRINGIFY(fnName) /*propName*/,                                      \
            DEEGEN_CODE_POINTER_FOR_LIB_FUNC(libName ## _ ## fnName) /*value*/);

    PP_FOR_EACH_CARTESIAN_PRODUCT(macro, (base), (LUA_LIB_BASE_FUNCTION_LIST))
    PP_FOR_EACH_CARTESIAN_PRODUCT(macro, (math), (LUA_LIB_MATH_FUNCTION_LIST))
    PP_FOR_EACH_CARTESIAN_PRODUCT(macro, (io), (LUA_LIB_IO_FUNCTION_LIST))
    PP_FOR_EACH_CARTESIAN_PRODUCT(macro, (debug), (LUA_LIB_DEBUG_FUNCTION_LIST))

#undef macro

    vm->InitLibBaseDotErrorFunctionObject(TValue::Create<tFunction>(libfn_base_error));
    vm->InitLibBaseDotNextFunctionObject(TValue::Create<tFunction>(libfn_base_next));

    return globalObject;
}
