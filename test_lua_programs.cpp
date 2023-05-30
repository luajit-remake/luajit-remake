#include <fstream>
#include "runtime_utils.h"
#include "gtest/gtest.h"
#include "json_utils.h"
#include "test_util_helper.h"
#include "test_vm_utils.h"
#include "lj_parser_wrapper.h"
#include "drt/baseline_jit_codegen_helper.h"
#include "test_lua_file_utils.h"

namespace {

// Just make sure the json parser library works
//
TEST(JSONParser, Sanity)
{
    json j = json::parse("{ \"a\" : 1, \"b\" : \"cd\" }");
    ReleaseAssert(j.is_object());
    ReleaseAssert(j.count("a"));
    ReleaseAssert(j["a"].is_primitive());
    ReleaseAssert(j["a"] == 1);
    ReleaseAssert(j.count("b"));
    ReleaseAssert(j["b"].is_string());
    ReleaseAssert(j["b"].get<std::string>() == "cd");
}

TEST(LuaTest, Fib)
{
    RunSimpleLuaTest("luatests/fib.lua", LuaTestOption::ForceInterpreter);
}

TEST(LuaTestForceBaselineJit, Fib)
{
    RunSimpleLuaTest("luatests/fib.lua", LuaTestOption::ForceBaselineJit);
}

static void LuaTest_TestPrint_Impl(LuaTestOption testOption)
{
    VM* vm = VM::Create();
    Auto(vm->Destroy());
    vm->SetEngineStartingTier(GetVMEngineStartingTierFromEngineTestOption(testOption));
    VMOutputInterceptor vmoutput(vm);

    std::unique_ptr<ScriptModule> module = ParseLuaScriptOrFail("luatests/test_print.lua", testOption);
    vm->LaunchScript(module.get());

    std::string out = vmoutput.GetAndResetStdOut();
    std::string err = vmoutput.GetAndResetStdErr();

    std::string expectedOut = "0.2\t3\tfalse\ttrue\tnil\tabc\tfunction: 0x";
    ReleaseAssert(out.starts_with(expectedOut));
    ReleaseAssert(err == "");
}

TEST(LuaTest, TestPrint)
{
    LuaTest_TestPrint_Impl(LuaTestOption::ForceInterpreter);
}

TEST(LuaTestForceBaselineJit, TestPrint)
{
    LuaTest_TestPrint_Impl(LuaTestOption::ForceBaselineJit);
}

TEST(LuaTest, TestTableDup)
{
    RunSimpleLuaTest("luatests/table_dup.lua", LuaTestOption::ForceInterpreter);
}

TEST(LuaTestForceBaselineJit, TestTableDup)
{
    RunSimpleLuaTest("luatests/table_dup.lua", LuaTestOption::ForceBaselineJit);
}

TEST(LuaTest, TestTableDup2)
{
    RunSimpleLuaTest("luatests/table_dup2.lua", LuaTestOption::ForceInterpreter);
}

TEST(LuaTestForceBaselineJit, TestTableDup2)
{
    RunSimpleLuaTest("luatests/table_dup2.lua", LuaTestOption::ForceBaselineJit);
}

TEST(LuaTest, TestTableDup3)
{
    RunSimpleLuaTest("luatests/table_dup3.lua", LuaTestOption::ForceInterpreter);
}

TEST(LuaTestForceBaselineJit, TestTableDup3)
{
    RunSimpleLuaTest("luatests/table_dup3.lua", LuaTestOption::ForceBaselineJit);
}

static void LuaTest_TestTableSizeHint_Impl(LuaTestOption testOption)
{
    VM* vm = VM::Create();
    Auto(vm->Destroy());
    vm->SetEngineStartingTier(GetVMEngineStartingTierFromEngineTestOption(testOption));
    VMOutputInterceptor vmoutput(vm);

    std::unique_ptr<ScriptModule> module = ParseLuaScriptOrFail("luatests/table_size_hint.lua", testOption);
    vm->LaunchScript(module.get());

    std::string out = vmoutput.GetAndResetStdOut();
    std::string err = vmoutput.GetAndResetStdErr();
    AssertIsExpectedOutput(out);
    ReleaseAssert(err == "");

    {
        TValue t = GetGlobalVariable(vm, "t");
        ReleaseAssert(t.IsPointer() && t.AsPointer<UserHeapGcObjectHeader>().As()->m_type == HeapEntityType::Table);
        TableObject* obj = AssertAndGetTableObject(t);
        Structure* structure = AssertAndGetStructure(obj);
        ReleaseAssert(structure->m_inlineNamedStorageCapacity == internal::x_optimalInlineCapacityArray[4]);
    }

    {
        TValue t = GetGlobalVariable(vm, "t2");
        ReleaseAssert(t.IsPointer() && t.AsPointer<UserHeapGcObjectHeader>().As()->m_type == HeapEntityType::Table);
        TableObject* obj = AssertAndGetTableObject(t);
        Structure* structure = AssertAndGetStructure(obj);
        ReleaseAssert(structure->m_inlineNamedStorageCapacity == internal::x_optimalInlineCapacityArray[3]);
    }
}

TEST(LuaTest, TestTableSizeHint)
{
    LuaTest_TestTableSizeHint_Impl(LuaTestOption::ForceInterpreter);
}

TEST(LuaTestForceBaselineJit, TestTableSizeHint)
{
    LuaTest_TestTableSizeHint_Impl(LuaTestOption::ForceBaselineJit);
}

TEST(LuaTest, Upvalue)
{
    RunSimpleLuaTest("luatests/upvalue.lua", LuaTestOption::ForceInterpreter);
}

TEST(LuaTestForceBaselineJit, Upvalue)
{
    RunSimpleLuaTest("luatests/upvalue.lua", LuaTestOption::ForceBaselineJit);
}

TEST(LuaTest, Fib_upvalue)
{
    RunSimpleLuaTest("luatests/fib_upvalue.lua", LuaTestOption::ForceInterpreter);
}

TEST(LuaTestForceBaselineJit, Fib_upvalue)
{
    RunSimpleLuaTest("luatests/fib_upvalue.lua", LuaTestOption::ForceBaselineJit);
}

TEST(LuaTest, LinearSieve)
{
    RunSimpleLuaTest("luatests/linear_sieve.lua", LuaTestOption::ForceInterpreter);
}

TEST(LuaTestForceBaselineJit, LinearSieve)
{
    RunSimpleLuaTest("luatests/linear_sieve.lua", LuaTestOption::ForceBaselineJit);
}

TEST(LuaTest, NaNEdgeCase)
{
    RunSimpleLuaTest("luatests/nan_edge_case.lua", LuaTestOption::ForceInterpreter);
}

TEST(LuaTestForceBaselineJit, NaNEdgeCase)
{
    RunSimpleLuaTest("luatests/nan_edge_case.lua", LuaTestOption::ForceBaselineJit);
}

TEST(LuaTest, ForLoopCoercion)
{
    RunSimpleLuaTest("luatests/for_loop_coercion.lua", LuaTestOption::ForceInterpreter);
}

TEST(LuaTestForceBaselineJit, ForLoopCoercion)
{
    RunSimpleLuaTest("luatests/for_loop_coercion.lua", LuaTestOption::ForceBaselineJit);
}

TEST(LuaTest, ForLoopEdgeCases)
{
    RunSimpleLuaTest("luatests/for_loop_edge_cases.lua", LuaTestOption::ForceInterpreter);
}

TEST(LuaTestForceBaselineJit, ForLoopEdgeCases)
{
    RunSimpleLuaTest("luatests/for_loop_edge_cases.lua", LuaTestOption::ForceBaselineJit);
}

TEST(LuaTest, PrimitiveConstants)
{
    RunSimpleLuaTest("luatests/primitive_constant.lua", LuaTestOption::ForceInterpreter);
}

TEST(LuaTestForceBaselineJit, PrimitiveConstants)
{
    RunSimpleLuaTest("luatests/primitive_constant.lua", LuaTestOption::ForceBaselineJit);
}

TEST(LuaTest, LogicalOpSanity)
{
    RunSimpleLuaTest("luatests/logical_op_sanity.lua", LuaTestOption::ForceInterpreter);
}

TEST(LuaTestForceBaselineJit, LogicalOpSanity)
{
    RunSimpleLuaTest("luatests/logical_op_sanity.lua", LuaTestOption::ForceBaselineJit);
}

TEST(LuaTest, PositiveAndNegativeInf)
{
    RunSimpleLuaTest("luatests/pos_and_neg_inf.lua", LuaTestOption::ForceInterpreter);
}

TEST(LuaTestForceBaselineJit, PositiveAndNegativeInf)
{
    RunSimpleLuaTest("luatests/pos_and_neg_inf.lua", LuaTestOption::ForceBaselineJit);
}

TEST(LuaTest, LogicalNot)
{
    RunSimpleLuaTest("luatests/logical_not.lua", LuaTestOption::ForceInterpreter);
}

TEST(LuaTestForceBaselineJit, LogicalNot)
{
    RunSimpleLuaTest("luatests/logical_not.lua", LuaTestOption::ForceBaselineJit);
}

TEST(LuaTest, LengthOperator)
{
    RunSimpleLuaTest("luatests/length_operator.lua", LuaTestOption::ForceInterpreter);
}

TEST(LuaTestForceBaselineJit, LengthOperator)
{
    RunSimpleLuaTest("luatests/length_operator.lua", LuaTestOption::ForceBaselineJit);
}

static void LuaTest_TailCall_Impl(LuaTestOption testOption)
{
    VM* vm = VM::Create();
    Auto(vm->Destroy());
    vm->SetEngineStartingTier(GetVMEngineStartingTierFromEngineTestOption(testOption));
    VMOutputInterceptor vmoutput(vm);

    std::unique_ptr<ScriptModule> module = ParseLuaScriptOrFail("luatests/tail_call.lua", testOption);

    // Manually lower the stack size
    //
    CoroutineRuntimeContext* rc = vm->GetRootCoroutine();
    rc->m_stackBegin = new TValue[200];

    vm->LaunchScript(module.get());

    std::string out = vmoutput.GetAndResetStdOut();
    std::string err = vmoutput.GetAndResetStdErr();
    AssertIsExpectedOutput(out);
    ReleaseAssert(err == "");
}

TEST(LuaTest, TailCall)
{
    LuaTest_TailCall_Impl(LuaTestOption::ForceInterpreter);
}

TEST(LuaTestForceBaselineJit, TailCall)
{
    LuaTest_TailCall_Impl(LuaTestOption::ForceBaselineJit);
}

static void LuaTest_VariadicTailCall_1_Impl(LuaTestOption testOption)
{
    VM* vm = VM::Create();
    Auto(vm->Destroy());
    vm->SetEngineStartingTier(GetVMEngineStartingTierFromEngineTestOption(testOption));
    VMOutputInterceptor vmoutput(vm);

    std::unique_ptr<ScriptModule> module = ParseLuaScriptOrFail("luatests/variadic_tail_call_1.lua", testOption);

    // Manually lower the stack size
    //
    CoroutineRuntimeContext* rc = vm->GetRootCoroutine();
    rc->m_stackBegin = new TValue[200];

    vm->LaunchScript(module.get());

    std::string out = vmoutput.GetAndResetStdOut();
    std::string err = vmoutput.GetAndResetStdErr();
    AssertIsExpectedOutput(out);
    ReleaseAssert(err == "");
}

TEST(LuaTest, VariadicTailCall_1)
{
    LuaTest_VariadicTailCall_1_Impl(LuaTestOption::ForceInterpreter);
}

TEST(LuaTestForceBaselineJit, VariadicTailCall_1)
{
    LuaTest_VariadicTailCall_1_Impl(LuaTestOption::ForceBaselineJit);
}

static void LuaTest_VariadicTailCall_2_Impl(LuaTestOption testOption)
{
    VM* vm = VM::Create();
    Auto(vm->Destroy());
    vm->SetEngineStartingTier(GetVMEngineStartingTierFromEngineTestOption(testOption));
    VMOutputInterceptor vmoutput(vm);

    std::unique_ptr<ScriptModule> module = ParseLuaScriptOrFail("luatests/variadic_tail_call_2.lua", testOption);

    // Manually lower the stack size
    //
    CoroutineRuntimeContext* rc = vm->GetRootCoroutine();
    rc->m_stackBegin = new TValue[200];

    vm->LaunchScript(module.get());

    std::string out = vmoutput.GetAndResetStdOut();
    std::string err = vmoutput.GetAndResetStdErr();
    AssertIsExpectedOutput(out);
    ReleaseAssert(err == "");
}

TEST(LuaTest, VariadicTailCall_2)
{
    LuaTest_VariadicTailCall_2_Impl(LuaTestOption::ForceInterpreter);
}

TEST(LuaTestForceBaselineJit, VariadicTailCall_2)
{
    LuaTest_VariadicTailCall_2_Impl(LuaTestOption::ForceBaselineJit);
}

static void LuaTest_VariadicTailCall_3_Impl(LuaTestOption testOption)
{
    VM* vm = VM::Create();
    Auto(vm->Destroy());
    vm->SetEngineStartingTier(GetVMEngineStartingTierFromEngineTestOption(testOption));
    VMOutputInterceptor vmoutput(vm);

    std::unique_ptr<ScriptModule> module = ParseLuaScriptOrFail("luatests/variadic_tail_call_3.lua", testOption);

    // Manually lower the stack size
    //
    CoroutineRuntimeContext* rc = vm->GetRootCoroutine();
    rc->m_stackBegin = new TValue[200];

    vm->LaunchScript(module.get());

    std::string out = vmoutput.GetAndResetStdOut();
    std::string err = vmoutput.GetAndResetStdErr();
    AssertIsExpectedOutput(out);
    ReleaseAssert(err == "");
}

TEST(LuaTest, VariadicTailCall_3)
{
    LuaTest_VariadicTailCall_3_Impl(LuaTestOption::ForceInterpreter);
}

TEST(LuaTestForceBaselineJit, VariadicTailCall_3)
{
    LuaTest_VariadicTailCall_3_Impl(LuaTestOption::ForceBaselineJit);
}

TEST(LuaTest, OpcodeKNIL)
{
    RunSimpleLuaTest("luatests/test_knil.lua", LuaTestOption::ForceInterpreter);
}

TEST(LuaTestForceBaselineJit, OpcodeKNIL)
{
    RunSimpleLuaTest("luatests/test_knil.lua", LuaTestOption::ForceBaselineJit);
}

TEST(LuaTest, IterativeForLoop)
{
    RunSimpleLuaTest("luatests/iter_for.lua", LuaTestOption::ForceInterpreter);
}

TEST(LuaTestForceBaselineJit, IterativeForLoop)
{
    RunSimpleLuaTest("luatests/iter_for.lua", LuaTestOption::ForceBaselineJit);
}

TEST(LuaTest, NegativeZeroAsIndex)
{
    RunSimpleLuaTest("luatests/negative_zero_as_index.lua", LuaTestOption::ForceInterpreter);
}

TEST(LuaTestForceBaselineJit, NegativeZeroAsIndex)
{
    RunSimpleLuaTest("luatests/negative_zero_as_index.lua", LuaTestOption::ForceBaselineJit);
}

// We have a few different tests by slightly changing the Lua source code, but expects the same output with insensitive order
// This function checks for that specific output..
//
static void CheckForPairsThreeTestOutput(std::stringstream& ss)
{
    std::string line;
    ReleaseAssert(std::getline(ss, line));
    ReleaseAssert(line == "-- test 1 --");

    std::set<std::string> expectedAnswerForTest1;
    expectedAnswerForTest1.insert("1\t1");
    expectedAnswerForTest1.insert("2\t3");
    expectedAnswerForTest1.insert("a\t1");
    expectedAnswerForTest1.insert("3\t5.6");
    expectedAnswerForTest1.insert("c\t1.23");
    expectedAnswerForTest1.insert("b\tx");

    std::set<std::string> expectedAnswerForTest2;
    expectedAnswerForTest2.insert("1\t1");
    expectedAnswerForTest2.insert("2\t3");
    expectedAnswerForTest2.insert("3\t5.6");
    expectedAnswerForTest2.insert("4\t7");
    expectedAnswerForTest2.insert("0\tz");
    expectedAnswerForTest2.insert("c\t1.23");
    expectedAnswerForTest2.insert("b\tx");
    expectedAnswerForTest2.insert("2.5\t234");
    expectedAnswerForTest2.insert("a\t1");

    std::set<std::string> expectedAnswerForTest3;
    expectedAnswerForTest3.insert("1	1");
    expectedAnswerForTest3.insert("2	3");
    expectedAnswerForTest3.insert("3	5.6");
    expectedAnswerForTest3.insert("4	7");
    expectedAnswerForTest3.insert("5	105");
    expectedAnswerForTest3.insert("6	106");
    expectedAnswerForTest3.insert("7	107");
    expectedAnswerForTest3.insert("8	108");
    expectedAnswerForTest3.insert("9	109");
    expectedAnswerForTest3.insert("10	110");
    expectedAnswerForTest3.insert("11	111");
    expectedAnswerForTest3.insert("12	112");
    expectedAnswerForTest3.insert("13	113");
    expectedAnswerForTest3.insert("14	114");
    expectedAnswerForTest3.insert("15	115");
    expectedAnswerForTest3.insert("16	116");
    expectedAnswerForTest3.insert("17	117");
    expectedAnswerForTest3.insert("18	118");
    expectedAnswerForTest3.insert("19	119");
    expectedAnswerForTest3.insert("20	120");
    expectedAnswerForTest3.insert("a	1");
    expectedAnswerForTest3.insert("1000000	8.9");
    expectedAnswerForTest3.insert("2.5	234");
    expectedAnswerForTest3.insert("b	x");
    expectedAnswerForTest3.insert("0	z");
    expectedAnswerForTest3.insert("c	1.23");

    while (true)
    {
        ReleaseAssert(std::getline(ss, line));
        if (line == "-- test 2 --")
        {
            break;
        }
        ReleaseAssert(expectedAnswerForTest1.count(line));
        expectedAnswerForTest1.erase(expectedAnswerForTest1.find(line));
    }
    ReleaseAssert(expectedAnswerForTest1.size() == 0);

    while (true)
    {
        ReleaseAssert(std::getline(ss, line));
        if (line == "-- test 3 --")
        {
            break;
        }
        ReleaseAssert(expectedAnswerForTest2.count(line));
        expectedAnswerForTest2.erase(expectedAnswerForTest2.find(line));
    }
    ReleaseAssert(expectedAnswerForTest2.size() == 0);

    while (std::getline(ss, line))
    {
        ReleaseAssert(expectedAnswerForTest3.count(line));
        expectedAnswerForTest3.erase(expectedAnswerForTest3.find(line));
    }
    ReleaseAssert(expectedAnswerForTest3.size() == 0);
}

static void LuaTest_ForPairs_Impl(LuaTestOption testOption)
{
    VM* vm = VM::Create();
    Auto(vm->Destroy());
    vm->SetEngineStartingTier(GetVMEngineStartingTierFromEngineTestOption(testOption));
    VMOutputInterceptor vmoutput(vm);

    std::unique_ptr<ScriptModule> module = ParseLuaScriptOrFail("luatests/for_pairs.lua", testOption);
    vm->LaunchScript(module.get());

    std::string out = vmoutput.GetAndResetStdOut();
    std::string err = vmoutput.GetAndResetStdErr();

    std::stringstream ss(out);
    CheckForPairsThreeTestOutput(ss);

    ReleaseAssert(err == "");
}

TEST(LuaTest, ForPairs)
{
    LuaTest_ForPairs_Impl(LuaTestOption::ForceInterpreter);
}

TEST(LuaTestForceBaselineJit, ForPairs)
{
    LuaTest_ForPairs_Impl(LuaTestOption::ForceBaselineJit);
}

static void LuaTest_ForPairsPoisonNext_Impl(LuaTestOption testOption)
{
    VM* vm = VM::Create();
    Auto(vm->Destroy());
    vm->SetEngineStartingTier(GetVMEngineStartingTierFromEngineTestOption(testOption));
    VMOutputInterceptor vmoutput(vm);

    std::unique_ptr<ScriptModule> module = ParseLuaScriptOrFail("luatests/for_pairs_poison_next.lua", testOption);
    vm->LaunchScript(module.get());

    std::string out = vmoutput.GetAndResetStdOut();
    std::string err = vmoutput.GetAndResetStdErr();

    std::stringstream ss(out);
    std::string line;
    ReleaseAssert(std::getline(ss, line));
    ReleaseAssert(line == "0");
    CheckForPairsThreeTestOutput(ss);

    ReleaseAssert(err == "");
}

TEST(LuaTest, ForPairsPoisonNext)
{
    LuaTest_ForPairsPoisonNext_Impl(LuaTestOption::ForceInterpreter);
}

TEST(LuaTestForceBaselineJit, ForPairsPoisonNext)
{
    LuaTest_ForPairsPoisonNext_Impl(LuaTestOption::ForceBaselineJit);
}

TEST(LuaTest, ForPairsPoisonPairs)
{
    RunSimpleLuaTest("luatests/for_pairs_poison_pairs.lua", LuaTestOption::ForceInterpreter);
}

TEST(LuaTestForceBaselineJit, ForPairsPoisonPairs)
{
    RunSimpleLuaTest("luatests/for_pairs_poison_pairs.lua", LuaTestOption::ForceBaselineJit);
}

TEST(LuaTest, ForPairsEmpty)
{
    RunSimpleLuaTest("luatests/for_pairs_empty.lua", LuaTestOption::ForceInterpreter);
}

TEST(LuaTestForceBaselineJit, ForPairsEmpty)
{
    RunSimpleLuaTest("luatests/for_pairs_empty.lua", LuaTestOption::ForceBaselineJit);
}

static void LuaTest_ForPairsSlowNext_Impl(LuaTestOption testOption)
{
    VM* vm = VM::Create();
    Auto(vm->Destroy());
    vm->SetEngineStartingTier(GetVMEngineStartingTierFromEngineTestOption(testOption));
    VMOutputInterceptor vmoutput(vm);

    std::unique_ptr<ScriptModule> module = ParseLuaScriptOrFail("luatests/for_pairs_slow_next.lua", testOption);
    vm->LaunchScript(module.get());

    std::string out = vmoutput.GetAndResetStdOut();
    std::string err = vmoutput.GetAndResetStdErr();

    std::stringstream ss(out);
    CheckForPairsThreeTestOutput(ss);

    ReleaseAssert(err == "");
}

TEST(LuaTest, ForPairsSlowNext)
{
    LuaTest_ForPairsSlowNext_Impl(LuaTestOption::ForceInterpreter);
}

TEST(LuaTestForceBaselineJit, ForPairsSlowNext)
{
    LuaTest_ForPairsSlowNext_Impl(LuaTestOption::ForceBaselineJit);
}

static void LuaTest_BooleanAsTableIndex_1(LuaTestOption testOption)
{
    VM* vm = VM::Create();
    Auto(vm->Destroy());
    vm->SetEngineStartingTier(GetVMEngineStartingTierFromEngineTestOption(testOption));
    VMOutputInterceptor vmoutput(vm);

    std::unique_ptr<ScriptModule> module = ParseLuaScriptOrFail("luatests/boolean_as_table_index_1.lua", testOption);
    vm->LaunchScript(module.get());

    std::string out = vmoutput.GetAndResetStdOut();
    std::string err = vmoutput.GetAndResetStdErr();

    std::stringstream ss(out);
    std::set<std::string> expectedAnswer {
        "1\t2",
        "2\t3",
        "a\t1",
        "true\t5",
        "c\t4",
        "b\t2",
        "d\t6",
        "0\t4",
        "false\t3"
    };

    std::string line;
    while (std::getline(ss, line))
    {
        ReleaseAssert(expectedAnswer.count(line));
        expectedAnswer.erase(expectedAnswer.find(line));
    }
    ReleaseAssert(expectedAnswer.size() == 0);

    ReleaseAssert(err == "");
}

TEST(LuaTest, BooleanAsTableIndex_1)
{
    LuaTest_BooleanAsTableIndex_1(LuaTestOption::ForceInterpreter);
}

TEST(LuaTestForceBaselineJit, BooleanAsTableIndex_1)
{
    LuaTest_BooleanAsTableIndex_1(LuaTestOption::ForceBaselineJit);
}

static void LuaTest_BooleanAsTableIndex_2(LuaTestOption testOption)
{
    VM* vm = VM::Create();
    Auto(vm->Destroy());
    vm->SetEngineStartingTier(GetVMEngineStartingTierFromEngineTestOption(testOption));
    VMOutputInterceptor vmoutput(vm);

    std::unique_ptr<ScriptModule> module = ParseLuaScriptOrFail("luatests/boolean_as_table_index_2.lua", testOption);
    vm->LaunchScript(module.get());

    std::string out = vmoutput.GetAndResetStdOut();
    std::string err = vmoutput.GetAndResetStdErr();

    std::stringstream ss(out);
    std::set<std::string> expectedAnswer {
        "1\t2",
        "2\t3",
        "a\t1",
        "true\t5",
        "c\t4",
        "b\t2",
        "d\t6",
        "0\t4",
        "false\t3"
    };

    std::string line;
    while (std::getline(ss, line))
    {
        ReleaseAssert(expectedAnswer.count(line));
        expectedAnswer.erase(expectedAnswer.find(line));
    }
    ReleaseAssert(expectedAnswer.size() == 0);

    ReleaseAssert(err == "");
}

TEST(LuaTest, BooleanAsTableIndex_2)
{
    LuaTest_BooleanAsTableIndex_2(LuaTestOption::ForceInterpreter);
}

TEST(LuaTestForceBaselineJit, BooleanAsTableIndex_2)
{
    LuaTest_BooleanAsTableIndex_2(LuaTestOption::ForceBaselineJit);
}

TEST(LuaTest, BooleanAsTableIndex_3)
{
    RunSimpleLuaTest("luatests/boolean_as_table_index_3.lua", LuaTestOption::ForceInterpreter);
}

TEST(LuaTestForceBaselineJit, BooleanAsTableIndex_3)
{
    RunSimpleLuaTest("luatests/boolean_as_table_index_3.lua", LuaTestOption::ForceBaselineJit);
}

TEST(LuaTest, ArithmeticSanity)
{
    RunSimpleLuaTest("luatests/arithmetic_sanity.lua", LuaTestOption::ForceInterpreter);
}

TEST(LuaTestForceBaselineJit, ArithmeticSanity)
{
    RunSimpleLuaTest("luatests/arithmetic_sanity.lua", LuaTestOption::ForceBaselineJit);
}

TEST(LuaTest, StringConcat)
{
    RunSimpleLuaTest("luatests/string_concat.lua", LuaTestOption::ForceInterpreter);
}

TEST(LuaTestForceBaselineJit, StringConcat)
{
    RunSimpleLuaTest("luatests/string_concat.lua", LuaTestOption::ForceBaselineJit);
}

TEST(LuaTest, TableVariadicPut)
{
    RunSimpleLuaTest("luatests/table_variadic_put.lua", LuaTestOption::ForceInterpreter);
}

TEST(LuaTestForceBaselineJit, TableVariadicPut)
{
    RunSimpleLuaTest("luatests/table_variadic_put.lua", LuaTestOption::ForceBaselineJit);
}

TEST(LuaTest, TableVariadicPut_2)
{
    RunSimpleLuaTest("luatests/table_variadic_put_2.lua", LuaTestOption::ForceInterpreter);
}

TEST(LuaTestForceBaselineJit, TableVariadicPut_2)
{
    RunSimpleLuaTest("luatests/table_variadic_put_2.lua", LuaTestOption::ForceBaselineJit);
}

TEST(LuaBenchmark, NBody)
{
    RunSimpleLuaTest("luatests/n-body.lua", LuaTestOption::ForceInterpreter);
}

TEST(LuaBenchmarkForceBaselineJit, NBody)
{
    RunSimpleLuaTest("luatests/n-body.lua", LuaTestOption::ForceBaselineJit);
}

static void LuaBenchmark_Ack_Impl(LuaTestOption testOption)
{
    VM* vm = VM::Create();
    Auto(vm->Destroy());
    vm->SetEngineStartingTier(GetVMEngineStartingTierFromEngineTestOption(testOption));
    VMOutputInterceptor vmoutput(vm);

    std::unique_ptr<ScriptModule> module = ParseLuaScriptOrFail("luatests/ack.lua", testOption);

    // This benchmark needs a larger stack
    //
    CoroutineRuntimeContext* rc = vm->GetRootCoroutine();
    rc->m_stackBegin = new TValue[1000000];

    vm->LaunchScript(module.get());

    std::string out = vmoutput.GetAndResetStdOut();
    std::string err = vmoutput.GetAndResetStdErr();
    AssertIsExpectedOutput(out);
    ReleaseAssert(err == "");
}

TEST(LuaBenchmark, Ack)
{
    LuaBenchmark_Ack_Impl(LuaTestOption::ForceInterpreter);
}

TEST(LuaBenchmarkForceBaselineJit, Ack)
{
    LuaBenchmark_Ack_Impl(LuaTestOption::ForceBaselineJit);
}

TEST(LuaBenchmark, BinaryTrees_1)
{
    RunSimpleLuaTest("luatests/binary-trees-1.lua", LuaTestOption::ForceInterpreter);
}

TEST(LuaBenchmarkForceBaselineJit, BinaryTrees_1)
{
    RunSimpleLuaTest("luatests/binary-trees-1.lua", LuaTestOption::ForceBaselineJit);
}

TEST(LuaBenchmark, BinaryTrees_2)
{
    RunSimpleLuaTest("luatests/binary-trees-2.lua", LuaTestOption::ForceInterpreter);
}

TEST(LuaBenchmarkForceBaselineJit, BinaryTrees_2)
{
    RunSimpleLuaTest("luatests/binary-trees-2.lua", LuaTestOption::ForceBaselineJit);
}

TEST(LuaBenchmark, Fannkuch_Redux)
{
    RunSimpleLuaTest("luatests/fannkuch-redux.lua", LuaTestOption::ForceInterpreter);
}

TEST(LuaBenchmarkForceBaselineJit, Fannkuch_Redux)
{
    RunSimpleLuaTest("luatests/fannkuch-redux.lua", LuaTestOption::ForceBaselineJit);
}

TEST(LuaBenchmark, Fixpoint_Fact)
{
    RunSimpleLuaTest("luatests/fixpoint-fact.lua", LuaTestOption::ForceInterpreter);
}

TEST(LuaBenchmarkForceBaselineJit, Fixpoint_Fact)
{
    RunSimpleLuaTest("luatests/fixpoint-fact.lua", LuaTestOption::ForceBaselineJit);
}

TEST(LuaBenchmark, Mandel_NoMetatable)
{
    RunSimpleLuaTest("luatests/mandel-nometatable.lua", LuaTestOption::ForceInterpreter);
}

TEST(LuaBenchmarkForceBaselineJit, Mandel_NoMetatable)
{
    RunSimpleLuaTest("luatests/mandel-nometatable.lua", LuaTestOption::ForceBaselineJit);
}

TEST(LuaBenchmark, Mandel)
{
    RunSimpleLuaTest("luatests/mandel.lua", LuaTestOption::ForceInterpreter);
}

TEST(LuaBenchmarkForceBaselineJit, Mandel)
{
    RunSimpleLuaTest("luatests/mandel.lua", LuaTestOption::ForceBaselineJit);
}

TEST(LuaBenchmark, QuadTree)
{
    RunSimpleLuaTest("luatests/qt.lua", LuaTestOption::ForceInterpreter);
}

TEST(LuaBenchmarkForceBaselineJit, QuadTree)
{
    RunSimpleLuaTest("luatests/qt.lua", LuaTestOption::ForceBaselineJit);
}

TEST(LuaBenchmark, Queen)
{
    RunSimpleLuaTest("luatests/queen.lua", LuaTestOption::ForceInterpreter);
}

TEST(LuaBenchmarkForceBaselineJit, Queen)
{
    RunSimpleLuaTest("luatests/queen.lua", LuaTestOption::ForceBaselineJit);
}

TEST(LuaBenchmark, NlgN_Sieve)
{
    RunSimpleLuaTest("luatests/nlgn_sieve.lua", LuaTestOption::ForceInterpreter);
}

TEST(LuaBenchmarkForceBaselineJit, NlgN_Sieve)
{
    RunSimpleLuaTest("luatests/nlgn_sieve.lua", LuaTestOption::ForceBaselineJit);
}

TEST(LuaBenchmark, Spectral_Norm)
{
    RunSimpleLuaTest("luatests/spectral-norm.lua", LuaTestOption::ForceInterpreter);
}

TEST(LuaBenchmarkForceBaselineJit, Spectral_Norm)
{
    RunSimpleLuaTest("luatests/spectral-norm.lua", LuaTestOption::ForceBaselineJit);
}

TEST(LuaBenchmark, chameneos)
{
    RunSimpleLuaTest("luatests/chameneos.lua", LuaTestOption::ForceInterpreter);
}

TEST(LuaBenchmarkForceBaselineJit, chameneos)
{
    RunSimpleLuaTest("luatests/chameneos.lua", LuaTestOption::ForceBaselineJit);
}

TEST(LuaTest, xpcall_1)
{
    RunSimpleLuaTest("luatests/xpcall_1.lua", LuaTestOption::ForceInterpreter);
}

TEST(LuaTestForceBaselineJit, xpcall_1)
{
    RunSimpleLuaTest("luatests/xpcall_1.lua", LuaTestOption::ForceBaselineJit);
}

TEST(LuaTest, xpcall_2)
{
    RunSimpleLuaTest("luatests/xpcall_2.lua", LuaTestOption::ForceInterpreter);
}

TEST(LuaTestForceBaselineJit, xpcall_2)
{
    RunSimpleLuaTest("luatests/xpcall_2.lua", LuaTestOption::ForceBaselineJit);
}

TEST(LuaTest, xpcall_3)
{
    RunSimpleLuaTest("luatests/xpcall_3.lua", LuaTestOption::ForceInterpreter);
}

TEST(LuaTestForceBaselineJit, xpcall_3)
{
    RunSimpleLuaTest("luatests/xpcall_3.lua", LuaTestOption::ForceBaselineJit);
}

TEST(LuaTest, xpcall_4)
{
    RunSimpleLuaTest("luatests/xpcall_4.lua", LuaTestOption::ForceInterpreter);
}

TEST(LuaTestForceBaselineJit, xpcall_4)
{
    RunSimpleLuaTest("luatests/xpcall_4.lua", LuaTestOption::ForceBaselineJit);
}

TEST(LuaTest, xpcall_5)
{
    RunSimpleLuaTest("luatests/xpcall_5.lua", LuaTestOption::ForceInterpreter);
}

TEST(LuaTestForceBaselineJit, xpcall_5)
{
    RunSimpleLuaTest("luatests/xpcall_5.lua", LuaTestOption::ForceBaselineJit);
}

TEST(LuaTest, xpcall_6)
{
    RunSimpleLuaTest("luatests/xpcall_6.lua", LuaTestOption::ForceInterpreter);
}

TEST(LuaTestForceBaselineJit, xpcall_6)
{
    RunSimpleLuaTest("luatests/xpcall_6.lua", LuaTestOption::ForceBaselineJit);
}

TEST(LuaTest, pcall_1)
{
    RunSimpleLuaTest("luatests/pcall_1.lua", LuaTestOption::ForceInterpreter);
}

TEST(LuaTestForceBaselineJit, pcall_1)
{
    RunSimpleLuaTest("luatests/pcall_1.lua", LuaTestOption::ForceBaselineJit);
}

TEST(LuaTest, pcall_2)
{
    RunSimpleLuaTest("luatests/pcall_2.lua", LuaTestOption::ForceInterpreter);
}

TEST(LuaTestForceBaselineJit, pcall_2)
{
    RunSimpleLuaTest("luatests/pcall_2.lua", LuaTestOption::ForceBaselineJit);
}

TEST(LuaTest, GetSetMetatable)
{
    RunSimpleLuaTest("luatests/get_set_metatable.lua", LuaTestOption::ForceInterpreter);
}

TEST(LuaTestForceBaselineJit, GetSetMetatable)
{
    RunSimpleLuaTest("luatests/get_set_metatable.lua", LuaTestOption::ForceBaselineJit);
}

TEST(LuaTest, getsetmetatable_2)
{
    RunSimpleLuaTest("luatests/getsetmetatable_2.lua", LuaTestOption::ForceInterpreter);
}

TEST(LuaTestForceBaselineJit, getsetmetatable_2)
{
    RunSimpleLuaTest("luatests/getsetmetatable_2.lua", LuaTestOption::ForceBaselineJit);
}

TEST(LuaTest, metatable_call_1)
{
    RunSimpleLuaTest("luatests/metatable_call_1.lua", LuaTestOption::ForceInterpreter);
}

TEST(LuaTestForceBaselineJit, metatable_call_1)
{
    RunSimpleLuaTest("luatests/metatable_call_1.lua", LuaTestOption::ForceBaselineJit);
}

TEST(LuaTest, metatable_call_2)
{
    RunSimpleLuaTest("luatests/metatable_call_2.lua", LuaTestOption::ForceInterpreter);
}

TEST(LuaTestForceBaselineJit, metatable_call_2)
{
    RunSimpleLuaTest("luatests/metatable_call_2.lua", LuaTestOption::ForceBaselineJit);
}

TEST(LuaTest, metatable_call_3)
{
    RunSimpleLuaTest("luatests/metatable_call_3.lua", LuaTestOption::ForceInterpreter);
}

TEST(LuaTestForceBaselineJit, metatable_call_3)
{
    RunSimpleLuaTest("luatests/metatable_call_3.lua", LuaTestOption::ForceBaselineJit);
}

TEST(LuaTest, metatable_call_4)
{
    RunSimpleLuaTest("luatests/metatable_call_4.lua", LuaTestOption::ForceInterpreter);
}

TEST(LuaTestForceBaselineJit, metatable_call_4)
{
    RunSimpleLuaTest("luatests/metatable_call_4.lua", LuaTestOption::ForceBaselineJit);
}

TEST(LuaTest, metatable_call_5)
{
    RunSimpleLuaTest("luatests/metatable_call_5.lua", LuaTestOption::ForceInterpreter);
}

TEST(LuaTestForceBaselineJit, metatable_call_5)
{
    RunSimpleLuaTest("luatests/metatable_call_5.lua", LuaTestOption::ForceBaselineJit);
}

TEST(LuaTest, xpcall_metatable)
{
    RunSimpleLuaTest("luatests/xpcall_metatable.lua", LuaTestOption::ForceInterpreter);
}

TEST(LuaTestForceBaselineJit, xpcall_metatable)
{
    RunSimpleLuaTest("luatests/xpcall_metatable.lua", LuaTestOption::ForceBaselineJit);
}

TEST(LuaTest, pcall_metatable)
{
    RunSimpleLuaTest("luatests/pcall_metatable.lua", LuaTestOption::ForceInterpreter);
}

TEST(LuaTestForceBaselineJit, pcall_metatable)
{
    RunSimpleLuaTest("luatests/pcall_metatable.lua", LuaTestOption::ForceBaselineJit);
}

TEST(LuaTest, metatable_add_1)
{
    RunSimpleLuaTest("luatests/metatable_add_1.lua", LuaTestOption::ForceInterpreter);
}

TEST(LuaTestForceBaselineJit, metatable_add_1)
{
    RunSimpleLuaTest("luatests/metatable_add_1.lua", LuaTestOption::ForceBaselineJit);
}

TEST(LuaTest, metatable_add_2)
{
    RunSimpleLuaTest("luatests/metatable_add_2.lua", LuaTestOption::ForceInterpreter);
}

TEST(LuaTestForceBaselineJit, metatable_add_2)
{
    RunSimpleLuaTest("luatests/metatable_add_2.lua", LuaTestOption::ForceBaselineJit);
}

TEST(LuaTest, metatable_add_3)
{
    RunSimpleLuaTest("luatests/metatable_add_3.lua", LuaTestOption::ForceInterpreter);
}

TEST(LuaTestForceBaselineJit, metatable_add_3)
{
    RunSimpleLuaTest("luatests/metatable_add_3.lua", LuaTestOption::ForceBaselineJit);
}

TEST(LuaTest, metatable_sub)
{
    RunSimpleLuaTest("luatests/metatable_sub.lua", LuaTestOption::ForceInterpreter);
}

TEST(LuaTestForceBaselineJit, metatable_sub)
{
    RunSimpleLuaTest("luatests/metatable_sub.lua", LuaTestOption::ForceBaselineJit);
}

TEST(LuaTest, metatable_mul)
{
    RunSimpleLuaTest("luatests/metatable_mul.lua", LuaTestOption::ForceInterpreter);
}

TEST(LuaTestForceBaselineJit, metatable_mul)
{
    RunSimpleLuaTest("luatests/metatable_mul.lua", LuaTestOption::ForceBaselineJit);
}

TEST(LuaTest, metatable_div)
{
    RunSimpleLuaTest("luatests/metatable_div.lua", LuaTestOption::ForceInterpreter);
}

TEST(LuaTestForceBaselineJit, metatable_div)
{
    RunSimpleLuaTest("luatests/metatable_div.lua", LuaTestOption::ForceBaselineJit);
}

TEST(LuaTest, metatable_mod)
{
    RunSimpleLuaTest("luatests/metatable_mod.lua", LuaTestOption::ForceInterpreter);
}

TEST(LuaTestForceBaselineJit, metatable_mod)
{
    RunSimpleLuaTest("luatests/metatable_mod.lua", LuaTestOption::ForceBaselineJit);
}

TEST(LuaTest, metatable_pow)
{
    RunSimpleLuaTest("luatests/metatable_pow.lua", LuaTestOption::ForceInterpreter);
}

TEST(LuaTestForceBaselineJit, metatable_pow)
{
    RunSimpleLuaTest("luatests/metatable_pow.lua", LuaTestOption::ForceBaselineJit);
}

TEST(LuaTest, metatable_unm)
{
    RunSimpleLuaTest("luatests/metatable_unm.lua", LuaTestOption::ForceInterpreter);
}

TEST(LuaTestForceBaselineJit, metatable_unm)
{
    RunSimpleLuaTest("luatests/metatable_unm.lua", LuaTestOption::ForceBaselineJit);
}

TEST(LuaTest, metatable_len)
{
    RunSimpleLuaTest("luatests/metatable_len.lua", LuaTestOption::ForceInterpreter);
}

TEST(LuaTestForceBaselineJit, metatable_len)
{
    RunSimpleLuaTest("luatests/metatable_len.lua", LuaTestOption::ForceBaselineJit);
}

TEST(LuaTest, metatable_concat)
{
    RunSimpleLuaTest("luatests/metatable_concat.lua", LuaTestOption::ForceInterpreter);
}

TEST(LuaTestForceBaselineJit, metatable_concat)
{
    RunSimpleLuaTest("luatests/metatable_concat.lua", LuaTestOption::ForceBaselineJit);
}

TEST(LuaTest, metatable_concat_2)
{
    RunSimpleLuaTest("luatests/metatable_concat_2.lua", LuaTestOption::ForceInterpreter);
}

TEST(LuaTestForceBaselineJit, metatable_concat_2)
{
    RunSimpleLuaTest("luatests/metatable_concat_2.lua", LuaTestOption::ForceBaselineJit);
}

TEST(LuaTest, metatable_concat_3)
{
    RunSimpleLuaTest("luatests/metatable_concat_3.lua", LuaTestOption::ForceInterpreter);
}

TEST(LuaTestForceBaselineJit, metatable_concat_3)
{
    RunSimpleLuaTest("luatests/metatable_concat_3.lua", LuaTestOption::ForceBaselineJit);
}

TEST(LuaTest, metatable_eq_1)
{
    RunSimpleLuaTest("luatests/metatable_eq_1.lua", LuaTestOption::ForceInterpreter);
}

TEST(LuaTestForceBaselineJit, metatable_eq_1)
{
    RunSimpleLuaTest("luatests/metatable_eq_1.lua", LuaTestOption::ForceBaselineJit);
}

TEST(LuaTest, metatable_eq_2)
{
    RunSimpleLuaTest("luatests/metatable_eq_2.lua", LuaTestOption::ForceInterpreter);
}

TEST(LuaTestForceBaselineJit, metatable_eq_2)
{
    RunSimpleLuaTest("luatests/metatable_eq_2.lua", LuaTestOption::ForceBaselineJit);
}

TEST(LuaTest, metatable_lt)
{
    RunSimpleLuaTest("luatests/metatable_lt.lua", LuaTestOption::ForceInterpreter);
}

TEST(LuaTestForceBaselineJit, metatable_lt)
{
    RunSimpleLuaTest("luatests/metatable_lt.lua", LuaTestOption::ForceBaselineJit);
}

TEST(LuaTest, metatable_le)
{
    RunSimpleLuaTest("luatests/metatable_le.lua", LuaTestOption::ForceInterpreter);
}

TEST(LuaTestForceBaselineJit, metatable_le)
{
    RunSimpleLuaTest("luatests/metatable_le.lua", LuaTestOption::ForceBaselineJit);
}

TEST(LuaTest, metatable_eq_3)
{
    RunSimpleLuaTest("luatests/metatable_eq_3.lua", LuaTestOption::ForceInterpreter);
}

TEST(LuaTestForceBaselineJit, metatable_eq_3)
{
    RunSimpleLuaTest("luatests/metatable_eq_3.lua", LuaTestOption::ForceBaselineJit);
}

TEST(LuaTest, getbyid_metatable)
{
    RunSimpleLuaTest("luatests/getbyid_metatable.lua", LuaTestOption::ForceInterpreter);
}

TEST(LuaTestForceBaselineJit, getbyid_metatable)
{
    RunSimpleLuaTest("luatests/getbyid_metatable.lua", LuaTestOption::ForceBaselineJit);
}

TEST(LuaTest, globalget_metatable)
{
    RunSimpleLuaTest("luatests/globalget_metatable.lua", LuaTestOption::ForceInterpreter);
}

TEST(LuaTestForceBaselineJit, globalget_metatable)
{
    RunSimpleLuaTest("luatests/globalget_metatable.lua", LuaTestOption::ForceBaselineJit);
}

TEST(LuaTest, getbyval_metatable)
{
    RunSimpleLuaTest("luatests/getbyval_metatable.lua", LuaTestOption::ForceInterpreter);
}

TEST(LuaTestForceBaselineJit, getbyval_metatable)
{
    RunSimpleLuaTest("luatests/getbyval_metatable.lua", LuaTestOption::ForceBaselineJit);
}

TEST(LuaTest, getbyintegerval_metatable)
{
    RunSimpleLuaTest("luatests/getbyintegerval_metatable.lua", LuaTestOption::ForceInterpreter);
}

TEST(LuaTestForceBaselineJit, getbyintegerval_metatable)
{
    RunSimpleLuaTest("luatests/getbyintegerval_metatable.lua", LuaTestOption::ForceBaselineJit);
}

TEST(LuaTest, rawget_and_rawset)
{
    RunSimpleLuaTest("luatests/rawget_rawset.lua", LuaTestOption::ForceInterpreter);
}

TEST(LuaTestForceBaselineJit, rawget_and_rawset)
{
    RunSimpleLuaTest("luatests/rawget_rawset.lua", LuaTestOption::ForceBaselineJit);
}

TEST(LuaTest, putbyid_metatable)
{
    RunSimpleLuaTest("luatests/putbyid_metatable.lua", LuaTestOption::ForceInterpreter);
}

TEST(LuaTestForceBaselineJit, putbyid_metatable)
{
    RunSimpleLuaTest("luatests/putbyid_metatable.lua", LuaTestOption::ForceBaselineJit);
}

TEST(LuaTest, globalput_metatable)
{
    RunSimpleLuaTest("luatests/globalput_metatable.lua", LuaTestOption::ForceInterpreter);
}

TEST(LuaTestForceBaselineJit, globalput_metatable)
{
    RunSimpleLuaTest("luatests/globalput_metatable.lua", LuaTestOption::ForceBaselineJit);
}

TEST(LuaTest, putbyintegerval_metatable)
{
    RunSimpleLuaTest("luatests/putbyintegerval_metatable.lua", LuaTestOption::ForceInterpreter);
}

TEST(LuaTestForceBaselineJit, putbyintegerval_metatable)
{
    RunSimpleLuaTest("luatests/putbyintegerval_metatable.lua", LuaTestOption::ForceBaselineJit);
}

TEST(LuaTest, putbyval_metatable)
{
    RunSimpleLuaTest("luatests/putbyval_metatable.lua", LuaTestOption::ForceInterpreter);
}

TEST(LuaTestForceBaselineJit, putbyval_metatable)
{
    RunSimpleLuaTest("luatests/putbyval_metatable.lua", LuaTestOption::ForceBaselineJit);
}

TEST(LuaTest, GlobalGetInterpreterIC)
{
    RunSimpleLuaTest("luatests/globalget_interpreter_ic.lua", LuaTestOption::ForceInterpreter);
}

TEST(LuaTestForceBaselineJit, GlobalGetInterpreterIC)
{
    RunSimpleLuaTest("luatests/globalget_interpreter_ic.lua", LuaTestOption::ForceBaselineJit);
}

TEST(LuaTest, TableGetByIdInterpreterIC)
{
    RunSimpleLuaTest("luatests/table_getbyid_interpreter_ic.lua", LuaTestOption::ForceInterpreter);
}

TEST(LuaTestForceBaselineJit, TableGetByIdInterpreterIC)
{
    RunSimpleLuaTest("luatests/table_getbyid_interpreter_ic.lua", LuaTestOption::ForceBaselineJit);
}

TEST(LuaTest, GetByImmInterpreterIC_1)
{
    RunSimpleLuaTest("luatests/get_by_imm_interpreter_ic_1.lua", LuaTestOption::ForceInterpreter);
}

TEST(LuaTestForceBaselineJit, GetByImmInterpreterIC_1)
{
    RunSimpleLuaTest("luatests/get_by_imm_interpreter_ic_1.lua", LuaTestOption::ForceBaselineJit);
}

TEST(LuaTest, GetByImmInterpreterIC_2)
{
    RunSimpleLuaTest("luatests/get_by_imm_interpreter_ic_2.lua", LuaTestOption::ForceInterpreter);
}

TEST(LuaTestForceBaselineJit, GetByImmInterpreterIC_2)
{
    RunSimpleLuaTest("luatests/get_by_imm_interpreter_ic_2.lua", LuaTestOption::ForceBaselineJit);
}

TEST(LuaTest, GetByValInterpreterIC_1)
{
    RunSimpleLuaTest("luatests/get_by_val_interpreter_ic_1.lua", LuaTestOption::ForceInterpreter);
}

TEST(LuaTestForceBaselineJit, GetByValInterpreterIC_1)
{
    RunSimpleLuaTest("luatests/get_by_val_interpreter_ic_1.lua", LuaTestOption::ForceBaselineJit);
}

TEST(LuaTest, GetByValInterpreterIC_2)
{
    RunSimpleLuaTest("luatests/get_by_val_interpreter_ic_2.lua", LuaTestOption::ForceInterpreter);
}

TEST(LuaTestForceBaselineJit, GetByValInterpreterIC_2)
{
    RunSimpleLuaTest("luatests/get_by_val_interpreter_ic_2.lua", LuaTestOption::ForceBaselineJit);
}

TEST(LuaTest, GetByValInterpreterIC_3)
{
    RunSimpleLuaTest("luatests/get_by_val_interpreter_ic_3.lua", LuaTestOption::ForceInterpreter);
}

TEST(LuaTestForceBaselineJit, GetByValInterpreterIC_3)
{
    RunSimpleLuaTest("luatests/get_by_val_interpreter_ic_3.lua", LuaTestOption::ForceBaselineJit);
}

TEST(LuaTest, GetByValInterpreterIC_4)
{
    RunSimpleLuaTest("luatests/get_by_val_interpreter_ic_4.lua", LuaTestOption::ForceInterpreter);
}

TEST(LuaTestForceBaselineJit, GetByValInterpreterIC_4)
{
    RunSimpleLuaTest("luatests/get_by_val_interpreter_ic_4.lua", LuaTestOption::ForceBaselineJit);
}

TEST(LuaTest, GetByValInterpreterIC_5)
{
    RunSimpleLuaTest("luatests/get_by_val_interpreter_ic_5.lua", LuaTestOption::ForceInterpreter);
}

TEST(LuaTestForceBaselineJit, GetByValInterpreterIC_5)
{
    RunSimpleLuaTest("luatests/get_by_val_interpreter_ic_5.lua", LuaTestOption::ForceBaselineJit);
}

TEST(LuaTest, GetByValInterpreterIC_6)
{
    RunSimpleLuaTest("luatests/get_by_val_interpreter_ic_6.lua", LuaTestOption::ForceInterpreter);
}

TEST(LuaTestForceBaselineJit, GetByValInterpreterIC_6)
{
    RunSimpleLuaTest("luatests/get_by_val_interpreter_ic_6.lua", LuaTestOption::ForceBaselineJit);
}

TEST(LuaTest, GlobalPutInterpreterIC_1)
{
    RunSimpleLuaTest("luatests/globalput_interpreter_ic_1.lua", LuaTestOption::ForceInterpreter);
}

TEST(LuaTestForceBaselineJit, GlobalPutInterpreterIC_1)
{
    RunSimpleLuaTest("luatests/globalput_interpreter_ic_1.lua", LuaTestOption::ForceBaselineJit);
}

TEST(LuaTest, GlobalPutInterpreterIC_2)
{
    RunSimpleLuaTest("luatests/globalput_interpreter_ic_2.lua", LuaTestOption::ForceInterpreter);
}

TEST(LuaTestForceBaselineJit, GlobalPutInterpreterIC_2)
{
    RunSimpleLuaTest("luatests/globalput_interpreter_ic_2.lua", LuaTestOption::ForceBaselineJit);
}

TEST(LuaTest, GlobalPutInterpreterIC_3)
{
    RunSimpleLuaTest("luatests/globalput_interpreter_ic_3.lua", LuaTestOption::ForceInterpreter);
}

TEST(LuaTestForceBaselineJit, GlobalPutInterpreterIC_3)
{
    RunSimpleLuaTest("luatests/globalput_interpreter_ic_3.lua", LuaTestOption::ForceBaselineJit);
}

TEST(LuaTest, GlobalPutInterpreterIC_4)
{
    RunSimpleLuaTest("luatests/globalput_interpreter_ic_4.lua", LuaTestOption::ForceInterpreter);
}

TEST(LuaTestForceBaselineJit, GlobalPutInterpreterIC_4)
{
    RunSimpleLuaTest("luatests/globalput_interpreter_ic_4.lua", LuaTestOption::ForceBaselineJit);
}

TEST(LuaTest, PutByIdInterpreterIC_1)
{
    RunSimpleLuaTest("luatests/putbyid_interpreter_ic_1.lua", LuaTestOption::ForceInterpreter);
}

TEST(LuaTestForceBaselineJit, PutByIdInterpreterIC_1)
{
    RunSimpleLuaTest("luatests/putbyid_interpreter_ic_1.lua", LuaTestOption::ForceBaselineJit);
}

TEST(LuaTest, PutByIdInterpreterIC_2)
{
    RunSimpleLuaTest("luatests/putbyid_interpreter_ic_2.lua", LuaTestOption::ForceInterpreter);
}

TEST(LuaTestForceBaselineJit, PutByIdInterpreterIC_2)
{
    RunSimpleLuaTest("luatests/putbyid_interpreter_ic_2.lua", LuaTestOption::ForceBaselineJit);
}

TEST(LuaTest, PutByIdInterpreterIC_3)
{
    RunSimpleLuaTest("luatests/putbyid_interpreter_ic_3.lua", LuaTestOption::ForceInterpreter);
}

TEST(LuaTestForceBaselineJit, PutByIdInterpreterIC_3)
{
    RunSimpleLuaTest("luatests/putbyid_interpreter_ic_3.lua", LuaTestOption::ForceBaselineJit);
}

TEST(LuaTest, PutByIdInterpreterIC_4)
{
    RunSimpleLuaTest("luatests/putbyid_interpreter_ic_4.lua", LuaTestOption::ForceInterpreter);
}

TEST(LuaTestForceBaselineJit, PutByIdInterpreterIC_4)
{
    RunSimpleLuaTest("luatests/putbyid_interpreter_ic_4.lua", LuaTestOption::ForceBaselineJit);
}

TEST(LuaTest, PutByIdInterpreterIC_5)
{
    RunSimpleLuaTest("luatests/putbyid_interpreter_ic_5.lua", LuaTestOption::ForceInterpreter);
}

TEST(LuaTestForceBaselineJit, PutByIdInterpreterIC_5)
{
    RunSimpleLuaTest("luatests/putbyid_interpreter_ic_5.lua", LuaTestOption::ForceBaselineJit);
}

TEST(LuaTest, PutByIdInterpreterIC_6)
{
    RunSimpleLuaTest("luatests/putbyid_interpreter_ic_6.lua", LuaTestOption::ForceInterpreter);
}

TEST(LuaTestForceBaselineJit, PutByIdInterpreterIC_6)
{
    RunSimpleLuaTest("luatests/putbyid_interpreter_ic_6.lua", LuaTestOption::ForceBaselineJit);
}

TEST(LuaTest, PutByIdInterpreterIC_7)
{
    RunSimpleLuaTest("luatests/putbyid_interpreter_ic_7.lua", LuaTestOption::ForceInterpreter);
}

TEST(LuaTestForceBaselineJit, PutByIdInterpreterIC_7)
{
    RunSimpleLuaTest("luatests/putbyid_interpreter_ic_7.lua", LuaTestOption::ForceBaselineJit);
}

TEST(LuaTest, PutByIdInterpreterIC_8)
{
    RunSimpleLuaTest("luatests/putbyid_interpreter_ic_8.lua", LuaTestOption::ForceInterpreter);
}

TEST(LuaTestForceBaselineJit, PutByIdInterpreterIC_8)
{
    RunSimpleLuaTest("luatests/putbyid_interpreter_ic_8.lua", LuaTestOption::ForceBaselineJit);
}

TEST(LuaTest, PutByImmInterpreterIC_1)
{
    RunSimpleLuaTest("luatests/putbyimm_interpreter_ic_1.lua", LuaTestOption::ForceInterpreter);
}

TEST(LuaTestForceBaselineJit, PutByImmInterpreterIC_1)
{
    RunSimpleLuaTest("luatests/putbyimm_interpreter_ic_1.lua", LuaTestOption::ForceBaselineJit);
}

TEST(LuaTest, PutByImmInterpreterIC_2)
{
    RunSimpleLuaTest("luatests/putbyimm_interpreter_ic_2.lua", LuaTestOption::ForceInterpreter);
}

TEST(LuaTestForceBaselineJit, PutByImmInterpreterIC_2)
{
    RunSimpleLuaTest("luatests/putbyimm_interpreter_ic_2.lua", LuaTestOption::ForceBaselineJit);
}

TEST(LuaTest, PutByImmInterpreterIC_3)
{
    RunSimpleLuaTest("luatests/putbyimm_interpreter_ic_3.lua", LuaTestOption::ForceInterpreter);
}

TEST(LuaTestForceBaselineJit, PutByImmInterpreterIC_3)
{
    RunSimpleLuaTest("luatests/putbyimm_interpreter_ic_3.lua", LuaTestOption::ForceBaselineJit);
}

TEST(LuaTest, PutByImmInterpreterIC_4)
{
    RunSimpleLuaTest("luatests/putbyimm_interpreter_ic_4.lua", LuaTestOption::ForceInterpreter);
}

TEST(LuaTestForceBaselineJit, PutByImmInterpreterIC_4)
{
    RunSimpleLuaTest("luatests/putbyimm_interpreter_ic_4.lua", LuaTestOption::ForceBaselineJit);
}

TEST(LuaTest, PutByValInterpreterIC_1)
{
    RunSimpleLuaTest("luatests/putbyval_interpreter_ic_1.lua", LuaTestOption::ForceInterpreter);
}

TEST(LuaTestForceBaselineJit, PutByValInterpreterIC_1)
{
    RunSimpleLuaTest("luatests/putbyval_interpreter_ic_1.lua", LuaTestOption::ForceBaselineJit);
}

TEST(LuaTest, PutByValInterpreterIC_2)
{
    RunSimpleLuaTest("luatests/putbyval_interpreter_ic_2.lua", LuaTestOption::ForceInterpreter);
}

TEST(LuaTestForceBaselineJit, PutByValInterpreterIC_2)
{
    RunSimpleLuaTest("luatests/putbyval_interpreter_ic_2.lua", LuaTestOption::ForceBaselineJit);
}

TEST(LuaTest, PutByValInterpreterIC_3)
{
    RunSimpleLuaTest("luatests/putbyval_interpreter_ic_3.lua", LuaTestOption::ForceInterpreter);
}

TEST(LuaTestForceBaselineJit, PutByValInterpreterIC_3)
{
    RunSimpleLuaTest("luatests/putbyval_interpreter_ic_3.lua", LuaTestOption::ForceBaselineJit);
}

TEST(LuaTest, PutByValInterpreterIC_4)
{
    RunSimpleLuaTest("luatests/putbyval_interpreter_ic_4.lua", LuaTestOption::ForceInterpreter);
}

TEST(LuaTestForceBaselineJit, PutByValInterpreterIC_4)
{
    RunSimpleLuaTest("luatests/putbyval_interpreter_ic_4.lua", LuaTestOption::ForceBaselineJit);
}

TEST(LuaTest, PutByValInterpreterIC_5)
{
    RunSimpleLuaTest("luatests/putbyval_interpreter_ic_5.lua", LuaTestOption::ForceInterpreter);
}

TEST(LuaTestForceBaselineJit, PutByValInterpreterIC_5)
{
    RunSimpleLuaTest("luatests/putbyval_interpreter_ic_5.lua", LuaTestOption::ForceBaselineJit);
}

TEST(LuaTest, istc_conditional_copy)
{
    RunSimpleLuaTest("luatests/istc_conditional_copy.lua", LuaTestOption::ForceInterpreter);
}

TEST(LuaTestForceBaselineJit, istc_conditional_copy)
{
    RunSimpleLuaTest("luatests/istc_conditional_copy.lua", LuaTestOption::ForceBaselineJit);
}

TEST(LuaTest, isfc_conditional_copy)
{
    RunSimpleLuaTest("luatests/isfc_conditional_copy.lua", LuaTestOption::ForceInterpreter);
}

TEST(LuaTestForceBaselineJit, isfc_conditional_copy)
{
    RunSimpleLuaTest("luatests/isfc_conditional_copy.lua", LuaTestOption::ForceBaselineJit);
}

TEST(LuaTest, le_use_lt_metamethod)
{
    RunSimpleLuaTest("luatests/le_use_lt_metamethod.lua", LuaTestOption::ForceInterpreter);
}

TEST(LuaTestForceBaselineJit, le_use_lt_metamethod)
{
    RunSimpleLuaTest("luatests/le_use_lt_metamethod.lua", LuaTestOption::ForceBaselineJit);
}

TEST(LuaLib, base_assert)
{
    RunSimpleLuaTest("luatests/lib_base_assert.lua", LuaTestOption::ForceInterpreter);
}

TEST(LuaLibForceBaselineJit, base_assert)
{
    RunSimpleLuaTest("luatests/lib_base_assert.lua", LuaTestOption::ForceBaselineJit);
}

TEST(LuaLib, base_assert_2)
{
    RunSimpleLuaTest("luatests/base_lib_assert_2.lua", LuaTestOption::ForceInterpreter);
}

TEST(LuaLibForceBaselineJit, base_assert_2)
{
    RunSimpleLuaTest("luatests/base_lib_assert_2.lua", LuaTestOption::ForceBaselineJit);
}

TEST(LuaLib, RawsetReturnsOriginalTable)
{
    RunSimpleLuaTest("luatests/rawset_returns_original_table.lua", LuaTestOption::ForceInterpreter);
}

TEST(LuaLibForceBaselineJit, RawsetReturnsOriginalTable)
{
    RunSimpleLuaTest("luatests/rawset_returns_original_table.lua", LuaTestOption::ForceBaselineJit);
}

TEST(LuaLib, InitEnvironment)
{
    RunSimpleLuaTest("luatests/init_environment.lua", LuaTestOption::ForceInterpreter);
}

TEST(LuaLibForceBaselineJit, InitEnvironment)
{
    RunSimpleLuaTest("luatests/init_environment.lua", LuaTestOption::ForceBaselineJit);
}

TEST(LuaLib, math_sqrt)
{
    RunSimpleLuaTest("luatests/math_sqrt.lua", LuaTestOption::ForceInterpreter);
}

TEST(LuaLibForceBaselineJit, math_sqrt)
{
    RunSimpleLuaTest("luatests/math_sqrt.lua", LuaTestOption::ForceBaselineJit);
}

TEST(LuaLib, math_constants)
{
    RunSimpleLuaTest("luatests/math_constants.lua", LuaTestOption::ForceInterpreter);
}

TEST(LuaLibForceBaselineJit, math_constants)
{
    RunSimpleLuaTest("luatests/math_constants.lua", LuaTestOption::ForceBaselineJit);
}

TEST(LuaLib, math_unary_fn)
{
    RunSimpleLuaTest("luatests/math_lib_unary.lua", LuaTestOption::ForceInterpreter);
}

TEST(LuaLibForceBaselineJit, math_unary_fn)
{
    RunSimpleLuaTest("luatests/math_lib_unary.lua", LuaTestOption::ForceBaselineJit);
}

TEST(LuaLib, math_misc_fn)
{
    RunSimpleLuaTest("luatests/math_lib_misc.lua", LuaTestOption::ForceInterpreter);
}

TEST(LuaLibForceBaselineJit, math_misc_fn)
{
    RunSimpleLuaTest("luatests/math_lib_misc.lua", LuaTestOption::ForceBaselineJit);
}

TEST(LuaLib, math_min_max)
{
    RunSimpleLuaTest("luatests/math_lib_min_max.lua", LuaTestOption::ForceInterpreter);
}

TEST(LuaLibForceBaselineJit, math_min_max)
{
    RunSimpleLuaTest("luatests/math_lib_min_max.lua", LuaTestOption::ForceBaselineJit);
}

TEST(LuaLib, math_random)
{
    RunSimpleLuaTest("luatests/math_lib_random.lua", LuaTestOption::ForceInterpreter);
}

TEST(LuaLibForceBaselineJit, math_random)
{
    RunSimpleLuaTest("luatests/math_lib_random.lua", LuaTestOption::ForceBaselineJit);
}

TEST(LuaLib, coroutine_1)
{
    RunSimpleLuaTest("luatests/coroutine_1.lua", LuaTestOption::ForceInterpreter);
}

TEST(LuaLibForceBaselineJit, coroutine_1)
{
    RunSimpleLuaTest("luatests/coroutine_1.lua", LuaTestOption::ForceBaselineJit);
}

TEST(LuaLib, coroutine_2)
{
    RunSimpleLuaTest("luatests/coroutine_2.lua", LuaTestOption::ForceInterpreter);
}

TEST(LuaLibForceBaselineJit, coroutine_2)
{
    RunSimpleLuaTest("luatests/coroutine_2.lua", LuaTestOption::ForceBaselineJit);
}

TEST(LuaLib, coroutine_3)
{
    RunSimpleLuaTest("luatests/coroutine_3.lua", LuaTestOption::ForceInterpreter);
}

TEST(LuaLibForceBaselineJit, coroutine_3)
{
    RunSimpleLuaTest("luatests/coroutine_3.lua", LuaTestOption::ForceBaselineJit);
}

TEST(LuaLib, coroutine_4)
{
    RunSimpleLuaTest("luatests/coroutine_4.lua", LuaTestOption::ForceInterpreter);
}

TEST(LuaLibForceBaselineJit, coroutine_4)
{
    RunSimpleLuaTest("luatests/coroutine_4.lua", LuaTestOption::ForceBaselineJit);
}

TEST(LuaLib, coroutine_5)
{
    RunSimpleLuaTest("luatests/coroutine_5.lua", LuaTestOption::ForceInterpreter);
}

TEST(LuaLibForceBaselineJit, coroutine_5)
{
    RunSimpleLuaTest("luatests/coroutine_5.lua", LuaTestOption::ForceBaselineJit);
}

TEST(LuaLib, coroutine_ring)
{
    RunSimpleLuaTest("luatests/coroutine_ring.lua", LuaTestOption::ForceInterpreter);
}

TEST(LuaLibForceBaselineJit, coroutine_ring)
{
    RunSimpleLuaTest("luatests/coroutine_ring.lua", LuaTestOption::ForceBaselineJit);
}

TEST(LuaLib, coroutine_error_1)
{
    RunSimpleLuaTest("luatests/coroutine_error_1.lua", LuaTestOption::ForceInterpreter);
}

TEST(LuaLibForceBaselineJit, coroutine_error_1)
{
    RunSimpleLuaTest("luatests/coroutine_error_1.lua", LuaTestOption::ForceBaselineJit);
}

TEST(LuaLib, coroutine_error_2)
{
    RunSimpleLuaTest("luatests/coroutine_error_2.lua", LuaTestOption::ForceInterpreter);
}

TEST(LuaLibForceBaselineJit, coroutine_error_2)
{
    RunSimpleLuaTest("luatests/coroutine_error_2.lua", LuaTestOption::ForceBaselineJit);
}

TEST(LuaLib, coroutine_error_3)
{
    RunSimpleLuaTest("luatests/coroutine_error_3.lua", LuaTestOption::ForceInterpreter);
}

TEST(LuaLibForceBaselineJit, coroutine_error_3)
{
    RunSimpleLuaTest("luatests/coroutine_error_3.lua", LuaTestOption::ForceBaselineJit);
}

TEST(LuaLib, base_ipairs)
{
    RunSimpleLuaTest("luatests/base_lib_ipairs.lua", LuaTestOption::ForceInterpreter);
}

TEST(LuaLibForceBaselineJit, base_ipairs)
{
    RunSimpleLuaTest("luatests/base_lib_ipairs.lua", LuaTestOption::ForceBaselineJit);
}

TEST(LuaLib, base_ipairs_2)
{
    RunSimpleLuaTest("luatests/base_lib_ipairs_2.lua", LuaTestOption::ForceInterpreter);
}

TEST(LuaLibForceBaselineJit, base_ipairs_2)
{
    RunSimpleLuaTest("luatests/base_lib_ipairs_2.lua", LuaTestOption::ForceBaselineJit);
}

TEST(LuaLib, base_rawequal)
{
    RunSimpleLuaTest("luatests/base_lib_rawequal.lua", LuaTestOption::ForceInterpreter);
}

TEST(LuaLibForceBaselineJit, base_rawequal)
{
    RunSimpleLuaTest("luatests/base_lib_rawequal.lua", LuaTestOption::ForceBaselineJit);
}

TEST(LuaLib, base_select_1)
{
    RunSimpleLuaTest("luatests/base_lib_select_1.lua", LuaTestOption::ForceInterpreter);
}

TEST(LuaLibForceBaselineJit, base_select_1)
{
    RunSimpleLuaTest("luatests/base_lib_select_1.lua", LuaTestOption::ForceBaselineJit);
}

TEST(LuaLib, base_select_2)
{
    RunSimpleLuaTest("luatests/base_lib_select_2.lua", LuaTestOption::ForceInterpreter);
}

TEST(LuaLibForceBaselineJit, base_select_2)
{
    RunSimpleLuaTest("luatests/base_lib_select_2.lua", LuaTestOption::ForceBaselineJit);
}

TEST(LuaLib, base_lib_type)
{
    RunSimpleLuaTest("luatests/base_lib_type.lua", LuaTestOption::ForceInterpreter);
}

TEST(LuaLibForceBaselineJit, base_lib_type)
{
    RunSimpleLuaTest("luatests/base_lib_type.lua", LuaTestOption::ForceBaselineJit);
}

TEST(LuaLib, base_lib_next)
{
    RunSimpleLuaTest("luatests/base_lib_next.lua", LuaTestOption::ForceInterpreter);
}

TEST(LuaLibForceBaselineJit, base_lib_next)
{
    RunSimpleLuaTest("luatests/base_lib_next.lua", LuaTestOption::ForceBaselineJit);
}

TEST(LuaLib, base_lib_pairs)
{
    RunSimpleLuaTest("luatests/base_lib_pairs.lua", LuaTestOption::ForceInterpreter);
}

TEST(LuaLibForceBaselineJit, base_lib_pairs)
{
    RunSimpleLuaTest("luatests/base_lib_pairs.lua", LuaTestOption::ForceBaselineJit);
}

TEST(LuaLib, base_lib_pcall)
{
    RunSimpleLuaTest("luatests/base_lib_pcall.lua", LuaTestOption::ForceInterpreter);
}

TEST(LuaLibForceBaselineJit, base_lib_pcall)
{
    RunSimpleLuaTest("luatests/base_lib_pcall.lua", LuaTestOption::ForceBaselineJit);
}

TEST(LuaLib, base_lib_tonumber)
{
    RunSimpleLuaTest("luatests/base_lib_tonumber.lua", LuaTestOption::ForceInterpreter);
}

TEST(LuaLibForceBaselineJit, base_lib_tonumber)
{
    RunSimpleLuaTest("luatests/base_lib_tonumber.lua", LuaTestOption::ForceBaselineJit);
}

TEST(LuaLib, base_lib_tonumber_2)
{
    RunSimpleLuaTest("luatests/base_lib_tonumber_2.lua", LuaTestOption::ForceInterpreter);
}

TEST(LuaLibForceBaselineJit, base_lib_tonumber_2)
{
    RunSimpleLuaTest("luatests/base_lib_tonumber_2.lua", LuaTestOption::ForceBaselineJit);
}

TEST(LuaLib, base_lib_tostring)
{
    RunSimpleLuaTest("luatests/base_lib_tostring.lua", LuaTestOption::ForceInterpreter);
}

TEST(LuaLibForceBaselineJit, base_lib_tostring)
{
    RunSimpleLuaTest("luatests/base_lib_tostring.lua", LuaTestOption::ForceBaselineJit);
}

TEST(LuaLib, base_lib_tostring_2)
{
    RunSimpleLuaTest("luatests/base_lib_tostring_2.lua", LuaTestOption::ForceInterpreter);
}

TEST(LuaLibForceBaselineJit, base_lib_tostring_2)
{
    RunSimpleLuaTest("luatests/base_lib_tostring_2.lua", LuaTestOption::ForceBaselineJit);
}

TEST(LuaLib, base_lib_tostring_3)
{
    RunSimpleLuaTest("luatests/base_lib_tostring_3.lua", LuaTestOption::ForceInterpreter);
}

TEST(LuaLibForceBaselineJit, base_lib_tostring_3)
{
    RunSimpleLuaTest("luatests/base_lib_tostring_3.lua", LuaTestOption::ForceBaselineJit);
}

TEST(LuaLib, base_lib_tostring_4)
{
    RunSimpleLuaTest("luatests/base_lib_tostring_4.lua", LuaTestOption::ForceInterpreter);
}

TEST(LuaLibForceBaselineJit, base_lib_tostring_4)
{
    RunSimpleLuaTest("luatests/base_lib_tostring_4.lua", LuaTestOption::ForceBaselineJit);
}

TEST(LuaLib, base_lib_tostring_5)
{
    RunSimpleLuaTest("luatests/base_lib_tostring_5.lua", LuaTestOption::ForceInterpreter);
}

TEST(LuaLibForceBaselineJit, base_lib_tostring_5)
{
    RunSimpleLuaTest("luatests/base_lib_tostring_5.lua", LuaTestOption::ForceBaselineJit);
}

TEST(LuaLib, base_lib_tostring_6)
{
    RunSimpleLuaTest("luatests/base_lib_tostring_6.lua", LuaTestOption::ForceInterpreter);
}

TEST(LuaLibForceBaselineJit, base_lib_tostring_6)
{
    RunSimpleLuaTest("luatests/base_lib_tostring_6.lua", LuaTestOption::ForceBaselineJit);
}

TEST(LuaLib, base_lib_print)
{
    RunSimpleLuaTest("luatests/base_lib_print.lua", LuaTestOption::ForceInterpreter);
}

TEST(LuaLibForceBaselineJit, base_lib_print)
{
    RunSimpleLuaTest("luatests/base_lib_print.lua", LuaTestOption::ForceBaselineJit);
}

TEST(LuaLib, base_lib_print_2)
{
    RunSimpleLuaTest("luatests/base_lib_print_2.lua", LuaTestOption::ForceInterpreter);
}

TEST(LuaLibForceBaselineJit, base_lib_print_2)
{
    RunSimpleLuaTest("luatests/base_lib_print_2.lua", LuaTestOption::ForceBaselineJit);
}

TEST(LuaLib, base_lib_unpack)
{
    RunSimpleLuaTest("luatests/base_lib_unpack.lua", LuaTestOption::ForceInterpreter);
}

TEST(LuaLibForceBaselineJit, base_lib_unpack)
{
    RunSimpleLuaTest("luatests/base_lib_unpack.lua", LuaTestOption::ForceBaselineJit);
}

TEST(LuaLib, string_lib_byte)
{
    RunSimpleLuaTest("luatests/string_lib_byte.lua", LuaTestOption::ForceInterpreter);
}

TEST(LuaLibForceBaselineJit, string_lib_byte)
{
    RunSimpleLuaTest("luatests/string_lib_byte.lua", LuaTestOption::ForceBaselineJit);
}

TEST(LuaLib, string_lib_byte_2)
{
    RunSimpleLuaTest("luatests/string_lib_byte_2.lua", LuaTestOption::ForceInterpreter);
}

TEST(LuaLibForceBaselineJit, string_lib_byte_2)
{
    RunSimpleLuaTest("luatests/string_lib_byte_2.lua", LuaTestOption::ForceBaselineJit);
}

TEST(LuaLib, string_lib_char)
{
    RunSimpleLuaTest("luatests/string_lib_char.lua", LuaTestOption::ForceInterpreter);
}

TEST(LuaLibForceBaselineJit, string_lib_char)
{
    RunSimpleLuaTest("luatests/string_lib_char.lua", LuaTestOption::ForceBaselineJit);
}

TEST(LuaLib, string_lib_char_2)
{
    RunSimpleLuaTest("luatests/string_lib_char_2.lua", LuaTestOption::ForceInterpreter);
}

TEST(LuaLibForceBaselineJit, string_lib_char_2)
{
    RunSimpleLuaTest("luatests/string_lib_char_2.lua", LuaTestOption::ForceBaselineJit);
}

TEST(LuaLib, string_lib_rep)
{
    RunSimpleLuaTest("luatests/string_lib_rep.lua", LuaTestOption::ForceInterpreter);
}

TEST(LuaLibForceBaselineJit, string_lib_rep)
{
    RunSimpleLuaTest("luatests/string_lib_rep.lua", LuaTestOption::ForceBaselineJit);
}

TEST(LuaLib, string_lib_rep_2)
{
    RunSimpleLuaTest("luatests/string_lib_rep_2.lua", LuaTestOption::ForceInterpreter);
}

TEST(LuaLibForceBaselineJit, string_lib_rep_2)
{
    RunSimpleLuaTest("luatests/string_lib_rep_2.lua", LuaTestOption::ForceBaselineJit);
}

TEST(LuaLib, string_lib_sub)
{
    RunSimpleLuaTest("luatests/string_lib_sub.lua", LuaTestOption::ForceInterpreter);
}

TEST(LuaLibForceBaselineJit, string_lib_sub)
{
    RunSimpleLuaTest("luatests/string_lib_sub.lua", LuaTestOption::ForceBaselineJit);
}

TEST(LuaLib, string_lib_sub_2)
{
    RunSimpleLuaTest("luatests/string_lib_sub_2.lua", LuaTestOption::ForceInterpreter);
}

TEST(LuaLibForceBaselineJit, string_lib_sub_2)
{
    RunSimpleLuaTest("luatests/string_lib_sub_2.lua", LuaTestOption::ForceBaselineJit);
}

TEST(LuaLib, string_format)
{
    RunSimpleLuaTest("luatests/string_format.lua", LuaTestOption::ForceInterpreter);
}

TEST(LuaLibForceBaselineJit, string_format)
{
    RunSimpleLuaTest("luatests/string_format.lua", LuaTestOption::ForceBaselineJit);
}

TEST(LuaLib, string_lib_len)
{
    RunSimpleLuaTest("luatests/string_lib_len.lua", LuaTestOption::ForceInterpreter);
}

TEST(LuaLibForceBaselineJit, string_lib_len)
{
    RunSimpleLuaTest("luatests/string_lib_len.lua", LuaTestOption::ForceBaselineJit);
}

TEST(LuaLib, string_lib_reverse)
{
    RunSimpleLuaTest("luatests/string_lib_reverse.lua", LuaTestOption::ForceInterpreter);
}

TEST(LuaLibForceBaselineJit, string_lib_reverse)
{
    RunSimpleLuaTest("luatests/string_lib_reverse.lua", LuaTestOption::ForceBaselineJit);
}

TEST(LuaLib, string_lib_lower_upper)
{
    RunSimpleLuaTest("luatests/string_lib_lower_upper.lua", LuaTestOption::ForceInterpreter);
}

TEST(LuaLibForceBaselineJit, string_lib_lower_upper)
{
    RunSimpleLuaTest("luatests/string_lib_lower_upper.lua", LuaTestOption::ForceBaselineJit);
}

TEST(LuaLib, string_lib_lower_upper_2)
{
    RunSimpleLuaTest("luatests/string_lib_lower_upper_2.lua", LuaTestOption::ForceInterpreter);
}

TEST(LuaLibForceBaselineJit, string_lib_lower_upper_2)
{
    RunSimpleLuaTest("luatests/string_lib_lower_upper_2.lua", LuaTestOption::ForceBaselineJit);
}

TEST(LuaLib, string_lib_misc)
{
    RunSimpleLuaTest("luatests/string_lib_misc.lua", LuaTestOption::ForceInterpreter);
}

TEST(LuaLibForceBaselineJit, string_lib_misc)
{
    RunSimpleLuaTest("luatests/string_lib_misc.lua", LuaTestOption::ForceBaselineJit);
}

TEST(LuaLib, table_sort_1)
{
    RunSimpleLuaTest("luatests/table_sort_1.lua", LuaTestOption::ForceInterpreter);
}

TEST(LuaLibForceBaselineJit, table_sort_1)
{
    RunSimpleLuaTest("luatests/table_sort_1.lua", LuaTestOption::ForceBaselineJit);
}

TEST(LuaLib, table_sort_2)
{
    RunSimpleLuaTest("luatests/table_sort_2.lua", LuaTestOption::ForceInterpreter);
}

TEST(LuaLibForceBaselineJit, table_sort_2)
{
    RunSimpleLuaTest("luatests/table_sort_2.lua", LuaTestOption::ForceBaselineJit);
}

TEST(LuaLib, table_sort_3)
{
    RunSimpleLuaTest("luatests/table_sort_3.lua", LuaTestOption::ForceInterpreter);
}

TEST(LuaLibForceBaselineJit, table_sort_3)
{
    RunSimpleLuaTest("luatests/table_sort_3.lua", LuaTestOption::ForceBaselineJit);
}

TEST(LuaLib, table_sort_4)
{
    RunSimpleLuaTest("luatests/table_sort_4.lua", LuaTestOption::ForceInterpreter);
}

TEST(LuaLibForceBaselineJit, table_sort_4)
{
    RunSimpleLuaTest("luatests/table_sort_4.lua", LuaTestOption::ForceBaselineJit);
}

TEST(LuaLib, table_lib_concat)
{
    RunSimpleLuaTest("luatests/table_lib_concat.lua", LuaTestOption::ForceInterpreter);
}

TEST(LuaLibForceBaselineJit, table_lib_concat)
{
    RunSimpleLuaTest("luatests/table_lib_concat.lua", LuaTestOption::ForceBaselineJit);
}

TEST(LuaLib, table_concat_overflow)
{
    RunSimpleLuaTest("luatests/table_concat_overflow.lua", LuaTestOption::ForceInterpreter);
}

TEST(LuaLibForceBaselineJit, table_concat_overflow)
{
    RunSimpleLuaTest("luatests/table_concat_overflow.lua", LuaTestOption::ForceBaselineJit);
}

TEST(LuaBenchmark, array3d)
{
    RunSimpleLuaTest("luatests/array3d.lua", LuaTestOption::ForceInterpreter);
}

TEST(LuaBenchmarkForceBaselineJit, array3d)
{
    RunSimpleLuaTest("luatests/array3d.lua", LuaTestOption::ForceBaselineJit);
}

TEST(LuaBenchmark, life)
{
    RunSimpleLuaTest("luatests/life.lua", LuaTestOption::ForceInterpreter);
}

TEST(LuaBenchmarkForceBaselineJit, life)
{
    RunSimpleLuaTest("luatests/life.lua", LuaTestOption::ForceBaselineJit);
}

TEST(LuaBenchmark, mandel2)
{
    RunSimpleLuaTest("luatests/mandel2.lua", LuaTestOption::ForceInterpreter);
}

TEST(LuaBenchmarkForceBaselineJit, mandel2)
{
    RunSimpleLuaTest("luatests/mandel2.lua", LuaTestOption::ForceBaselineJit);
}

TEST(LuaBenchmark, heapsort)
{
    RunSimpleLuaTest("luatests/heapsort.lua", LuaTestOption::ForceInterpreter);
}

TEST(LuaBenchmarkForceBaselineJit, heapsort)
{
    RunSimpleLuaTest("luatests/heapsort.lua", LuaTestOption::ForceBaselineJit);
}

TEST(LuaBenchmark, nsieve)
{
    RunSimpleLuaTest("luatests/nsieve.lua", LuaTestOption::ForceInterpreter);
}

TEST(LuaBenchmarkForceBaselineJit, nsieve)
{
    RunSimpleLuaTest("luatests/nsieve.lua", LuaTestOption::ForceBaselineJit);
}

TEST(LuaBenchmark, quadtree2)
{
    RunSimpleLuaTest("luatests/quadtree2.lua", LuaTestOption::ForceInterpreter);
}

TEST(LuaBenchmarkForceBaselineJit, quadtree2)
{
    RunSimpleLuaTest("luatests/quadtree2.lua", LuaTestOption::ForceBaselineJit);
}

TEST(LuaBenchmark, ray)
{
    RunSimpleLuaTest("luatests/ray.lua", LuaTestOption::ForceInterpreter);
}

TEST(LuaBenchmarkForceBaselineJit, ray)
{
    RunSimpleLuaTest("luatests/ray.lua", LuaTestOption::ForceBaselineJit);
}

TEST(LuaBenchmark, ray2)
{
    RunSimpleLuaTest("luatests/ray2.lua", LuaTestOption::ForceInterpreter);
}

TEST(LuaBenchmarkForceBaselineJit, ray2)
{
    RunSimpleLuaTest("luatests/ray2.lua", LuaTestOption::ForceBaselineJit);
}

TEST(LuaBenchmark, series)
{
    RunSimpleLuaTest("luatests/series.lua", LuaTestOption::ForceInterpreter);
}

TEST(LuaBenchmarkForceBaselineJit, series)
{
    RunSimpleLuaTest("luatests/series.lua", LuaTestOption::ForceBaselineJit);
}

TEST(LuaBenchmark, scimark_fft)
{
    RunSimpleLuaTest("luatests/scimark_fft.lua", LuaTestOption::ForceInterpreter);
}

TEST(LuaBenchmarkForceBaselineJit, scimark_fft)
{
    RunSimpleLuaTest("luatests/scimark_fft.lua", LuaTestOption::ForceBaselineJit);
}

TEST(LuaBenchmark, scimark_lu)
{
    RunSimpleLuaTest("luatests/scimark_lu.lua", LuaTestOption::ForceInterpreter);
}

TEST(LuaBenchmarkForceBaselineJit, scimark_lu)
{
    RunSimpleLuaTest("luatests/scimark_lu.lua", LuaTestOption::ForceBaselineJit);
}

TEST(LuaBenchmark, scimark_sor)
{
    RunSimpleLuaTest("luatests/scimark_sor.lua", LuaTestOption::ForceInterpreter);
}

TEST(LuaBenchmarkForceBaselineJit, scimark_sor)
{
    RunSimpleLuaTest("luatests/scimark_sor.lua", LuaTestOption::ForceBaselineJit);
}

TEST(LuaBenchmark, scimark_sparse)
{
    RunSimpleLuaTest("luatests/scimark_sparse.lua", LuaTestOption::ForceInterpreter);
}

TEST(LuaBenchmarkForceBaselineJit, scimark_sparse)
{
    RunSimpleLuaTest("luatests/scimark_sparse.lua", LuaTestOption::ForceBaselineJit);
}

TEST(LuaBenchmark, table_sort)
{
    RunSimpleLuaTest("luatests/table_sort.lua", LuaTestOption::ForceInterpreter);
}

TEST(LuaBenchmarkForceBaselineJit, table_sort)
{
    RunSimpleLuaTest("luatests/table_sort.lua", LuaTestOption::ForceBaselineJit);
}

TEST(LuaBenchmark, table_sort_cmp)
{
    RunSimpleLuaTest("luatests/table_sort_cmp.lua", LuaTestOption::ForceInterpreter);
}

TEST(LuaBenchmarkForceBaselineJit, table_sort_cmp)
{
    RunSimpleLuaTest("luatests/table_sort_cmp.lua", LuaTestOption::ForceBaselineJit);
}

TEST(LuaLib, base_loadstring)
{
    RunSimpleLuaTest("luatests/base_loadstring.lua", LuaTestOption::ForceInterpreter);
}

TEST(LuaLibForceBaselineJit, base_loadstring)
{
    RunSimpleLuaTest("luatests/base_loadstring.lua", LuaTestOption::ForceBaselineJit);
}

TEST(LuaLib, base_load)
{
    RunSimpleLuaTest("luatests/base_load.lua", LuaTestOption::ForceInterpreter);
}

TEST(LuaLibForceBaselineJit, base_load)
{
    RunSimpleLuaTest("luatests/base_load.lua", LuaTestOption::ForceBaselineJit);
}

TEST(LuaLib, base_loadfile)
{
    RunSimpleLuaTest("luatests/base_loadfile.lua", LuaTestOption::ForceInterpreter);
}

TEST(LuaLibForceBaselineJit, base_loadfile)
{
    RunSimpleLuaTest("luatests/base_loadfile.lua", LuaTestOption::ForceBaselineJit);
}

TEST(LuaLib, base_loadfile_nonexistent)
{
    RunSimpleLuaTest("luatests/base_loadfile_nonexistent.lua", LuaTestOption::ForceInterpreter);
}

TEST(LuaLibForceBaselineJit, base_loadfile_nonexistent)
{
    RunSimpleLuaTest("luatests/base_loadfile_nonexistent.lua", LuaTestOption::ForceBaselineJit);
}

TEST(LuaLib, base_lib_dofile)
{
    RunSimpleLuaTest("luatests/base_lib_dofile.lua", LuaTestOption::ForceInterpreter);
}

TEST(LuaLibForceBaselineJit, base_lib_dofile)
{
    RunSimpleLuaTest("luatests/base_lib_dofile.lua", LuaTestOption::ForceBaselineJit);
}

TEST(LuaLib, base_lib_dofile_nonexistent)
{
    RunSimpleLuaTest("luatests/base_lib_dofile_nonexistent.lua", LuaTestOption::ForceInterpreter);
}

TEST(LuaLibForceBaselineJit, base_lib_dofile_nonexistent)
{
    RunSimpleLuaTest("luatests/base_lib_dofile_nonexistent.lua", LuaTestOption::ForceBaselineJit);
}

TEST(LuaLib, base_lib_dofile_bad_syntax)
{
    RunSimpleLuaTest("luatests/base_lib_dofile_bad_syntax.lua", LuaTestOption::ForceInterpreter);
}

TEST(LuaLibForceBaselineJit, base_lib_dofile_bad_syntax)
{
    RunSimpleLuaTest("luatests/base_lib_dofile_bad_syntax.lua", LuaTestOption::ForceBaselineJit);
}

TEST(LuaLib, base_lib_dofile_throw)
{
    RunSimpleLuaTest("luatests/base_lib_dofile_throw.lua", LuaTestOption::ForceInterpreter);
}

TEST(LuaLibForceBaselineJit, base_lib_dofile_throw)
{
    RunSimpleLuaTest("luatests/base_lib_dofile_throw.lua", LuaTestOption::ForceBaselineJit);
}

TEST(LuaBenchmark, fasta)
{
    RunSimpleLuaTest("luatests/fasta.lua", LuaTestOption::ForceInterpreter);
}

TEST(LuaBenchmarkForceBaselineJit, fasta)
{
    RunSimpleLuaTest("luatests/fasta.lua", LuaTestOption::ForceBaselineJit);
}

TEST(LuaBenchmark, pidigits)
{
    RunSimpleLuaTest("luatests/pidigits-nogmp.lua", LuaTestOption::ForceInterpreter);
}

TEST(LuaBenchmarkForceBaselineJit, pidigits)
{
    RunSimpleLuaTest("luatests/pidigits-nogmp.lua", LuaTestOption::ForceBaselineJit);
}

TEST(LuaBenchmark, revcomp)
{
    RunSimpleLuaTest("luatests/revcomp.lua", LuaTestOption::ForceInterpreter);
}

TEST(LuaBenchmarkForceBaselineJit, revcomp)
{
    RunSimpleLuaTest("luatests/revcomp.lua", LuaTestOption::ForceBaselineJit);
}

TEST(LuaBenchmark, knucleotide)
{
    RunSimpleLuaTest("luatests/k-nucleotide.lua", LuaTestOption::ForceInterpreter);
}

TEST(LuaBenchmarkForceBaselineJit, knucleotide)
{
    RunSimpleLuaTest("luatests/k-nucleotide.lua", LuaTestOption::ForceBaselineJit);
}

TEST(LuaTest, comparison_one_side_constant)
{
    RunSimpleLuaTest("luatests/comparison_one_side_constant.lua", LuaTestOption::ForceInterpreter);
}

TEST(LuaTestForceBaselineJit, comparison_one_side_constant)
{
    RunSimpleLuaTest("luatests/comparison_one_side_constant.lua", LuaTestOption::ForceBaselineJit);
}

TEST(LuaBenchmark, bounce)
{
    RunSimpleLuaTest("luatests/bounce.lua", LuaTestOption::ForceInterpreter);
}

TEST(LuaBenchmarkForceBaselineJit, bounce)
{
    RunSimpleLuaTest("luatests/bounce.lua", LuaTestOption::ForceBaselineJit);
}

TEST(LuaBenchmark, cd)
{
    RunSimpleLuaTest("luatests/cd.lua", LuaTestOption::ForceInterpreter);
}

TEST(LuaBenchmarkForceBaselineJit, cd)
{
    RunSimpleLuaTest("luatests/cd.lua", LuaTestOption::ForceBaselineJit);
}

TEST(LuaBenchmark, deltablue)
{
    RunSimpleLuaTest("luatests/deltablue.lua", LuaTestOption::ForceInterpreter);
}

TEST(LuaBenchmarkForceBaselineJit, deltablue)
{
    RunSimpleLuaTest("luatests/deltablue.lua", LuaTestOption::ForceBaselineJit);
}

TEST(LuaBenchmark, havlak)
{
    RunSimpleLuaTest("luatests/havlak.lua", LuaTestOption::ForceInterpreter);
}

TEST(LuaBenchmarkForceBaselineJit, havlak)
{
    RunSimpleLuaTest("luatests/havlak.lua", LuaTestOption::ForceBaselineJit);
}

TEST(LuaBenchmark, json)
{
    RunSimpleLuaTest("luatests/json.lua", LuaTestOption::ForceInterpreter);
}

TEST(LuaBenchmarkForceBaselineJit, json)
{
    RunSimpleLuaTest("luatests/json.lua", LuaTestOption::ForceBaselineJit);
}

TEST(LuaBenchmark, list)
{
    RunSimpleLuaTest("luatests/list.lua", LuaTestOption::ForceInterpreter);
}

TEST(LuaBenchmarkForceBaselineJit, list)
{
    RunSimpleLuaTest("luatests/list.lua", LuaTestOption::ForceBaselineJit);
}

TEST(LuaBenchmark, permute)
{
    RunSimpleLuaTest("luatests/permute.lua", LuaTestOption::ForceInterpreter);
}

TEST(LuaBenchmarkForceBaselineJit, permute)
{
    RunSimpleLuaTest("luatests/permute.lua", LuaTestOption::ForceBaselineJit);
}

TEST(LuaBenchmark, richard)
{
    RunSimpleLuaTest("luatests/richard.lua", LuaTestOption::ForceInterpreter);
}

TEST(LuaBenchmarkForceBaselineJit, richard)
{
    RunSimpleLuaTest("luatests/richard.lua", LuaTestOption::ForceBaselineJit);
}

TEST(LuaBenchmark, storage)
{
    RunSimpleLuaTest("luatests/storage.lua", LuaTestOption::ForceInterpreter);
}

TEST(LuaBenchmarkForceBaselineJit, storage)
{
    RunSimpleLuaTest("luatests/storage.lua", LuaTestOption::ForceBaselineJit);
}

TEST(LuaBenchmark, towers)
{
    RunSimpleLuaTest("luatests/towers.lua", LuaTestOption::ForceInterpreter);
}

TEST(LuaBenchmarkForceBaselineJit, towers)
{
    RunSimpleLuaTest("luatests/towers.lua", LuaTestOption::ForceBaselineJit);
}

}   // anonymous namespace
