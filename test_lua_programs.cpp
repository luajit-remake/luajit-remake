#include <fstream>
#include "runtime_utils.h"
#include "gtest/gtest.h"
#include "json_utils.h"
#include "test_util_helper.h"
#include "test_vm_utils.h"
#include "lj_parser_wrapper.h"

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

std::unique_ptr<ScriptModule> ParseLuaScriptOrFail(const std::string& filename)
{
    ReleaseAssert(filename.ends_with(".lua"));
    VM* vm = VM::GetActiveVMForCurrentThread();
    std::string content = LoadFile(filename);
    ParseResult res = ParseLuaScript(vm->GetRootCoroutine(), content);
    if (res.m_scriptModule.get() == nullptr)
    {
        fprintf(stderr, "Parsing file '%s' failed with error = %d, token = %d, errmsg = %s\n",
                filename.c_str(), static_cast<int>(res.errorCode), static_cast<int>(res.errorTok), (res.errMsg ? res.errMsg : "(none)"));
        abort();
    }
    return std::move(res.m_scriptModule);
}

[[maybe_unused]] std::unique_ptr<ScriptModule> ParseLuaScriptOrJsonBytecodeDumpOrFail(const std::string& filename)
{
    VM* vm = VM::GetActiveVMForCurrentThread();
    if (filename.ends_with(".json"))
    {
        return ScriptModule::LegacyParseScriptFromJSONBytecodeDump(vm, vm->GetRootGlobalObject(), LoadFile(filename));
    }
    else
    {
        ReleaseAssert(filename.ends_with(".lua"));
        return ParseLuaScriptOrFail(filename);
    }
}

void RunSimpleLuaTest(const std::string& filename)
{
    VM* vm = VM::Create();
    Auto(vm->Destroy());
    VMOutputInterceptor vmoutput(vm);

    std::unique_ptr<ScriptModule> module = ParseLuaScriptOrFail(filename);
    vm->LaunchScript(module.get());

    std::string out = vmoutput.GetAndResetStdOut();
    std::string err = vmoutput.GetAndResetStdErr();
    AssertIsExpectedOutput(out);
    ReleaseAssert(err == "");
}

TEST(LuaTest, Fib)
{
    RunSimpleLuaTest("luatests/fib.lua");
}

TEST(LuaTest, TestPrint)
{
    VM* vm = VM::Create();
    Auto(vm->Destroy());
    VMOutputInterceptor vmoutput(vm);

    std::unique_ptr<ScriptModule> module = ParseLuaScriptOrFail("luatests/test_print.lua");
    vm->LaunchScript(module.get());

    std::string out = vmoutput.GetAndResetStdOut();
    std::string err = vmoutput.GetAndResetStdErr();

    std::string expectedOut = "0.2\t3\tfalse\ttrue\tnil\tabc\tfunction: 0x";
    ReleaseAssert(out.starts_with(expectedOut));
    ReleaseAssert(err == "");
}

TEST(LuaTest, TestTableDup)
{
    RunSimpleLuaTest("luatests/table_dup.lua");
}

TEST(LuaTest, TestTableDup2)
{
    RunSimpleLuaTest("luatests/table_dup2.lua");
}

TEST(LuaTest, TestTableDup3)
{
    RunSimpleLuaTest("luatests/table_dup3.lua");
}

TEST(LuaTest, TestTableSizeHint)
{
    VM* vm = VM::Create();
    Auto(vm->Destroy());
    VMOutputInterceptor vmoutput(vm);

    std::unique_ptr<ScriptModule> module = ParseLuaScriptOrFail("luatests/table_size_hint.lua");
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

TEST(LuaTest, Upvalue)
{
    RunSimpleLuaTest("luatests/upvalue.lua");
}

TEST(LuaTest, Fib_upvalue)
{
    RunSimpleLuaTest("luatests/fib_upvalue.lua");
}

TEST(LuaTest, LinearSieve)
{
    RunSimpleLuaTest("luatests/linear_sieve.lua");
}

TEST(LuaTest, NaNEdgeCase)
{
    RunSimpleLuaTest("luatests/nan_edge_case.lua");
}

TEST(LuaTest, ForLoopCoercion)
{
    RunSimpleLuaTest("luatests/for_loop_coercion.lua");
}

TEST(LuaTest, ForLoopEdgeCases)
{
    RunSimpleLuaTest("luatests/for_loop_edge_cases.lua");
}

TEST(LuaTest, PrimitiveConstants)
{
    RunSimpleLuaTest("luatests/primitive_constant.lua");
}

TEST(LuaTest, LogicalOpSanity)
{
    RunSimpleLuaTest("luatests/logical_op_sanity.lua");
}

TEST(LuaTest, PositiveAndNegativeInf)
{
    RunSimpleLuaTest("luatests/pos_and_neg_inf.lua");
}

TEST(LuaTest, LogicalNot)
{
    RunSimpleLuaTest("luatests/logical_not.lua");
}

TEST(LuaTest, LengthOperator)
{
    RunSimpleLuaTest("luatests/length_operator.lua");
}

TEST(LuaTest, TailCall)
{
    VM* vm = VM::Create();
    Auto(vm->Destroy());
    VMOutputInterceptor vmoutput(vm);

    std::unique_ptr<ScriptModule> module = ParseLuaScriptOrFail("luatests/tail_call.lua");

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
    VM* vm = VM::Create();
    Auto(vm->Destroy());
    VMOutputInterceptor vmoutput(vm);

    std::unique_ptr<ScriptModule> module = ParseLuaScriptOrFail("luatests/variadic_tail_call_1.lua");

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
    VM* vm = VM::Create();
    Auto(vm->Destroy());
    VMOutputInterceptor vmoutput(vm);

    std::unique_ptr<ScriptModule> module = ParseLuaScriptOrFail("luatests/variadic_tail_call_2.lua");

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
    VM* vm = VM::Create();
    Auto(vm->Destroy());
    VMOutputInterceptor vmoutput(vm);

    std::unique_ptr<ScriptModule> module = ParseLuaScriptOrFail("luatests/variadic_tail_call_3.lua");

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

TEST(LuaTest, OpcodeKNIL)
{
    RunSimpleLuaTest("luatests/test_knil.lua");
}

TEST(LuaTest, IterativeForLoop)
{
    RunSimpleLuaTest("luatests/iter_for.lua");
}

TEST(LuaTest, NegativeZeroAsIndex)
{
    RunSimpleLuaTest("luatests/negative_zero_as_index.lua");
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

    std::unique_ptr<ScriptModule> module = ParseLuaScriptOrFail("luatests/for_pairs.lua");
    vm->LaunchScript(module.get());

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

    std::unique_ptr<ScriptModule> module = ParseLuaScriptOrFail("luatests/for_pairs_poison_next.lua");
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

TEST(LuaTest, ForPairsPoisonPairs)
{
    RunSimpleLuaTest("luatests/for_pairs_poison_pairs.lua");
}

TEST(LuaTest, ForPairsEmpty)
{
    RunSimpleLuaTest("luatests/for_pairs_empty.lua");
}

TEST(LuaTest, ForPairsSlowNext)
{
    VM* vm = VM::Create();
    Auto(vm->Destroy());
    VMOutputInterceptor vmoutput(vm);

    std::unique_ptr<ScriptModule> module = ParseLuaScriptOrFail("luatests/for_pairs_slow_next.lua");
    vm->LaunchScript(module.get());

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

    std::unique_ptr<ScriptModule> module = ParseLuaScriptOrFail("luatests/boolean_as_table_index_1.lua");
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
    VM* vm = VM::Create();
    Auto(vm->Destroy());
    VMOutputInterceptor vmoutput(vm);

    std::unique_ptr<ScriptModule> module = ParseLuaScriptOrFail("luatests/boolean_as_table_index_2.lua");
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

TEST(LuaTest, BooleanAsTableIndex_3)
{
    RunSimpleLuaTest("luatests/boolean_as_table_index_3.lua");
}

TEST(LuaTest, ArithmeticSanity)
{
    RunSimpleLuaTest("luatests/arithmetic_sanity.lua");
}

TEST(LuaTest, StringConcat)
{
    RunSimpleLuaTest("luatests/string_concat.lua");
}

TEST(LuaTest, TableVariadicPut)
{
    RunSimpleLuaTest("luatests/table_variadic_put.lua");
}

TEST(LuaTest, TableVariadicPut_2)
{
    RunSimpleLuaTest("luatests/table_variadic_put_2.lua");
}

TEST(LuaBenchmark, NBody)
{
    RunSimpleLuaTest("luatests/n-body.lua");
}

TEST(LuaBenchmark, Ack)
{
    VM* vm = VM::Create();
    Auto(vm->Destroy());
    VMOutputInterceptor vmoutput(vm);

    std::unique_ptr<ScriptModule> module = ParseLuaScriptOrFail("luatests/ack.lua");

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

TEST(LuaBenchmark, BinaryTrees_1)
{
    RunSimpleLuaTest("luatests/binary-trees-1.lua");
}

TEST(LuaBenchmark, BinaryTrees_2)
{
    RunSimpleLuaTest("luatests/binary-trees-2.lua");
}

TEST(LuaBenchmark, Fannkuch_Redux)
{
    RunSimpleLuaTest("luatests/fannkuch-redux.lua");
}

TEST(LuaBenchmark, Fixpoint_Fact)
{
    RunSimpleLuaTest("luatests/fixpoint-fact.lua");
}

TEST(LuaBenchmark, Mandel_NoMetatable)
{
    RunSimpleLuaTest("luatests/mandel-nometatable.lua");
}

TEST(LuaBenchmark, Mandel)
{
    RunSimpleLuaTest("luatests/mandel.lua");
}

TEST(LuaBenchmark, QuadTree)
{
    RunSimpleLuaTest("luatests/qt.lua");
}

TEST(LuaBenchmark, Queen)
{
    RunSimpleLuaTest("luatests/queen.lua");
}

TEST(LuaBenchmark, NlgN_Sieve)
{
    RunSimpleLuaTest("luatests/nlgn_sieve.lua");
}

TEST(LuaBenchmark, Spectral_Norm)
{
    RunSimpleLuaTest("luatests/spectral-norm.lua");
}

TEST(LuaBenchmark, chameneos)
{
    RunSimpleLuaTest("luatests/chameneos.lua");
}

TEST(LuaTest, xpcall_1)
{
    RunSimpleLuaTest("luatests/xpcall_1.lua");
}

TEST(LuaTest, xpcall_2)
{
    RunSimpleLuaTest("luatests/xpcall_2.lua");
}

TEST(LuaTest, xpcall_3)
{
    RunSimpleLuaTest("luatests/xpcall_3.lua");
}

TEST(LuaTest, xpcall_4)
{
    RunSimpleLuaTest("luatests/xpcall_4.lua");
}

TEST(LuaTest, xpcall_5)
{
    RunSimpleLuaTest("luatests/xpcall_5.lua");
}

TEST(LuaTest, xpcall_6)
{
    RunSimpleLuaTest("luatests/xpcall_6.lua");
}

TEST(LuaTest, pcall_1)
{
    RunSimpleLuaTest("luatests/pcall_1.lua");
}

TEST(LuaTest, pcall_2)
{
    RunSimpleLuaTest("luatests/pcall_2.lua");
}

TEST(LuaTest, GetSetMetatable)
{
    RunSimpleLuaTest("luatests/get_set_metatable.lua");
}

TEST(LuaTest, getsetmetatable_2)
{
    RunSimpleLuaTest("luatests/getsetmetatable_2.lua");
}

TEST(LuaTest, metatable_call_1)
{
    RunSimpleLuaTest("luatests/metatable_call_1.lua");
}

TEST(LuaTest, metatable_call_2)
{
    RunSimpleLuaTest("luatests/metatable_call_2.lua");
}

TEST(LuaTest, metatable_call_3)
{
    RunSimpleLuaTest("luatests/metatable_call_3.lua");
}

TEST(LuaTest, metatable_call_4)
{
    RunSimpleLuaTest("luatests/metatable_call_4.lua");
}

TEST(LuaTest, metatable_call_5)
{
    RunSimpleLuaTest("luatests/metatable_call_5.lua");
}

TEST(LuaTest, xpcall_metatable)
{
    RunSimpleLuaTest("luatests/xpcall_metatable.lua");
}

TEST(LuaTest, pcall_metatable)
{
    RunSimpleLuaTest("luatests/pcall_metatable.lua");
}

TEST(LuaTest, metatable_add_1)
{
    RunSimpleLuaTest("luatests/metatable_add_1.lua");
}

TEST(LuaTest, metatable_add_2)
{
    RunSimpleLuaTest("luatests/metatable_add_2.lua");
}

TEST(LuaTest, metatable_add_3)
{
    RunSimpleLuaTest("luatests/metatable_add_3.lua");
}

TEST(LuaTest, metatable_sub)
{
    RunSimpleLuaTest("luatests/metatable_sub.lua");
}

TEST(LuaTest, metatable_mul)
{
    RunSimpleLuaTest("luatests/metatable_mul.lua");
}

TEST(LuaTest, metatable_div)
{
    RunSimpleLuaTest("luatests/metatable_div.lua");
}

TEST(LuaTest, metatable_mod)
{
    RunSimpleLuaTest("luatests/metatable_mod.lua");
}

TEST(LuaTest, metatable_pow)
{
    RunSimpleLuaTest("luatests/metatable_pow.lua");
}

TEST(LuaTest, metatable_unm)
{
    RunSimpleLuaTest("luatests/metatable_unm.lua");
}

TEST(LuaTest, metatable_len)
{
    RunSimpleLuaTest("luatests/metatable_len.lua");
}

TEST(LuaTest, metatable_concat)
{
    RunSimpleLuaTest("luatests/metatable_concat.lua");
}

TEST(LuaTest, metatable_concat_2)
{
    RunSimpleLuaTest("luatests/metatable_concat_2.lua");
}

TEST(LuaTest, metatable_eq_1)
{
    RunSimpleLuaTest("luatests/metatable_eq_1.lua");
}

TEST(LuaTest, metatable_eq_2)
{
    RunSimpleLuaTest("luatests/metatable_eq_2.lua");
}

TEST(LuaTest, metatable_lt)
{
    RunSimpleLuaTest("luatests/metatable_lt.lua");
}

TEST(LuaTest, metatable_le)
{
    RunSimpleLuaTest("luatests/metatable_le.lua");
}

TEST(LuaTest, metatable_eq_3)
{
    RunSimpleLuaTest("luatests/metatable_eq_3.lua");
}

TEST(LuaTest, getbyid_metatable)
{
    RunSimpleLuaTest("luatests/getbyid_metatable.lua");
}

TEST(LuaTest, globalget_metatable)
{
    RunSimpleLuaTest("luatests/globalget_metatable.lua");
}

TEST(LuaTest, getbyval_metatable)
{
    RunSimpleLuaTest("luatests/getbyval_metatable.lua");
}

TEST(LuaTest, getbyintegerval_metatable)
{
    RunSimpleLuaTest("luatests/getbyintegerval_metatable.lua");
}

TEST(LuaTest, rawget_and_rawset)
{
    RunSimpleLuaTest("luatests/rawget_rawset.lua");
}

TEST(LuaTest, putbyid_metatable)
{
    RunSimpleLuaTest("luatests/putbyid_metatable.lua");
}

TEST(LuaTest, globalput_metatable)
{
    RunSimpleLuaTest("luatests/globalput_metatable.lua");
}

TEST(LuaTest, putbyintegerval_metatable)
{
    RunSimpleLuaTest("luatests/putbyintegerval_metatable.lua");
}

TEST(LuaTest, putbyval_metatable)
{
    RunSimpleLuaTest("luatests/putbyval_metatable.lua");
}

TEST(LuaTest, GlobalGetInterpreterIC)
{
    RunSimpleLuaTest("luatests/globalget_interpreter_ic.lua");
}

TEST(LuaTest, TableGetByIdInterpreterIC)
{
    RunSimpleLuaTest("luatests/table_getbyid_interpreter_ic.lua");
}

TEST(LuaTest, GetByImmInterpreterIC_1)
{
    RunSimpleLuaTest("luatests/get_by_imm_interpreter_ic_1.lua");
}

TEST(LuaTest, GetByImmInterpreterIC_2)
{
    RunSimpleLuaTest("luatests/get_by_imm_interpreter_ic_2.lua");
}

TEST(LuaTest, GetByValInterpreterIC_1)
{
    RunSimpleLuaTest("luatests/get_by_val_interpreter_ic_1.lua");
}

TEST(LuaTest, GetByValInterpreterIC_2)
{
    RunSimpleLuaTest("luatests/get_by_val_interpreter_ic_2.lua");
}

TEST(LuaTest, GetByValInterpreterIC_3)
{
    RunSimpleLuaTest("luatests/get_by_val_interpreter_ic_3.lua");
}

TEST(LuaTest, GetByValInterpreterIC_4)
{
    RunSimpleLuaTest("luatests/get_by_val_interpreter_ic_4.lua");
}

TEST(LuaTest, GetByValInterpreterIC_5)
{
    RunSimpleLuaTest("luatests/get_by_val_interpreter_ic_5.lua");
}

TEST(LuaTest, GetByValInterpreterIC_6)
{
    RunSimpleLuaTest("luatests/get_by_val_interpreter_ic_6.lua");
}

TEST(LuaTest, GlobalPutInterpreterIC_1)
{
    RunSimpleLuaTest("luatests/globalput_interpreter_ic_1.lua");
}

TEST(LuaTest, GlobalPutInterpreterIC_2)
{
    RunSimpleLuaTest("luatests/globalput_interpreter_ic_2.lua");
}

TEST(LuaTest, GlobalPutInterpreterIC_3)
{
    RunSimpleLuaTest("luatests/globalput_interpreter_ic_3.lua");
}

TEST(LuaTest, GlobalPutInterpreterIC_4)
{
    RunSimpleLuaTest("luatests/globalput_interpreter_ic_4.lua");
}

TEST(LuaTest, PutByIdInterpreterIC_1)
{
    RunSimpleLuaTest("luatests/putbyid_interpreter_ic_1.lua");
}

TEST(LuaTest, PutByIdInterpreterIC_2)
{
    RunSimpleLuaTest("luatests/putbyid_interpreter_ic_2.lua");
}

TEST(LuaTest, PutByIdInterpreterIC_3)
{
    RunSimpleLuaTest("luatests/putbyid_interpreter_ic_3.lua");
}

TEST(LuaTest, PutByIdInterpreterIC_4)
{
    RunSimpleLuaTest("luatests/putbyid_interpreter_ic_4.lua");
}

TEST(LuaTest, PutByIdInterpreterIC_5)
{
    RunSimpleLuaTest("luatests/putbyid_interpreter_ic_5.lua");
}

TEST(LuaTest, PutByIdInterpreterIC_6)
{
    RunSimpleLuaTest("luatests/putbyid_interpreter_ic_6.lua");
}

TEST(LuaTest, PutByIdInterpreterIC_7)
{
    RunSimpleLuaTest("luatests/putbyid_interpreter_ic_7.lua");
}

TEST(LuaTest, PutByIdInterpreterIC_8)
{
    RunSimpleLuaTest("luatests/putbyid_interpreter_ic_8.lua");
}

TEST(LuaTest, PutByImmInterpreterIC_1)
{
    RunSimpleLuaTest("luatests/putbyimm_interpreter_ic_1.lua");
}

TEST(LuaTest, PutByImmInterpreterIC_2)
{
    RunSimpleLuaTest("luatests/putbyimm_interpreter_ic_2.lua");
}

TEST(LuaTest, PutByImmInterpreterIC_3)
{
    RunSimpleLuaTest("luatests/putbyimm_interpreter_ic_3.lua");
}

TEST(LuaTest, PutByImmInterpreterIC_4)
{
    RunSimpleLuaTest("luatests/putbyimm_interpreter_ic_4.lua");
}

TEST(LuaTest, PutByValInterpreterIC_1)
{
    RunSimpleLuaTest("luatests/putbyval_interpreter_ic_1.lua");
}

TEST(LuaTest, PutByValInterpreterIC_2)
{
    RunSimpleLuaTest("luatests/putbyval_interpreter_ic_2.lua");
}

TEST(LuaTest, PutByValInterpreterIC_3)
{
    RunSimpleLuaTest("luatests/putbyval_interpreter_ic_3.lua");
}

TEST(LuaTest, PutByValInterpreterIC_4)
{
    RunSimpleLuaTest("luatests/putbyval_interpreter_ic_4.lua");
}

TEST(LuaTest, PutByValInterpreterIC_5)
{
    RunSimpleLuaTest("luatests/putbyval_interpreter_ic_5.lua");
}

TEST(LuaTest, istc_conditional_copy)
{
    RunSimpleLuaTest("luatests/istc_conditional_copy.lua");
}

TEST(LuaTest, isfc_conditional_copy)
{
    RunSimpleLuaTest("luatests/isfc_conditional_copy.lua");
}

TEST(LuaTest, le_use_lt_metamethod)
{
    RunSimpleLuaTest("luatests/le_use_lt_metamethod.lua");
}

TEST(LuaLib, base_assert)
{
    RunSimpleLuaTest("luatests/lib_base_assert.lua");
}

TEST(LuaLib, base_assert_2)
{
    RunSimpleLuaTest("luatests/base_lib_assert_2.lua");
}

TEST(LuaLib, RawsetReturnsOriginalTable)
{
    RunSimpleLuaTest("luatests/rawset_returns_original_table.lua");
}

TEST(LuaLib, InitEnvironment)
{
    RunSimpleLuaTest("luatests/init_environment.lua");
}

TEST(LuaLib, math_sqrt)
{
    RunSimpleLuaTest("luatests/math_sqrt.lua");
}

TEST(LuaLib, math_constants)
{
    RunSimpleLuaTest("luatests/math_constants.lua");
}

TEST(LuaLib, math_unary_fn)
{
    RunSimpleLuaTest("luatests/math_lib_unary.lua");
}

TEST(LuaLib, math_misc_fn)
{
    RunSimpleLuaTest("luatests/math_lib_misc.lua");
}

TEST(LuaLib, math_min_max)
{
    RunSimpleLuaTest("luatests/math_lib_min_max.lua");
}

TEST(LuaLib, math_random)
{
    RunSimpleLuaTest("luatests/math_lib_random.lua");
}

TEST(LuaLib, coroutine_1)
{
    RunSimpleLuaTest("luatests/coroutine_1.lua");
}

TEST(LuaLib, coroutine_2)
{
    RunSimpleLuaTest("luatests/coroutine_2.lua");
}

TEST(LuaLib, coroutine_3)
{
    RunSimpleLuaTest("luatests/coroutine_3.lua");
}

TEST(LuaLib, coroutine_4)
{
    RunSimpleLuaTest("luatests/coroutine_4.lua");
}

TEST(LuaLib, coroutine_5)
{
    RunSimpleLuaTest("luatests/coroutine_5.lua");
}

TEST(LuaLib, coroutine_ring)
{
    RunSimpleLuaTest("luatests/coroutine_ring.lua");
}

TEST(LuaLib, coroutine_error_1)
{
    RunSimpleLuaTest("luatests/coroutine_error_1.lua");
}

TEST(LuaLib, coroutine_error_2)
{
    RunSimpleLuaTest("luatests/coroutine_error_2.lua");
}

TEST(LuaLib, coroutine_error_3)
{
    RunSimpleLuaTest("luatests/coroutine_error_3.lua");
}

TEST(LuaLib, base_ipairs)
{
    RunSimpleLuaTest("luatests/base_lib_ipairs.lua");
}

TEST(LuaLib, base_ipairs_2)
{
    RunSimpleLuaTest("luatests/base_lib_ipairs_2.lua");
}

TEST(LuaLib, base_rawequal)
{
    RunSimpleLuaTest("luatests/base_lib_rawequal.lua");
}

TEST(LuaLib, base_select_1)
{
    RunSimpleLuaTest("luatests/base_lib_select_1.lua");
}

TEST(LuaLib, base_select_2)
{
    RunSimpleLuaTest("luatests/base_lib_select_2.lua");
}

TEST(LuaLib, base_lib_type)
{
    RunSimpleLuaTest("luatests/base_lib_type.lua");
}

TEST(LuaLib, base_lib_next)
{
    RunSimpleLuaTest("luatests/base_lib_next.lua");
}

TEST(LuaLib, base_lib_pairs)
{
    RunSimpleLuaTest("luatests/base_lib_pairs.lua");
}

TEST(LuaLib, base_lib_pcall)
{
    RunSimpleLuaTest("luatests/base_lib_pcall.lua");
}

TEST(LuaLib, base_lib_tonumber)
{
    RunSimpleLuaTest("luatests/base_lib_tonumber.lua");
}

TEST(LuaLib, base_lib_tonumber_2)
{
    RunSimpleLuaTest("luatests/base_lib_tonumber_2.lua");
}

TEST(LuaLib, base_lib_tostring)
{
    RunSimpleLuaTest("luatests/base_lib_tostring.lua");
}

TEST(LuaLib, base_lib_tostring_2)
{
    RunSimpleLuaTest("luatests/base_lib_tostring_2.lua");
}

TEST(LuaLib, base_lib_tostring_3)
{
    RunSimpleLuaTest("luatests/base_lib_tostring_3.lua");
}

TEST(LuaLib, base_lib_tostring_4)
{
    RunSimpleLuaTest("luatests/base_lib_tostring_4.lua");
}

TEST(LuaLib, base_lib_tostring_5)
{
    RunSimpleLuaTest("luatests/base_lib_tostring_5.lua");
}

TEST(LuaLib, base_lib_tostring_6)
{
    RunSimpleLuaTest("luatests/base_lib_tostring_6.lua");
}

TEST(LuaLib, base_lib_print)
{
    RunSimpleLuaTest("luatests/base_lib_print.lua");
}

TEST(LuaLib, base_lib_print_2)
{
    RunSimpleLuaTest("luatests/base_lib_print_2.lua");
}

TEST(LuaLib, base_lib_unpack)
{
    RunSimpleLuaTest("luatests/base_lib_unpack.lua");
}

TEST(LuaLib, string_lib_byte)
{
    RunSimpleLuaTest("luatests/string_lib_byte.lua");
}

TEST(LuaLib, string_lib_byte_2)
{
    RunSimpleLuaTest("luatests/string_lib_byte_2.lua");
}

TEST(LuaLib, string_lib_char)
{
    RunSimpleLuaTest("luatests/string_lib_char.lua");
}

TEST(LuaLib, string_lib_char_2)
{
    RunSimpleLuaTest("luatests/string_lib_char_2.lua");
}

TEST(LuaLib, string_lib_rep)
{
    RunSimpleLuaTest("luatests/string_lib_rep.lua");
}

TEST(LuaLib, string_lib_rep_2)
{
    RunSimpleLuaTest("luatests/string_lib_rep_2.lua");
}

TEST(LuaLib, string_lib_sub)
{
    RunSimpleLuaTest("luatests/string_lib_sub.lua");
}

TEST(LuaLib, string_lib_sub_2)
{
    RunSimpleLuaTest("luatests/string_lib_sub_2.lua");
}

TEST(LuaLib, string_format)
{
    RunSimpleLuaTest("luatests/string_format.lua");
}

TEST(LuaLib, string_lib_len)
{
    RunSimpleLuaTest("luatests/string_lib_len.lua");
}

TEST(LuaLib, string_lib_reverse)
{
    RunSimpleLuaTest("luatests/string_lib_reverse.lua");
}

TEST(LuaLib, string_lib_lower_upper)
{
    RunSimpleLuaTest("luatests/string_lib_lower_upper.lua");
}

TEST(LuaLib, string_lib_lower_upper_2)
{
    RunSimpleLuaTest("luatests/string_lib_lower_upper_2.lua");
}

TEST(LuaLib, string_lib_misc)
{
    RunSimpleLuaTest("luatests/string_lib_misc.lua");
}

TEST(LuaLib, table_sort_1)
{
    RunSimpleLuaTest("luatests/table_sort_1.lua");
}

TEST(LuaLib, table_sort_2)
{
    RunSimpleLuaTest("luatests/table_sort_2.lua");
}

TEST(LuaLib, table_sort_3)
{
    RunSimpleLuaTest("luatests/table_sort_3.lua");
}

TEST(LuaLib, table_sort_4)
{
    RunSimpleLuaTest("luatests/table_sort_4.lua");
}

TEST(LuaLib, table_lib_concat)
{
    RunSimpleLuaTest("luatests/table_lib_concat.lua");
}

TEST(LuaBenchmark, array3d)
{
    RunSimpleLuaTest("luatests/array3d.lua");
}

TEST(LuaBenchmark, life)
{
    RunSimpleLuaTest("luatests/life.lua");
}

TEST(LuaBenchmark, mandel2)
{
    RunSimpleLuaTest("luatests/mandel2.lua");
}

TEST(LuaBenchmark, heapsort)
{
    RunSimpleLuaTest("luatests/heapsort.lua");
}

TEST(LuaBenchmark, nsieve)
{
    RunSimpleLuaTest("luatests/nsieve.lua");
}

TEST(LuaBenchmark, quadtree2)
{
    RunSimpleLuaTest("luatests/quadtree2.lua");
}

TEST(LuaBenchmark, ray)
{
    RunSimpleLuaTest("luatests/ray.lua");
}

TEST(LuaBenchmark, ray2)
{
    RunSimpleLuaTest("luatests/ray2.lua");
}

TEST(LuaBenchmark, series)
{
    RunSimpleLuaTest("luatests/series.lua");
}

TEST(LuaBenchmark, scimark_fft)
{
    RunSimpleLuaTest("luatests/scimark_fft.lua");
}

TEST(LuaBenchmark, scimark_lu)
{
    RunSimpleLuaTest("luatests/scimark_lu.lua");
}

TEST(LuaBenchmark, scimark_sor)
{
    RunSimpleLuaTest("luatests/scimark_sor.lua");
}

TEST(LuaBenchmark, scimark_sparse)
{
    RunSimpleLuaTest("luatests/scimark_sparse.lua");
}

TEST(LuaBenchmark, table_sort)
{
    RunSimpleLuaTest("luatests/table_sort.lua");
}

TEST(LuaBenchmark, table_sort_cmp)
{
    RunSimpleLuaTest("luatests/table_sort_cmp.lua");
}

}   // anonymous namespace
