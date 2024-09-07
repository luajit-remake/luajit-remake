#include "runtime_utils.h"
#include "api_define_lib_function.h"
#include "lj_parser_wrapper.h"
#include <numbers>

#define LUA_LIB_BASE_FUNCTION_LIST      \
    assert                              \
  , collectgarbage                      \
  , dofile                              \
  , error                               \
  , getfenv                             \
  , getmetatable                        \
  , ipairs                              \
  , load                                \
  , loadfile                            \
  , loadstring                          \
  , module                              \
  , next                                \
  , pairs                               \
  , pcall                               \
  , print                               \
  , rawequal                            \
  , rawget                              \
  , rawset                              \
  , require                             \
  , select                              \
  , setfenv                             \
  , setmetatable                        \
  , tonumber                            \
  , tostring                            \
  , type                                \
  , unpack                              \
  , xpcall                              \
  , gcinfo                              \
  , newproxy                            \

#define LUA_LIB_COROUTINE_FUNCTION_LIST \
    create                              \
  , resume                              \
  , running                             \
  , status                              \
  , wrap                                \
  , yield                               \

#define LUA_LIB_DEBUG_FUNCTION_LIST     \
    debug                               \
  , getfenv                             \
  , gethook                             \
  , getinfo                             \
  , getlocal                            \
  , getmetatable                        \
  , getregistry                         \
  , getupvalue                          \
  , setfenv                             \
  , sethook                             \
  , setlocal                            \
  , setmetatable                        \
  , setupvalue                          \
  , traceback                           \

#define LUA_LIB_IO_FUNCTION_LIST        \
    close                               \
  , flush                               \
  , input                               \
  , lines                               \
  , open                                \
  , output                              \
  , popen                               \
  , read                                \
  , tmpfile                             \
  , type                                \
  , write                               \

#define LUA_LIB_MATH_FUNCTION_LIST      \
    abs                                 \
  , acos                                \
  , asin                                \
  , atan                                \
  , atan2                               \
  , ceil                                \
  , cos                                 \
  , cosh                                \
  , deg                                 \
  , exp                                 \
  , floor                               \
  , fmod                                \
  , frexp                               \
  , ldexp                               \
  , log                                 \
  , log10                               \
  , max                                 \
  , min                                 \
  , modf                                \
  , pow                                 \
  , rad                                 \
  , random                              \
  , randomseed                          \
  , sin                                 \
  , sinh                                \
  , sqrt                                \
  , tan                                 \
  , tanh                                \

#define LUA_LIB_OS_FUNCTION_LIST        \
    clock                               \
  , date                                \
  , difftime                            \
  , execute                             \
  , exit                                \
  , getenv                              \
  , remove                              \
  , rename                              \
  , setlocale                           \
  , time                                \
  , tmpname                             \

#define LUA_LIB_PACKAGE_FUNCTION_LIST   \
    loadlib                             \
  , seeall                              \

#define LUA_LIB_STRING_FUNCTION_LIST    \
    byte                                \
  , char                                \
  , dump                                \
  , find                                \
  , format                              \
  , gmatch                              \
  , gsub                                \
  , len                                 \
  , lower                               \
  , match                               \
  , rep                                 \
  , reverse                             \
  , sub                                 \
  , upper                               \

#define LUA_LIB_TABLE_FUNCTION_LIST     \
    concat                              \
  , insert                              \
  , maxn                                \
  , remove                              \
  , sort                                \

// Create forward declaration for all the library functions
//
#define macro(libName, fnName) DEEGEN_FORWARD_DECLARE_LIB_FUNC(libName ## _ ## fnName);
PP_FOR_EACH_CARTESIAN_PRODUCT(macro, (base), (LUA_LIB_BASE_FUNCTION_LIST))
PP_FOR_EACH_CARTESIAN_PRODUCT(macro, (coroutine), (LUA_LIB_COROUTINE_FUNCTION_LIST))
PP_FOR_EACH_CARTESIAN_PRODUCT(macro, (debug), (LUA_LIB_DEBUG_FUNCTION_LIST))
PP_FOR_EACH_CARTESIAN_PRODUCT(macro, (io), (LUA_LIB_IO_FUNCTION_LIST))
PP_FOR_EACH_CARTESIAN_PRODUCT(macro, (math), (LUA_LIB_MATH_FUNCTION_LIST))
PP_FOR_EACH_CARTESIAN_PRODUCT(macro, (os), (LUA_LIB_OS_FUNCTION_LIST))
PP_FOR_EACH_CARTESIAN_PRODUCT(macro, (package), (LUA_LIB_PACKAGE_FUNCTION_LIST))
PP_FOR_EACH_CARTESIAN_PRODUCT(macro, (string), (LUA_LIB_STRING_FUNCTION_LIST))
PP_FOR_EACH_CARTESIAN_PRODUCT(macro, (table), (LUA_LIB_TABLE_FUNCTION_LIST))
#undef macro

// Count the number of functions in each library
// Note that this only counts functions. The library may also contain fields that are not functions.
//
#define macro(fnName) + 1
[[maybe_unused]] constexpr uint32_t x_num_functions_in_lib_base = 0 PP_FOR_EACH(macro, LUA_LIB_BASE_FUNCTION_LIST);
[[maybe_unused]] constexpr uint32_t x_num_functions_in_lib_coroutine = 0 PP_FOR_EACH(macro, LUA_LIB_COROUTINE_FUNCTION_LIST);
[[maybe_unused]] constexpr uint32_t x_num_functions_in_lib_debug = 0 PP_FOR_EACH(macro, LUA_LIB_DEBUG_FUNCTION_LIST);
[[maybe_unused]] constexpr uint32_t x_num_functions_in_lib_io = 0 PP_FOR_EACH(macro, LUA_LIB_IO_FUNCTION_LIST);
[[maybe_unused]] constexpr uint32_t x_num_functions_in_lib_math = 0 PP_FOR_EACH(macro, LUA_LIB_MATH_FUNCTION_LIST);
[[maybe_unused]] constexpr uint32_t x_num_functions_in_lib_os = 0 PP_FOR_EACH(macro, LUA_LIB_OS_FUNCTION_LIST);
[[maybe_unused]] constexpr uint32_t x_num_functions_in_lib_package = 0 PP_FOR_EACH(macro, LUA_LIB_PACKAGE_FUNCTION_LIST);
[[maybe_unused]] constexpr uint32_t x_num_functions_in_lib_string = 0 PP_FOR_EACH(macro, LUA_LIB_STRING_FUNCTION_LIST);
[[maybe_unused]] constexpr uint32_t x_num_functions_in_lib_table = 0 PP_FOR_EACH(macro, LUA_LIB_TABLE_FUNCTION_LIST);
#undef macro

struct CreateGlobalObjectHelper
{
    CreateGlobalObjectHelper(VM* vm_) : vm(vm_) { }

    void InsertField(TableObject* r, const char* propName, TValue value)
    {
        UserHeapPointer<HeapString> hs = vm->CreateStringObjectFromRawString(propName, static_cast<uint32_t>(strlen(propName)));
        PutByIdICInfo icInfo;
        TableObject::PreparePutById(r, hs /*prop*/, icInfo /*out*/);
        TableObject::PutById(r, hs.As<void>(), value, icInfo);
    }

    FunctionObject* CreateCFunc(void* func)
    {
        return FunctionObject::CreateCFunc(vm, ExecutableCode::CreateCFunction(vm, func)).As();
    }

    FunctionObject* InsertCFunc(TableObject* r, const char* propName, void* func)
    {
        FunctionObject* funcObj = CreateCFunc(func);
        InsertField(r, propName, TValue::Create<tFunction>(funcObj));
        return funcObj;
    }

    HeapString* InsertString(TableObject* r, const char* propName, const char* stringValue)
    {
        UserHeapPointer<HeapString> o = vm->CreateStringObjectFromRawString(stringValue, static_cast<uint32_t>(strlen(stringValue)));
        InsertField(r, propName, TValue::CreatePointer(o));
        return o.As();
    }

    TableObject* InsertObject(TableObject* r, const char* propName, uint32_t inlineCapacity)
    {
        UserHeapPointer<TableObject> o = TableObject::CreateEmptyTableObject(vm, inlineCapacity, 0 /*initialButterflyArrayPartCapacity*/);
        InsertField(r, propName, TValue::CreatePointer(o));
        return o.As();
    }

    VM* vm;
};

DEEGEN_FORWARD_DECLARE_LIB_FUNC(coroutine_wrap_call);
DEEGEN_FORWARD_DECLARE_LIB_FUNC(base_ipairs_iterator);
DEEGEN_FORWARD_DECLARE_LIB_FUNC(io_lines_iter);

#define INSERT_LIBFN(libName, fnName)                                               \
    [[maybe_unused]] FunctionObject* libfn_ ## libName ##_ ## fnName =      \
        h.InsertCFunc(                                                              \
            libobj_ ## libName /*object*/,                                          \
            PP_STRINGIFY(fnName) /*propName*/,                                      \
            DEEGEN_CODE_POINTER_FOR_LIB_FUNC(libName ## _ ## fnName) /*value*/);

UserHeapPointer<TableObject> CreateGlobalObject(VM* vm)
{
    CreateGlobalObjectHelper h(vm);

    vm->m_emptyString = vm->CreateStringObjectFromRawCString("");

    TableObject* globalObject = TableObject::CreateEmptyGlobalObject(vm);
    h.InsertField(globalObject, "_G", TValue::Create<tTable>(globalObject));
    h.InsertString(globalObject, "_VERSION", "Lua 5.1");

    lj_lex_init(vm);

    // Insert the base library functions into the global object
    //
    TableObject* libobj_base = globalObject;
    PP_FOR_EACH_CARTESIAN_PRODUCT(INSERT_LIBFN, (base), (LUA_LIB_BASE_FUNCTION_LIST))

    vm->InitializeLibFn<VM::LibFn::BaseError>(TValue::Create<tFunction>(libfn_base_error));
    vm->InitializeLibFn<VM::LibFn::BaseNext>(TValue::Create<tFunction>(libfn_base_next));
    vm->InitializeLibFn<VM::LibFn::BaseIPairsIter>(TValue::Create<tFunction>(h.CreateCFunc(DEEGEN_CODE_POINTER_FOR_LIB_FUNC(base_ipairs_iterator))));
    vm->InitializeLibFn<VM::LibFn::BaseToString>(TValue::Create<tFunction>(libfn_base_tostring));
    vm->InitializeLibFn<VM::LibFn::BaseLoad>(TValue::Create<tFunction>(libfn_base_load));
    vm->InitializeLibFn<VM::LibFn::BaseNextValidationOk>(TValue::Create<tTable>(TableObject::CreateEmptyTableObject(vm, 0U /*inlineCapacity*/, 0 /*initialButterflyArrayPartCapacity*/)));
    vm->m_stringNameForToStringMetamethod = vm->CreateStringObjectFromRawCString("__tostring");
    vm->m_toStringString = vm->CreateStringObjectFromRawCString("tostring");

    // Initialize coroutine library
    // The coroutine library has no non-function fields
    //
    TableObject* libobj_coroutine = h.InsertObject(globalObject, "coroutine", x_num_functions_in_lib_coroutine);
    PP_FOR_EACH_CARTESIAN_PRODUCT(INSERT_LIBFN, (coroutine), (LUA_LIB_COROUTINE_FUNCTION_LIST))

    vm->InitializeLibFnProto<VM::LibFnProto::CoroutineWrapCall>(ExecutableCode::CreateCFunction(vm, DEEGEN_CODE_POINTER_FOR_LIB_FUNC(coroutine_wrap_call)));

    // Initialize debug library
    // The debug library has no non-function fields
    //
    TableObject* libobj_debug = h.InsertObject(globalObject, "debug", x_num_functions_in_lib_debug);
    PP_FOR_EACH_CARTESIAN_PRODUCT(INSERT_LIBFN, (debug), (LUA_LIB_DEBUG_FUNCTION_LIST))

    // Initialize io library
    // The io library has 3 non-function fields: stdin, stdout, stderr
    // TODO: we need to implement these fields.
    //
    TableObject* libobj_io = h.InsertObject(globalObject, "io", x_num_functions_in_lib_io + 3);
    PP_FOR_EACH_CARTESIAN_PRODUCT(INSERT_LIBFN, (io), (LUA_LIB_IO_FUNCTION_LIST))

    // Initialize math library
    // The math library has 2 non-function fields: huge and pi
    // Additionally, it has 1 field for compatibility: math.mod = math.fmod
    //
    constexpr bool x_enable_lua_compat_math_mod = true;
    TableObject* libobj_math = h.InsertObject(globalObject, "math", x_num_functions_in_lib_math + 2 + (x_enable_lua_compat_math_mod ? 1 : 0));
    h.InsertField(libobj_math, "pi", TValue::Create<tDouble>(std::numbers::pi));
    h.InsertField(libobj_math, "huge", TValue::Create<tDouble>(HUGE_VAL));
    PP_FOR_EACH_CARTESIAN_PRODUCT(INSERT_LIBFN, (math), (LUA_LIB_MATH_FUNCTION_LIST))
    if (x_enable_lua_compat_math_mod)
    {
        h.InsertField(libobj_math, "mod", TValue::Create<tFunction>(libfn_math_fmod));
    }

    // Initialize os library
    // The os library has no non-function fields
    //
    TableObject* libobj_os = h.InsertObject(globalObject, "os", x_num_functions_in_lib_os);
    PP_FOR_EACH_CARTESIAN_PRODUCT(INSERT_LIBFN, (os), (LUA_LIB_OS_FUNCTION_LIST))

    // Initialize package library
    // The package library has 6 non-function fields: cpath, loaded, loaders, path, preload, config
    // TODO: we need to implement these fields.
    //
    TableObject* libobj_package = h.InsertObject(globalObject, "package", x_num_functions_in_lib_package + 6);
    PP_FOR_EACH_CARTESIAN_PRODUCT(INSERT_LIBFN, (package), (LUA_LIB_PACKAGE_FUNCTION_LIST))

    // Initialize string library
    // The string library has no non-function fields
    // Additionally, it has 1 field for compatibility: string.gfind = string.find
    //
    constexpr bool x_enable_lua_compat_string_gfind = true;
    TableObject* libobj_string = h.InsertObject(globalObject, "string", x_num_functions_in_lib_string + (x_enable_lua_compat_string_gfind ? 1 : 0));
    PP_FOR_EACH_CARTESIAN_PRODUCT(INSERT_LIBFN, (string), (LUA_LIB_STRING_FUNCTION_LIST))
    if (x_enable_lua_compat_string_gfind)
    {
        h.InsertField(libobj_string, "gfind", TValue::Create<tFunction>(libfn_string_find));
    }

    // According to Lua standard, we need to set a metatable for strings where the __index field points to the string table,
    // so that string functions can be used in object-oriented style, e.g., string.byte(s, i) can be written as s:byte(i).
    //
    {
        TableObject* string_type_metatable = TableObject::CreateEmptyTableObject(vm, 1 /*inlineCapacity*/, 0 /*initialButterflyArrayPartCapacity*/);
        h.InsertField(string_type_metatable, "__index", TValue::Create<tTable>(libobj_string));
        vm->m_metatableForString = string_type_metatable;
        vm->m_initialHiddenClassOfMetatableForString = string_type_metatable->m_hiddenClass;
    }

    // Initialize table library
    // The table library has no non-function fields
    //
    TableObject* libobj_table = h.InsertObject(globalObject, "table", x_num_functions_in_lib_table);
    PP_FOR_EACH_CARTESIAN_PRODUCT(INSERT_LIBFN, (table), (LUA_LIB_TABLE_FUNCTION_LIST))
    vm->InitializeLibFn<VM::LibFn::IoLinesIter>(TValue::Create<tFunction>(h.CreateCFunc(DEEGEN_CODE_POINTER_FOR_LIB_FUNC(io_lines_iter))));

    return globalObject;
}

#undef INSERT_LIBFN
