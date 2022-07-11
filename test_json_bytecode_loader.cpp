#include <fstream>
#include "bytecode.h"
#include "gtest/gtest.h"
#include "json_utils.h"
#include "test_vm_utils.h"

using namespace ToyLang;

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
    VM* vm = VM::Create();
    Auto(vm->Destroy());
    VMOutputInterceptor vmoutput(vm);

    ScriptModule* module = ScriptModule::ParseFromJSON(vm, LoadFile("luatests/fib.lua.json"));
    vm->LaunchScript(module);

    std::string out = vmoutput.GetAndResetStdOut();
    std::string err = vmoutput.GetAndResetStdErr();
    ReleaseAssert(out == "610\n");
    ReleaseAssert(err == "");
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
    ReleaseAssert(StartsWith(out, expectedOut));
    ReleaseAssert(err == "");
}

TEST(LuaTest, TestTableDup)
{
    VM* vm = VM::Create();
    Auto(vm->Destroy());
    VMOutputInterceptor vmoutput(vm);

    ScriptModule* module = ScriptModule::ParseFromJSON(vm, LoadFile("luatests/table_dup.lua.json"));
    vm->LaunchScript(module);

    std::string out = vmoutput.GetAndResetStdOut();
    std::string err = vmoutput.GetAndResetStdErr();

    std::string expectedOut =
            "0\t25\t50\t100\t150\t200\t250\t275\t299\tnil\t300\t304\t301\t302\t303\n"
            "1\t26\t51\t100\t150\t200\t251\t275\t300\tnil\t301\t305\t302\t302\t303\n"
            "0\t25\t50\t100\t150\t200\t250\t275\t299\tnil\t300\t304\t301\t302\t303\n"
            "1\t26\t51\t100\t150\t200\t251\t275\t300\tnil\t301\t305\t302\t302\t303\n";

    ReleaseAssert(out == expectedOut);
    ReleaseAssert(err == "");
}

TEST(LuaTest, TestTableDup2)
{
    VM* vm = VM::Create();
    Auto(vm->Destroy());
    VMOutputInterceptor vmoutput(vm);

    ScriptModule* module = ScriptModule::ParseFromJSON(vm, LoadFile("luatests/table_dup2.lua.json"));
    vm->LaunchScript(module);

    std::string out = vmoutput.GetAndResetStdOut();
    std::string err = vmoutput.GetAndResetStdErr();

    std::string expectedOut =
            "0\t1\t2\t3\t4\tnil\tnil\tnil\t301\t302\t303\tnil\n"
            "1\t1\t2\t3\t4\tnil\tnil\tnil\t302\t302\t303\tnil\n"
            "0\t1\t2\t3\t4\tnil\tnil\tnil\t301\t302\t303\tnil\n"
            "1\t1\t2\t3\t4\tnil\tnil\tnil\t302\t302\t303\tnil\n";

    ReleaseAssert(out == expectedOut);
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
        ReleaseAssert(t.IsPointer(TValue::x_mivTag) && t.AsPointer<UserHeapGcObjectHeader>().As()->m_type == Type::TABLE);
        TableObject* obj = AssertAndGetTableObject(t);
        Structure* structure = AssertAndGetStructure(obj);
        ReleaseAssert(structure->m_inlineNamedStorageCapacity == ToyLang::internal::x_optimalInlineCapacityArray[4]);
    }

    {
        TValue t = GetGlobalVariable(vm, "t2");
        ReleaseAssert(t.IsPointer(TValue::x_mivTag) && t.AsPointer<UserHeapGcObjectHeader>().As()->m_type == Type::TABLE);
        TableObject* obj = AssertAndGetTableObject(t);
        Structure* structure = AssertAndGetStructure(obj);
        ReleaseAssert(structure->m_inlineNamedStorageCapacity == ToyLang::internal::x_optimalInlineCapacityArray[3]);
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

TEST(LuaTest, Sieve)
{
    VM* vm = VM::Create();
    Auto(vm->Destroy());
    VMOutputInterceptor vmoutput(vm);

    ScriptModule* module = ScriptModule::ParseFromJSON(vm, LoadFile("luatests/sieve.lua.json"));
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

TEST(LuaTest, NBody)
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

}   // anonymous namespace
