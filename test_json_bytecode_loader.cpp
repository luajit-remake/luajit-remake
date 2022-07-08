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

    std::string expectedOut = "2E-1\t3\tfalse\ttrue\tnil\tabc\tfunction: 0x";
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

    printf("%s\n", out.c_str());
    ReleaseAssert(err == "");
}

}   // anonymous namespace
