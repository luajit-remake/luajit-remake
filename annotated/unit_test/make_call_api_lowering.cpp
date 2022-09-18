#include "force_release_build.h"

#include "bytecode_definition_utils.h"
#include "deegen_api.h"

HeapPtr<FunctionObject> callee();

TValue a1();
TValue a2();
TValue a3();
TValue a4();
TValue a5();

TValue* r1();
size_t s1();

inline void NO_RETURN rc1()
{
    Return();
}

inline void NO_RETURN fn1()
{
    MakeCall(callee(), a1(), a2(), a3(), rc1);
}

DEEGEN_DEFINE_BYTECODE(test1)
{
    Operands();
    Result(NoOutput);
    Implementation(fn1);
    Variant();
}

DEEGEN_END_BYTECODE_DEFINITIONS
