#include "disable_assertions.h"

#include "api_define_bytecode.h"
#include "deegen_api.h"

static void NO_RETURN fn1()
{
    ThrowError("test error");
}

DEEGEN_DEFINE_BYTECODE(test1)
{
    Operands();
    Result(NoOutput);
    Implementation(fn1);
    Variant();
}

static void NO_RETURN fn2(TValue x)
{
    ThrowError(x);
}

DEEGEN_DEFINE_BYTECODE(test2)
{
    Operands(BytecodeSlot("x"));
    Result(NoOutput);
    Implementation(fn2);
    Variant();
}

DEEGEN_END_BYTECODE_DEFINITIONS
