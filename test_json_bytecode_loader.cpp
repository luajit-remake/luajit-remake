#include "bytecode.h"
#include "gtest/gtest.h"
#include "json_utils.h"
#include <fstream>

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
    std::istreambuf_iterator<char> iter { infile }, end;
    return std::string { iter, end };
}

TEST(JSONParser, Fib)
{
    VM* vm = VM::Create();
    Auto(vm->Destroy());
    vm->SetUpSegmentationRegister();

    UserHeapPointer<TableObject> globalObject = CreateGlobalObject(vm);
    CoroutineRuntimeContext* rc = CoroutineRuntimeContext::Create(vm, globalObject);
    ScriptModule* module = ScriptModule::ParseFromJSON(vm, globalObject, LoadFile("luatests/fib.json"));
    TValue* stack = new TValue[1000];

    module->EnterVM(rc, stack);
}

}   // anonymous namespace
