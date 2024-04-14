#include "force_release_build.h"

#include "api_define_bytecode.h"
#include "deegen_api.h"

FunctionObject* callee();

TValue a1();
TValue a2();
TValue a3();
TValue a4();
TValue a5();
TValue a6();
TValue a7();
TValue a8();
TValue a9();

TValue* r1();
size_t s1();

static void NO_RETURN rc()
{
    Return();
}

static void NO_RETURN fn1()
{
    MakeCall(callee(), a1(), a2(), a3(), rc);
}

DEEGEN_DEFINE_BYTECODE(test1)
{
    Operands();
    Result(NoOutput);
    Implementation(fn1);
    Variant();
}

static void NO_RETURN fn2()
{
    MakeCall(callee(), rc);
}

DEEGEN_DEFINE_BYTECODE(test2)
{
    Operands();
    Result(NoOutput);
    Implementation(fn2);
    Variant();
}

static void NO_RETURN fn3()
{
    MakeCall(callee(), a1(), a2(), a3(), a4(), r1(), s1(), a5(), a6(), a7(), a8(), rc);
}

DEEGEN_DEFINE_BYTECODE(test3)
{
    Operands();
    Result(NoOutput);
    Implementation(fn3);
    Variant();
}

static void NO_RETURN fn4()
{
    TValue* r = r1();
    MakeInPlaceCall(*reinterpret_cast<FunctionObject**>(r - x_numSlotsForStackFrameHeader), r, s1(), rc);
}

DEEGEN_DEFINE_BYTECODE(test4)
{
    Operands();
    Result(NoOutput);
    Implementation(fn4);
    Variant();
}

static void NO_RETURN fn5()
{
    TValue* r = r1();
    MakeInPlaceCallPassingVariadicRes(*reinterpret_cast<FunctionObject**>(r - x_numSlotsForStackFrameHeader), r, s1(), rc);
}

DEEGEN_DEFINE_BYTECODE(test5)
{
    Operands();
    Result(NoOutput);
    Implementation(fn5);
    Variant();
}

static void NO_RETURN fn6()
{
    MakeCallPassingVariadicRes(callee(), a1(), a2(), a3(), rc);
}

DEEGEN_DEFINE_BYTECODE(test6)
{
    Operands();
    Result(NoOutput);
    Implementation(fn6);
    Variant();
}

static void NO_RETURN fn7()
{
    MakeCallPassingVariadicRes(callee(), rc);
}

DEEGEN_DEFINE_BYTECODE(test7)
{
    Operands();
    Result(NoOutput);
    Implementation(fn7);
    Variant();
}

static void NO_RETURN fn8()
{
    MakeCallPassingVariadicRes(callee(), a1(), a2(), a3(), a4(), r1(), s1(), a5(), a6(), a7(), a8(), rc);
}

DEEGEN_DEFINE_BYTECODE(test8)
{
    Operands();
    Result(NoOutput);
    Implementation(fn8);
    Variant();
}

static void NO_RETURN fn9()
{
    MakeTailCall(callee(), a1(), a2(), a3());
}

DEEGEN_DEFINE_BYTECODE(test9)
{
    Operands();
    Result(NoOutput);
    Implementation(fn9);
    Variant();
}

static void NO_RETURN fn10()
{
    MakeTailCall(callee());
}

DEEGEN_DEFINE_BYTECODE(test10)
{
    Operands();
    Result(NoOutput);
    Implementation(fn10);
    Variant();
}

static void NO_RETURN fn11()
{
    MakeTailCall(callee(), a1(), a2(), a3(), a4(), r1(), s1(), a5(), a6(), a7(), a8());
}

DEEGEN_DEFINE_BYTECODE(test11)
{
    Operands();
    Result(NoOutput);
    Implementation(fn11);
    Variant();
}

static void NO_RETURN fn12()
{
    TValue* r = r1();
    MakeInPlaceTailCall(*reinterpret_cast<FunctionObject**>(r - x_numSlotsForStackFrameHeader), r, s1());
}

DEEGEN_DEFINE_BYTECODE(test12)
{
    Operands();
    Result(NoOutput);
    Implementation(fn12);
    Variant();
}

static void NO_RETURN fn13()
{
    TValue* r = r1();
    MakeInPlaceTailCallPassingVariadicRes(*reinterpret_cast<FunctionObject**>(r - x_numSlotsForStackFrameHeader), r, s1());
}

DEEGEN_DEFINE_BYTECODE(test13)
{
    Operands();
    Result(NoOutput);
    Implementation(fn13);
    Variant();
}

static void NO_RETURN fn14()
{
    MakeTailCallPassingVariadicRes(callee(), a1(), a2(), a3());
}

DEEGEN_DEFINE_BYTECODE(test14)
{
    Operands();
    Result(NoOutput);
    Implementation(fn14);
    Variant();
}

static void NO_RETURN fn15()
{
    MakeTailCallPassingVariadicRes(callee());
}

DEEGEN_DEFINE_BYTECODE(test15)
{
    Operands();
    Result(NoOutput);
    Implementation(fn15);
    Variant();
}

static void NO_RETURN fn16()
{
    MakeTailCallPassingVariadicRes(callee(), a1(), a2(), a3(), a4(), r1(), s1(), a5(), a6(), a7(), a8());
}

DEEGEN_DEFINE_BYTECODE(test16)
{
    Operands();
    Result(NoOutput);
    Implementation(fn16);
    Variant();
}

DEEGEN_END_BYTECODE_DEFINITIONS
