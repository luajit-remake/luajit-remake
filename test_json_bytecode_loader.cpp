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

bool WARN_UNUSED StartsWith(const std::string& s1, const std::string& s2)
{
    return s1.length() >= s2.length() && s1.substr(0, s2.length()) == s2;
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
    ReleaseAssert(out == "610\n");
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
    ReleaseAssert(StartsWith(out, expectedOut));
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
    VM* vm = VM::Create();
    Auto(vm->Destroy());
    VMOutputInterceptor vmoutput(vm);

    ScriptModule* module = ScriptModule::ParseFromJSON(vm, LoadFile("luatests/table_size_hint.lua.json"));
    vm->LaunchScript(module);

    std::string out = vmoutput.GetAndResetStdOut();
    std::string err = vmoutput.GetAndResetStdErr();

    std::string expectedOut =
            "3\t4\tnil\t1\t2\tnil\t1\t1\tnil\n";

    ReleaseAssert(out == expectedOut);
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
    VM* vm = VM::Create();
    Auto(vm->Destroy());
    VMOutputInterceptor vmoutput(vm);

    ScriptModule* module = ScriptModule::ParseFromJSON(vm, LoadFile("luatests/upvalue.lua.json"));
    vm->LaunchScript(module);

    std::string out = vmoutput.GetAndResetStdOut();
    std::string err = vmoutput.GetAndResetStdErr();

    std::string expectedOut =
            "10123\t456\t1001\t2\t178\t90\t13\t54321\n"
            "10765\t43\t1001\t2\t112\t963\t13\t4356\n"
            "20123\t456\t2001\t2\t278\t90\t23\t65432\n"
            "20765\t43\t2001\t2\t333\t852\t13\t7531\n"
            "30123\t456\t3001\t2\t112\t34\t13\t987\n"
            "30765\t43\t3001\t2\t433\t852\t23\t9999\n"
            "40123\t456\t4001\t2\t212\t34\t23\t654\n"
            "40765\t43\t4001\t2\t533\t852\t33\t987\n"
            "50123\t456\t5001\t2\t378\t90\t33\t432\n"
            "50765\t43\t5001\t2\t212\t963\t23\t3232\n"
            "60123\t456\t6001\t2\t478\t90\t43\t321\n"
            "70123\t456\t7001\t2\t312\t34\t33\t16\n"
            "80123\t456\t8001\t2\t9112\t234\t13\t88\n";

    ReleaseAssert(out == expectedOut);
    ReleaseAssert(err == "");
}

TEST(LuaTest, Fib_upvalue)
{
    VM* vm = VM::Create();
    Auto(vm->Destroy());
    VMOutputInterceptor vmoutput(vm);

    ScriptModule* module = ScriptModule::ParseFromJSON(vm, LoadFile("luatests/fib_upvalue.lua.json"));
    vm->LaunchScript(module);

    std::string out = vmoutput.GetAndResetStdOut();
    std::string err = vmoutput.GetAndResetStdErr();
    ReleaseAssert(out == "610\n");
    ReleaseAssert(err == "");
}

TEST(LuaTest, LinearSieve)
{
    VM* vm = VM::Create();
    Auto(vm->Destroy());
    VMOutputInterceptor vmoutput(vm);

    ScriptModule* module = ScriptModule::ParseFromJSON(vm, LoadFile("luatests/linear_sieve.lua.json"));
    vm->LaunchScript(module);

    std::string out = vmoutput.GetAndResetStdOut();
    std::string err = vmoutput.GetAndResetStdErr();

    ReleaseAssert(out == "9592\n");
    ReleaseAssert(err == "");
}

TEST(LuaTest, NaNEdgeCase)
{
    VM* vm = VM::Create();
    Auto(vm->Destroy());
    VMOutputInterceptor vmoutput(vm);

    ScriptModule* module = ScriptModule::ParseFromJSON(vm, LoadFile("luatests/nan_edge_case.lua.json"));
    vm->LaunchScript(module);

    std::string out = vmoutput.GetAndResetStdOut();
    std::string err = vmoutput.GetAndResetStdErr();

    ReleaseAssert(out == "1\n0\n0\n0\n0\n0\n1\n0\n1\n0\n0\n0\n0\n1\n0\n0\n0\n0\n0\n1\n1\n0\n0\n0\n0\n0\n1\n0\n0\n0\n1\n1\n0\n1\n1\n1\n");
    ReleaseAssert(err == "");
}

TEST(LuaTest, ForLoopCoercion)
{
    VM* vm = VM::Create();
    Auto(vm->Destroy());
    VMOutputInterceptor vmoutput(vm);

    ScriptModule* module = ScriptModule::ParseFromJSON(vm, LoadFile("luatests/for_loop_coercion.lua.json"));
    vm->LaunchScript(module);

    std::string out = vmoutput.GetAndResetStdOut();
    std::string err = vmoutput.GetAndResetStdErr();

    ReleaseAssert(out == "124\n125\n126\n127\n128\n129\n130\n131\n132\n133\n");
    ReleaseAssert(err == "");
}

TEST(LuaTest, ForLoopEdgeCases)
{
    VM* vm = VM::Create();
    Auto(vm->Destroy());
    VMOutputInterceptor vmoutput(vm);

    ScriptModule* module = ScriptModule::ParseFromJSON(vm, LoadFile("luatests/for_loop_edge_cases.lua.json"));
    vm->LaunchScript(module);

    std::string out = vmoutput.GetAndResetStdOut();
    std::string err = vmoutput.GetAndResetStdErr();

    ReleaseAssert(out == "test1\ntest2\n5\n5\n5\ntest3\ntest4\ntest5\ntest6\ntest7\ntest8\ntest9\ntest10\n");
    ReleaseAssert(err == "");
}

TEST(LuaTest, PrimitiveConstants)
{
    VM* vm = VM::Create();
    Auto(vm->Destroy());
    VMOutputInterceptor vmoutput(vm);

    ScriptModule* module = ScriptModule::ParseFromJSON(vm, LoadFile("luatests/primitive_constant.lua.json"));
    vm->LaunchScript(module);

    std::string out = vmoutput.GetAndResetStdOut();
    std::string err = vmoutput.GetAndResetStdErr();

    ReleaseAssert(out == "0\n1\n0\n0\n0\n0\n0\n0\n1\n0\n0\n0\n0\n0\n0\n1\n0\n0\n1\n0\n1\n1\n1\n1\n1\n1\n0\n1\n1\n1\n1\n1\n1\n0\n1\n1\n0\nfalse\ntrue\nnil\n0\ntrue\n0\ntrue\n");
    ReleaseAssert(err == "");
}

TEST(LuaTest, LogicalOpSanity)
{
    VM* vm = VM::Create();
    Auto(vm->Destroy());
    VMOutputInterceptor vmoutput(vm);

    ScriptModule* module = ScriptModule::ParseFromJSON(vm, LoadFile("luatests/logical_op_sanity.lua.json"));
    vm->LaunchScript(module);

    std::string out = vmoutput.GetAndResetStdOut();
    std::string err = vmoutput.GetAndResetStdErr();

    ReleaseAssert(out == "f1\n2\n4\n5\n7\n9\n11\n13\n15\nf2\n17\n19\n22\n24\n26\n28\n30\n32\nf3\nnil\nfalse\n35\n36\n37\n38\n39\n40\n");
    ReleaseAssert(err == "");
}

TEST(LuaTest, PositiveAndNegativeInf)
{
    VM* vm = VM::Create();
    Auto(vm->Destroy());
    VMOutputInterceptor vmoutput(vm);

    ScriptModule* module = ScriptModule::ParseFromJSON(vm, LoadFile("luatests/pos_and_neg_inf.lua.json"));
    vm->LaunchScript(module);

    std::string out = vmoutput.GetAndResetStdOut();
    std::string err = vmoutput.GetAndResetStdErr();

    ReleaseAssert(out == "inf\n-inf\n");
    ReleaseAssert(err == "");
}

TEST(LuaTest, LogicalNot)
{
    VM* vm = VM::Create();
    Auto(vm->Destroy());
    VMOutputInterceptor vmoutput(vm);

    ScriptModule* module = ScriptModule::ParseFromJSON(vm, LoadFile("luatests/logical_not.lua.json"));
    vm->LaunchScript(module);

    std::string out = vmoutput.GetAndResetStdOut();
    std::string err = vmoutput.GetAndResetStdErr();

    ReleaseAssert(out == "true\ntrue\nfalse\nfalse\nfalse\nfalse\nfalse\n");
    ReleaseAssert(err == "");
}

TEST(LuaTest, LengthOperator)
{
    VM* vm = VM::Create();
    Auto(vm->Destroy());
    VMOutputInterceptor vmoutput(vm);

    ScriptModule* module = ScriptModule::ParseFromJSON(vm, LoadFile("luatests/length_operator.lua.json"));
    vm->LaunchScript(module);

    std::string out = vmoutput.GetAndResetStdOut();
    std::string err = vmoutput.GetAndResetStdErr();

    ReleaseAssert(out == "sanity test\n5\n3\n4\n6\nstress test\ntest end\n");
    ReleaseAssert(err == "");
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
    delete [] rc->m_stackBegin;
    rc->m_stackBegin = new TValue[200];

    vm->LaunchScript(module);

    std::string out = vmoutput.GetAndResetStdOut();
    std::string err = vmoutput.GetAndResetStdErr();

    ReleaseAssert(out == "\n100001\n123\t456\t789\t10\n100024\n");
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
    delete [] rc->m_stackBegin;
    rc->m_stackBegin = new TValue[200];

    vm->LaunchScript(module);

    std::string out = vmoutput.GetAndResetStdOut();
    std::string err = vmoutput.GetAndResetStdErr();

    ReleaseAssert(out == "\n100000\n");
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
    delete [] rc->m_stackBegin;
    rc->m_stackBegin = new TValue[200];

    vm->LaunchScript(module);

    std::string out = vmoutput.GetAndResetStdOut();
    std::string err = vmoutput.GetAndResetStdErr();

    ReleaseAssert(out == "\n100000\t80000\t20000\t120000\t40000\t120000\t60000\t80000\t80000\t0\t100000\t0\t100000\n");
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
    delete [] rc->m_stackBegin;
    rc->m_stackBegin = new TValue[200];

    vm->LaunchScript(module);

    std::string out = vmoutput.GetAndResetStdOut();
    std::string err = vmoutput.GetAndResetStdErr();

    ReleaseAssert(out == "\n100000\t50000\t25000\n");
    ReleaseAssert(err == "");
}

TEST(LuaTest, OpcodeKNIL)
{
    VM* vm = VM::Create();
    Auto(vm->Destroy());
    VMOutputInterceptor vmoutput(vm);

    ScriptModule* module = ScriptModule::ParseFromJSON(vm, LoadFile("luatests/test_knil.lua.json"));

    // Manually lower the stack size
    //
    CoroutineRuntimeContext* rc = vm->GetRootCoroutine();
    delete [] rc->m_stackBegin;
    rc->m_stackBegin = new TValue[200];

    vm->LaunchScript(module);

    std::string out = vmoutput.GetAndResetStdOut();
    std::string err = vmoutput.GetAndResetStdErr();

    ReleaseAssert(out == "nil\tnil\tnil\tnil\tnil\tnil\tnil\n1\tnil\ta\tnil\t1.2\tnil\tnil\n");
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
    VM* vm = VM::Create();
    Auto(vm->Destroy());
    VMOutputInterceptor vmoutput(vm);

    ScriptModule* module = ScriptModule::ParseFromJSON(vm, LoadFile("luatests/negative_zero_as_index.lua.json"));
    vm->LaunchScript(module);

    std::string out = vmoutput.GetAndResetStdOut();
    std::string err = vmoutput.GetAndResetStdErr();

    ReleaseAssert(out == "2\t2\n");
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
    VM* vm = VM::Create();
    Auto(vm->Destroy());
    VMOutputInterceptor vmoutput(vm);

    ScriptModule* module = ScriptModule::ParseFromJSON(vm, LoadFile("luatests/arithmetic_sanity.lua.json"));
    vm->LaunchScript(module);

    std::string out = vmoutput.GetAndResetStdOut();
    std::string err = vmoutput.GetAndResetStdErr();

    std::string expectedOut =
            "5\n-5\n-1\n1\n6\n6\n0.66666666666667\n0.66666666666667\n3\n-3\n-1\n1\n8\n1.4142135623731\n0.70710678118655\n-0.125\nnan\n";

    ReleaseAssert(out == expectedOut);
    ReleaseAssert(err == "");
}

TEST(LuaTest, StringConcat)
{
    VM* vm = VM::Create();
    Auto(vm->Destroy());
    VMOutputInterceptor vmoutput(vm);

    ScriptModule* module = ScriptModule::ParseFromJSON(vm, LoadFile("luatests/string_concat.lua.json"));
    vm->LaunchScript(module);

    std::string out = vmoutput.GetAndResetStdOut();
    std::string err = vmoutput.GetAndResetStdErr();

    std::string expectedOut =
            "abcdefgHIJ\na2340.66666666666667-1024g\n";

    ReleaseAssert(out == expectedOut);
    ReleaseAssert(err == "");
}

TEST(LuaTest, TableVariadicPut)
{
    VM* vm = VM::Create();
    Auto(vm->Destroy());
    VMOutputInterceptor vmoutput(vm);

    ScriptModule* module = ScriptModule::ParseFromJSON(vm, LoadFile("luatests/table_variadic_put.lua.json"));
    vm->LaunchScript(module);

    std::string out = vmoutput.GetAndResetStdOut();
    std::string err = vmoutput.GetAndResetStdErr();

    std::string expectedOut =
            "nil\t1\t2\t3\tnil\n123\t456\tnil\t1\t2\t3\tnil\n4\tnil\tnil\tccc\tddd\t1\t2\t3\t4\tnil\nnil\tnil\tnil\tddd\tccc\tnil\nnil\tnil\tnil\tnil\nnil\nnil\tnil\tnil\t1\t2\t3\t4\tnil\ta\tnil\tnil\tnil\tnil\tnil\n";

    ReleaseAssert(out == expectedOut);
    ReleaseAssert(err == "");
}

TEST(LuaBenchmark, NBody)
{
    VM* vm = VM::Create();
    Auto(vm->Destroy());
    VMOutputInterceptor vmoutput(vm);

    ScriptModule* module = ScriptModule::ParseFromJSON(vm, LoadFile("luatests/n-body.lua.json"));
    vm->LaunchScript(module);

    std::string out = vmoutput.GetAndResetStdOut();
    std::string err = vmoutput.GetAndResetStdErr();

    ReleaseAssert(out == "-0.16907516382852\n-0.16908760523461\n");
    ReleaseAssert(err == "");
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
    delete [] rc->m_stackBegin;
    rc->m_stackBegin = new TValue[1000000];

    vm->LaunchScript(module);

    std::string out = vmoutput.GetAndResetStdOut();
    std::string err = vmoutput.GetAndResetStdErr();

    ReleaseAssert(out == "2045\n");
    ReleaseAssert(err == "");
}

TEST(LuaBenchmark, BinaryTrees_1)
{
    VM* vm = VM::Create();
    Auto(vm->Destroy());
    VMOutputInterceptor vmoutput(vm);

    ScriptModule* module = ScriptModule::ParseFromJSON(vm, LoadFile("luatests/binary-trees-1.lua.json"));
    vm->LaunchScript(module);

    std::string out = vmoutput.GetAndResetStdOut();
    std::string err = vmoutput.GetAndResetStdErr();

    std::string expectedOut =
            "stretch tree of depth 7\t check: -1\n"
            "128\t trees of depth 4\t check: -128\n"
            "32\t trees of depth 6\t check: -32\n"
            "long lived tree of depth 6\t check: -1\n";

    ReleaseAssert(out == expectedOut);
    ReleaseAssert(err == "");
}

TEST(LuaBenchmark, BinaryTrees_2)
{
    VM* vm = VM::Create();
    Auto(vm->Destroy());
    VMOutputInterceptor vmoutput(vm);

    ScriptModule* module = ScriptModule::ParseFromJSON(vm, LoadFile("luatests/binary-trees-2.lua.json"));
    vm->LaunchScript(module);

    std::string out = vmoutput.GetAndResetStdOut();
    std::string err = vmoutput.GetAndResetStdErr();

    std::string expectedOut =
            "stretch tree of depth 7\t check: -1\n"
            "128\t trees of depth 4\t check: -128\n"
            "32\t trees of depth 6\t check: -32\n"
            "long lived tree of depth 6\t check: -1\n";

    ReleaseAssert(out == expectedOut);
    ReleaseAssert(err == "");
}

TEST(LuaBenchmark, Fannkuch_Redux)
{
    VM* vm = VM::Create();
    Auto(vm->Destroy());
    VMOutputInterceptor vmoutput(vm);

    ScriptModule* module = ScriptModule::ParseFromJSON(vm, LoadFile("luatests/fannkuch-redux.lua.json"));
    vm->LaunchScript(module);

    std::string out = vmoutput.GetAndResetStdOut();
    std::string err = vmoutput.GetAndResetStdErr();

    std::string expectedOut =
            "228\n"
            "Pfannkuchen(7) = 16\n";

    ReleaseAssert(out == expectedOut);
    ReleaseAssert(err == "");
}

TEST(LuaBenchmark, Fixpoint_Fact)
{
    VM* vm = VM::Create();
    Auto(vm->Destroy());
    VMOutputInterceptor vmoutput(vm);

    ScriptModule* module = ScriptModule::ParseFromJSON(vm, LoadFile("luatests/fixpoint-fact.lua.json"));
    vm->LaunchScript(module);

    std::string out = vmoutput.GetAndResetStdOut();
    std::string err = vmoutput.GetAndResetStdErr();

    std::string expectedOut =
            "9.426900168371e+157\n";

    ReleaseAssert(out == expectedOut);
    ReleaseAssert(err == "");
}

TEST(LuaBenchmark, Mandel_NoMetatable)
{
    VM* vm = VM::Create();
    Auto(vm->Destroy());
    VMOutputInterceptor vmoutput(vm);

    ScriptModule* module = ScriptModule::ParseFromJSON(vm, LoadFile("luatests/mandel-nometatable.lua.json"));
    vm->LaunchScript(module);

    std::string out = vmoutput.GetAndResetStdOut();
    std::string err = vmoutput.GetAndResetStdErr();

    std::string expectedOut =
            "P2\n"
            "# mandelbrot set\t-2\t2\t-2\t2\t32\n"
            "32\t32\t255\n"
            "28620\n";

    ReleaseAssert(out == expectedOut);
    ReleaseAssert(err == "");
}

TEST(LuaBenchmark, Mandel)
{
    VM* vm = VM::Create();
    Auto(vm->Destroy());
    VMOutputInterceptor vmoutput(vm);

    ScriptModule* module = ScriptModule::ParseFromJSON(vm, LoadFile("luatests/mandel.lua.json"));
    vm->LaunchScript(module);

    std::string out = vmoutput.GetAndResetStdOut();
    std::string err = vmoutput.GetAndResetStdErr();

    std::string expectedOut =
            "P2\n"
            "# mandelbrot set\t-2\t2\t-2\t2\t32\n"
            "32\t32\t255\n"
            "28620\n";

    ReleaseAssert(out == expectedOut);
    ReleaseAssert(err == "");
}

TEST(LuaBenchmark, QuadTree)
{
    VM* vm = VM::Create();
    Auto(vm->Destroy());
    VMOutputInterceptor vmoutput(vm);

    ScriptModule* module = ScriptModule::ParseFromJSON(vm, LoadFile("luatests/qt.lua.json"));
    vm->LaunchScript(module);

    std::string out = vmoutput.GetAndResetStdOut();
    std::string err = vmoutput.GetAndResetStdErr();

    std::string expectedOut =
            "\nstep\t1\ncolor\t0\nprewhite\t4\nprewhite\t0\ncolorup\t0\narea\t1\t0\t1\nedges\t16\n"
            "\nstep\t2\ncolor\t0\nprewhite\t14\nprewhite\t0\ncolorup\t0\narea\t0.875\t0\t0.875\nedges\t100\n"
            "\nstep\t3\ncolor\t8\ncolor\t0\nprewhite\t34\nprewhite\t0\ncolorup\t0\narea\t0.53125\t0\t0.53125\nedges\t340\n"
            "\nstep\t4\ncolor\t30\ncolor\t6\ncolor\t0\nprewhite\t70\nprewhite\t18\nprewhite\t0\ncolorup\t6\narea\t0.34375\t0\t0.34375\nedges\t980\n"
            "\nstep\t5\ncolor\t78\ncolor\t13\ncolor\t1\ncolor\t0\nprewhite\t161\nprewhite\t79\nprewhite\t16\nprewhite\t0\ncolorup\t18\narea\t0.25\t0\t0.25\nedges\t2564\n"
            "\nstep\t6\ncolor\t188\ncolor\t62\ncolor\t15\ncolor\t3\ncolor\t0\nprewhite\t396\nprewhite\t223\nprewhite\t128\nprewhite\t9\nprewhite\t0\ncolorup\t56\narea\t0.1845703125\t0\t0.1845703125\nedges\t7510\n"
            "\nstep\t7\ncolor\t365\ncolor\t115\ncolor\t45\ncolor\t9\ncolor\t6\ncolor\t0\nprewhite\t1048\nprewhite\t630\nprewhite\t588\nprewhite\t196\nprewhite\t22\nprewhite\t0\ncolorup\t114\narea\t0.151611328125\t0\t0.151611328125\nedges\t22812\n";

    ReleaseAssert(out == expectedOut);
    ReleaseAssert(err == "");
}

TEST(LuaBenchmark, Queen)
{
    VM* vm = VM::Create();
    Auto(vm->Destroy());
    VMOutputInterceptor vmoutput(vm);

    ScriptModule* module = ScriptModule::ParseFromJSON(vm, LoadFile("luatests/queen.lua.json"));
    vm->LaunchScript(module);

    std::string out = vmoutput.GetAndResetStdOut();
    std::string err = vmoutput.GetAndResetStdErr();
    std::string expectedOut = LoadFile("luatests/queen.expected.out");

    ReleaseAssert(out == expectedOut);
    ReleaseAssert(err == "");
}

TEST(LuaBenchmark, NlgN_Sieve)
{
    VM* vm = VM::Create();
    Auto(vm->Destroy());
    VMOutputInterceptor vmoutput(vm);

    ScriptModule* module = ScriptModule::ParseFromJSON(vm, LoadFile("luatests/nlgn_sieve.lua.json"));
    vm->LaunchScript(module);

    std::string out = vmoutput.GetAndResetStdOut();
    std::string err = vmoutput.GetAndResetStdErr();
    std::string expectedOut = "10\t8192\nCount: \t1028\n";

    ReleaseAssert(out == expectedOut);
    ReleaseAssert(err == "");
}

TEST(LuaBenchmark, Spectral_Norm)
{
    VM* vm = VM::Create();
    Auto(vm->Destroy());
    VMOutputInterceptor vmoutput(vm);

    ScriptModule* module = ScriptModule::ParseFromJSON(vm, LoadFile("luatests/spectral-norm.lua.json"));
    vm->LaunchScript(module);

    std::string out = vmoutput.GetAndResetStdOut();
    std::string err = vmoutput.GetAndResetStdErr();
    std::string expectedOut = "1.2742199912349\n";

    ReleaseAssert(out == expectedOut);
    ReleaseAssert(err == "");
}

TEST(LuaTest, xpcall_1)
{
    VM* vm = VM::Create();
    Auto(vm->Destroy());
    VMOutputInterceptor vmoutput(vm);

    ScriptModule* module = ScriptModule::ParseFromJSON(vm, LoadFile("luatests/xpcall_1.lua.json"));
    vm->LaunchScript(module);

    std::string out = vmoutput.GetAndResetStdOut();
    std::string err = vmoutput.GetAndResetStdErr();
    std::string expectedOut =
            "test 1\n"
            "enter f_bad\tnil\tnil\n"
            "enter err handler\ttrue\tnil\n"
            "false\t123\n"
            "test 2\n"
            "enter f_good\tnil\tnil\n"
            "true\t233\t2333\n";

    ReleaseAssert(out == expectedOut);
    ReleaseAssert(err == "");
}

TEST(LuaTest, xpcall_2)
{
    VM* vm = VM::Create();
    Auto(vm->Destroy());
    VMOutputInterceptor vmoutput(vm);

    ScriptModule* module = ScriptModule::ParseFromJSON(vm, LoadFile("luatests/xpcall_2.lua.json"));
    vm->LaunchScript(module);

    std::string out = vmoutput.GetAndResetStdOut();
    std::string err = vmoutput.GetAndResetStdErr();
    std::string expectedOut =
            "enter f\tnil\tnil\n"
            "enter g\ttrue\tnil\n"
            "throwing error\n"
            "enter g\ttrue\tnil\n"
            "throwing error\n"
            "enter g\tfalse\tnil\n"
            "throwing error\n"
            "enter g\ttrue\tnil\n"
            "throwing error\n"
            "enter g\tfalse\tnil\n"
            "throwing error\n"
            "enter g\ttrue\tnil\n"
            "throwing error\n"
            "enter g\tfalse\tnil\n"
            "throwing error\n"
            "enter g\ttrue\tnil\n"
            "throwing error\n"
            "enter g\tfalse\tnil\n"
            "throwing error\n"
            "enter g\ttrue\tnil\n"
            "false\t1\n";

    ReleaseAssert(out == expectedOut);
    ReleaseAssert(err == "");
}

TEST(LuaTest, xpcall_3)
{
    VM* vm = VM::Create();
    Auto(vm->Destroy());
    VMOutputInterceptor vmoutput(vm);

    ScriptModule* module = ScriptModule::ParseFromJSON(vm, LoadFile("luatests/xpcall_3.lua.json"));
    vm->LaunchScript(module);

    std::string out = vmoutput.GetAndResetStdOut();
    std::string err = vmoutput.GetAndResetStdErr();
    std::string expectedOut =
            "enter f\tnil\tnil\n"
            "enter g\ttrue\tnil\n"
            "doing xpcall\n"
            "enter f\tnil\tnil\n"
            "enter g\ttrue\tnil\n"
            "doing xpcall\n"
            "enter f\tnil\tnil\n"
            "enter g\ttrue\tnil\n"
            "doing xpcall\n"
            "enter f\tnil\tnil\n"
            "enter g\ttrue\tnil\n"
            "doing xpcall\n"
            "enter f\tnil\tnil\n"
            "enter g\ttrue\tnil\n"
            "returned from xpcall\tfalse\t14\n"
            "returned from xpcall\tfalse\t3\n"
            "returned from xpcall\tfalse\t2\n"
            "returned from xpcall\tfalse\t1\n"
            "false\t0\n";

    ReleaseAssert(out == expectedOut);
    ReleaseAssert(err == "");
}

TEST(LuaTest, xpcall_4)
{
    VM* vm = VM::Create();
    Auto(vm->Destroy());
    VMOutputInterceptor vmoutput(vm);

    ScriptModule* module = ScriptModule::ParseFromJSON(vm, LoadFile("luatests/xpcall_4.lua.json"));
    vm->LaunchScript(module);

    std::string out = vmoutput.GetAndResetStdOut();
    std::string err = vmoutput.GetAndResetStdErr();

    std::string expectedOut =
            "test1\n"
            "enter h\n"
            "false\t123\n"
            "test2\n"
            "enter h\n"
            "false\t123\n"
            "test3\n"
            "enter f\tnil\n"
            "true\t123\t456\t789\n"
            "test4\n"
            "enter f2\tnil\n"
            "false\terror in error handling\n"
            "test5\n"
            "false\terror in error handling\n"
            "test6\n"
            "false\terror in error handling\n"
            "test7\n"
            "enter h\tattempt to call a nil value\n"
            "false\t124\n"
            "test8\n"
            "enter h\tattempt to call a table value\n"
            "false\t124\n"
            "test10\n"
            "false\terror in error handling\n"
            "test11\n"
            "enter f3\tnil\tnil\n"
            "true\t21\t43\n"
            "test12\n"
            "enter f4\tnil\tnil\n"
            "enter h\n"
            "false\t123\n";

    ReleaseAssert(out == expectedOut);
    ReleaseAssert(err == "");
}

TEST(LuaTest, xpcall_5)
{
    VM* vm = VM::Create();
    Auto(vm->Destroy());
    VMOutputInterceptor vmoutput(vm);

    ScriptModule* module = ScriptModule::ParseFromJSON(vm, LoadFile("luatests/xpcall_5.lua.json"));
    vm->LaunchScript(module);

    std::string out = vmoutput.GetAndResetStdOut();
    std::string err = vmoutput.GetAndResetStdErr();

    std::string expectedOut =
            "enter g\tattempt to call a nil value\tnil\n"
            "throwing error\n"
            "enter g\ttrue\tnil\n"
            "throwing error\n"
            "enter g\tfalse\tnil\n"
            "throwing error\n"
            "enter g\ttrue\tnil\n"
            "throwing error\n"
            "enter g\tfalse\tnil\n"
            "throwing error\n"
            "enter g\ttrue\tnil\n"
            "throwing error\n"
            "enter g\tfalse\tnil\n"
            "throwing error\n"
            "enter g\ttrue\tnil\n"
            "throwing error\n"
            "enter g\tfalse\tnil\n"
            "throwing error\n"
            "enter g\ttrue\tnil\n"
            "false\t1\n";

    ReleaseAssert(out == expectedOut);
    ReleaseAssert(err == "");
}

TEST(LuaTest, xpcall_6)
{
    VM* vm = VM::Create();
    Auto(vm->Destroy());
    VMOutputInterceptor vmoutput(vm);

    ScriptModule* module = ScriptModule::ParseFromJSON(vm, LoadFile("luatests/xpcall_6.lua.json"));
    vm->LaunchScript(module);

    std::string out = vmoutput.GetAndResetStdOut();
    std::string err = vmoutput.GetAndResetStdErr();

    std::string expectedOut =
            "enter g\tattempt to call a nil value\tnil\n"
            "doing xpcall\n"
            "enter g\tattempt to call a nil value\tnil\n"
            "doing xpcall\n"
            "enter g\tattempt to call a nil value\tnil\n"
            "doing xpcall\n"
            "enter g\tattempt to call a nil value\tnil\n"
            "doing xpcall\n"
            "enter g\tattempt to call a nil value\tnil\n"
            "returned from xpcall\tfalse\t14\n"
            "returned from xpcall\tfalse\t3\n"
            "returned from xpcall\tfalse\t2\n"
            "returned from xpcall\tfalse\t1\n"
            "false\t0\n";

    ReleaseAssert(out == expectedOut);
    ReleaseAssert(err == "");
}

TEST(LuaTest, pcall_1)
{
    VM* vm = VM::Create();
    Auto(vm->Destroy());
    VMOutputInterceptor vmoutput(vm);

    ScriptModule* module = ScriptModule::ParseFromJSON(vm, LoadFile("luatests/pcall_1.lua.json"));
    vm->LaunchScript(module);

    std::string out = vmoutput.GetAndResetStdOut();
    std::string err = vmoutput.GetAndResetStdErr();
    std::string expectedOut =
            "test 1\n"
            "enter f_bad\t1\t2\n"
            "false\ttrue\n"
            "test 2\n"
            "enter f_good\t1\t2\n"
            "true\t233\t2333\n";

    ReleaseAssert(out == expectedOut);
    ReleaseAssert(err == "");
}

TEST(LuaTest, pcall_2)
{
    VM* vm = VM::Create();
    Auto(vm->Destroy());
    VMOutputInterceptor vmoutput(vm);

    ScriptModule* module = ScriptModule::ParseFromJSON(vm, LoadFile("luatests/pcall_2.lua.json"));
    vm->LaunchScript(module);

    std::string out = vmoutput.GetAndResetStdOut();
    std::string err = vmoutput.GetAndResetStdErr();
    std::string expectedOut =
            "enter f\n"
            "false\t123\n"
            "false\tattempt to call a nil value\n"
            "false\tattempt to call a nil value\n"
            "false\tattempt to call a number value\n"
            "false\tattempt to call a table value\n"
            "enter g\tnil\tnil\tnil\n"
            "true\t321\t654\n"
            "enter g\t1\tnil\tnil\n"
            "true\t321\t654\n"
            "enter g\t1\t2\t3\n"
            "true\t321\t654\n"
            "enter g\t1\t2\t3\n"
            "true\t321\t654\n"
            "enter g2\tnil\tnil\tnil\t0\tnil\tnil\tnil\tnil\tnil\tnil\tnil\tnil\n"
            "true\t233\t124\n"
            "enter g2\t1\tnil\tnil\t0\tnil\tnil\tnil\tnil\tnil\tnil\tnil\tnil\n"
            "true\t233\t124\n"
            "enter g2\t1\t2\t3\t0\tnil\tnil\tnil\tnil\tnil\tnil\tnil\tnil\n"
            "true\t233\t124\n"
            "enter g2\t1\t2\t3\t2\tnil\t4\t5\tnil\tnil\tnil\tnil\tnil\n"
            "true\t233\t124\n"
            "enter g2\t1\t2\t3\t4\tnil\t4\t5\t6\t7\tnil\tnil\tnil\n"
            "true\t233\t124\n"
            "enter g2\t1\t2\t3\t6\tnil\t4\t5\t6\t7\t8\t9\tnil\n"
            "true\t233\t124\n";

    ReleaseAssert(out == expectedOut);
    ReleaseAssert(err == "");
}

TEST(LuaTest, GetSetMetatable)
{
    VM* vm = VM::Create();
    Auto(vm->Destroy());
    VMOutputInterceptor vmoutput(vm);

    ScriptModule* module = ScriptModule::ParseFromJSON(vm, LoadFile("luatests/get_set_metatable.lua.json"));
    vm->LaunchScript(module);

    std::string out = vmoutput.GetAndResetStdOut();
    std::string err = vmoutput.GetAndResetStdErr();

    std::string expectedOut =
            "--- part 1 ---\n"
            "false\n"
            "false\n"
            "false\n"
            "false\n"
            "false\n"
            "false\n"
            "false\n"
            "--- part 2 ---\n"
            "nil\n"
            "nil\n"
            "nil\n"
            "nil\n"
            "nil\n"
            "nil\n"
            "nil\n"
            "nil\n"
            "nil\n"
            "nil\n"
            "nil\n"
            "nil\n"
            "--- part 3 ---\n"
            "nil\n"
            "nil\n"
            "t\n"
            "mt_t\n"
            "mt_t\n"
            "abcdefg\n"
            "mt_t\n"
            "false\n"
            "true\n"
            "overwritten\n"
            "overwritten\n"
            "--- part 4 ---\n"
            "false\n"
            "false\n"
            "false\n"
            "false\n"
            "false\n"
            "false\n"
            "--- part 5 ---\n"
            "true\n"
            "true\n"
            "true\n"
            "nil_mt\n"
            "nil_mt\n"
            "bool_mt\n"
            "bool_mt\n"
            "bool_mt\n"
            "bool_mt\n"
            "number_mt\n"
            "number_mt\n"
            "func_mt\n"
            "func_mt\n"
            "string_mt\n"
            "string_mt\n"
            "--- part 6 ---\n"
            "protect!\n"
            "nil_mt\n"
            "true\n"
            "nil_mt_2\n"
            "--- part 7 ---\n"
            "true\n"
            "nil\n"
            "false\n"
            "true\n"
            "nil\n"
            "nil\n"
            "t\n"
            "mt_t_2\n"
            "t\n"
            "nil\n"
            "nil\n";

    ReleaseAssert(out == expectedOut);
    ReleaseAssert(err == "");
}

TEST(LuaTest, metatable_call_1)
{
    VM* vm = VM::Create();
    Auto(vm->Destroy());
    VMOutputInterceptor vmoutput(vm);

    ScriptModule* module = ScriptModule::ParseFromJSON(vm, LoadFile("luatests/metatable_call_1.lua.json"));
    vm->LaunchScript(module);

    std::string out = vmoutput.GetAndResetStdOut();
    std::string err = vmoutput.GetAndResetStdErr();

    std::string expectedOut =
            "t1\t1\tnil\tnil\n"
            "1\t2\t3\n"
            "3\t4\tnil\tnil\n"
            "2\t3\t4\n"
            "nil\t5\tnil\tnil\n"
            "2\t3\t4\n"
            "false\t6\tnil\tnil\n"
            "2\t3\t4\n"
            "true\t7\tnil\tnil\n"
            "2\t3\t4\n"
            "false\n"
            "false\n"
            "false\n"
            "false\n";

    ReleaseAssert(out == expectedOut);
    ReleaseAssert(err == "");
}

TEST(LuaTest, metatable_call_2)
{
    VM* vm = VM::Create();
    Auto(vm->Destroy());
    VMOutputInterceptor vmoutput(vm);

    ScriptModule* module = ScriptModule::ParseFromJSON(vm, LoadFile("luatests/metatable_call_2.lua.json"));
    vm->LaunchScript(module);

    std::string out = vmoutput.GetAndResetStdOut();
    std::string err = vmoutput.GetAndResetStdErr();

    std::string expectedOut =
            "h x \n"
            "h h x \n"
            "h h h x \n"
            "h h h h x \n"
            "h h h h h x \n"
            "h h h h h h x \n"
            "h h h h h h h x \n"
            "h h h h h h h h x \n"
            "h h h h h h h h h x \n"
            "123\t456\n";

    ReleaseAssert(out == expectedOut);
    ReleaseAssert(err == "");
}

TEST(LuaTest, metatable_call_3)
{
    VM* vm = VM::Create();
    Auto(vm->Destroy());
    VMOutputInterceptor vmoutput(vm);

    ScriptModule* module = ScriptModule::ParseFromJSON(vm, LoadFile("luatests/metatable_call_3.lua.json"));
    vm->LaunchScript(module);

    std::string out = vmoutput.GetAndResetStdOut();
    std::string err = vmoutput.GetAndResetStdErr();

    std::string expectedOut =
            "h x \n"
            "h h x \n"
            "h h h x \n"
            "h h h h x \n"
            "h h h h h x \n"
            "h h h h h h x \n"
            "h h h h h h h x \n"
            "h h h h h h h h x \n"
            "h h h h h h h h h x \n"
            "2\t3\t4\t5\t6\t7\t8\t9\t123\t456\n";

    ReleaseAssert(out == expectedOut);
    ReleaseAssert(err == "");
}

TEST(LuaTest, metatable_call_4)
{
    VM* vm = VM::Create();
    Auto(vm->Destroy());
    VMOutputInterceptor vmoutput(vm);

    ScriptModule* module = ScriptModule::ParseFromJSON(vm, LoadFile("luatests/metatable_call_4.lua.json"));
    vm->LaunchScript(module);

    std::string out = vmoutput.GetAndResetStdOut();
    std::string err = vmoutput.GetAndResetStdErr();

    std::string expectedOut =
            "h x \n"
            "h h x \n"
            "h h h x \n"
            "h h h h x \n"
            "h h h h h x \n"
            "h h h h h h x \n"
            "h h h h h h h x \n"
            "h h h h h h h h x \n"
            "h h h h h h h h h x \n"
            "h h h h h h h h h h x \n"
            "h h h h h h h h h x \n"
            "h h h h h h h h x \n"
            "h h h h h h h x \n"
            "h h h h h h x \n"
            "h h h h h x \n"
            "h h h h x \n"
            "h h h x \n"
            "h h x \n"
            "h x \n"
            "h \n"
            "123\t456\n";

    ReleaseAssert(out == expectedOut);
    ReleaseAssert(err == "");
}

TEST(LuaTest, metatable_call_5)
{
    VM* vm = VM::Create();
    Auto(vm->Destroy());
    VMOutputInterceptor vmoutput(vm);

    ScriptModule* module = ScriptModule::ParseFromJSON(vm, LoadFile("luatests/metatable_call_5.lua.json"));
    vm->LaunchScript(module);

    std::string out = vmoutput.GetAndResetStdOut();
    std::string err = vmoutput.GetAndResetStdErr();

    std::string expectedOut =
            "h x \n"
            "h h x \n"
            "h h h x \n"
            "h h h h x \n"
            "h h h h h x \n"
            "h h h h h h x \n"
            "h h h h h h h x \n"
            "h h h h h h h h x \n"
            "h h h h h h h h h x \n"
            "h h h h h h h h h h x \n"
            "h h h h h h h h h x \n"
            "h h h h h h h h x \n"
            "h h h h h h h x \n"
            "h h h h h h x \n"
            "h h h h h x \n"
            "h h h h x \n"
            "h h h x \n"
            "h h x \n"
            "h x \n"
            "h \n"
            "2\t3\t4\t5\t6\t7\t8\t9\t10\t11\t10\t9\t8\t7\t6\t5\t4\t3\t2\t123\t456\n";

    ReleaseAssert(out == expectedOut);
    ReleaseAssert(err == "");
}

TEST(LuaTest, xpcall_metatable)
{
    VM* vm = VM::Create();
    Auto(vm->Destroy());
    VMOutputInterceptor vmoutput(vm);

    ScriptModule* module = ScriptModule::ParseFromJSON(vm, LoadFile("luatests/xpcall_metatable.lua.json"));
    vm->LaunchScript(module);

    std::string out = vmoutput.GetAndResetStdOut();
    std::string err = vmoutput.GetAndResetStdErr();

    std::string expectedOut =
            "enter g\tgg\tnil\n"
            "enter h\n"
            "true\n"
            "false\t123\n"
            "enter g2\tgg\tnil\n"
            "true\t789\t10\n";

    ReleaseAssert(out == expectedOut);
    ReleaseAssert(err == "");
}

TEST(LuaTest, pcall_metatable)
{
    VM* vm = VM::Create();
    Auto(vm->Destroy());
    VMOutputInterceptor vmoutput(vm);

    ScriptModule* module = ScriptModule::ParseFromJSON(vm, LoadFile("luatests/pcall_metatable.lua.json"));
    vm->LaunchScript(module);

    std::string out = vmoutput.GetAndResetStdOut();
    std::string err = vmoutput.GetAndResetStdErr();

    std::string expectedOut =
            "enter g\tgg\t1\t2\tnil\tnil\n"
            "false\ttrue\n"
            "enter g2\tgg\t1\t2\tnil\tnil\n"
            "true\t1\t2\t3\t4\n";

    ReleaseAssert(out == expectedOut);
    ReleaseAssert(err == "");
}

TEST(LuaTest, metatable_add_1)
{
    VM* vm = VM::Create();
    Auto(vm->Destroy());
    VMOutputInterceptor vmoutput(vm);

    ScriptModule* module = ScriptModule::ParseFromJSON(vm, LoadFile("luatests/metatable_add_1.lua.json"));
    vm->LaunchScript(module);

    std::string out = vmoutput.GetAndResetStdOut();
    std::string err = vmoutput.GetAndResetStdErr();

    std::string expectedOut =
            "a\t2333\tnil\n"
            "12\n"
            "a\t2333\tnil\n"
            "12\n"
            "321\ta\tnil\n"
            "43\n"
            "cba\ta\tnil\n"
            "43\n"
            "f2\ta\tb\tnil\n"
            "67\n"
            "f3\tb\ta\tnil\n"
            "98\n"
            "f2\ta\tb\tnil\n"
            "67\n"
            "f2\tb\ta\tnil\n"
            "67\n"
            "false\ta\t233\tnil\n"
            "123\n"
            "false\ta\tabb\tnil\n"
            "123\n"
            "false\t233\ta\tnil\n"
            "124\n"
            "false\t233\ta\tnil\n"
            "124\n"
            "c\ta\t234\tnil\n"
            "98\n"
            "c\ta\txyz\tnil\n"
            "98\n"
            "c\t432\ta\tnil\n"
            "87\n"
            "c\tzwx\ta\tnil\n"
            "87\n"
            "false\n"
            "false\n"
            "687\n"
            "138\n"
            "139\n"
            "1515\n"
            "723\n";

    ReleaseAssert(out == expectedOut);
    ReleaseAssert(err == "");
}

TEST(LuaTest, metatable_add_2)
{
    VM* vm = VM::Create();
    Auto(vm->Destroy());
    VMOutputInterceptor vmoutput(vm);

    ScriptModule* module = ScriptModule::ParseFromJSON(vm, LoadFile("luatests/metatable_add_2.lua.json"));
    vm->LaunchScript(module);

    std::string out = vmoutput.GetAndResetStdOut();
    std::string err = vmoutput.GetAndResetStdErr();

    std::string expectedOut =
            "5050\n";

    ReleaseAssert(out == expectedOut);
    ReleaseAssert(err == "");
}

TEST(LuaTest, metatable_add_3)
{
    VM* vm = VM::Create();
    Auto(vm->Destroy());
    VMOutputInterceptor vmoutput(vm);

    ScriptModule* module = ScriptModule::ParseFromJSON(vm, LoadFile("luatests/metatable_add_3.lua.json"));
    vm->LaunchScript(module);

    std::string out = vmoutput.GetAndResetStdOut();
    std::string err = vmoutput.GetAndResetStdErr();

    std::string expectedOut =
            "3\n"
            "enter f\ta\t1\tnil\n"
            "nil\n"
            "enter f\t1\ta\tnil\n"
            "nil\n"
            "16.2\n"
            "enter f\t1.2\t0xG\tnil\n"
            "nil\n"
            "16.2\n"
            "enter f\t0xG\t1.2\tnil\n"
            "nil\n"
            "18\n"
            "enter f\t    0xG        \t3\tnil\n"
            "nil\n"
            "30\n"
            "enter f\t0xG\t 0xG \tnil\n"
            "nil\n";

    ReleaseAssert(out == expectedOut);
    ReleaseAssert(err == "");
}

TEST(LuaTest, metatable_sub)
{
    VM* vm = VM::Create();
    Auto(vm->Destroy());
    VMOutputInterceptor vmoutput(vm);

    ScriptModule* module = ScriptModule::ParseFromJSON(vm, LoadFile("luatests/metatable_sub.lua.json"));
    vm->LaunchScript(module);

    std::string out = vmoutput.GetAndResetStdOut();
    std::string err = vmoutput.GetAndResetStdErr();

    std::string expectedOut =
            "a\t2333\tnil\n"
            "12\n"
            "a\t2333\tnil\n"
            "12\n"
            "321\ta\tnil\n"
            "43\n"
            "cba\ta\tnil\n"
            "43\n"
            "f2\ta\tb\tnil\n"
            "67\n"
            "f3\tb\ta\tnil\n"
            "98\n"
            "f2\ta\tb\tnil\n"
            "67\n"
            "f2\tb\ta\tnil\n"
            "67\n"
            "false\ta\t233\tnil\n"
            "123\n"
            "false\ta\tabb\tnil\n"
            "123\n"
            "false\t233\ta\tnil\n"
            "124\n"
            "false\t233\ta\tnil\n"
            "124\n"
            "c\ta\t234\tnil\n"
            "98\n"
            "c\ta\txyz\tnil\n"
            "98\n"
            "c\t432\ta\tnil\n"
            "87\n"
            "c\tzwx\ta\tnil\n"
            "87\n"
            "false\n"
            "false\n"
            "-441\n"
            "108\n"
            "-107\n"
            "-387\n"
            "-405\n"
            "-5050\n"
            "-1\n"
            "enter f\ta\t1\tnil\n"
            "nil\n"
            "enter f\t1\ta\tnil\n"
            "nil\n"
            "-13.8\n"
            "enter f\t1.2\t0xG\tnil\n"
            "nil\n"
            "13.8\n"
            "enter f\t0xG\t1.2\tnil\n"
            "nil\n"
            "12\n"
            "enter f\t    0xG        \t3\tnil\n"
            "nil\n"
            "0\n"
            "enter f\t0xG\t 0xG \tnil\n"
            "nil\n";

    ReleaseAssert(out == expectedOut);
    ReleaseAssert(err == "");
}

TEST(LuaTest, metatable_mul)
{
    VM* vm = VM::Create();
    Auto(vm->Destroy());
    VMOutputInterceptor vmoutput(vm);

    ScriptModule* module = ScriptModule::ParseFromJSON(vm, LoadFile("luatests/metatable_mul.lua.json"));
    vm->LaunchScript(module);

    std::string out = vmoutput.GetAndResetStdOut();
    std::string err = vmoutput.GetAndResetStdErr();

    std::string expectedOut =
            "a\t2333\tnil\n"
            "12\n"
            "a\t2333\tnil\n"
            "12\n"
            "321\ta\tnil\n"
            "43\n"
            "cba\ta\tnil\n"
            "43\n"
            "f2\ta\tb\tnil\n"
            "67\n"
            "f3\tb\ta\tnil\n"
            "98\n"
            "f2\ta\tb\tnil\n"
            "67\n"
            "f2\tb\ta\tnil\n"
            "67\n"
            "false\ta\t233\tnil\n"
            "123\n"
            "false\ta\tabb\tnil\n"
            "123\n"
            "false\t233\ta\tnil\n"
            "124\n"
            "false\t233\ta\tnil\n"
            "124\n"
            "c\ta\t234\tnil\n"
            "98\n"
            "c\ta\txyz\tnil\n"
            "98\n"
            "c\t432\ta\tnil\n"
            "87\n"
            "c\tzwx\ta\tnil\n"
            "87\n"
            "false\n"
            "false\n"
            "69372\n"
            "1845\n"
            "1968\n"
            "536364\n"
            "89676\n"
            "0\n"
            "2\n"
            "enter f\ta\t1\tnil\n"
            "nil\n"
            "enter f\t1\ta\tnil\n"
            "nil\n"
            "18\n"
            "enter f\t1.2\t0xG\tnil\n"
            "nil\n"
            "18\n"
            "enter f\t0xG\t1.2\tnil\n"
            "nil\n"
            "45\n"
            "enter f\t    0xG        \t3\tnil\n"
            "nil\n"
            "225\n"
            "enter f\t0xG\t 0xG \tnil\n"
            "nil\n";

    ReleaseAssert(out == expectedOut);
    ReleaseAssert(err == "");
}

TEST(LuaTest, metatable_div)
{
    VM* vm = VM::Create();
    Auto(vm->Destroy());
    VMOutputInterceptor vmoutput(vm);

    ScriptModule* module = ScriptModule::ParseFromJSON(vm, LoadFile("luatests/metatable_div.lua.json"));
    vm->LaunchScript(module);

    std::string out = vmoutput.GetAndResetStdOut();
    std::string err = vmoutput.GetAndResetStdErr();

    std::string expectedOut =
            "a\t2333\tnil\n"
            "12\n"
            "a\t2333\tnil\n"
            "12\n"
            "321\ta\tnil\n"
            "43\n"
            "cba\ta\tnil\n"
            "43\n"
            "f2\ta\tb\tnil\n"
            "67\n"
            "f3\tb\ta\tnil\n"
            "98\n"
            "f2\ta\tb\tnil\n"
            "67\n"
            "f2\tb\ta\tnil\n"
            "67\n"
            "false\ta\t233\tnil\n"
            "123\n"
            "false\ta\tabb\tnil\n"
            "123\n"
            "false\t233\ta\tnil\n"
            "124\n"
            "false\t233\ta\tnil\n"
            "124\n"
            "c\ta\t234\tnil\n"
            "98\n"
            "c\ta\txyz\tnil\n"
            "98\n"
            "c\t432\ta\tnil\n"
            "87\n"
            "c\tzwx\ta\tnil\n"
            "87\n"
            "false\n"
            "false\n"
            "0.21808510638298\n"
            "8.2\n"
            "0.13008130081301\n"
            "0.59305993690852\n"
            "0.28191489361702\n"
            "0\n"
            "0.5\n"
            "enter f\ta\t1\tnil\n"
            "nil\n"
            "enter f\t1\ta\tnil\n"
            "nil\n"
            "0.08\n"
            "enter f\t1.2\t0xG\tnil\n"
            "nil\n"
            "12.5\n"
            "enter f\t0xG\t1.2\tnil\n"
            "nil\n"
            "5\n"
            "enter f\t    0xG        \t3\tnil\n"
            "nil\n"
            "1\n"
            "enter f\t0xG\t 0xG \tnil\n"
            "nil\n";

    ReleaseAssert(out == expectedOut);
    ReleaseAssert(err == "");
}

TEST(LuaTest, metatable_mod)
{
    VM* vm = VM::Create();
    Auto(vm->Destroy());
    VMOutputInterceptor vmoutput(vm);

    ScriptModule* module = ScriptModule::ParseFromJSON(vm, LoadFile("luatests/metatable_mod.lua.json"));
    vm->LaunchScript(module);

    std::string out = vmoutput.GetAndResetStdOut();
    std::string err = vmoutput.GetAndResetStdErr();

    std::string expectedOut =
            "a\t2333\tnil\n"
            "12\n"
            "a\t2333\tnil\n"
            "12\n"
            "321\ta\tnil\n"
            "43\n"
            "cba\ta\tnil\n"
            "43\n"
            "f2\ta\tb\tnil\n"
            "67\n"
            "f3\tb\ta\tnil\n"
            "98\n"
            "f2\ta\tb\tnil\n"
            "67\n"
            "f2\tb\ta\tnil\n"
            "67\n"
            "false\ta\t233\tnil\n"
            "123\n"
            "false\ta\tabb\tnil\n"
            "123\n"
            "false\t233\ta\tnil\n"
            "124\n"
            "false\t233\ta\tnil\n"
            "124\n"
            "c\ta\t234\tnil\n"
            "98\n"
            "c\ta\txyz\tnil\n"
            "98\n"
            "c\t432\ta\tnil\n"
            "87\n"
            "c\tzwx\ta\tnil\n"
            "87\n"
            "false\n"
            "false\n"
            "123\n"
            "3\n"
            "16\n"
            "564\n"
            "159\n"
            "0\n"
            "1\n"
            "enter f\ta\t1\tnil\n"
            "nil\n"
            "enter f\t1\ta\tnil\n"
            "nil\n"
            "1.2\n"
            "enter f\t1.2\t0xG\tnil\n"
            "nil\n"
            "0.6\n"
            "enter f\t0xG\t1.2\tnil\n"
            "nil\n"
            "0\n"
            "enter f\t    0xG        \t3\tnil\n"
            "nil\n"
            "0\n"
            "enter f\t0xG\t 0xG \tnil\n"
            "nil\n";

    ReleaseAssert(out == expectedOut);
    ReleaseAssert(err == "");
}

TEST(LuaTest, metatable_pow)
{
    VM* vm = VM::Create();
    Auto(vm->Destroy());
    VMOutputInterceptor vmoutput(vm);

    ScriptModule* module = ScriptModule::ParseFromJSON(vm, LoadFile("luatests/metatable_pow.lua.json"));
    vm->LaunchScript(module);

    std::string out = vmoutput.GetAndResetStdOut();
    std::string err = vmoutput.GetAndResetStdErr();

    std::string expectedOut =
            "a\t2333\tnil\n"
            "12\n"
            "a\t2333\tnil\n"
            "12\n"
            "321\ta\tnil\n"
            "43\n"
            "cba\ta\tnil\n"
            "43\n"
            "f2\ta\tb\tnil\n"
            "67\n"
            "f3\tb\ta\tnil\n"
            "98\n"
            "f2\ta\tb\tnil\n"
            "67\n"
            "f2\tb\ta\tnil\n"
            "67\n"
            "false\ta\t233\tnil\n"
            "123\n"
            "false\ta\tabb\tnil\n"
            "123\n"
            "false\t233\ta\tnil\n"
            "124\n"
            "false\t233\ta\tnil\n"
            "124\n"
            "c\ta\t234\tnil\n"
            "98\n"
            "c\ta\txyz\tnil\n"
            "98\n"
            "c\t432\ta\tnil\n"
            "87\n"
            "c\tzwx\ta\tnil\n"
            "87\n"
            "false\n"
            "false\n"
            "inf\n"
            "2.231396109767e+31\n"
            "1.2786682062094e+148\n"
            "inf\n"
            "inf\n"
            "0\n"
            "1\n"
            "enter f\ta\t1\tnil\n"
            "nil\n"
            "enter f\t1\ta\tnil\n"
            "nil\n"
            "15.407021574586\n"
            "enter f\t1.2\t0xG\tnil\n"
            "nil\n"
            "25.781578913812\n"
            "enter f\t0xG\t1.2\tnil\n"
            "nil\n"
            "3375\n"
            "enter f\t    0xG        \t3\tnil\n"
            "nil\n"
            "4.3789389038086e+17\n"
            "enter f\t0xG\t 0xG \tnil\n"
            "nil\n";

    ReleaseAssert(out == expectedOut);
    ReleaseAssert(err == "");
}

TEST(LuaTest, metatable_unm)
{
    VM* vm = VM::Create();
    Auto(vm->Destroy());
    VMOutputInterceptor vmoutput(vm);

    ScriptModule* module = ScriptModule::ParseFromJSON(vm, LoadFile("luatests/metatable_unm.lua.json"));
    vm->LaunchScript(module);

    std::string out = vmoutput.GetAndResetStdOut();
    std::string err = vmoutput.GetAndResetStdErr();

    std::string expectedOut =
            "false\n"
            "a\ta\tnil\n"
            "12\n"
            "a\ta\tnil\n"
            "nil\n"
            "false\n"
            "-233\n"
            "-233\n"
            "-563\n"
            "false\n"
            "-233\n"
            "-563\n"
            "-233\n"
            "-563\n"
            "0x233G\t0x233G\tnil\n"
            "nil\n"
            "b\ta\ta\tnil\n"
            "1234\n"
            "false\n"
            "xxxx\ta\ta\tnil\n"
            "nil\n";

    ReleaseAssert(out == expectedOut);
    ReleaseAssert(err == "");
}

TEST(LuaTest, metatable_len)
{
    VM* vm = VM::Create();
    Auto(vm->Destroy());
    VMOutputInterceptor vmoutput(vm);

    ScriptModule* module = ScriptModule::ParseFromJSON(vm, LoadFile("luatests/metatable_len.lua.json"));
    vm->LaunchScript(module);

    std::string out = vmoutput.GetAndResetStdOut();
    std::string err = vmoutput.GetAndResetStdErr();

    std::string expectedOut =
            "6\n"
            "7\n"
            "7\n"
            "9\n"
            "9\n"
            "9\n"
            "false\n"
            "true\tnil\tnil\tnil\n"
            "123\n"
            "false\tnil\tnil\tnil\n"
            "123\n"
            "false\n"
            "abc\tfalse\tnil\tnil\n"
            "123\n"
            "false\n";

    ReleaseAssert(out == expectedOut);
    ReleaseAssert(err == "");
}

TEST(LuaTest, metatable_concat)
{
    VM* vm = VM::Create();
    Auto(vm->Destroy());
    VMOutputInterceptor vmoutput(vm);

    ScriptModule* module = ScriptModule::ParseFromJSON(vm, LoadFile("luatests/metatable_concat.lua.json"));
    vm->LaunchScript(module);

    std::string out = vmoutput.GetAndResetStdOut();
    std::string err = vmoutput.GetAndResetStdErr();

    std::string expectedOut =
            "enter concat a\ta\t234\n"
            "enter concat b\t123\tb\n"
            "x789345.6qwerty\n"
            "\n"
            "enter concat a\ta\t234x\n"
            "enter concat b\t123\tb\n"
            "789345.6qwerty\n"
            "\n"
            "enter concat a\ta\t234x789345.6\n"
            "enter concat b\t123\tb\n"
            "qwerty\n"
            "\n"
            "enter concat a\ta\t234\n"
            "enter concat b\t123\tb\n"
            "enter concat a\ta\t345.6qwerty\n"
            "enter concat b\tx\tb\n"
            "qwerty\n"
            "\n"
            "enter concat a\ta\t234\n"
            "enter concat str\t123\tb\n"
            "x789345.6asdf\n"
            "\n"
            "enter concat a\ta\t234x\n"
            "enter concat str\t123\tb\n"
            "789345.6asdf\n"
            "\n"
            "enter concat a\ta\t234x789345.6\n"
            "enter concat str\t123\tb\n"
            "asdf\n"
            "\n"
            "enter concat a\ta\t234\n"
            "enter concat str\t123\tb\n"
            "enter concat a\ta\t345.6asdf\n"
            "enter concat str\tx\tb\n"
            "asdf\n"
            "\n"
            "enter len\t1234\n"
            "enter concat c\tc\t1234\t67\n"
            "nil\n"
            "\n"
            "enter len\t1234\n"
            "enter concat d\t1234\td\t67\n"
            "nil\n"
            "\n"
            "enter concat a\ta\t234\n"
            "enter concat b\t123\tb\n"
            "false\n"
            "\n"
            "enter concat a\ta\t234789x\n"
            "enter concat b\t123\tb\n"
            "false\n"
            "\n"
            "false\n"
            "ok\n";

    ReleaseAssert(out == expectedOut);
    ReleaseAssert(err == "");
}

TEST(LuaTest, metatable_eq_1)
{
    VM* vm = VM::Create();
    Auto(vm->Destroy());
    VMOutputInterceptor vmoutput(vm);

    ScriptModule* module = ScriptModule::ParseFromJSON(vm, LoadFile("luatests/metatable_eq_1.lua.json"));
    vm->LaunchScript(module);

    std::string out = vmoutput.GetAndResetStdOut();
    std::string err = vmoutput.GetAndResetStdErr();

    std::string expectedOut =
            "--- test 1 ---\n"
            "true\n"
            "false\n"
            "f1\tt1\tt2\tnil\n"
            "true\n"
            "f1\tt1\tt2\tnil\n"
            "false\n"
            "--- test 2 ---\n"
            "true\n"
            "false\n"
            "f2\tt1\tt2\tnil\n"
            "false\n"
            "f2\tt1\tt2\tnil\n"
            "true\n"
            "--- test 3 ---\n"
            "true\n"
            "false\n"
            "false\n"
            "true\n"
            "--- test 4 ---\n"
            "true\n"
            "false\n"
            "t3_1\tt3\tt1\tt2\tnil\n"
            "true\n"
            "t3_1\tt3\tt1\tt2\tnil\n"
            "false\n"
            "--- test 5 ---\n"
            "true\n"
            "false\n"
            "t3_2\tt3\tt1\tt2\tnil\n"
            "false\n"
            "t3_2\tt3\tt1\tt2\tnil\n"
            "true\n"
            "--- test 6 ---\n"
            "true\n"
            "false\n"
            "tn\t1234\tt1\tt2\tnil\n"
            "true\n"
            "tn\t1234\tt1\tt2\tnil\n"
            "false\n"
            "--- test 7 ---\n"
            "true\n"
            "false\n"
            "tn_2\t1234\tt1\tt2\tnil\n"
            "false\n"
            "tn_2\t1234\tt1\tt2\tnil\n"
            "true\n"
            "--- test 8 ---\n"
            "true\n"
            "false\n"
            "false\n"
            "true\n"
            "--- test 9 ---\n"
            "true\n"
            "false\n"
            "false\n"
            "true\n"
            "--- test 10 ---\n"
            "true\n"
            "false\n"
            "tn_3\t0\tt1\tt2\tnil\n"
            "true\n"
            "tn_3\t0\tt1\tt2\tnil\n"
            "false\n"
            "--- test 11 ---\n"
            "true\n"
            "false\n"
            "tn_4\t0\tt1\tt2\tnil\n"
            "false\n"
            "tn_4\t0\tt1\tt2\tnil\n"
            "true\n"
            "--- test 12 ---\n"
            "true\n"
            "false\n"
            "false\n"
            "false\n"
            "test end\n";

    ReleaseAssert(out == expectedOut);
    ReleaseAssert(err == "");
}

TEST(LuaTest, metatable_eq_2)
{
    VM* vm = VM::Create();
    Auto(vm->Destroy());
    VMOutputInterceptor vmoutput(vm);

    ScriptModule* module = ScriptModule::ParseFromJSON(vm, LoadFile("luatests/metatable_eq_2.lua.json"));
    vm->LaunchScript(module);

    std::string out = vmoutput.GetAndResetStdOut();
    std::string err = vmoutput.GetAndResetStdErr();

    std::string expectedOut =
            "false\n"
            "true\n"
            "false\n"
            "true\n"
            "true\n"
            "false\n"
            "true\n"
            "false\n"
            "false\n"
            "true\n"
            "false\n"
            "true\n";

    ReleaseAssert(out == expectedOut);
    ReleaseAssert(err == "");
}

TEST(LuaTest, metatable_lt)
{
    VM* vm = VM::Create();
    Auto(vm->Destroy());
    VMOutputInterceptor vmoutput(vm);

    ScriptModule* module = ScriptModule::ParseFromJSON(vm, LoadFile("luatests/metatable_lt.lua.json"));
    vm->LaunchScript(module);

    std::string out = vmoutput.GetAndResetStdOut();
    std::string err = vmoutput.GetAndResetStdErr();

    std::string expectedOut =
            "-- test 1 --\n"
            "false\n"
            "false\n"
            "false\n"
            "false\n"
            "false\n"
            "false\n"
            "-- test 2 --\n"
            "mt_nil\tnil\tnil\n"
            "true\n"
            "false\n"
            "false\n"
            "mt_nil\tnil\tnil\n"
            "false\n"
            "false\n"
            "false\n"
            "-- test 3 --\n"
            "mt_nil_2\tnil\tnil\n"
            "false\n"
            "false\n"
            "false\n"
            "mt_nil_2\tnil\tnil\n"
            "true\n"
            "false\n"
            "false\n"
            "-- test 4 --\n"
            "mt_nil_2\tnil\tnil\n"
            "false\n"
            "false\n"
            "mt_bool\tfalse\ttrue\n"
            "true\n"
            "mt_nil_2\tnil\tnil\n"
            "true\n"
            "false\n"
            "mt_bool\tfalse\ttrue\n"
            "false\n"
            "-- test 5 --\n"
            "mt_nil_2\tnil\tnil\n"
            "false\n"
            "false\n"
            "mt_bool_2\tfalse\ttrue\n"
            "false\n"
            "mt_nil_2\tnil\tnil\n"
            "true\n"
            "false\n"
            "mt_bool_2\tfalse\ttrue\n"
            "true\n"
            "-- test 6 --\n"
            "true\n"
            "false\n"
            "false\n"
            "true\n"
            "false\n"
            "true\n"
            "true\n"
            "false\n"
            "-- test 7 --\n"
            "true\n"
            "false\n"
            "false\n"
            "true\n"
            "false\n"
            "true\n"
            "true\n"
            "false\n"
            "-- test 8 --\n"
            "false\n"
            "false\n"
            "func_mt\n"
            "true\n"
            "func_mt\n"
            "false\n"
            "-- test 9 --\n"
            "false\n"
            "false\n"
            "false\n"
            "false\n"
            "true\n"
            "false\n"
            "false\n"
            "true\n"
            "false\n"
            "true\n"
            "--- test 10 ---\n"
            "f1\tt1\tt1\tnil\n"
            "true\n"
            "f1\tt1\tt1\tnil\n"
            "false\n"
            "f1\tt1\tt2\tnil\n"
            "true\n"
            "f1\tt1\tt2\tnil\n"
            "false\n"
            "--- test 11 ---\n"
            "f2\tt1\tt1\tnil\n"
            "false\n"
            "f2\tt1\tt1\tnil\n"
            "true\n"
            "f2\tt1\tt2\tnil\n"
            "false\n"
            "f2\tt1\tt2\tnil\n"
            "true\n"
            "--- test 12 ---\n"
            "t3_0\tt3\tt1\tt1\tnil\n"
            "true\n"
            "t3_0\tt3\tt1\tt1\tnil\n"
            "false\n"
            "false\n"
            "false\n"
            "--- test 13 ---\n"
            "t3_1\tt3\tt1\tt1\tnil\n"
            "true\n"
            "t3_1\tt3\tt1\tt1\tnil\n"
            "false\n"
            "t3_1\tt3\tt1\tt2\tnil\n"
            "true\n"
            "t3_1\tt3\tt1\tt2\tnil\n"
            "false\n"
            "--- test 14 ---\n"
            "t3_2\tt3\tt1\tt1\tnil\n"
            "false\n"
            "t3_2\tt3\tt1\tt1\tnil\n"
            "true\n"
            "t3_2\tt3\tt1\tt2\tnil\n"
            "false\n"
            "t3_2\tt3\tt1\tt2\tnil\n"
            "true\n"
            "--- test 15 ---\n"
            "tn\t1234\tt1\tt1\tnil\n"
            "true\n"
            "tn\t1234\tt1\tt1\tnil\n"
            "false\n"
            "tn\t1234\tt1\tt2\tnil\n"
            "true\n"
            "tn\t1234\tt1\tt2\tnil\n"
            "false\n"
            "--- test 16 ---\n"
            "tn_2\t1234\tt1\tt1\tnil\n"
            "false\n"
            "tn_2\t1234\tt1\tt1\tnil\n"
            "true\n"
            "tn_2\t1234\tt1\tt2\tnil\n"
            "false\n"
            "tn_2\t1234\tt1\tt2\tnil\n"
            "true\n"
            "--- test 17 ---\n"
            "tn_3\t1234\tt1\tt1\tnil\n"
            "true\n"
            "tn_3\t1234\tt1\tt1\tnil\n"
            "false\n"
            "false\n"
            "false\n"
            "--- test 18 ---\n"
            "false\n"
            "false\n"
            "false\n"
            "false\n"
            "--- test 19 ---\n"
            "tn_3\t0\tt1\tt1\tnil\n"
            "true\n"
            "tn_3\t0\tt1\tt1\tnil\n"
            "false\n"
            "tn_3\t0\tt1\tt2\tnil\n"
            "true\n"
            "tn_3\t0\tt1\tt2\tnil\n"
            "false\n"
            "--- test 20 ---\n"
            "tn_4\t0\tt1\tt1\tnil\n"
            "false\n"
            "tn_4\t0\tt1\tt1\tnil\n"
            "true\n"
            "tn_4\t0\tt1\tt2\tnil\n"
            "false\n"
            "tn_4\t0\tt1\tt2\tnil\n"
            "true\n"
            "--- test 21 ---\n"
            "false\n"
            "false\n"
            "false\n"
            "false\n"
            "test end\n";

    ReleaseAssert(out == expectedOut);
    ReleaseAssert(err == "");
}

TEST(LuaTest, metatable_le)
{
    VM* vm = VM::Create();
    Auto(vm->Destroy());
    VMOutputInterceptor vmoutput(vm);

    ScriptModule* module = ScriptModule::ParseFromJSON(vm, LoadFile("luatests/metatable_le.lua.json"));
    vm->LaunchScript(module);

    std::string out = vmoutput.GetAndResetStdOut();
    std::string err = vmoutput.GetAndResetStdErr();

    std::string expectedOut =
            "-- test 1 --\n"
            "false\n"
            "false\n"
            "false\n"
            "false\n"
            "false\n"
            "false\n"
            "-- test 2 --\n"
            "mt_nil\tnil\tnil\n"
            "true\n"
            "false\n"
            "false\n"
            "mt_nil\tnil\tnil\n"
            "false\n"
            "false\n"
            "false\n"
            "-- test 3 --\n"
            "mt_nil_2\tnil\tnil\n"
            "false\n"
            "false\n"
            "false\n"
            "mt_nil_2\tnil\tnil\n"
            "true\n"
            "false\n"
            "false\n"
            "-- test 4 --\n"
            "mt_nil_2\tnil\tnil\n"
            "false\n"
            "false\n"
            "mt_bool\tfalse\ttrue\n"
            "true\n"
            "mt_nil_2\tnil\tnil\n"
            "true\n"
            "false\n"
            "mt_bool\tfalse\ttrue\n"
            "false\n"
            "-- test 5 --\n"
            "mt_nil_2\tnil\tnil\n"
            "false\n"
            "false\n"
            "mt_bool_2\tfalse\ttrue\n"
            "false\n"
            "mt_nil_2\tnil\tnil\n"
            "true\n"
            "false\n"
            "mt_bool_2\tfalse\ttrue\n"
            "true\n"
            "-- test 6 --\n"
            "true\n"
            "false\n"
            "true\n"
            "true\n"
            "false\n"
            "true\n"
            "false\n"
            "false\n"
            "-- test 7 --\n"
            "true\n"
            "false\n"
            "true\n"
            "true\n"
            "false\n"
            "true\n"
            "false\n"
            "false\n"
            "-- test 8 --\n"
            "false\n"
            "false\n"
            "func_mt\n"
            "true\n"
            "func_mt\n"
            "false\n"
            "-- test 9 --\n"
            "false\n"
            "false\n"
            "false\n"
            "false\n"
            "true\n"
            "false\n"
            "false\n"
            "true\n"
            "true\n"
            "false\n"
            "--- test 10 ---\n"
            "f1\tt1\tt1\tnil\n"
            "true\n"
            "f1\tt1\tt1\tnil\n"
            "false\n"
            "f1\tt1\tt2\tnil\n"
            "true\n"
            "f1\tt1\tt2\tnil\n"
            "false\n"
            "--- test 11 ---\n"
            "f2\tt1\tt1\tnil\n"
            "false\n"
            "f2\tt1\tt1\tnil\n"
            "true\n"
            "f2\tt1\tt2\tnil\n"
            "false\n"
            "f2\tt1\tt2\tnil\n"
            "true\n"
            "--- test 12 ---\n"
            "t3_0\tt3\tt1\tt1\tnil\n"
            "true\n"
            "t3_0\tt3\tt1\tt1\tnil\n"
            "false\n"
            "false\n"
            "false\n"
            "--- test 13 ---\n"
            "t3_1\tt3\tt1\tt1\tnil\n"
            "true\n"
            "t3_1\tt3\tt1\tt1\tnil\n"
            "false\n"
            "t3_1\tt3\tt1\tt2\tnil\n"
            "true\n"
            "t3_1\tt3\tt1\tt2\tnil\n"
            "false\n"
            "--- test 14 ---\n"
            "t3_2\tt3\tt1\tt1\tnil\n"
            "false\n"
            "t3_2\tt3\tt1\tt1\tnil\n"
            "true\n"
            "t3_2\tt3\tt1\tt2\tnil\n"
            "false\n"
            "t3_2\tt3\tt1\tt2\tnil\n"
            "true\n"
            "--- test 15 ---\n"
            "tn\t1234\tt1\tt1\tnil\n"
            "true\n"
            "tn\t1234\tt1\tt1\tnil\n"
            "false\n"
            "tn\t1234\tt1\tt2\tnil\n"
            "true\n"
            "tn\t1234\tt1\tt2\tnil\n"
            "false\n"
            "--- test 16 ---\n"
            "tn_2\t1234\tt1\tt1\tnil\n"
            "false\n"
            "tn_2\t1234\tt1\tt1\tnil\n"
            "true\n"
            "tn_2\t1234\tt1\tt2\tnil\n"
            "false\n"
            "tn_2\t1234\tt1\tt2\tnil\n"
            "true\n"
            "--- test 17 ---\n"
            "tn_3\t1234\tt1\tt1\tnil\n"
            "true\n"
            "tn_3\t1234\tt1\tt1\tnil\n"
            "false\n"
            "false\n"
            "false\n"
            "--- test 18 ---\n"
            "false\n"
            "false\n"
            "false\n"
            "false\n"
            "--- test 19 ---\n"
            "tn_3\t0\tt1\tt1\tnil\n"
            "true\n"
            "tn_3\t0\tt1\tt1\tnil\n"
            "false\n"
            "tn_3\t0\tt1\tt2\tnil\n"
            "true\n"
            "tn_3\t0\tt1\tt2\tnil\n"
            "false\n"
            "--- test 20 ---\n"
            "tn_4\t0\tt1\tt1\tnil\n"
            "false\n"
            "tn_4\t0\tt1\tt1\tnil\n"
            "true\n"
            "tn_4\t0\tt1\tt2\tnil\n"
            "false\n"
            "tn_4\t0\tt1\tt2\tnil\n"
            "true\n"
            "--- test 21 ---\n"
            "false\n"
            "false\n"
            "false\n"
            "false\n"
            "test end\n";

    ReleaseAssert(out == expectedOut);
    ReleaseAssert(err == "");
}

TEST(LuaTest, metatable_eq_3)
{
    VM* vm = VM::Create();
    Auto(vm->Destroy());
    VMOutputInterceptor vmoutput(vm);

    ScriptModule* module = ScriptModule::ParseFromJSON(vm, LoadFile("luatests/metatable_eq_3.lua.json"));
    vm->LaunchScript(module);

    std::string out = vmoutput.GetAndResetStdOut();
    std::string err = vmoutput.GetAndResetStdErr();

    std::string expectedOut =
            "true\n"
            "false\n"
            "false\n"
            "true\n"
            "true\n"
            "false\n"
            "true\n"
            "false\n"
            "false\n"
            "true\n"
            "true\n"
            "false\n"
            "true\n"
            "false\n"
            "false\n"
            "true\n"
            "true\n"
            "false\n"
            "false\n"
            "false\n"
            "true\n"
            "true\n"
            "false\n"
            "true\n"
            "false\n"
            "f\tt1\tt2\n"
            "true\n"
            "f\tt1\tt2\n"
            "false\n"
            "true\n"
            "false\n"
            "true\n"
            "false\n"
            "false\n"
            "true\n"
            "true\n"
            "false\n"
            "true\n"
            "false\n"
            "f2\tt1\tt2\n"
            "false\n"
            "f2\tt1\tt2\n"
            "true\n"
            "true\n"
            "false\n"
            "true\n"
            "false\n"
            "false\n"
            "true\n"
            "true\n"
            "false\n"
            "true\n"
            "false\n"
            "f\tt1\tt2\n"
            "true\n"
            "f\tt1\tt2\n"
            "false\n"
            "true\n"
            "false\n";

    ReleaseAssert(out == expectedOut);
    ReleaseAssert(err == "");
}

TEST(LuaTest, getbyid_metatable)
{
    VM* vm = VM::Create();
    Auto(vm->Destroy());
    VMOutputInterceptor vmoutput(vm);

    ScriptModule* module = ScriptModule::ParseFromJSON(vm, LoadFile("luatests/getbyid_metatable.lua.json"));
    vm->LaunchScript(module);

    std::string out = vmoutput.GetAndResetStdOut();
    std::string err = vmoutput.GetAndResetStdErr();

    std::string expectedOut =
            "-- test 1 --\n"
            "nil\n"
            "nil\n"
            "-- test 2 --\n"
            "x\n"
            "f1\tt\tb\tnil\n"
            "233\n"
            "-- test 3 --\n"
            "f1\tt\ta\tnil\n"
            "233\n"
            "y\n"
            "-- test 4 --\n"
            "false\n"
            "1.2\n"
            "-- test 5 --\n"
            "false\n"
            "1.2\n"
            "-- test 6 --\n"
            "false\n"
            "0\n"
            "-- test 7 --\n"
            "f3\t123\ta\tnil\tnil\n"
            "433\n"
            "0\n"
            "-- test 8 --\n"
            "false\n"
            "0\n"
            "-- test 9 --\n"
            "false\n"
            "false\n"
            "-- test 10 --\n"
            "f3\tfalse\ta\tnil\n"
            "455\n"
            "f3\tfalse\tb\tnil\n"
            "455\n"
            "-- test 11 --\n"
            "false\n"
            "false\n"
            "-- test 12 --\n"
            "false\n"
            "false\n"
            "-- test 13 --\n"
            "f5\t566\ta\tnil\tnil\n"
            "899\n"
            "f5\t566\tb\tnil\tnil\n"
            "899\n"
            "-- test 14 --\n"
            "1\n"
            "2\n"
            "3\n"
            "4\n"
            "5\n"
            "6\n"
            "7\n"
            "8\n"
            "9\n"
            "10\n"
            "f6\tt10\ta11\tnil\n"
            "900\n"
            "f6\tt10\ta1\tnil\n"
            "900\n"
            "2\n"
            "3\n"
            "f6\tt10\ta11\tnil\n"
            "900\n"
            "-- test 15 --\n"
            "10\n"
            "f5\t456\ta11\tnil\tnil\n"
            "899\n"
            "3\n"
            "f5\t456\ta11\tnil\tnil\n"
            "899\n"
            "-- test 16 --\n"
            "10\n"
            "11\n"
            "nil\n"
            "3\n"
            "11\n"
            "nil\n";

    ReleaseAssert(out == expectedOut);
    ReleaseAssert(err == "");
}

TEST(LuaTest, globalget_metatable)
{
    VM* vm = VM::Create();
    Auto(vm->Destroy());
    VMOutputInterceptor vmoutput(vm);

    ScriptModule* module = ScriptModule::ParseFromJSON(vm, LoadFile("luatests/globalget_metatable.lua.json"));
    vm->LaunchScript(module);

    std::string out = vmoutput.GetAndResetStdOut();
    std::string err = vmoutput.GetAndResetStdErr();

    std::string expectedOut =
            "-- test 1 --\n"
            "nil\n"
            "nil\n"
            "-- test 2 --\n"
            "f1\ttrue\tx\tnil\n"
            "123\n"
            "f1\ttrue\ty\tnil\n"
            "123\n"
            "-- test 3 --\n"
            "xx\n"
            "f1\ttrue\ty\tnil\n"
            "123\n"
            "-- test 4 --\n"
            "xx\n"
            "ww\n"
            "-- test 5 --\n"
            "xx\n"
            "y1\n"
            "-- test 6 --\n"
            "x1\n"
            "y1\n"
            "-- test 7 --\n"
            "abc\n"
            "y1\n"
            "-- test 8 --\n"
            "abc\n"
            "f2\t233\ty\tnil\tnil\n"
            "233\n"
            "-- test 9 --\n"
            "abc\n"
            "false\n"
            "-- test 10 --\n"
            "abc\n"
            "f6\tt10\ty\tnil\n"
            "900\n"
            "1\n"
            "2\n"
            "3\n"
            "4\n"
            "5\n"
            "6\n"
            "7\n"
            "8\n"
            "9\n"
            "10\n"
            "f6\tt10\ta11\tnil\n"
            "900\n"
            "f6\tt10\ta12\tnil\n"
            "900\n";

    ReleaseAssert(out == expectedOut);
    ReleaseAssert(err == "");
}

TEST(LuaTest, getbyval_metatable)
{
    VM* vm = VM::Create();
    Auto(vm->Destroy());
    VMOutputInterceptor vmoutput(vm);

    ScriptModule* module = ScriptModule::ParseFromJSON(vm, LoadFile("luatests/getbyval_metatable.lua.json"));
    vm->LaunchScript(module);

    std::string out = vmoutput.GetAndResetStdOut();
    std::string err = vmoutput.GetAndResetStdErr();

    std::string expectedOut =
            "-- test 1 --\n"
            "nil\n"
            "nil\n"
            "nil\n"
            "nil\n"
            "nil\n"
            "nil\n"
            "nil\n"
            "nil\n"
            "-- test 2 --\n"
            "x\n"
            "f1\tt\tb\tnil\n"
            "233\n"
            "f1\tt\t1.2\tnil\n"
            "233\n"
            "8765\n"
            "f1\tt\t-5\tnil\n"
            "233\n"
            "f1\tt\tfalse\tnil\n"
            "233\n"
            "f1\tt\tnan\tnil\n"
            "233\n"
            "f1\tt\tnil\tnil\n"
            "233\n"
            "-- test 3 --\n"
            "f1\tt\ta\tnil\n"
            "233\n"
            "y\n"
            "345\n"
            "f1\tt\t3\tnil\n"
            "233\n"
            "f1\tt\t-5\tnil\n"
            "233\n"
            "f1\tt\tfalse\tnil\n"
            "233\n"
            "f1\tt\tnan\tnil\n"
            "233\n"
            "f1\tt\tnil\tnil\n"
            "233\n"
            "-- test 4 --\n"
            "false\n"
            "1.2\n"
            "f1\tt\t1.2\tnil\n"
            "233\n"
            "f1\tt\t3\tnil\n"
            "233\n"
            "www\n"
            "f1\tt\tfalse\tnil\n"
            "233\n"
            "f1\tt\tnan\tnil\n"
            "233\n"
            "f1\tt\tnil\tnil\n"
            "233\n"
            "-- test 5 --\n"
            "false\n"
            "1.2\n"
            "false\n"
            "false\n"
            "www\n"
            "false\n"
            "false\n"
            "false\n"
            "-- test 6 --\n"
            "false\n"
            "0\n"
            "false\n"
            "22\n"
            "false\n"
            "44\n"
            "false\n"
            "false\n"
            "-- test 7 --\n"
            "f3\t123\ta\tnil\tnil\n"
            "433\n"
            "0\n"
            "f3\t123\t1.2\tnil\tnil\n"
            "433\n"
            "22\n"
            "f3\t123\t-5\tnil\tnil\n"
            "433\n"
            "44\n"
            "f3\t123\tnan\tnil\tnil\n"
            "433\n"
            "f3\t123\tnil\tnil\tnil\n"
            "433\n"
            "-- test 8 --\n"
            "false\n"
            "0\n"
            "false\n"
            "22\n"
            "false\n"
            "44\n"
            "false\n"
            "false\n"
            "-- test 9 --\n"
            "false\n"
            "false\n"
            "false\n"
            "false\n"
            "false\n"
            "false\n"
            "false\n"
            "-- test 10 --\n"
            "f3\tfalse\tb\tnil\n"
            "455\n"
            "f3\tfalse\t1.2\tnil\n"
            "455\n"
            "f3\tfalse\t3\tnil\n"
            "455\n"
            "f3\tfalse\t-5\tnil\n"
            "455\n"
            "f3\tfalse\tfalse\tnil\n"
            "455\n"
            "f3\tfalse\tnan\tnil\n"
            "455\n"
            "f3\tfalse\tnil\tnil\n"
            "455\n"
            "-- test 11 --\n"
            "false\n"
            "false\n"
            "false\n"
            "false\n"
            "false\n"
            "false\n"
            "false\n"
            "-- test 12 --\n"
            "false\n"
            "false\n"
            "false\n"
            "false\n"
            "false\n"
            "false\n"
            "false\n"
            "-- test 13 --\n"
            "f5\t566\tb\tnil\tnil\n"
            "899\n"
            "f5\t566\t1.2\tnil\tnil\n"
            "899\n"
            "f5\t566\t3\tnil\tnil\n"
            "899\n"
            "f5\t566\t-5\tnil\tnil\n"
            "899\n"
            "f5\t566\tfalse\tnil\tnil\n"
            "899\n"
            "f5\t566\tnan\tnil\tnil\n"
            "899\n"
            "f5\t566\tnil\tnil\tnil\n"
            "899\n"
            "-- test 14 --\n"
            "1\n"
            "2\n"
            "3\n"
            "4\n"
            "5\n"
            "6\n"
            "7\n"
            "8\n"
            "9\n"
            "10\n"
            "f6\tt10\ta11\tnil\n"
            "900\n"
            "4\n"
            "5\n"
            "6\n"
            "7\n"
            "8\n"
            "9\n"
            "10\n"
            "11\n"
            "f6\tt10\t20\tnil\n"
            "900\n"
            "13\n"
            "f6\tt10\t31\tnil\n"
            "900\n"
            "14\n"
            "f6\tt10\ttrue\tnil\n"
            "900\n"
            "f6\tt10\tnil\tnil\n"
            "900\n"
            "f6\tt10\tnan\tnil\n"
            "900\n"
            "f6\tt10\ta1\tnil\n"
            "900\n"
            "2\n"
            "3\n"
            "f6\tt10\ta11\tnil\n"
            "900\n"
            "f6\tt10\tnan\tnil\n"
            "900\n"
            "f6\tt10\t21\tnil\n"
            "900\n"
            "5\n"
            "6\n"
            "f6\tt10\t21\tnil\n"
            "900\n"
            "14\n"
            "f6\tt10\ttrue\tnil\n"
            "900\n"
            "f6\tt10\tnil\tnil\n"
            "900\n"
            "-- test 15 --\n"
            "10\n"
            "f5\t456\ta11\tnil\tnil\n"
            "899\n"
            "4\n"
            "f5\t456\t31\tnil\tnil\n"
            "899\n"
            "14\n"
            "f5\t456\ttrue\tnil\tnil\n"
            "899\n"
            "f5\t456\tnil\tnil\tnil\n"
            "899\n"
            "f5\t456\tnan\tnil\tnil\n"
            "899\n"
            "10\n"
            "f5\t456\ta11\tnil\tnil\n"
            "899\n"
            "f5\t456\t21\tnil\tnil\n"
            "899\n"
            "f5\t456\t31\tnil\tnil\n"
            "899\n"
            "14\n"
            "f5\t456\ttrue\tnil\tnil\n"
            "899\n"
            "f5\t456\tnil\tnil\tnil\n"
            "899\n"
            "f5\t456\tnan\tnil\tnil\n"
            "899\n"
            "-- test 16 --\n"
            "10\n"
            "11\n"
            "4\n"
            "nil\n"
            "14\n"
            "nil\n"
            "nil\n"
            "nil\n"
            "10\n"
            "11\n"
            "nil\n"
            "nil\n"
            "14\n"
            "nil\n"
            "nil\n"
            "nil\n";

    ReleaseAssert(out == expectedOut);
    ReleaseAssert(err == "");
}

TEST(LuaTest, getbyintegerval_metatable)
{
    VM* vm = VM::Create();
    Auto(vm->Destroy());
    VMOutputInterceptor vmoutput(vm);

    ScriptModule* module = ScriptModule::ParseFromJSON(vm, LoadFile("luatests/getbyintegerval_metatable.lua.json"));
    vm->LaunchScript(module);

    std::string out = vmoutput.GetAndResetStdOut();
    std::string err = vmoutput.GetAndResetStdErr();

    std::string expectedOut =
            "-- test 1 --\n"
            "nil\n"
            "nil\n"
            "-- test 2 --\n"
            "x\n"
            "f1\tt\t2\tnil\n"
            "233\n"
            "-- test 3 --\n"
            "f1\tt\t1\tnil\n"
            "233\n"
            "y\n"
            "-- test 4 --\n"
            "false\n"
            "1.2\n"
            "-- test 5 --\n"
            "false\n"
            "1.2\n"
            "-- test 6 --\n"
            "false\n"
            "0\n"
            "-- test 7 --\n"
            "f3\t123\t1\tnil\tnil\n"
            "433\n"
            "0\n"
            "-- test 8 --\n"
            "false\n"
            "0\n"
            "-- test 9 --\n"
            "false\n"
            "false\n"
            "-- test 10 --\n"
            "f3\tfalse\t1\tnil\n"
            "455\n"
            "f3\tfalse\t2\tnil\n"
            "455\n"
            "-- test 11 --\n"
            "false\n"
            "false\n"
            "-- test 12 --\n"
            "false\n"
            "false\n"
            "-- test 13 --\n"
            "f5\t566\t1\tnil\tnil\n"
            "899\n"
            "f5\t566\t2\tnil\tnil\n"
            "899\n"
            "-- test 14 --\n"
            "1\n"
            "2\n"
            "3\n"
            "4\n"
            "5\n"
            "6\n"
            "7\n"
            "8\n"
            "9\n"
            "10\n"
            "f6\tt10\t11\tnil\n"
            "900\n"
            "f6\tt10\t1\tnil\n"
            "900\n"
            "2\n"
            "3\n"
            "f6\tt10\t11\tnil\n"
            "900\n"
            "-- test 15 --\n"
            "10\n"
            "f5\t456\t11\tnil\tnil\n"
            "899\n"
            "3\n"
            "f5\t456\t11\tnil\tnil\n"
            "899\n"
            "-- test 16 --\n"
            "10\n"
            "11\n"
            "nil\n"
            "3\n"
            "11\n"
            "nil\n";

    ReleaseAssert(out == expectedOut);
    ReleaseAssert(err == "");
}

TEST(LuaTest, rawget_and_rawset)
{
    VM* vm = VM::Create();
    Auto(vm->Destroy());
    VMOutputInterceptor vmoutput(vm);

    ScriptModule* module = ScriptModule::ParseFromJSON(vm, LoadFile("luatests/rawget_rawset.lua.json"));
    vm->LaunchScript(module);

    std::string out = vmoutput.GetAndResetStdOut();
    std::string err = vmoutput.GetAndResetStdErr();

    std::string expectedOut =
            "-- rawget --\n"
            "nil\n"
            "nil\n"
            "nil\n"
            "nil\n"
            "nil\n"
            "nil\n"
            "nil\n"
            "nil\n"
            "nil\n"
            "nil\n"
            "nil\n"
            "-- rawset --\n"
            "false\n"
            "false\n"
            "-- rawget --\n"
            "1\n"
            "2\n"
            "3\n"
            "nil\n"
            "5\n"
            "6\n"
            "7\n"
            "8\n"
            "11\n"
            "nil\n"
            "11\n"
            "-- error cases --\n"
            "false\n"
            "false\n"
            "false\n"
            "false\n"
            "false\n"
            "false\n"
            "false\n"
            "false\n"
            "false\n";

    ReleaseAssert(out == expectedOut);
    ReleaseAssert(err == "");
}

TEST(LuaTest, putbyid_metatable)
{
    VM* vm = VM::Create();
    Auto(vm->Destroy());
    VMOutputInterceptor vmoutput(vm);

    ScriptModule* module = ScriptModule::ParseFromJSON(vm, LoadFile("luatests/putbyid_metatable.lua.json"));
    vm->LaunchScript(module);

    std::string out = vmoutput.GetAndResetStdOut();
    std::string err = vmoutput.GetAndResetStdErr();

    std::string expectedOut =
            "-- test 1 --\n"
            "nil\t123\n"
            "nil\t234\n"
            "-- test 2 --\n"
            "before\t1\tnil\n"
            "t\ta\t123\tnil\n"
            "after\t1\tnil\n"
            "before\t2\tnil\n"
            "t\ta\t123\tnil\n"
            "after\t2\tnil\n"
            "before\t3\tnil\n"
            "t\ta\t123\tnil\n"
            "after\t3\t246\n"
            "before\t4\t246\n"
            "after\t4\t123\n"
            "before\t5\t123\n"
            "after\t5\t123\n"
            "before\t6\t123\n"
            "after\t6\t123\n"
            "-- test 2 --\n"
            "f1\tt\txx\t20\tnil\n"
            "f2\tt\txx\t40\tnil\n"
            "120\n"
            "30\n"
            "-- test 3 --\n"
            "false\n"
            "f3\t233\tzz\t12\tnil\tnil\n"
            "nil\n"
            "f3\t233\tzz\t23\tnil\tnil\n"
            "nil\n"
            "-- test 4 --\n"
            "f6\tt6\ta7\t28\tnil\n"
            "t1\t22\tnil\tnil\tnil\tnil\tnil\tnil\n"
            "t2\t2\t23\tnil\tnil\tnil\tnil\tnil\n"
            "t3\t4\t5\t24\tnil\tnil\tnil\tnil\n"
            "t4\t7\t8\t9\t25\tnil\tnil\tnil\n"
            "t5\t11\t12\t13\t14\t26\tnil\tnil\n"
            "t6\t16\t17\t18\t19\t20\t27\t2800\n"
            "f6\tt6\ta8\t36\tnil\n"
            "t1\t29\tnil\tnil\tnil\tnil\tnil\tnil\tnil\n"
            "t2\t2\t30\tnil\tnil\tnil\tnil\tnil\tnil\n"
            "t3\t4\t5\t31\tnil\tnil\tnil\tnil\tnil\n"
            "t4\t7\t8\t9\t32\tnil\tnil\tnil\tnil\n"
            "t5\t11\t12\t13\t14\t33\tnil\tnil\tnil\n"
            "t6\t16\t17\t18\t19\t20\t34\t35\t3600\n"
            "-- test 5 --\n"
            "f7\tt6\ta7\t28\tnil\n"
            "t1\t22\tnil\tnil\tnil\tnil\tnil\tnil\n"
            "t2\t2\t23\tnil\tnil\tnil\tnil\tnil\n"
            "t3\t4\t5\t24\tnil\tnil\tnil\tnil\n"
            "t4\t7\t8\t9\t25\tnil\tnil\tnil\n"
            "t5\t11\t12\t13\t14\t26\tnil\tnil\n"
            "t6\t16\t17\t18\t19\t20\t27\t2800\n"
            "f7\tt6\ta8\t36\tnil\n"
            "t1\t29\tnil\tnil\tnil\tnil\tnil\tnil\tnil\n"
            "t2\t2\t30\tnil\tnil\tnil\tnil\tnil\tnil\n"
            "t3\t4\t5\t31\tnil\tnil\tnil\tnil\tnil\n"
            "t4\t7\t8\t9\t32\tnil\tnil\tnil\tnil\n"
            "t5\t11\t12\t13\t14\t33\tnil\tnil\tnil\n"
            "t6\t16\t17\t18\t19\t20\t34\t35\t3600\n";

    ReleaseAssert(out == expectedOut);
    ReleaseAssert(err == "");
}

TEST(LuaTest, globalput_metatable)
{
    VM* vm = VM::Create();
    Auto(vm->Destroy());
    VMOutputInterceptor vmoutput(vm);

    ScriptModule* module = ScriptModule::ParseFromJSON(vm, LoadFile("luatests/globalput_metatable.lua.json"));
    vm->LaunchScript(module);

    std::string out = vmoutput.GetAndResetStdOut();
    std::string err = vmoutput.GetAndResetStdErr();

    std::string expectedOut =
            "-- test 1 --\n"
            "nil\n"
            "nil\n"
            "f1\ttrue\tx1\t12\n"
            "f1\ttrue\ty1\t34\n"
            "24\n"
            "68\n"
            "12\n"
            "34\n"
            "-- test 2 --\n"
            "34\n"
            "nil\n"
            "12\n"
            "nil\n"
            "xx\n"
            "23\n"
            "-- test 3 --\n"
            "45\n"
            "nil\n"
            "21\n"
            "nil\n"
            "xxx\n"
            "32\n"
            "-- test 4 --\n"
            "nil\n"
            "nil\n"
            "xxx\n"
            "nil\n"
            "nil\n"
            "nil\n"
            "43\n"
            "54\n"
            "-- test 5 --\n"
            "65\n"
            "nil\n"
            "f2\t233\ty4\t87\tnil\n"
            "76\n"
            "261\n"
            "98\n"
            "89\n"
            "-- test 6 --\n"
            "98\n"
            "12\n"
            "false\n"
            "-- test 7 --\n"
            "nil\n"
            "nil\n"
            "nil\n"
            "nil\n"
            "nil\n"
            "nil\n"
            "nil\n"
            "f6\tt6\ta7\t28\tnil\n"
            "nil\n"
            "nil\n"
            "nil\n"
            "nil\n"
            "nil\n"
            "nil\n"
            "nil\n"
            "t1\t22\tnil\tnil\tnil\tnil\tnil\tnil\n"
            "t2\t2\t23\tnil\tnil\tnil\tnil\tnil\n"
            "t3\t4\t5\t24\tnil\tnil\tnil\tnil\n"
            "t4\t7\t8\t9\t25\tnil\tnil\tnil\n"
            "t5\t11\t12\t13\t14\t26\tnil\tnil\n"
            "t6\t16\t17\t18\t19\t20\t27\t2800\n"
            "f6\tt6\ta8\t36\tnil\n"
            "nil\n"
            "nil\n"
            "nil\n"
            "nil\n"
            "nil\n"
            "nil\n"
            "nil\n"
            "nil\n"
            "t1\t29\tnil\tnil\tnil\tnil\tnil\tnil\tnil\n"
            "t2\t2\t30\tnil\tnil\tnil\tnil\tnil\tnil\n"
            "t3\t4\t5\t31\tnil\tnil\tnil\tnil\tnil\n"
            "t4\t7\t8\t9\t32\tnil\tnil\tnil\tnil\n"
            "t5\t11\t12\t13\t14\t33\tnil\tnil\tnil\n"
            "t6\t16\t17\t18\t19\t20\t34\t35\t3600\n"
            "-- test 9 --\n"
            "nil\n"
            "nil\n"
            "nil\n"
            "nil\n"
            "nil\n"
            "nil\n"
            "nil\n"
            "f6\tt6\ta7\t28\tnil\n"
            "nil\n"
            "nil\n"
            "nil\n"
            "nil\n"
            "nil\n"
            "nil\n"
            "nil\n"
            "t1\t22\tnil\tnil\tnil\tnil\tnil\tnil\n"
            "t2\t2\t23\tnil\tnil\tnil\tnil\tnil\n"
            "t3\t4\t5\t24\tnil\tnil\tnil\tnil\n"
            "t4\t7\t8\t9\t25\tnil\tnil\tnil\n"
            "t5\t11\t12\t13\t14\t26\tnil\tnil\n"
            "t6\t16\t17\t18\t19\t20\t27\t2800\n"
            "f6\tt6\ta8\t36\tnil\n"
            "nil\n"
            "nil\n"
            "nil\n"
            "nil\n"
            "nil\n"
            "nil\n"
            "nil\n"
            "nil\n"
            "t1\t29\tnil\tnil\tnil\tnil\tnil\tnil\tnil\n"
            "t2\t2\t30\tnil\tnil\tnil\tnil\tnil\tnil\n"
            "t3\t4\t5\t31\tnil\tnil\tnil\tnil\tnil\n"
            "t4\t7\t8\t9\t32\tnil\tnil\tnil\tnil\n"
            "t5\t11\t12\t13\t14\t33\tnil\tnil\tnil\n"
            "t6\t16\t17\t18\t19\t20\t34\t35\t3600\n";

    ReleaseAssert(out == expectedOut);
    ReleaseAssert(err == "");
}

TEST(LuaTest, putbyintegerval_metatable)
{
    VM* vm = VM::Create();
    Auto(vm->Destroy());
    VMOutputInterceptor vmoutput(vm);

    ScriptModule* module = ScriptModule::ParseFromJSON(vm, LoadFile("luatests/putbyintegerval_metatable.lua.json"));
    vm->LaunchScript(module);

    std::string out = vmoutput.GetAndResetStdOut();
    std::string err = vmoutput.GetAndResetStdErr();

    std::string expectedOut =
            "-- test 1 --\n"
            "123\n"
            "234\n"
            "321\n"
            "432\n"
            "345\n"
            "456\n"
            "567\n"
            "678\n"
            "-- test 2 --\n"
            "before\t1\tnil\n"
            "t\t1\t123\tnil\n"
            "after\t1\tnil\n"
            "before\t2\tnil\n"
            "t\t1\t123\tnil\n"
            "after\t2\tnil\n"
            "before\t3\tnil\n"
            "t\t1\t123\tnil\n"
            "after\t3\t246\n"
            "before\t4\t246\n"
            "after\t4\t123\n"
            "before\t5\t123\n"
            "after\t5\t123\n"
            "before\t6\t123\n"
            "after\t6\t123\n"
            "before\t1\tnil\n"
            "t\t2\t234\tnil\n"
            "after\t1\tnil\n"
            "before\t2\tnil\n"
            "t\t2\t234\tnil\n"
            "after\t2\tnil\n"
            "before\t3\tnil\n"
            "t\t2\t234\tnil\n"
            "after\t3\t468\n"
            "before\t4\t468\n"
            "after\t4\t234\n"
            "before\t5\t234\n"
            "after\t5\t234\n"
            "before\t6\t234\n"
            "after\t6\t234\n"
            "before\t1\tnil\n"
            "t\t4\t345\tnil\n"
            "after\t1\tnil\n"
            "before\t2\tnil\n"
            "t\t4\t345\tnil\n"
            "after\t2\tnil\n"
            "before\t3\tnil\n"
            "t\t4\t345\tnil\n"
            "after\t3\t690\n"
            "before\t4\t690\n"
            "after\t4\t345\n"
            "before\t5\t345\n"
            "after\t5\t345\n"
            "before\t6\t345\n"
            "after\t6\t345\n"
            "before\t1\tnil\n"
            "t\t100\t456\tnil\n"
            "after\t1\tnil\n"
            "before\t2\tnil\n"
            "t\t100\t456\tnil\n"
            "after\t2\tnil\n"
            "before\t3\tnil\n"
            "t\t100\t456\tnil\n"
            "after\t3\t912\n"
            "before\t4\t912\n"
            "after\t4\t456\n"
            "before\t5\t456\n"
            "after\t5\t456\n"
            "before\t6\t456\n"
            "after\t6\t456\n"
            "-- test 3 --\n"
            "f1\tt\t1\t20\tnil\n"
            "f2\tt\txx\t40\tnil\n"
            "nil\n"
            "30\n"
            "f1\tt\t2\t21\tnil\n"
            "nil\n"
            "f2\tt\t2\t31\tnil\n"
            "93\n"
            "f1\tt\t4\t22\tnil\n"
            "nil\n"
            "f2\tt\t4\t32\tnil\n"
            "96\n"
            "f1\tt\t100\t23\tnil\n"
            "nil\n"
            "f2\tt\t100\t33\tnil\n"
            "99\n"
            "-- test 4 --\n"
            "false\n"
            "f3\t233\t1\t12\tnil\tnil\n"
            "nil\n"
            "f3\t233\t1\t23\tnil\tnil\n"
            "nil\n"
            "-- test 5 --\n"
            "f6\tt6\t7\t28\tnil\n"
            "t1\t22\tnil\tnil\tnil\tnil\tnil\tnil\n"
            "t2\t2\t23\tnil\tnil\tnil\tnil\tnil\n"
            "t3\t4\t5\t24\tnil\tnil\tnil\tnil\n"
            "t4\t7\t8\t9\t25\tnil\tnil\tnil\n"
            "t5\t11\t12\t13\t14\t26\tnil\tnil\n"
            "t6\t16\t17\t18\t19\t20\t27\t2800\n"
            "f6\tt6\t8\t36\tnil\n"
            "t1\t29\tnil\tnil\tnil\tnil\tnil\tnil\tnil\n"
            "t2\t2\t30\tnil\tnil\tnil\tnil\tnil\tnil\n"
            "t3\t4\t5\t31\tnil\tnil\tnil\tnil\tnil\n"
            "t4\t7\t8\t9\t32\tnil\tnil\tnil\tnil\n"
            "t5\t11\t12\t13\t14\t33\tnil\tnil\tnil\n"
            "t6\t16\t17\t18\t19\t20\t34\t35\t3600\n"
            "-- test 6 --\n"
            "f6\tt6\t7\t28\tnil\n"
            "t1\t22\tnil\tnil\tnil\tnil\tnil\tnil\n"
            "t2\t2\t23\tnil\tnil\tnil\tnil\tnil\n"
            "t3\t4\t5\t24\tnil\tnil\tnil\tnil\n"
            "t4\t7\t8\t9\t25\tnil\tnil\tnil\n"
            "t5\t11\t12\t13\t14\t26\tnil\tnil\n"
            "t6\t16\t17\t18\t19\t20\t27\t2800\n"
            "f6\tt6\t8\t36\tnil\n"
            "t1\t29\tnil\tnil\tnil\tnil\tnil\tnil\tnil\n"
            "t2\t2\t30\tnil\tnil\tnil\tnil\tnil\tnil\n"
            "t3\t4\t5\t31\tnil\tnil\tnil\tnil\tnil\n"
            "t4\t7\t8\t9\t32\tnil\tnil\tnil\tnil\n"
            "t5\t11\t12\t13\t14\t33\tnil\tnil\tnil\n"
            "t6\t16\t17\t18\t19\t20\t34\t35\t3600\n";

    ReleaseAssert(out == expectedOut);
    ReleaseAssert(err == "");
}

TEST(LuaTest, putbyval_metatable)
{
    VM* vm = VM::Create();
    Auto(vm->Destroy());
    VMOutputInterceptor vmoutput(vm);

    ScriptModule* module = ScriptModule::ParseFromJSON(vm, LoadFile("luatests/putbyval_metatable.lua.json"));
    vm->LaunchScript(module);

    std::string out = vmoutput.GetAndResetStdOut();
    std::string err = vmoutput.GetAndResetStdErr();

    std::string expectedOut =
            "-- test 1 --\n"
            "f1\tt\ta\t1\tnil\n"
            "f1\tt\tb\t2\tnil\n"
            "f1\tt\t1.2\t3\tnil\n"
            "f1\tt\t3\t4\tnil\n"
            "f1\tt\t-5\t5\tnil\n"
            "f1\tt\tfalse\t6\tnil\n"
            "false\n"
            "false\n"
            "2\n"
            "4\n"
            "6\n"
            "8\n"
            "10\n"
            "12\n"
            "nil\n"
            "nil\n"
            "false\n"
            "false\n"
            "1\n"
            "2\n"
            "3\n"
            "4\n"
            "5\n"
            "6\n"
            "nil\n"
            "nil\n"
            "-- test 2 --\n"
            "f2\t233\ta\t1\tnil\n"
            "f2\t233\tb\t2\tnil\n"
            "f2\t233\t1.2\t3\tnil\n"
            "f2\t233\t3\t4\tnil\n"
            "f2\t233\t-5\t5\tnil\n"
            "f2\t233\tfalse\t6\tnil\n"
            "false\n"
            "false\n"
            "3\n"
            "6\n"
            "9\n"
            "12\n"
            "15\n"
            "18\n"
            "nil\n"
            "nil\n"
            "false\n"
            "false\n"
            "1\n"
            "2\n"
            "3\n"
            "4\n"
            "5\n"
            "6\n"
            "nil\n"
            "nil\n"
            "-- test 3 --\n"
            "f3\tt6\t0\t107\tnil\n"
            "false\n"
            "false\n"
            "t0\tnil\tnil\tnil\tnil\tnil\tnil\tnil\tnil\tnil\n"
            "t1\t101\tnil\tnil\tnil\tnil\tnil\tnil\tnil\tnil\n"
            "t2\t2\t102\tnil\tnil\tnil\tnil\tnil\tnil\tnil\n"
            "t3\t4\t5\t103\tnil\tnil\tnil\tnil\tnil\tnil\n"
            "t4\t7\t8\t9\t104\tnil\tnil\tnil\tnil\tnil\n"
            "t5\t11\t12\t13\t14\t105\tnil\tnil\tnil\tnil\n"
            "t6\t16\t17\t18\t19\t20\t106\t10700\tnil\tnil\n"
            "false\n"
            "false\n"
            "t0\tnil\tnil\tnil\tnil\tnil\tnil\tnil\tnil\tnil\n"
            "t1\t101\tnil\tnil\tnil\tnil\tnil\tnil\tnil\tnil\n"
            "t2\t2\t102\tnil\tnil\tnil\tnil\tnil\tnil\tnil\n"
            "t3\t4\t5\t103\tnil\tnil\tnil\tnil\tnil\tnil\n"
            "t4\t7\t8\t9\t104\tnil\tnil\tnil\tnil\tnil\n"
            "t5\t11\t12\t13\t14\t105\tnil\tnil\tnil\tnil\n"
            "t6\t16\t17\t18\t19\t20\t106\t107\tnil\tnil\n"
            "-- test 4 --\n"
            "f4\tt6\t0\t107\tnil\n"
            "false\n"
            "false\n"
            "t0\tnil\tnil\tnil\tnil\tnil\tnil\tnil\tnil\tnil\n"
            "t1\t101\tnil\tnil\tnil\tnil\tnil\tnil\tnil\tnil\n"
            "t2\t2\t102\tnil\tnil\tnil\tnil\tnil\tnil\tnil\n"
            "t3\t4\t5\t103\tnil\tnil\tnil\tnil\tnil\tnil\n"
            "t4\t7\t8\t9\t104\tnil\tnil\tnil\tnil\tnil\n"
            "t5\t11\t12\t13\t14\t105\tnil\tnil\tnil\tnil\n"
            "t6\t16\t17\t18\t19\t20\t106\t10700\tnil\tnil\n"
            "false\n"
            "false\n"
            "t0\tnil\tnil\tnil\tnil\tnil\tnil\tnil\tnil\tnil\n"
            "t1\t101\tnil\tnil\tnil\tnil\tnil\tnil\tnil\tnil\n"
            "t2\t2\t102\tnil\tnil\tnil\tnil\tnil\tnil\tnil\n"
            "t3\t4\t5\t103\tnil\tnil\tnil\tnil\tnil\tnil\n"
            "t4\t7\t8\t9\t104\tnil\tnil\tnil\tnil\tnil\n"
            "t5\t11\t12\t13\t14\t105\tnil\tnil\tnil\tnil\n"
            "t6\t16\t17\t18\t19\t20\t106\t107\tnil\tnil\n"
            "-- test 5 --\n"
            "f5\t765\ta\t1\tnil\n"
            "f5\t765\tb\t2\tnil\n"
            "f5\t765\t1.2\t3\tnil\n"
            "f5\t765\t3\t4\tnil\n"
            "f5\t765\t-5\t5\tnil\n"
            "f5\t765\tfalse\t6\tnil\n"
            "f5\t765\tnan\t7\tnil\n"
            "f5\t765\tnil\t8\tnil\n"
            "f5\t765\ta\t1\tnil\n"
            "f5\t765\tb\t2\tnil\n"
            "f5\t765\t1.2\t3\tnil\n"
            "f5\t765\t3\t4\tnil\n"
            "f5\t765\t-5\t5\tnil\n"
            "f5\t765\tfalse\t6\tnil\n"
            "f5\t765\tnan\t7\tnil\n"
            "f5\t765\tnil\t8\tnil\n"
            "-- test 6 --\n"
            "f6\tt2\ta\t1\tnil\n"
            "f6\tt2\tb\t2\tnil\n"
            "f6\tt2\t1.2\t3\tnil\n"
            "f6\tt2\t3\t4\tnil\n"
            "f6\tt2\t-5\t5\tnil\n"
            "f6\tt2\tfalse\t6\tnil\n"
            "false\n"
            "false\n"
            "4\n"
            "8\n"
            "12\n"
            "16\n"
            "20\n"
            "24\n"
            "nil\n"
            "nil\n"
            "false\n"
            "false\n"
            "1\n"
            "2\n"
            "3\n"
            "4\n"
            "5\n"
            "6\n"
            "nil\n"
            "nil\n"
            "-- test 7 --\n"
            "f7\tt2\ta\t1\tnil\n"
            "f7\tt2\tb\t2\tnil\n"
            "f7\tt2\t1.2\t3\tnil\n"
            "f7\tt2\t3\t4\tnil\n"
            "f7\tt2\t-5\t5\tnil\n"
            "f7\tt2\tfalse\t6\tnil\n"
            "false\n"
            "false\n"
            "4\n"
            "8\n"
            "12\n"
            "16\n"
            "20\n"
            "24\n"
            "nil\n"
            "nil\n"
            "false\n"
            "false\n"
            "1\n"
            "2\n"
            "3\n"
            "4\n"
            "5\n"
            "6\n"
            "nil\n"
            "nil\n"
            "-- test 8 --\n"
            "f8\t876\ta\t1\tnil\n"
            "f8\t876\tb\t2\tnil\n"
            "f8\t876\t1.2\t3\tnil\n"
            "f8\t876\t3\t4\tnil\n"
            "f8\t876\t-5\t5\tnil\n"
            "f8\t876\tfalse\t6\tnil\n"
            "f8\t876\tnan\t7\tnil\n"
            "f8\t876\tnil\t8\tnil\n"
            "f8\t876\ta\t1\tnil\n"
            "f8\t876\tb\t2\tnil\n"
            "f8\t876\t1.2\t3\tnil\n"
            "f8\t876\t3\t4\tnil\n"
            "f8\t876\t-5\t5\tnil\n"
            "f8\t876\tfalse\t6\tnil\n"
            "f8\t876\tnan\t7\tnil\n"
            "f8\t876\tnil\t8\tnil\n";

    ReleaseAssert(out == expectedOut);
    ReleaseAssert(err == "");
}

}   // anonymous namespace
