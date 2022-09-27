#include "force_release_build.h"

#include "bytecode_definition_utils.h"
#include "deegen_api.h"

HeapPtr<FunctionObject> callee();

size_t a1();

TValue* d1();

inline void NO_RETURN rc1()
{
    Return(GetReturnValue(0));
}

inline void NO_RETURN fn1()
{
    MakeCall(callee(), rc1);
}

DEEGEN_DEFINE_BYTECODE(test1)
{
    Operands();
    Result(BytecodeValue);
    Implementation(fn1);
    Variant();
}

inline void NO_RETURN rc2()
{
    Return(GetReturnValue(1));
}

inline void NO_RETURN fn2()
{
    MakeCall(callee(), rc2);
}

DEEGEN_DEFINE_BYTECODE(test2)
{
    Operands();
    Result(BytecodeValue);
    Implementation(fn2);
    Variant();
}

inline void NO_RETURN rc3()
{
    Return(GetReturnValue(10));
}

inline void NO_RETURN fn3()
{
    MakeCall(callee(), rc3);
}

DEEGEN_DEFINE_BYTECODE(test3)
{
    Operands();
    Result(BytecodeValue);
    Implementation(fn3);
    Variant();
}

inline void NO_RETURN rc4()
{
    Return(GetReturnValue(a1()));
}

inline void NO_RETURN fn4()
{
    MakeCall(callee(), rc4);
}

DEEGEN_DEFINE_BYTECODE(test4)
{
    Operands();
    Result(BytecodeValue);
    Implementation(fn4);
    Variant();
}

inline void NO_RETURN rc5()
{
    StoreReturnValuesTo(d1(), a1());
    Return();
}

inline void NO_RETURN fn5()
{
    MakeCall(callee(), rc5);
}

DEEGEN_DEFINE_BYTECODE(test5)
{
    Operands();
    Result(NoOutput);
    Implementation(fn5);
    Variant();
}

inline void NO_RETURN rc6()
{
    StoreReturnValuesTo(d1(), 1);
    Return();
}

inline void NO_RETURN fn6()
{
    MakeCall(callee(), rc6);
}

DEEGEN_DEFINE_BYTECODE(test6)
{
    Operands();
    Result(NoOutput);
    Implementation(fn6);
    Variant();
}

inline void NO_RETURN rc7()
{
    StoreReturnValuesTo(d1(), 2);
    Return();
}

inline void NO_RETURN fn7()
{
    MakeCall(callee(), rc7);
}

DEEGEN_DEFINE_BYTECODE(test7)
{
    Operands();
    Result(NoOutput);
    Implementation(fn7);
    Variant();
}

DEEGEN_END_BYTECODE_DEFINITIONS