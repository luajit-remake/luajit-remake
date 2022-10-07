#include <fstream>
#include "bytecode.h"
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

TEST(LuaTest, Fib)
{
    VM* vm = VM::Create(true /*forNewInterpreter*/);
    Auto(vm->Destroy());
    VMOutputInterceptor vmoutput(vm);

    ScriptModule* module = ScriptModule::ParseFromJSON2(vm, LoadFile("luatests/fib.lua.json"));
    vm->LaunchScript2(module);

    std::string out = vmoutput.GetAndResetStdOut();
    std::string err = vmoutput.GetAndResetStdErr();
    AssertIsExpectedOutput(out);
    ReleaseAssert(err == "");
}

TEST(LuaTest, TestPrint)
{
    VM* vm = VM::Create(true /*forNewInterpreter*/);
    Auto(vm->Destroy());
    VMOutputInterceptor vmoutput(vm);

    ScriptModule* module = ScriptModule::ParseFromJSON2(vm, LoadFile("luatests/test_print.lua.json"));
    vm->LaunchScript2(module);

    std::string out = vmoutput.GetAndResetStdOut();
    std::string err = vmoutput.GetAndResetStdErr();

    std::string expectedOut = "0.2\t3\tfalse\ttrue\tnil\tabc\tfunction: 0x";
    ReleaseAssert(out.starts_with(expectedOut));
    ReleaseAssert(err == "");
}

TEST(LuaTest, TestTableDup)
{
    VM* vm = VM::Create(true /*forNewInterpreter*/);
    Auto(vm->Destroy());
    VMOutputInterceptor vmoutput(vm);

    ScriptModule* module = ScriptModule::ParseFromJSON2(vm, LoadFile("luatests/table_dup.lua.json"));
    vm->LaunchScript2(module);

    std::string out = vmoutput.GetAndResetStdOut();
    std::string err = vmoutput.GetAndResetStdErr();
    AssertIsExpectedOutput(out);
    ReleaseAssert(err == "");
}

TEST(LuaTest, TestTableDup2)
{
    VM* vm = VM::Create(true /*forNewInterpreter*/);
    Auto(vm->Destroy());
    VMOutputInterceptor vmoutput(vm);

    ScriptModule* module = ScriptModule::ParseFromJSON2(vm, LoadFile("luatests/table_dup2.lua.json"));
    vm->LaunchScript2(module);

    std::string out = vmoutput.GetAndResetStdOut();
    std::string err = vmoutput.GetAndResetStdErr();
    AssertIsExpectedOutput(out);
    ReleaseAssert(err == "");
}

TEST(LuaTest, TestTableDup3)
{
    VM* vm = VM::Create(true /*forNewInterpreter*/);
    Auto(vm->Destroy());
    VMOutputInterceptor vmoutput(vm);

    ScriptModule* module = ScriptModule::ParseFromJSON2(vm, LoadFile("luatests/table_dup3.lua.json"));
    vm->LaunchScript2(module);

    std::string out = vmoutput.GetAndResetStdOut();
    std::string err = vmoutput.GetAndResetStdErr();
    AssertIsExpectedOutput(out);
    ReleaseAssert(err == "");
}

TEST(LuaTest, TestTableSizeHint)
{
    VM* vm = VM::Create(true /*forNewInterpreter*/);
    Auto(vm->Destroy());
    VMOutputInterceptor vmoutput(vm);

    ScriptModule* module = ScriptModule::ParseFromJSON2(vm, LoadFile("luatests/table_size_hint.lua.json"));
    vm->LaunchScript2(module);

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
    VM* vm = VM::Create(true /*forNewInterpreter*/);
    Auto(vm->Destroy());
    VMOutputInterceptor vmoutput(vm);

    ScriptModule* module = ScriptModule::ParseFromJSON2(vm, LoadFile("luatests/upvalue.lua.json"));
    vm->LaunchScript2(module);

    std::string out = vmoutput.GetAndResetStdOut();
    std::string err = vmoutput.GetAndResetStdErr();
    AssertIsExpectedOutput(out);
    ReleaseAssert(err == "");
}

TEST(LuaTest, Fib_upvalue)
{
    VM* vm = VM::Create(true /*forNewInterpreter*/);
    Auto(vm->Destroy());
    VMOutputInterceptor vmoutput(vm);

    ScriptModule* module = ScriptModule::ParseFromJSON2(vm, LoadFile("luatests/fib_upvalue.lua.json"));
    vm->LaunchScript2(module);

    std::string out = vmoutput.GetAndResetStdOut();
    std::string err = vmoutput.GetAndResetStdErr();
    AssertIsExpectedOutput(out);
    ReleaseAssert(err == "");
}

TEST(LuaTest, LinearSieve)
{
    VM* vm = VM::Create(true /*forNewInterpreter*/);
    Auto(vm->Destroy());
    VMOutputInterceptor vmoutput(vm);

    ScriptModule* module = ScriptModule::ParseFromJSON2(vm, LoadFile("luatests/linear_sieve.lua.json"));
    vm->LaunchScript2(module);

    std::string out = vmoutput.GetAndResetStdOut();
    std::string err = vmoutput.GetAndResetStdErr();
    AssertIsExpectedOutput(out);
    ReleaseAssert(err == "");
}

TEST(LuaTest, NaNEdgeCase)
{
    VM* vm = VM::Create(true /*forNewInterpreter*/);
    Auto(vm->Destroy());
    VMOutputInterceptor vmoutput(vm);

    ScriptModule* module = ScriptModule::ParseFromJSON2(vm, LoadFile("luatests/nan_edge_case.lua.json"));
    vm->LaunchScript2(module);

    std::string out = vmoutput.GetAndResetStdOut();
    std::string err = vmoutput.GetAndResetStdErr();
    AssertIsExpectedOutput(out);
    ReleaseAssert(err == "");
}

TEST(LuaTest, ForLoopCoercion)
{
    VM* vm = VM::Create(true /*forNewInterpreter*/);
    Auto(vm->Destroy());
    VMOutputInterceptor vmoutput(vm);

    ScriptModule* module = ScriptModule::ParseFromJSON2(vm, LoadFile("luatests/for_loop_coercion.lua.json"));
    vm->LaunchScript2(module);

    std::string out = vmoutput.GetAndResetStdOut();
    std::string err = vmoutput.GetAndResetStdErr();
    AssertIsExpectedOutput(out);
    ReleaseAssert(err == "");
}

TEST(LuaTest, ForLoopEdgeCases)
{
    VM* vm = VM::Create(true /*forNewInterpreter*/);
    Auto(vm->Destroy());
    VMOutputInterceptor vmoutput(vm);

    ScriptModule* module = ScriptModule::ParseFromJSON2(vm, LoadFile("luatests/for_loop_edge_cases.lua.json"));
    vm->LaunchScript2(module);

    std::string out = vmoutput.GetAndResetStdOut();
    std::string err = vmoutput.GetAndResetStdErr();
    AssertIsExpectedOutput(out);
    ReleaseAssert(err == "");
}

TEST(LuaTest, PrimitiveConstants)
{
    VM* vm = VM::Create(true /*forNewInterpreter*/);
    Auto(vm->Destroy());
    VMOutputInterceptor vmoutput(vm);

    ScriptModule* module = ScriptModule::ParseFromJSON2(vm, LoadFile("luatests/primitive_constant.lua.json"));
    vm->LaunchScript2(module);

    std::string out = vmoutput.GetAndResetStdOut();
    std::string err = vmoutput.GetAndResetStdErr();
    AssertIsExpectedOutput(out);
    ReleaseAssert(err == "");
}

TEST(LuaTest, LogicalOpSanity)
{
    VM* vm = VM::Create(true /*forNewInterpreter*/);
    Auto(vm->Destroy());
    VMOutputInterceptor vmoutput(vm);

    ScriptModule* module = ScriptModule::ParseFromJSON2(vm, LoadFile("luatests/logical_op_sanity.lua.json"));
    vm->LaunchScript2(module);

    std::string out = vmoutput.GetAndResetStdOut();
    std::string err = vmoutput.GetAndResetStdErr();
    AssertIsExpectedOutput(out);
    ReleaseAssert(err == "");
}

TEST(LuaTest, PositiveAndNegativeInf)
{
    VM* vm = VM::Create(true /*forNewInterpreter*/);
    Auto(vm->Destroy());
    VMOutputInterceptor vmoutput(vm);

    ScriptModule* module = ScriptModule::ParseFromJSON2(vm, LoadFile("luatests/pos_and_neg_inf.lua.json"));
    vm->LaunchScript2(module);

    std::string out = vmoutput.GetAndResetStdOut();
    std::string err = vmoutput.GetAndResetStdErr();
    AssertIsExpectedOutput(out);
    ReleaseAssert(err == "");
}

TEST(LuaTest, LogicalNot)
{
    VM* vm = VM::Create(true /*forNewInterpreter*/);
    Auto(vm->Destroy());
    VMOutputInterceptor vmoutput(vm);

    ScriptModule* module = ScriptModule::ParseFromJSON2(vm, LoadFile("luatests/logical_not.lua.json"));
    vm->LaunchScript2(module);

    std::string out = vmoutput.GetAndResetStdOut();
    std::string err = vmoutput.GetAndResetStdErr();
    AssertIsExpectedOutput(out);
    ReleaseAssert(err == "");
}

TEST(LuaTest, LengthOperator)
{
    VM* vm = VM::Create(true /*forNewInterpreter*/);
    Auto(vm->Destroy());
    VMOutputInterceptor vmoutput(vm);

    ScriptModule* module = ScriptModule::ParseFromJSON2(vm, LoadFile("luatests/length_operator.lua.json"));
    vm->LaunchScript2(module);

    std::string out = vmoutput.GetAndResetStdOut();
    std::string err = vmoutput.GetAndResetStdErr();
    AssertIsExpectedOutput(out);
    ReleaseAssert(err == "");
}

TEST(LuaTest, TailCall)
{
    VM* vm = VM::Create(true /*forNewInterpreter*/);
    Auto(vm->Destroy());
    VMOutputInterceptor vmoutput(vm);

    ScriptModule* module = ScriptModule::ParseFromJSON2(vm, LoadFile("luatests/tail_call.lua.json"));

    // Manually lower the stack size
    //
    CoroutineRuntimeContext* rc = vm->GetRootCoroutine();
    delete [] rc->m_stackBegin;
    rc->m_stackBegin = new TValue[200];

    vm->LaunchScript2(module);

    std::string out = vmoutput.GetAndResetStdOut();
    std::string err = vmoutput.GetAndResetStdErr();
    AssertIsExpectedOutput(out);
    ReleaseAssert(err == "");
}

TEST(LuaTest, VariadicTailCall_1)
{
    VM* vm = VM::Create(true /*forNewInterpreter*/);
    Auto(vm->Destroy());
    VMOutputInterceptor vmoutput(vm);

    ScriptModule* module = ScriptModule::ParseFromJSON2(vm, LoadFile("luatests/variadic_tail_call_1.lua.json"));

    // Manually lower the stack size
    //
    CoroutineRuntimeContext* rc = vm->GetRootCoroutine();
    delete [] rc->m_stackBegin;
    rc->m_stackBegin = new TValue[200];

    vm->LaunchScript2(module);

    std::string out = vmoutput.GetAndResetStdOut();
    std::string err = vmoutput.GetAndResetStdErr();
    AssertIsExpectedOutput(out);
    ReleaseAssert(err == "");
}

TEST(LuaTest, VariadicTailCall_2)
{
    VM* vm = VM::Create(true /*forNewInterpreter*/);
    Auto(vm->Destroy());
    VMOutputInterceptor vmoutput(vm);

    ScriptModule* module = ScriptModule::ParseFromJSON2(vm, LoadFile("luatests/variadic_tail_call_2.lua.json"));

    // Manually lower the stack size
    //
    CoroutineRuntimeContext* rc = vm->GetRootCoroutine();
    delete [] rc->m_stackBegin;
    rc->m_stackBegin = new TValue[200];

    vm->LaunchScript2(module);

    std::string out = vmoutput.GetAndResetStdOut();
    std::string err = vmoutput.GetAndResetStdErr();
    AssertIsExpectedOutput(out);
    ReleaseAssert(err == "");
}

TEST(LuaTest, VariadicTailCall_3)
{
    VM* vm = VM::Create(true /*forNewInterpreter*/);
    Auto(vm->Destroy());
    VMOutputInterceptor vmoutput(vm);

    ScriptModule* module = ScriptModule::ParseFromJSON2(vm, LoadFile("luatests/variadic_tail_call_3.lua.json"));

    // Manually lower the stack size
    //
    CoroutineRuntimeContext* rc = vm->GetRootCoroutine();
    delete [] rc->m_stackBegin;
    rc->m_stackBegin = new TValue[200];

    vm->LaunchScript2(module);

    std::string out = vmoutput.GetAndResetStdOut();
    std::string err = vmoutput.GetAndResetStdErr();
    AssertIsExpectedOutput(out);
    ReleaseAssert(err == "");
}

TEST(LuaTest, OpcodeKNIL)
{
    VM* vm = VM::Create(true /*forNewInterpreter*/);
    Auto(vm->Destroy());
    VMOutputInterceptor vmoutput(vm);

    ScriptModule* module = ScriptModule::ParseFromJSON2(vm, LoadFile("luatests/test_knil.lua.json"));
    vm->LaunchScript2(module);

    std::string out = vmoutput.GetAndResetStdOut();
    std::string err = vmoutput.GetAndResetStdErr();
    AssertIsExpectedOutput(out);
    ReleaseAssert(err == "");
}

TEST(LuaTest, IterativeForLoop)
{
    VM* vm = VM::Create();
    Auto(vm->Destroy());
    VMOutputInterceptor vmoutput(vm);

    ScriptModule* module = ScriptModule::ParseFromJSON(vm, LoadFile("luatests/iter_for.lua.json"));
    vm->LaunchScript(module);

    std::string out = vmoutput.GetAndResetStdOut();
    std::string err = vmoutput.GetAndResetStdErr();

    ReleaseAssert(out == "for init\n1\t1\tnil\tnil\n2\t1\tnil\tnil\n3\t1\tnil\tnil\n4\t1\tnil\tnil\n");
    ReleaseAssert(err == "");
}

TEST(LuaTest, NegativeZeroAsIndex)
{
    VM* vm = VM::Create(true /*forNewInterpreter*/);
    Auto(vm->Destroy());
    VMOutputInterceptor vmoutput(vm);

    ScriptModule* module = ScriptModule::ParseFromJSON2(vm, LoadFile("luatests/negative_zero_as_index.lua.json"));
    vm->LaunchScript2(module);

    std::string out = vmoutput.GetAndResetStdOut();
    std::string err = vmoutput.GetAndResetStdErr();
    AssertIsExpectedOutput(out);
    ReleaseAssert(err == "");
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
    VM* vm = VM::Create();
    Auto(vm->Destroy());
    VMOutputInterceptor vmoutput(vm);

    ScriptModule* module = ScriptModule::ParseFromJSON(vm, LoadFile("luatests/for_pairs_poison_pairs.lua.json"));
    vm->LaunchScript(module);

    std::string out = vmoutput.GetAndResetStdOut();
    std::string err = vmoutput.GetAndResetStdErr();

    ReleaseAssert(out == "x\t1\n2\t3\nenter pairs\n100\t200\n201\t1\n100\t201\n202\t1\n100\t202\n203\t1\n100\t203\n204\t1\n100\t204\nx\t1\n2\t3\nenter pairs\n100\t200\n201\t1\n100\t201\n202\t1\n100\t202\n203\t1\n100\t203\n204\t1\n100\t204\n");
    ReleaseAssert(err == "");
}

TEST(LuaTest, ForPairsEmpty)
{
    VM* vm = VM::Create();
    Auto(vm->Destroy());
    VMOutputInterceptor vmoutput(vm);

    ScriptModule* module = ScriptModule::ParseFromJSON(vm, LoadFile("luatests/for_pairs_empty.lua.json"));
    vm->LaunchScript(module);

    std::string out = vmoutput.GetAndResetStdOut();
    std::string err = vmoutput.GetAndResetStdErr();

    ReleaseAssert(out == "test start\ntest end\n");
    ReleaseAssert(err == "");
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
    VM* vm = VM::Create();
    Auto(vm->Destroy());
    VMOutputInterceptor vmoutput(vm);

    ScriptModule* module = ScriptModule::ParseFromJSON(vm, LoadFile("luatests/boolean_as_table_index_3.lua.json"));
    vm->LaunchScript(module);

    std::string out = vmoutput.GetAndResetStdOut();
    std::string err = vmoutput.GetAndResetStdErr();

    std::string expectedOut =
            "1\t3\t2\tnil\t4\tnil\n"
            "5\t7\t6\tnil\t8\tnil\n"
            "nil\tnil\tnil\tnil\tnil\tnil\n";

    ReleaseAssert(out == expectedOut);
    ReleaseAssert(err == "");
}

TEST(LuaTest, ArithmeticSanity)
{
    VM* vm = VM::Create(true /*forNewInterpreter*/);
    Auto(vm->Destroy());
    VMOutputInterceptor vmoutput(vm);

    ScriptModule* module = ScriptModule::ParseFromJSON2(vm, LoadFile("luatests/arithmetic_sanity.lua.json"));
    vm->LaunchScript2(module);

    std::string out = vmoutput.GetAndResetStdOut();
    std::string err = vmoutput.GetAndResetStdErr();
    AssertIsExpectedOutput(out);
    ReleaseAssert(err == "");
}

TEST(LuaTest, StringConcat)
{
    VM* vm = VM::Create(true /*forNewInterpreter*/);
    Auto(vm->Destroy());
    VMOutputInterceptor vmoutput(vm);

    ScriptModule* module = ScriptModule::ParseFromJSON2(vm, LoadFile("luatests/string_concat.lua.json"));
    vm->LaunchScript2(module);

    std::string out = vmoutput.GetAndResetStdOut();
    std::string err = vmoutput.GetAndResetStdErr();
    AssertIsExpectedOutput(out);
    ReleaseAssert(err == "");
}

TEST(LuaTest, TableVariadicPut)
{
    VM* vm = VM::Create(true /*forNewInterpreter*/);
    Auto(vm->Destroy());
    VMOutputInterceptor vmoutput(vm);

    ScriptModule* module = ScriptModule::ParseFromJSON2(vm, LoadFile("luatests/table_variadic_put.lua.json"));
    vm->LaunchScript2(module);

    std::string out = vmoutput.GetAndResetStdOut();
    std::string err = vmoutput.GetAndResetStdErr();
    AssertIsExpectedOutput(out);
    ReleaseAssert(err == "");
}

TEST(LuaBenchmark, NBody)
{
    VM* vm = VM::Create(true /*forNewInterpreter*/);
    Auto(vm->Destroy());
    VMOutputInterceptor vmoutput(vm);

    ScriptModule* module = ScriptModule::ParseFromJSON2(vm, LoadFile("luatests/n-body.lua.json"));
    vm->LaunchScript2(module);

    std::string out = vmoutput.GetAndResetStdOut();
    std::string err = vmoutput.GetAndResetStdErr();
    AssertIsExpectedOutput(out);
    ReleaseAssert(err == "");
}

TEST(LuaBenchmark, Ack)
{
    VM* vm = VM::Create(true /*forNewInterpreter*/);
    Auto(vm->Destroy());
    VMOutputInterceptor vmoutput(vm);

    ScriptModule* module = ScriptModule::ParseFromJSON2(vm, LoadFile("luatests/ack.lua.json"));

    // This benchmark needs a larger stack
    //
    CoroutineRuntimeContext* rc = vm->GetRootCoroutine();
    delete [] rc->m_stackBegin;
    rc->m_stackBegin = new TValue[1000000];

    vm->LaunchScript2(module);

    std::string out = vmoutput.GetAndResetStdOut();
    std::string err = vmoutput.GetAndResetStdErr();
    AssertIsExpectedOutput(out);
    ReleaseAssert(err == "");
}

TEST(LuaBenchmark, BinaryTrees_1)
{
    VM* vm = VM::Create(true /*forNewInterpreter*/);
    Auto(vm->Destroy());
    VMOutputInterceptor vmoutput(vm);

    ScriptModule* module = ScriptModule::ParseFromJSON2(vm, LoadFile("luatests/binary-trees-1.lua.json"));
    vm->LaunchScript2(module);

    std::string out = vmoutput.GetAndResetStdOut();
    std::string err = vmoutput.GetAndResetStdErr();
    AssertIsExpectedOutput(out);
    ReleaseAssert(err == "");
}

TEST(LuaBenchmark, BinaryTrees_2)
{
    VM* vm = VM::Create(true /*forNewInterpreter*/);
    Auto(vm->Destroy());
    VMOutputInterceptor vmoutput(vm);

    ScriptModule* module = ScriptModule::ParseFromJSON2(vm, LoadFile("luatests/binary-trees-2.lua.json"));
    vm->LaunchScript2(module);

    std::string out = vmoutput.GetAndResetStdOut();
    std::string err = vmoutput.GetAndResetStdErr();
    AssertIsExpectedOutput(out);
    ReleaseAssert(err == "");
}

TEST(LuaBenchmark, Fannkuch_Redux)
{
    VM* vm = VM::Create(true /*forNewInterpreter*/);
    Auto(vm->Destroy());
    VMOutputInterceptor vmoutput(vm);

    ScriptModule* module = ScriptModule::ParseFromJSON2(vm, LoadFile("luatests/fannkuch-redux.lua.json"));
    vm->LaunchScript2(module);

    std::string out = vmoutput.GetAndResetStdOut();
    std::string err = vmoutput.GetAndResetStdErr();
    AssertIsExpectedOutput(out);
    ReleaseAssert(err == "");
}

TEST(LuaBenchmark, Fixpoint_Fact)
{
    VM* vm = VM::Create(true /*forNewInterpreter*/);
    Auto(vm->Destroy());
    VMOutputInterceptor vmoutput(vm);

    ScriptModule* module = ScriptModule::ParseFromJSON2(vm, LoadFile("luatests/fixpoint-fact.lua.json"));
    vm->LaunchScript2(module);

    std::string out = vmoutput.GetAndResetStdOut();
    std::string err = vmoutput.GetAndResetStdErr();
    AssertIsExpectedOutput(out);
    ReleaseAssert(err == "");
}

TEST(LuaBenchmark, Mandel_NoMetatable)
{
    VM* vm = VM::Create(true /*forNewInterpreter*/);
    Auto(vm->Destroy());
    VMOutputInterceptor vmoutput(vm);

    ScriptModule* module = ScriptModule::ParseFromJSON2(vm, LoadFile("luatests/mandel-nometatable.lua.json"));
    vm->LaunchScript2(module);

    std::string out = vmoutput.GetAndResetStdOut();
    std::string err = vmoutput.GetAndResetStdErr();
    AssertIsExpectedOutput(out);
    ReleaseAssert(err == "");
}

TEST(LuaBenchmark, Mandel)
{
    VM* vm = VM::Create(true /*forNewInterpreter*/);
    Auto(vm->Destroy());
    VMOutputInterceptor vmoutput(vm);

    ScriptModule* module = ScriptModule::ParseFromJSON2(vm, LoadFile("luatests/mandel.lua.json"));
    vm->LaunchScript2(module);

    std::string out = vmoutput.GetAndResetStdOut();
    std::string err = vmoutput.GetAndResetStdErr();
    AssertIsExpectedOutput(out);
    ReleaseAssert(err == "");
}

TEST(LuaBenchmark, QuadTree)
{
    VM* vm = VM::Create(true /*forNewInterpreter*/);
    Auto(vm->Destroy());
    VMOutputInterceptor vmoutput(vm);

    ScriptModule* module = ScriptModule::ParseFromJSON2(vm, LoadFile("luatests/qt.lua.json"));
    vm->LaunchScript2(module);

    std::string out = vmoutput.GetAndResetStdOut();
    std::string err = vmoutput.GetAndResetStdErr();
    AssertIsExpectedOutput(out);
    ReleaseAssert(err == "");
}

TEST(LuaBenchmark, Queen)
{
    VM* vm = VM::Create(true /*forNewInterpreter*/);
    Auto(vm->Destroy());
    VMOutputInterceptor vmoutput(vm);

    ScriptModule* module = ScriptModule::ParseFromJSON2(vm, LoadFile("luatests/queen.lua.json"));
    vm->LaunchScript2(module);

    std::string out = vmoutput.GetAndResetStdOut();
    std::string err = vmoutput.GetAndResetStdErr();
    AssertIsExpectedOutput(out);
    ReleaseAssert(err == "");
}

TEST(LuaBenchmark, NlgN_Sieve)
{
    VM* vm = VM::Create(true /*forNewInterpreter*/);
    Auto(vm->Destroy());
    VMOutputInterceptor vmoutput(vm);

    ScriptModule* module = ScriptModule::ParseFromJSON2(vm, LoadFile("luatests/nlgn_sieve.lua.json"));
    vm->LaunchScript2(module);

    std::string out = vmoutput.GetAndResetStdOut();
    std::string err = vmoutput.GetAndResetStdErr();
    AssertIsExpectedOutput(out);
    ReleaseAssert(err == "");
}

TEST(LuaBenchmark, Spectral_Norm)
{
    VM* vm = VM::Create(true /*forNewInterpreter*/);
    Auto(vm->Destroy());
    VMOutputInterceptor vmoutput(vm);

    ScriptModule* module = ScriptModule::ParseFromJSON2(vm, LoadFile("luatests/spectral-norm.lua.json"));
    vm->LaunchScript2(module);

    std::string out = vmoutput.GetAndResetStdOut();
    std::string err = vmoutput.GetAndResetStdErr();
    AssertIsExpectedOutput(out);
    ReleaseAssert(err == "");
}

TEST(LuaTest, xpcall_1)
{
    VM* vm = VM::Create(true /*forNewInterpreter*/);
    Auto(vm->Destroy());
    VMOutputInterceptor vmoutput(vm);

    ScriptModule* module = ScriptModule::ParseFromJSON2(vm, LoadFile("luatests/xpcall_1.lua.json"));
    vm->LaunchScript2(module);

    std::string out = vmoutput.GetAndResetStdOut();
    std::string err = vmoutput.GetAndResetStdErr();
    AssertIsExpectedOutput(out);
    ReleaseAssert(err == "");
}

TEST(LuaTest, xpcall_2)
{
    VM* vm = VM::Create(true /*forNewInterpreter*/);
    Auto(vm->Destroy());
    VMOutputInterceptor vmoutput(vm);

    ScriptModule* module = ScriptModule::ParseFromJSON2(vm, LoadFile("luatests/xpcall_2.lua.json"));
    vm->LaunchScript2(module);

    std::string out = vmoutput.GetAndResetStdOut();
    std::string err = vmoutput.GetAndResetStdErr();
    AssertIsExpectedOutput(out);
    ReleaseAssert(err == "");
}

TEST(LuaTest, xpcall_3)
{
    VM* vm = VM::Create(true /*forNewInterpreter*/);
    Auto(vm->Destroy());
    VMOutputInterceptor vmoutput(vm);

    ScriptModule* module = ScriptModule::ParseFromJSON2(vm, LoadFile("luatests/xpcall_3.lua.json"));
    vm->LaunchScript2(module);

    std::string out = vmoutput.GetAndResetStdOut();
    std::string err = vmoutput.GetAndResetStdErr();
    AssertIsExpectedOutput(out);
    ReleaseAssert(err == "");
}

TEST(LuaTest, xpcall_4)
{
    VM* vm = VM::Create(true /*forNewInterpreter*/);
    Auto(vm->Destroy());
    VMOutputInterceptor vmoutput(vm);

    ScriptModule* module = ScriptModule::ParseFromJSON2(vm, LoadFile("luatests/xpcall_4.lua.json"));
    vm->LaunchScript2(module);

    std::string out = vmoutput.GetAndResetStdOut();
    std::string err = vmoutput.GetAndResetStdErr();
    AssertIsExpectedOutput(out);
    ReleaseAssert(err == "");
}

TEST(LuaTest, xpcall_5)
{
    VM* vm = VM::Create(true /*forNewInterpreter*/);
    Auto(vm->Destroy());
    VMOutputInterceptor vmoutput(vm);

    ScriptModule* module = ScriptModule::ParseFromJSON2(vm, LoadFile("luatests/xpcall_5.lua.json"));
    vm->LaunchScript2(module);

    std::string out = vmoutput.GetAndResetStdOut();
    std::string err = vmoutput.GetAndResetStdErr();
    AssertIsExpectedOutput(out);
    ReleaseAssert(err == "");
}

TEST(LuaTest, xpcall_6)
{
    VM* vm = VM::Create(true /*forNewInterpreter*/);
    Auto(vm->Destroy());
    VMOutputInterceptor vmoutput(vm);

    ScriptModule* module = ScriptModule::ParseFromJSON2(vm, LoadFile("luatests/xpcall_6.lua.json"));
    vm->LaunchScript2(module);

    std::string out = vmoutput.GetAndResetStdOut();
    std::string err = vmoutput.GetAndResetStdErr();
    AssertIsExpectedOutput(out);
    ReleaseAssert(err == "");
}

TEST(LuaTest, pcall_1)
{
    VM* vm = VM::Create(true /*forNewInterpreter*/);
    Auto(vm->Destroy());
    VMOutputInterceptor vmoutput(vm);

    ScriptModule* module = ScriptModule::ParseFromJSON2(vm, LoadFile("luatests/pcall_1.lua.json"));
    vm->LaunchScript2(module);

    std::string out = vmoutput.GetAndResetStdOut();
    std::string err = vmoutput.GetAndResetStdErr();
    AssertIsExpectedOutput(out);
    ReleaseAssert(err == "");
}

TEST(LuaTest, pcall_2)
{
    VM* vm = VM::Create(true /*forNewInterpreter*/);
    Auto(vm->Destroy());
    VMOutputInterceptor vmoutput(vm);

    ScriptModule* module = ScriptModule::ParseFromJSON2(vm, LoadFile("luatests/pcall_2.lua.json"));
    vm->LaunchScript2(module);

    std::string out = vmoutput.GetAndResetStdOut();
    std::string err = vmoutput.GetAndResetStdErr();
    AssertIsExpectedOutput(out);
    ReleaseAssert(err == "");
}

TEST(LuaTest, GetSetMetatable)
{
    VM* vm = VM::Create(true /*forNewInterpreter*/);
    Auto(vm->Destroy());
    VMOutputInterceptor vmoutput(vm);

    ScriptModule* module = ScriptModule::ParseFromJSON2(vm, LoadFile("luatests/get_set_metatable.lua.json"));
    vm->LaunchScript2(module);

    std::string out = vmoutput.GetAndResetStdOut();
    std::string err = vmoutput.GetAndResetStdErr();
    AssertIsExpectedOutput(out);
    ReleaseAssert(err == "");
}

TEST(LuaTest, metatable_call_1)
{
    VM* vm = VM::Create(true /*forNewInterpreter*/);
    Auto(vm->Destroy());
    VMOutputInterceptor vmoutput(vm);

    ScriptModule* module = ScriptModule::ParseFromJSON2(vm, LoadFile("luatests/metatable_call_1.lua.json"));
    vm->LaunchScript2(module);

    std::string out = vmoutput.GetAndResetStdOut();
    std::string err = vmoutput.GetAndResetStdErr();
    AssertIsExpectedOutput(out);
    ReleaseAssert(err == "");
}

TEST(LuaTest, metatable_call_2)
{
    VM* vm = VM::Create(true /*forNewInterpreter*/);
    Auto(vm->Destroy());
    VMOutputInterceptor vmoutput(vm);

    ScriptModule* module = ScriptModule::ParseFromJSON2(vm, LoadFile("luatests/metatable_call_2.lua.json"));
    vm->LaunchScript2(module);

    std::string out = vmoutput.GetAndResetStdOut();
    std::string err = vmoutput.GetAndResetStdErr();
    AssertIsExpectedOutput(out);
    ReleaseAssert(err == "");
}

TEST(LuaTest, metatable_call_3)
{
    VM* vm = VM::Create(true /*forNewInterpreter*/);
    Auto(vm->Destroy());
    VMOutputInterceptor vmoutput(vm);

    ScriptModule* module = ScriptModule::ParseFromJSON2(vm, LoadFile("luatests/metatable_call_3.lua.json"));
    vm->LaunchScript2(module);

    std::string out = vmoutput.GetAndResetStdOut();
    std::string err = vmoutput.GetAndResetStdErr();
    AssertIsExpectedOutput(out);
    ReleaseAssert(err == "");
}

TEST(LuaTest, metatable_call_4)
{
    VM* vm = VM::Create(true /*forNewInterpreter*/);
    Auto(vm->Destroy());
    VMOutputInterceptor vmoutput(vm);

    ScriptModule* module = ScriptModule::ParseFromJSON2(vm, LoadFile("luatests/metatable_call_4.lua.json"));
    vm->LaunchScript2(module);

    std::string out = vmoutput.GetAndResetStdOut();
    std::string err = vmoutput.GetAndResetStdErr();
    AssertIsExpectedOutput(out);
    ReleaseAssert(err == "");
}

TEST(LuaTest, metatable_call_5)
{
    VM* vm = VM::Create(true /*forNewInterpreter*/);
    Auto(vm->Destroy());
    VMOutputInterceptor vmoutput(vm);

    ScriptModule* module = ScriptModule::ParseFromJSON2(vm, LoadFile("luatests/metatable_call_5.lua.json"));
    vm->LaunchScript2(module);

    std::string out = vmoutput.GetAndResetStdOut();
    std::string err = vmoutput.GetAndResetStdErr();
    AssertIsExpectedOutput(out);
    ReleaseAssert(err == "");
}

TEST(LuaTest, xpcall_metatable)
{
    VM* vm = VM::Create(true /*forNewInterpreter*/);
    Auto(vm->Destroy());
    VMOutputInterceptor vmoutput(vm);

    ScriptModule* module = ScriptModule::ParseFromJSON2(vm, LoadFile("luatests/xpcall_metatable.lua.json"));
    vm->LaunchScript2(module);

    std::string out = vmoutput.GetAndResetStdOut();
    std::string err = vmoutput.GetAndResetStdErr();
    AssertIsExpectedOutput(out);
    ReleaseAssert(err == "");
}

TEST(LuaTest, pcall_metatable)
{
    VM* vm = VM::Create(true /*forNewInterpreter*/);
    Auto(vm->Destroy());
    VMOutputInterceptor vmoutput(vm);

    ScriptModule* module = ScriptModule::ParseFromJSON2(vm, LoadFile("luatests/pcall_metatable.lua.json"));
    vm->LaunchScript2(module);

    std::string out = vmoutput.GetAndResetStdOut();
    std::string err = vmoutput.GetAndResetStdErr();
    AssertIsExpectedOutput(out);
    ReleaseAssert(err == "");
}

TEST(LuaTest, metatable_add_1)
{
    VM* vm = VM::Create(true /*forNewInterpreter*/);
    Auto(vm->Destroy());
    VMOutputInterceptor vmoutput(vm);

    ScriptModule* module = ScriptModule::ParseFromJSON2(vm, LoadFile("luatests/metatable_add_1.lua.json"));
    vm->LaunchScript2(module);

    std::string out = vmoutput.GetAndResetStdOut();
    std::string err = vmoutput.GetAndResetStdErr();
    AssertIsExpectedOutput(out);
    ReleaseAssert(err == "");
}

TEST(LuaTest, metatable_add_2)
{
    VM* vm = VM::Create(true /*forNewInterpreter*/);
    Auto(vm->Destroy());
    VMOutputInterceptor vmoutput(vm);

    ScriptModule* module = ScriptModule::ParseFromJSON2(vm, LoadFile("luatests/metatable_add_2.lua.json"));
    vm->LaunchScript2(module);

    std::string out = vmoutput.GetAndResetStdOut();
    std::string err = vmoutput.GetAndResetStdErr();
    AssertIsExpectedOutput(out);
    ReleaseAssert(err == "");
}

TEST(LuaTest, metatable_add_3)
{
    VM* vm = VM::Create(true /*forNewInterpreter*/);
    Auto(vm->Destroy());
    VMOutputInterceptor vmoutput(vm);

    ScriptModule* module = ScriptModule::ParseFromJSON2(vm, LoadFile("luatests/metatable_add_3.lua.json"));
    vm->LaunchScript2(module);

    std::string out = vmoutput.GetAndResetStdOut();
    std::string err = vmoutput.GetAndResetStdErr();
    AssertIsExpectedOutput(out);
    ReleaseAssert(err == "");
}

TEST(LuaTest, metatable_sub)
{
    VM* vm = VM::Create(true /*forNewInterpreter*/);
    Auto(vm->Destroy());
    VMOutputInterceptor vmoutput(vm);

    ScriptModule* module = ScriptModule::ParseFromJSON2(vm, LoadFile("luatests/metatable_sub.lua.json"));
    vm->LaunchScript2(module);

    std::string out = vmoutput.GetAndResetStdOut();
    std::string err = vmoutput.GetAndResetStdErr();
    AssertIsExpectedOutput(out);
    ReleaseAssert(err == "");
}

TEST(LuaTest, metatable_mul)
{
    VM* vm = VM::Create(true /*forNewInterpreter*/);
    Auto(vm->Destroy());
    VMOutputInterceptor vmoutput(vm);

    ScriptModule* module = ScriptModule::ParseFromJSON2(vm, LoadFile("luatests/metatable_mul.lua.json"));
    vm->LaunchScript2(module);

    std::string out = vmoutput.GetAndResetStdOut();
    std::string err = vmoutput.GetAndResetStdErr();
    AssertIsExpectedOutput(out);
    ReleaseAssert(err == "");
}

TEST(LuaTest, metatable_div)
{
    VM* vm = VM::Create(true /*forNewInterpreter*/);
    Auto(vm->Destroy());
    VMOutputInterceptor vmoutput(vm);

    ScriptModule* module = ScriptModule::ParseFromJSON2(vm, LoadFile("luatests/metatable_div.lua.json"));
    vm->LaunchScript2(module);

    std::string out = vmoutput.GetAndResetStdOut();
    std::string err = vmoutput.GetAndResetStdErr();
    AssertIsExpectedOutput(out);
    ReleaseAssert(err == "");
}

TEST(LuaTest, metatable_mod)
{
    VM* vm = VM::Create(true /*forNewInterpreter*/);
    Auto(vm->Destroy());
    VMOutputInterceptor vmoutput(vm);

    ScriptModule* module = ScriptModule::ParseFromJSON2(vm, LoadFile("luatests/metatable_mod.lua.json"));
    vm->LaunchScript2(module);

    std::string out = vmoutput.GetAndResetStdOut();
    std::string err = vmoutput.GetAndResetStdErr();
    AssertIsExpectedOutput(out);
    ReleaseAssert(err == "");
}

TEST(LuaTest, metatable_pow)
{
    VM* vm = VM::Create(true /*forNewInterpreter*/);
    Auto(vm->Destroy());
    VMOutputInterceptor vmoutput(vm);

    ScriptModule* module = ScriptModule::ParseFromJSON2(vm, LoadFile("luatests/metatable_pow.lua.json"));
    vm->LaunchScript2(module);

    std::string out = vmoutput.GetAndResetStdOut();
    std::string err = vmoutput.GetAndResetStdErr();
    AssertIsExpectedOutput(out);
    ReleaseAssert(err == "");
}

TEST(LuaTest, metatable_unm)
{
    VM* vm = VM::Create(true /*forNewInterpreter*/);
    Auto(vm->Destroy());
    VMOutputInterceptor vmoutput(vm);

    ScriptModule* module = ScriptModule::ParseFromJSON2(vm, LoadFile("luatests/metatable_unm.lua.json"));
    vm->LaunchScript2(module);

    std::string out = vmoutput.GetAndResetStdOut();
    std::string err = vmoutput.GetAndResetStdErr();
    AssertIsExpectedOutput(out);
    ReleaseAssert(err == "");
}

TEST(LuaTest, metatable_len)
{
    VM* vm = VM::Create(true /*forNewInterpreter*/);
    Auto(vm->Destroy());
    VMOutputInterceptor vmoutput(vm);

    ScriptModule* module = ScriptModule::ParseFromJSON2(vm, LoadFile("luatests/metatable_len.lua.json"));
    vm->LaunchScript2(module);

    std::string out = vmoutput.GetAndResetStdOut();
    std::string err = vmoutput.GetAndResetStdErr();
    AssertIsExpectedOutput(out);
    ReleaseAssert(err == "");
}

TEST(LuaTest, metatable_concat)
{
    VM* vm = VM::Create(true /*forNewInterpreter*/);
    Auto(vm->Destroy());
    VMOutputInterceptor vmoutput(vm);

    ScriptModule* module = ScriptModule::ParseFromJSON2(vm, LoadFile("luatests/metatable_concat.lua.json"));
    vm->LaunchScript2(module);

    std::string out = vmoutput.GetAndResetStdOut();
    std::string err = vmoutput.GetAndResetStdErr();
    AssertIsExpectedOutput(out);
    ReleaseAssert(err == "");
}

TEST(LuaTest, metatable_concat_2)
{
    VM* vm = VM::Create(true /*forNewInterpreter*/);
    Auto(vm->Destroy());
    VMOutputInterceptor vmoutput(vm);

    ScriptModule* module = ScriptModule::ParseFromJSON2(vm, LoadFile("luatests/metatable_concat_2.lua.json"));
    vm->LaunchScript2(module);

    std::string out = vmoutput.GetAndResetStdOut();
    std::string err = vmoutput.GetAndResetStdErr();
    AssertIsExpectedOutput(out);
    ReleaseAssert(err == "");
}

TEST(LuaTest, metatable_eq_1)
{
    VM* vm = VM::Create(true /*forNewInterpreter*/);
    Auto(vm->Destroy());
    VMOutputInterceptor vmoutput(vm);

    ScriptModule* module = ScriptModule::ParseFromJSON2(vm, LoadFile("luatests/metatable_eq_1.lua.json"));
    vm->LaunchScript2(module);

    std::string out = vmoutput.GetAndResetStdOut();
    std::string err = vmoutput.GetAndResetStdErr();
    AssertIsExpectedOutput(out);
    ReleaseAssert(err == "");
}

TEST(LuaTest, metatable_eq_2)
{
    VM* vm = VM::Create(true /*forNewInterpreter*/);
    Auto(vm->Destroy());
    VMOutputInterceptor vmoutput(vm);

    ScriptModule* module = ScriptModule::ParseFromJSON2(vm, LoadFile("luatests/metatable_eq_2.lua.json"));
    vm->LaunchScript2(module);

    std::string out = vmoutput.GetAndResetStdOut();
    std::string err = vmoutput.GetAndResetStdErr();
    AssertIsExpectedOutput(out);
    ReleaseAssert(err == "");
}

TEST(LuaTest, metatable_lt)
{
    VM* vm = VM::Create(true /*forNewInterpreter*/);
    Auto(vm->Destroy());
    VMOutputInterceptor vmoutput(vm);

    ScriptModule* module = ScriptModule::ParseFromJSON2(vm, LoadFile("luatests/metatable_lt.lua.json"));
    vm->LaunchScript2(module);

    std::string out = vmoutput.GetAndResetStdOut();
    std::string err = vmoutput.GetAndResetStdErr();
    AssertIsExpectedOutput(out);
    ReleaseAssert(err == "");
}

TEST(LuaTest, metatable_le)
{
    VM* vm = VM::Create(true /*forNewInterpreter*/);
    Auto(vm->Destroy());
    VMOutputInterceptor vmoutput(vm);

    ScriptModule* module = ScriptModule::ParseFromJSON2(vm, LoadFile("luatests/metatable_le.lua.json"));
    vm->LaunchScript2(module);

    std::string out = vmoutput.GetAndResetStdOut();
    std::string err = vmoutput.GetAndResetStdErr();
    AssertIsExpectedOutput(out);
    ReleaseAssert(err == "");
}

TEST(LuaTest, metatable_eq_3)
{
    VM* vm = VM::Create(true /*forNewInterpreter*/);
    Auto(vm->Destroy());
    VMOutputInterceptor vmoutput(vm);

    ScriptModule* module = ScriptModule::ParseFromJSON2(vm, LoadFile("luatests/metatable_eq_3.lua.json"));
    vm->LaunchScript2(module);

    std::string out = vmoutput.GetAndResetStdOut();
    std::string err = vmoutput.GetAndResetStdErr();
    AssertIsExpectedOutput(out);
    ReleaseAssert(err == "");
}

TEST(LuaTest, getbyid_metatable)
{
    VM* vm = VM::Create(true /*forNewInterpreter*/);
    Auto(vm->Destroy());
    VMOutputInterceptor vmoutput(vm);

    ScriptModule* module = ScriptModule::ParseFromJSON2(vm, LoadFile("luatests/getbyid_metatable.lua.json"));
    vm->LaunchScript2(module);

    std::string out = vmoutput.GetAndResetStdOut();
    std::string err = vmoutput.GetAndResetStdErr();
    AssertIsExpectedOutput(out);
    ReleaseAssert(err == "");
}

TEST(LuaTest, globalget_metatable)
{
    VM* vm = VM::Create(true /*forNewInterpreter*/);
    Auto(vm->Destroy());
    VMOutputInterceptor vmoutput(vm);

    ScriptModule* module = ScriptModule::ParseFromJSON2(vm, LoadFile("luatests/globalget_metatable.lua.json"));
    vm->LaunchScript2(module);

    std::string out = vmoutput.GetAndResetStdOut();
    std::string err = vmoutput.GetAndResetStdErr();
    AssertIsExpectedOutput(out);
    ReleaseAssert(err == "");
}

TEST(LuaTest, getbyval_metatable)
{
    VM* vm = VM::Create(true /*forNewInterpreter*/);
    Auto(vm->Destroy());
    VMOutputInterceptor vmoutput(vm);

    ScriptModule* module = ScriptModule::ParseFromJSON2(vm, LoadFile("luatests/getbyval_metatable.lua.json"));
    vm->LaunchScript2(module);

    std::string out = vmoutput.GetAndResetStdOut();
    std::string err = vmoutput.GetAndResetStdErr();
    AssertIsExpectedOutput(out);
    ReleaseAssert(err == "");
}

TEST(LuaTest, getbyintegerval_metatable)
{
    VM* vm = VM::Create(true /*forNewInterpreter*/);
    Auto(vm->Destroy());
    VMOutputInterceptor vmoutput(vm);

    ScriptModule* module = ScriptModule::ParseFromJSON2(vm, LoadFile("luatests/getbyintegerval_metatable.lua.json"));
    vm->LaunchScript2(module);

    std::string out = vmoutput.GetAndResetStdOut();
    std::string err = vmoutput.GetAndResetStdErr();
    AssertIsExpectedOutput(out);
    ReleaseAssert(err == "");
}

TEST(LuaTest, rawget_and_rawset)
{
    VM* vm = VM::Create(true /*forNewInterpreter*/);
    Auto(vm->Destroy());
    VMOutputInterceptor vmoutput(vm);

    ScriptModule* module = ScriptModule::ParseFromJSON2(vm, LoadFile("luatests/rawget_rawset.lua.json"));
    vm->LaunchScript2(module);

    std::string out = vmoutput.GetAndResetStdOut();
    std::string err = vmoutput.GetAndResetStdErr();
    AssertIsExpectedOutput(out);
    ReleaseAssert(err == "");
}

TEST(LuaTest, putbyid_metatable)
{
    VM* vm = VM::Create(true /*forNewInterpreter*/);
    Auto(vm->Destroy());
    VMOutputInterceptor vmoutput(vm);

    ScriptModule* module = ScriptModule::ParseFromJSON2(vm, LoadFile("luatests/putbyid_metatable.lua.json"));
    vm->LaunchScript2(module);

    std::string out = vmoutput.GetAndResetStdOut();
    std::string err = vmoutput.GetAndResetStdErr();
    AssertIsExpectedOutput(out);
    ReleaseAssert(err == "");
}

TEST(LuaTest, globalput_metatable)
{
    VM* vm = VM::Create(true /*forNewInterpreter*/);
    Auto(vm->Destroy());
    VMOutputInterceptor vmoutput(vm);

    ScriptModule* module = ScriptModule::ParseFromJSON2(vm, LoadFile("luatests/globalput_metatable.lua.json"));
    vm->LaunchScript2(module);

    std::string out = vmoutput.GetAndResetStdOut();
    std::string err = vmoutput.GetAndResetStdErr();
    AssertIsExpectedOutput(out);
    ReleaseAssert(err == "");
}

TEST(LuaTest, putbyintegerval_metatable)
{
    VM* vm = VM::Create(true /*forNewInterpreter*/);
    Auto(vm->Destroy());
    VMOutputInterceptor vmoutput(vm);

    ScriptModule* module = ScriptModule::ParseFromJSON2(vm, LoadFile("luatests/putbyintegerval_metatable.lua.json"));
    vm->LaunchScript2(module);

    std::string out = vmoutput.GetAndResetStdOut();
    std::string err = vmoutput.GetAndResetStdErr();
    AssertIsExpectedOutput(out);
    ReleaseAssert(err == "");
}

TEST(LuaTest, putbyval_metatable)
{
    VM* vm = VM::Create(true /*forNewInterpreter*/);
    Auto(vm->Destroy());
    VMOutputInterceptor vmoutput(vm);

    ScriptModule* module = ScriptModule::ParseFromJSON2(vm, LoadFile("luatests/putbyval_metatable.lua.json"));
    vm->LaunchScript2(module);

    std::string out = vmoutput.GetAndResetStdOut();
    std::string err = vmoutput.GetAndResetStdErr();
    AssertIsExpectedOutput(out);
    ReleaseAssert(err == "");
}

}   // anonymous namespace
