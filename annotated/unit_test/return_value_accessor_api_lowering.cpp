#include "force_release_build.h"

#include "api_define_bytecode.h"
#include "deegen_api.h"

FunctionObject* callee();

size_t a1();

TValue* d1();

static void NO_RETURN rc1()
{
    Return(GetReturnValue(0));
}

static void NO_RETURN fn1()
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

static void NO_RETURN rc2()
{
    Return(GetReturnValue(1));
}

static void NO_RETURN fn2()
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

static void NO_RETURN rc3()
{
    Return(GetReturnValue(10));
}

static void NO_RETURN fn3()
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

static void NO_RETURN rc4()
{
    Return(GetReturnValue(a1()));
}

static void NO_RETURN fn4()
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

static void NO_RETURN rc5()
{
    StoreReturnValuesTo(d1(), a1());
    Return();
}

static void NO_RETURN fn5()
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

static void NO_RETURN rc6()
{
    StoreReturnValuesTo(d1(), 1);
    Return();
}

static void NO_RETURN fn6()
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

static void NO_RETURN rc7()
{
    StoreReturnValuesTo(d1(), 2);
    Return();
}

static void NO_RETURN fn7()
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
