#include <fstream>
#include "runtime_utils.h"
#include "gtest/gtest.h"
#include "json_utils.h"
#include "test_util_helper.h"
#include "test_vm_utils.h"

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

std::string LoadFile(std::string filename)
{
    std::ifstream infile(filename, std::ios_base::binary);
    ReleaseAssert(infile.rdstate() == std::ios_base::goodbit);
    std::istreambuf_iterator<char> iter { infile }, end;
    return std::string { iter, end };
}

void RunSimpleLuaTest(const std::string& filename)
{
    VM* vm = VM::Create();
    Auto(vm->Destroy());
    VMOutputInterceptor vmoutput(vm);

    ScriptModule* module = ScriptModule::ParseFromJSON(vm, LoadFile(filename));
    vm->LaunchScript(module);

    std::string out = vmoutput.GetAndResetStdOut();
    std::string err = vmoutput.GetAndResetStdErr();
    AssertIsExpectedOutput(out);
    ReleaseAssert(err == "");
}

TEST(LuaTest, Fib)
{
    RunSimpleLuaTest("luatests/fib.lua.json");
}

TEST(LuaTest, TestPrint)
{
    VM* vm = VM::Create();
    Auto(vm->Destroy());
    VMOutputInterceptor vmoutput(vm);

    ScriptModule* module = ScriptModule::ParseFromJSON(vm, LoadFile("luatests/test_print.lua.json"));
    vm->LaunchScript(module);

    std::string out = vmoutput.GetAndResetStdOut();
    std::string err = vmoutput.GetAndResetStdErr();

    std::string expectedOut = "0.2\t3\tfalse\ttrue\tnil\tabc\tfunction: 0x";
    ReleaseAssert(out.starts_with(expectedOut));
    ReleaseAssert(err == "");
}

TEST(LuaTest, TestTableDup)
{
    RunSimpleLuaTest("luatests/table_dup.lua.json");
}

TEST(LuaTest, TestTableDup2)
{
    RunSimpleLuaTest("luatests/table_dup2.lua.json");
}

TEST(LuaTest, TestTableDup3)
{
    RunSimpleLuaTest("luatests/table_dup3.lua.json");
}

TEST(LuaTest, TestTableSizeHint)
{
    VM* vm = VM::Create();
    Auto(vm->Destroy());
    VMOutputInterceptor vmoutput(vm);

    ScriptModule* module = ScriptModule::ParseFromJSON(vm, LoadFile("luatests/table_size_hint.lua.json"));
    vm->LaunchScript(module);

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

TEST(LuaTest, Upvalue)
{
    RunSimpleLuaTest("luatests/upvalue.lua.json");
}

TEST(LuaTest, Fib_upvalue)
{
    RunSimpleLuaTest("luatests/fib_upvalue.lua.json");
}

TEST(LuaTest, LinearSieve)
{
    RunSimpleLuaTest("luatests/linear_sieve.lua.json");
}

TEST(LuaTest, NaNEdgeCase)
{
    RunSimpleLuaTest("luatests/nan_edge_case.lua.json");
}

TEST(LuaTest, ForLoopCoercion)
{
    RunSimpleLuaTest("luatests/for_loop_coercion.lua.json");
}

TEST(LuaTest, ForLoopEdgeCases)
{
    RunSimpleLuaTest("luatests/for_loop_edge_cases.lua.json");
}

TEST(LuaTest, PrimitiveConstants)
{
    RunSimpleLuaTest("luatests/primitive_constant.lua.json");
}

TEST(LuaTest, LogicalOpSanity)
{
    RunSimpleLuaTest("luatests/logical_op_sanity.lua.json");
}

TEST(LuaTest, PositiveAndNegativeInf)
{
    RunSimpleLuaTest("luatests/pos_and_neg_inf.lua.json");
}

TEST(LuaTest, LogicalNot)
{
    RunSimpleLuaTest("luatests/logical_not.lua.json");
}

TEST(LuaTest, LengthOperator)
{
    RunSimpleLuaTest("luatests/length_operator.lua.json");
}

TEST(LuaTest, TailCall)
{
    VM* vm = VM::Create();
    Auto(vm->Destroy());
    VMOutputInterceptor vmoutput(vm);

    ScriptModule* module = ScriptModule::ParseFromJSON(vm, LoadFile("luatests/tail_call.lua.json"));

    // Manually lower the stack size
    //
    CoroutineRuntimeContext* rc = vm->GetRootCoroutine();
    rc->m_stackBegin = new TValue[200];

    vm->LaunchScript(module);

    std::string out = vmoutput.GetAndResetStdOut();
    std::string err = vmoutput.GetAndResetStdErr();
    AssertIsExpectedOutput(out);
    ReleaseAssert(err == "");
}

TEST(LuaTest, VariadicTailCall_1)
{
    VM* vm = VM::Create();
    Auto(vm->Destroy());
    VMOutputInterceptor vmoutput(vm);

    ScriptModule* module = ScriptModule::ParseFromJSON(vm, LoadFile("luatests/variadic_tail_call_1.lua.json"));

    // Manually lower the stack size
    //
    CoroutineRuntimeContext* rc = vm->GetRootCoroutine();
    rc->m_stackBegin = new TValue[200];

    vm->LaunchScript(module);

    std::string out = vmoutput.GetAndResetStdOut();
    std::string err = vmoutput.GetAndResetStdErr();
    AssertIsExpectedOutput(out);
    ReleaseAssert(err == "");
}

TEST(LuaTest, VariadicTailCall_2)
{
    VM* vm = VM::Create();
    Auto(vm->Destroy());
    VMOutputInterceptor vmoutput(vm);

    ScriptModule* module = ScriptModule::ParseFromJSON(vm, LoadFile("luatests/variadic_tail_call_2.lua.json"));

    // Manually lower the stack size
    //
    CoroutineRuntimeContext* rc = vm->GetRootCoroutine();
    rc->m_stackBegin = new TValue[200];

    vm->LaunchScript(module);

    std::string out = vmoutput.GetAndResetStdOut();
    std::string err = vmoutput.GetAndResetStdErr();
    AssertIsExpectedOutput(out);
    ReleaseAssert(err == "");
}

TEST(LuaTest, VariadicTailCall_3)
{
    VM* vm = VM::Create();
    Auto(vm->Destroy());
    VMOutputInterceptor vmoutput(vm);

    ScriptModule* module = ScriptModule::ParseFromJSON(vm, LoadFile("luatests/variadic_tail_call_3.lua.json"));

    // Manually lower the stack size
    //
    CoroutineRuntimeContext* rc = vm->GetRootCoroutine();
    rc->m_stackBegin = new TValue[200];

    vm->LaunchScript(module);

    std::string out = vmoutput.GetAndResetStdOut();
    std::string err = vmoutput.GetAndResetStdErr();
    AssertIsExpectedOutput(out);
    ReleaseAssert(err == "");
}

TEST(LuaTest, OpcodeKNIL)
{
    RunSimpleLuaTest("luatests/test_knil.lua.json");
}

TEST(LuaTest, IterativeForLoop)
{
    RunSimpleLuaTest("luatests/iter_for.lua.json");
}

TEST(LuaTest, NegativeZeroAsIndex)
{
    RunSimpleLuaTest("luatests/negative_zero_as_index.lua.json");
}

// We have a few different tests by slightly changing the Lua source code, but expects the same output with insensitive order
// This function checks for that specific output..
//
void CheckForPairsThreeTestOutput(std::stringstream& ss)
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

TEST(LuaTest, ForPairs)
{
    VM* vm = VM::Create();
    Auto(vm->Destroy());
    VMOutputInterceptor vmoutput(vm);

    ScriptModule* module = ScriptModule::ParseFromJSON(vm, LoadFile("luatests/for_pairs.lua.json"));
    vm->LaunchScript(module);

    std::string out = vmoutput.GetAndResetStdOut();
    std::string err = vmoutput.GetAndResetStdErr();

    std::stringstream ss(out);
    CheckForPairsThreeTestOutput(ss);

    ReleaseAssert(err == "");
}

TEST(LuaTest, ForPairsPoisonNext)
{
    VM* vm = VM::Create();
    Auto(vm->Destroy());
    VMOutputInterceptor vmoutput(vm);

    ScriptModule* module = ScriptModule::ParseFromJSON(vm, LoadFile("luatests/for_pairs_poison_next.lua.json"));
    vm->LaunchScript(module);

    std::string out = vmoutput.GetAndResetStdOut();
    std::string err = vmoutput.GetAndResetStdErr();

    std::stringstream ss(out);
    std::string line;
    ReleaseAssert(std::getline(ss, line));
    ReleaseAssert(line == "0");
    CheckForPairsThreeTestOutput(ss);

    ReleaseAssert(err == "");
}

TEST(LuaTest, ForPairsPoisonPairs)
{
    RunSimpleLuaTest("luatests/for_pairs_poison_pairs.lua.json");
}

TEST(LuaTest, ForPairsEmpty)
{
    RunSimpleLuaTest("luatests/for_pairs_empty.lua.json");
}

TEST(LuaTest, ForPairsSlowNext)
{
    VM* vm = VM::Create();
    Auto(vm->Destroy());
    VMOutputInterceptor vmoutput(vm);

    ScriptModule* module = ScriptModule::ParseFromJSON(vm, LoadFile("luatests/for_pairs_slow_next.lua.json"));
    vm->LaunchScript(module);

    std::string out = vmoutput.GetAndResetStdOut();
    std::string err = vmoutput.GetAndResetStdErr();

    std::stringstream ss(out);
    CheckForPairsThreeTestOutput(ss);

    ReleaseAssert(err == "");
}

TEST(LuaTest, BooleanAsTableIndex_1)
{
    VM* vm = VM::Create();
    Auto(vm->Destroy());
    VMOutputInterceptor vmoutput(vm);

    ScriptModule* module = ScriptModule::ParseFromJSON(vm, LoadFile("luatests/boolean_as_table_index_1.lua.json"));
    vm->LaunchScript(module);

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
    VM* vm = VM::Create();
    Auto(vm->Destroy());
    VMOutputInterceptor vmoutput(vm);

    ScriptModule* module = ScriptModule::ParseFromJSON(vm, LoadFile("luatests/boolean_as_table_index_2.lua.json"));
    vm->LaunchScript(module);

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

TEST(LuaTest, BooleanAsTableIndex_3)
{
    RunSimpleLuaTest("luatests/boolean_as_table_index_3.lua.json");
}

TEST(LuaTest, ArithmeticSanity)
{
    RunSimpleLuaTest("luatests/arithmetic_sanity.lua.json");
}

TEST(LuaTest, StringConcat)
{
    RunSimpleLuaTest("luatests/string_concat.lua.json");
}

TEST(LuaTest, TableVariadicPut)
{
    RunSimpleLuaTest("luatests/table_variadic_put.lua.json");
}

TEST(LuaBenchmark, NBody)
{
    RunSimpleLuaTest("luatests/n-body.lua.json");
}

TEST(LuaBenchmark, Ack)
{
    VM* vm = VM::Create();
    Auto(vm->Destroy());
    VMOutputInterceptor vmoutput(vm);

    ScriptModule* module = ScriptModule::ParseFromJSON(vm, LoadFile("luatests/ack.lua.json"));

    // This benchmark needs a larger stack
    //
    CoroutineRuntimeContext* rc = vm->GetRootCoroutine();
    rc->m_stackBegin = new TValue[1000000];

    vm->LaunchScript(module);

    std::string out = vmoutput.GetAndResetStdOut();
    std::string err = vmoutput.GetAndResetStdErr();
    AssertIsExpectedOutput(out);
    ReleaseAssert(err == "");
}

TEST(LuaBenchmark, BinaryTrees_1)
{
    RunSimpleLuaTest("luatests/binary-trees-1.lua.json");
}

TEST(LuaBenchmark, BinaryTrees_2)
{
    RunSimpleLuaTest("luatests/binary-trees-2.lua.json");
}

TEST(LuaBenchmark, Fannkuch_Redux)
{
    RunSimpleLuaTest("luatests/fannkuch-redux.lua.json");
}

TEST(LuaBenchmark, Fixpoint_Fact)
{
    RunSimpleLuaTest("luatests/fixpoint-fact.lua.json");
}

TEST(LuaBenchmark, Mandel_NoMetatable)
{
    RunSimpleLuaTest("luatests/mandel-nometatable.lua.json");
}

TEST(LuaBenchmark, Mandel)
{
    RunSimpleLuaTest("luatests/mandel.lua.json");
}

TEST(LuaBenchmark, QuadTree)
{
    RunSimpleLuaTest("luatests/qt.lua.json");
}

TEST(LuaBenchmark, Queen)
{
    RunSimpleLuaTest("luatests/queen.lua.json");
}

TEST(LuaBenchmark, NlgN_Sieve)
{
    RunSimpleLuaTest("luatests/nlgn_sieve.lua.json");
}

TEST(LuaBenchmark, Spectral_Norm)
{
    RunSimpleLuaTest("luatests/spectral-norm.lua.json");
}

TEST(LuaBenchmark, chameneos)
{
    RunSimpleLuaTest("luatests/chameneos.lua.json");
}

TEST(LuaTest, xpcall_1)
{
    RunSimpleLuaTest("luatests/xpcall_1.lua.json");
}

TEST(LuaTest, xpcall_2)
{
    RunSimpleLuaTest("luatests/xpcall_2.lua.json");
}

TEST(LuaTest, xpcall_3)
{
    RunSimpleLuaTest("luatests/xpcall_3.lua.json");
}

TEST(LuaTest, xpcall_4)
{
    RunSimpleLuaTest("luatests/xpcall_4.lua.json");
}

TEST(LuaTest, xpcall_5)
{
    RunSimpleLuaTest("luatests/xpcall_5.lua.json");
}

TEST(LuaTest, xpcall_6)
{
    RunSimpleLuaTest("luatests/xpcall_6.lua.json");
}

TEST(LuaTest, pcall_1)
{
    RunSimpleLuaTest("luatests/pcall_1.lua.json");
}

TEST(LuaTest, pcall_2)
{
    RunSimpleLuaTest("luatests/pcall_2.lua.json");
}

TEST(LuaTest, GetSetMetatable)
{
    RunSimpleLuaTest("luatests/get_set_metatable.lua.json");
}

TEST(LuaTest, getsetmetatable_2)
{
    RunSimpleLuaTest("luatests/getsetmetatable_2.lua.json");
}

TEST(LuaTest, metatable_call_1)
{
    RunSimpleLuaTest("luatests/metatable_call_1.lua.json");
}

TEST(LuaTest, metatable_call_2)
{
    RunSimpleLuaTest("luatests/metatable_call_2.lua.json");
}

TEST(LuaTest, metatable_call_3)
{
    RunSimpleLuaTest("luatests/metatable_call_3.lua.json");
}

TEST(LuaTest, metatable_call_4)
{
    RunSimpleLuaTest("luatests/metatable_call_4.lua.json");
}

TEST(LuaTest, metatable_call_5)
{
    RunSimpleLuaTest("luatests/metatable_call_5.lua.json");
}

TEST(LuaTest, xpcall_metatable)
{
    RunSimpleLuaTest("luatests/xpcall_metatable.lua.json");
}

TEST(LuaTest, pcall_metatable)
{
    RunSimpleLuaTest("luatests/pcall_metatable.lua.json");
}

TEST(LuaTest, metatable_add_1)
{
    RunSimpleLuaTest("luatests/metatable_add_1.lua.json");
}

TEST(LuaTest, metatable_add_2)
{
    RunSimpleLuaTest("luatests/metatable_add_2.lua.json");
}

TEST(LuaTest, metatable_add_3)
{
    RunSimpleLuaTest("luatests/metatable_add_3.lua.json");
}

TEST(LuaTest, metatable_sub)
{
    RunSimpleLuaTest("luatests/metatable_sub.lua.json");
}

TEST(LuaTest, metatable_mul)
{
    RunSimpleLuaTest("luatests/metatable_mul.lua.json");
}

TEST(LuaTest, metatable_div)
{
    RunSimpleLuaTest("luatests/metatable_div.lua.json");
}

TEST(LuaTest, metatable_mod)
{
    RunSimpleLuaTest("luatests/metatable_mod.lua.json");
}

TEST(LuaTest, metatable_pow)
{
    RunSimpleLuaTest("luatests/metatable_pow.lua.json");
}

TEST(LuaTest, metatable_unm)
{
    RunSimpleLuaTest("luatests/metatable_unm.lua.json");
}

TEST(LuaTest, metatable_len)
{
    RunSimpleLuaTest("luatests/metatable_len.lua.json");
}

TEST(LuaTest, metatable_concat)
{
    RunSimpleLuaTest("luatests/metatable_concat.lua.json");
}

TEST(LuaTest, metatable_concat_2)
{
    RunSimpleLuaTest("luatests/metatable_concat_2.lua.json");
}

TEST(LuaTest, metatable_eq_1)
{
    RunSimpleLuaTest("luatests/metatable_eq_1.lua.json");
}

TEST(LuaTest, metatable_eq_2)
{
    RunSimpleLuaTest("luatests/metatable_eq_2.lua.json");
}

TEST(LuaTest, metatable_lt)
{
    RunSimpleLuaTest("luatests/metatable_lt.lua.json");
}

TEST(LuaTest, metatable_le)
{
    RunSimpleLuaTest("luatests/metatable_le.lua.json");
}

TEST(LuaTest, metatable_eq_3)
{
    RunSimpleLuaTest("luatests/metatable_eq_3.lua.json");
}

TEST(LuaTest, getbyid_metatable)
{
    RunSimpleLuaTest("luatests/getbyid_metatable.lua.json");
}

TEST(LuaTest, globalget_metatable)
{
    RunSimpleLuaTest("luatests/globalget_metatable.lua.json");
}

TEST(LuaTest, getbyval_metatable)
{
    RunSimpleLuaTest("luatests/getbyval_metatable.lua.json");
}

TEST(LuaTest, getbyintegerval_metatable)
{
    RunSimpleLuaTest("luatests/getbyintegerval_metatable.lua.json");
}

TEST(LuaTest, rawget_and_rawset)
{
    RunSimpleLuaTest("luatests/rawget_rawset.lua.json");
}

TEST(LuaTest, putbyid_metatable)
{
    RunSimpleLuaTest("luatests/putbyid_metatable.lua.json");
}

TEST(LuaTest, globalput_metatable)
{
    RunSimpleLuaTest("luatests/globalput_metatable.lua.json");
}

TEST(LuaTest, putbyintegerval_metatable)
{
    RunSimpleLuaTest("luatests/putbyintegerval_metatable.lua.json");
}

TEST(LuaTest, putbyval_metatable)
{
    RunSimpleLuaTest("luatests/putbyval_metatable.lua.json");
}

TEST(LuaTest, GlobalGetInterpreterIC)
{
    RunSimpleLuaTest("luatests/globalget_interpreter_ic.lua.json");
}

TEST(LuaTest, TableGetByIdInterpreterIC)
{
    RunSimpleLuaTest("luatests/table_getbyid_interpreter_ic.lua.json");
}

TEST(LuaTest, GetByImmInterpreterIC_1)
{
    RunSimpleLuaTest("luatests/get_by_imm_interpreter_ic_1.lua.json");
}

TEST(LuaTest, GetByImmInterpreterIC_2)
{
    RunSimpleLuaTest("luatests/get_by_imm_interpreter_ic_2.lua.json");
}

TEST(LuaTest, GetByValInterpreterIC_1)
{
    RunSimpleLuaTest("luatests/get_by_val_interpreter_ic_1.lua.json");
}

TEST(LuaTest, GetByValInterpreterIC_2)
{
    RunSimpleLuaTest("luatests/get_by_val_interpreter_ic_2.lua.json");
}

TEST(LuaTest, GetByValInterpreterIC_3)
{
    RunSimpleLuaTest("luatests/get_by_val_interpreter_ic_3.lua.json");
}

TEST(LuaTest, GetByValInterpreterIC_4)
{
    RunSimpleLuaTest("luatests/get_by_val_interpreter_ic_4.lua.json");
}

TEST(LuaTest, GetByValInterpreterIC_5)
{
    RunSimpleLuaTest("luatests/get_by_val_interpreter_ic_5.lua.json");
}

TEST(LuaTest, GetByValInterpreterIC_6)
{
    RunSimpleLuaTest("luatests/get_by_val_interpreter_ic_6.lua.json");
}

TEST(LuaTest, GlobalPutInterpreterIC_1)
{
    RunSimpleLuaTest("luatests/globalput_interpreter_ic_1.lua.json");
}

TEST(LuaTest, GlobalPutInterpreterIC_2)
{
    RunSimpleLuaTest("luatests/globalput_interpreter_ic_2.lua.json");
}

TEST(LuaTest, GlobalPutInterpreterIC_3)
{
    RunSimpleLuaTest("luatests/globalput_interpreter_ic_3.lua.json");
}

TEST(LuaTest, GlobalPutInterpreterIC_4)
{
    RunSimpleLuaTest("luatests/globalput_interpreter_ic_4.lua.json");
}

TEST(LuaTest, PutByIdInterpreterIC_1)
{
    RunSimpleLuaTest("luatests/putbyid_interpreter_ic_1.lua.json");
}

TEST(LuaTest, PutByIdInterpreterIC_2)
{
    RunSimpleLuaTest("luatests/putbyid_interpreter_ic_2.lua.json");
}

TEST(LuaTest, PutByIdInterpreterIC_3)
{
    RunSimpleLuaTest("luatests/putbyid_interpreter_ic_3.lua.json");
}

TEST(LuaTest, PutByIdInterpreterIC_4)
{
    RunSimpleLuaTest("luatests/putbyid_interpreter_ic_4.lua.json");
}

TEST(LuaTest, PutByIdInterpreterIC_5)
{
    RunSimpleLuaTest("luatests/putbyid_interpreter_ic_5.lua.json");
}

TEST(LuaTest, PutByIdInterpreterIC_6)
{
    RunSimpleLuaTest("luatests/putbyid_interpreter_ic_6.lua.json");
}

TEST(LuaTest, PutByIdInterpreterIC_7)
{
    RunSimpleLuaTest("luatests/putbyid_interpreter_ic_7.lua.json");
}

TEST(LuaTest, PutByIdInterpreterIC_8)
{
    RunSimpleLuaTest("luatests/putbyid_interpreter_ic_8.lua.json");
}

TEST(LuaTest, PutByImmInterpreterIC_1)
{
    RunSimpleLuaTest("luatests/putbyimm_interpreter_ic_1.lua.json");
}

TEST(LuaTest, PutByImmInterpreterIC_2)
{
    RunSimpleLuaTest("luatests/putbyimm_interpreter_ic_2.lua.json");
}

TEST(LuaTest, PutByImmInterpreterIC_3)
{
    RunSimpleLuaTest("luatests/putbyimm_interpreter_ic_3.lua.json");
}

TEST(LuaTest, PutByImmInterpreterIC_4)
{
    RunSimpleLuaTest("luatests/putbyimm_interpreter_ic_4.lua.json");
}

TEST(LuaTest, PutByValInterpreterIC_1)
{
    RunSimpleLuaTest("luatests/putbyval_interpreter_ic_1.lua.json");
}

TEST(LuaTest, PutByValInterpreterIC_2)
{
    RunSimpleLuaTest("luatests/putbyval_interpreter_ic_2.lua.json");
}

TEST(LuaTest, PutByValInterpreterIC_3)
{
    RunSimpleLuaTest("luatests/putbyval_interpreter_ic_3.lua.json");
}

TEST(LuaTest, PutByValInterpreterIC_4)
{
    RunSimpleLuaTest("luatests/putbyval_interpreter_ic_4.lua.json");
}

TEST(LuaTest, PutByValInterpreterIC_5)
{
    RunSimpleLuaTest("luatests/putbyval_interpreter_ic_5.lua.json");
}

TEST(LuaLib, base_assert)
{
    RunSimpleLuaTest("luatests/lib_base_assert.lua.json");
}

TEST(LuaLib, base_assert_2)
{
    RunSimpleLuaTest("luatests/base_lib_assert_2.lua.json");
}

TEST(LuaLib, RawsetReturnsOriginalTable)
{
    RunSimpleLuaTest("luatests/rawset_returns_original_table.lua.json");
}

TEST(LuaLib, InitEnvironment)
{
    RunSimpleLuaTest("luatests/init_environment.lua.json");
}

TEST(LuaLib, math_sqrt)
{
    RunSimpleLuaTest("luatests/math_sqrt.lua.json");
}

TEST(LuaLib, math_constants)
{
    RunSimpleLuaTest("luatests/math_constants.lua.json");
}

TEST(LuaLib, math_unary_fn)
{
    RunSimpleLuaTest("luatests/math_lib_unary.lua.json");
}

TEST(LuaLib, math_misc_fn)
{
    RunSimpleLuaTest("luatests/math_lib_misc.lua.json");
}

TEST(LuaLib, math_min_max)
{
    RunSimpleLuaTest("luatests/math_lib_min_max.lua.json");
}

TEST(LuaLib, math_random)
{
    RunSimpleLuaTest("luatests/math_lib_random.lua.json");
}

TEST(LuaLib, coroutine_1)
{
    RunSimpleLuaTest("luatests/coroutine_1.lua.json");
}

TEST(LuaLib, coroutine_2)
{
    RunSimpleLuaTest("luatests/coroutine_2.lua.json");
}

TEST(LuaLib, coroutine_3)
{
    RunSimpleLuaTest("luatests/coroutine_3.lua.json");
}

TEST(LuaLib, coroutine_4)
{
    RunSimpleLuaTest("luatests/coroutine_4.lua.json");
}

TEST(LuaLib, coroutine_5)
{
    RunSimpleLuaTest("luatests/coroutine_5.lua.json");
}

TEST(LuaLib, coroutine_ring)
{
    RunSimpleLuaTest("luatests/coroutine_ring.lua.json");
}

TEST(LuaLib, coroutine_error_1)
{
    RunSimpleLuaTest("luatests/coroutine_error_1.lua.json");
}

TEST(LuaLib, coroutine_error_2)
{
    RunSimpleLuaTest("luatests/coroutine_error_2.lua.json");
}

TEST(LuaLib, coroutine_error_3)
{
    RunSimpleLuaTest("luatests/coroutine_error_3.lua.json");
}

TEST(LuaLib, base_ipairs)
{
    RunSimpleLuaTest("luatests/base_lib_ipairs.lua.json");
}

TEST(LuaLib, base_ipairs_2)
{
    RunSimpleLuaTest("luatests/base_lib_ipairs_2.lua.json");
}

TEST(LuaLib, base_rawequal)
{
    RunSimpleLuaTest("luatests/base_lib_rawequal.lua.json");
}

TEST(LuaLib, base_select_1)
{
    RunSimpleLuaTest("luatests/base_lib_select_1.lua.json");
}

TEST(LuaLib, base_select_2)
{
    RunSimpleLuaTest("luatests/base_lib_select_2.lua.json");
}

TEST(LuaLib, base_lib_type)
{
    RunSimpleLuaTest("luatests/base_lib_type.lua.json");
}

TEST(LuaLib, base_lib_next)
{
    RunSimpleLuaTest("luatests/base_lib_next.lua.json");
}

TEST(LuaLib, base_lib_pairs)
{
    RunSimpleLuaTest("luatests/base_lib_pairs.lua.json");
}

TEST(LuaLib, base_lib_pcall)
{
    RunSimpleLuaTest("luatests/base_lib_pcall.lua.json");
}

TEST(LuaLib, base_lib_tonumber)
{
    RunSimpleLuaTest("luatests/base_lib_tonumber.lua.json");
}

TEST(LuaLib, base_lib_tonumber_2)
{
    RunSimpleLuaTest("luatests/base_lib_tonumber_2.lua.json");
}

TEST(LuaLib, base_lib_tostring)
{
    RunSimpleLuaTest("luatests/base_lib_tostring.lua.json");
}

TEST(LuaLib, base_lib_tostring_2)
{
    RunSimpleLuaTest("luatests/base_lib_tostring_2.lua.json");
}

TEST(LuaLib, base_lib_tostring_3)
{
    RunSimpleLuaTest("luatests/base_lib_tostring_3.lua.json");
}

TEST(LuaLib, base_lib_tostring_4)
{
    RunSimpleLuaTest("luatests/base_lib_tostring_4.lua.json");
}

TEST(LuaLib, base_lib_tostring_5)
{
    RunSimpleLuaTest("luatests/base_lib_tostring_5.lua.json");
}

TEST(LuaLib, base_lib_tostring_6)
{
    RunSimpleLuaTest("luatests/base_lib_tostring_6.lua.json");
}

TEST(LuaLib, base_lib_print)
{
    RunSimpleLuaTest("luatests/base_lib_print.lua.json");
}

TEST(LuaLib, base_lib_print_2)
{
    RunSimpleLuaTest("luatests/base_lib_print_2.lua.json");
}

TEST(LuaLib, base_lib_unpack)
{
    RunSimpleLuaTest("luatests/base_lib_unpack.lua.json");
}

}   // anonymous namespace
