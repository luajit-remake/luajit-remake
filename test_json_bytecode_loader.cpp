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
    vm->SetUpSegmentationRegister();
    VMOutputInterceptor vmoutput(vm);

    UserHeapPointer<TableObject> globalObject = CreateGlobalObject(vm);
    CoroutineRuntimeContext* rc = CoroutineRuntimeContext::Create(vm, globalObject);
    ScriptModule* module = ScriptModule::ParseFromJSON(vm, globalObject, LoadFile("luatests/fib.lua.json"));
    TValue* stack = new TValue[1000];

    module->EnterVM(rc, stack);

    std::string out = vmoutput.GetAndResetStdOut();
    std::string err = vmoutput.GetAndResetStdErr();
    ReleaseAssert(out == "610\n");
    ReleaseAssert(err == "");
}

TEST(LuaTest, TestPrint)
{
    VM* vm = VM::Create();
    Auto(vm->Destroy());
    vm->SetUpSegmentationRegister();
    VMOutputInterceptor vmoutput(vm);

    UserHeapPointer<TableObject> globalObject = CreateGlobalObject(vm);
    CoroutineRuntimeContext* rc = CoroutineRuntimeContext::Create(vm, globalObject);
    ScriptModule* module = ScriptModule::ParseFromJSON(vm, globalObject, LoadFile("luatests/test_print.lua.json"));
    TValue* stack = new TValue[1000];

    module->EnterVM(rc, stack);

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
    vm->SetUpSegmentationRegister();
    VMOutputInterceptor vmoutput(vm);

    UserHeapPointer<TableObject> globalObject = CreateGlobalObject(vm);
    CoroutineRuntimeContext* rc = CoroutineRuntimeContext::Create(vm, globalObject);
    ScriptModule* module = ScriptModule::ParseFromJSON(vm, globalObject, LoadFile("luatests/table_dup.lua.json"));
    TValue* stack = new TValue[1000];

    module->EnterVM(rc, stack);

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
    vm->SetUpSegmentationRegister();
    VMOutputInterceptor vmoutput(vm);

    UserHeapPointer<TableObject> globalObject = CreateGlobalObject(vm);
    CoroutineRuntimeContext* rc = CoroutineRuntimeContext::Create(vm, globalObject);
    ScriptModule* module = ScriptModule::ParseFromJSON(vm, globalObject, LoadFile("luatests/table_dup2.lua.json"));
    TValue* stack = new TValue[1000];

    module->EnterVM(rc, stack);

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


}   // anonymous namespace
