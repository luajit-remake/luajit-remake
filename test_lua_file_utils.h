#pragma once

#include <fstream>
#include "runtime_utils.h"
#include "gtest/gtest.h"
#include "json_utils.h"
#include "test_util_helper.h"
#include "test_vm_utils.h"
#include "lj_parser_wrapper.h"

inline std::string LoadFile(std::string filename)
{
    std::ifstream infile(filename, std::ios_base::binary);
    ReleaseAssert(infile.rdstate() == std::ios_base::goodbit);
    std::istreambuf_iterator<char> iter { infile }, end;
    return std::string { iter, end };
}

enum class LuaTestOption
{
    // The test shall be run fully in interpreter mode, never tier up to anything else
    //
    ForceInterpreter,
    // The test shall be run fully in baseline JIT mode
    // This means all Lua functions are immediately compiled to baseline JIT code, the interpreter is never invoked
    //
    ForceBaselineJit,
    // The test shall start in interpreter mode, can tier up to baseline JIT, but not further
    //
    UpToBaselineJit
};

inline VM::EngineStartingTier WARN_UNUSED GetVMEngineStartingTierFromEngineTestOption(LuaTestOption testOption)
{
    switch (testOption)
    {
    case LuaTestOption::ForceInterpreter: { return VM::EngineStartingTier::Interpreter; }
    case LuaTestOption::ForceBaselineJit: { return VM::EngineStartingTier::BaselineJIT; }
    case LuaTestOption::UpToBaselineJit: { return VM::EngineStartingTier::Interpreter; }
    }
}

inline VM::EngineMaxTier WARN_UNUSED GetVMEngineMaxTierFromEngineTestOption(LuaTestOption testOption)
{
    switch (testOption)
    {
    case LuaTestOption::ForceInterpreter: { return VM::EngineMaxTier::Interpreter; }
    case LuaTestOption::ForceBaselineJit: { return VM::EngineMaxTier::BaselineJIT; }
    case LuaTestOption::UpToBaselineJit: { return VM::EngineMaxTier::BaselineJIT; }
    }
}

inline std::unique_ptr<ScriptModule> ParseLuaScriptOrFail(const std::string& filename, LuaTestOption testOptionForAssertion)
{
    ReleaseAssert(filename.ends_with(".lua"));
    VM* vm = VM::GetActiveVMForCurrentThread();
    std::string content = LoadFile(filename);
    ParseResult res = ParseLuaScript(vm->GetRootCoroutine(), content);
    if (res.m_scriptModule.get() == nullptr)
    {
        fprintf(stderr, "Parsing file '%s' failed!\n", filename.c_str());
        PrintTValue(stderr, res.errMsg);
        abort();
    }

    if (GetVMEngineStartingTierFromEngineTestOption(testOptionForAssertion) == VM::EngineStartingTier::BaselineJIT)
    {
        // Sanity check that the entry point of the module indeed points to the baseline JIT code
        //
        FunctionObject* obj = res.m_scriptModule->m_defaultEntryPoint.As();
        ExecutableCode* ec = TranslateToRawPointer(obj->m_executable.As());
        ReleaseAssert(ec->IsBytecodeFunction());
        CodeBlock* cb = static_cast<CodeBlock*>(ec);
        ReleaseAssert(ec->m_bestEntryPoint == cb->m_baselineCodeBlock->m_jitCodeEntry);
    }

    return std::move(res.m_scriptModule);
}

#if 0
[[maybe_unused]] inline std::unique_ptr<ScriptModule> ParseLuaScriptOrJsonBytecodeDumpOrFail(const std::string& filename)
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
#endif

inline void RunSimpleLuaTest(const std::string& filename, LuaTestOption testOption)
{
    VM* vm = VM::Create();
    Auto(vm->Destroy());
    vm->SetEngineStartingTier(GetVMEngineStartingTierFromEngineTestOption(testOption));
    vm->SetEngineMaxTier(GetVMEngineMaxTierFromEngineTestOption(testOption));
    VMOutputInterceptor vmoutput(vm);

    std::unique_ptr<ScriptModule> module = ParseLuaScriptOrFail(filename, testOption);
    vm->LaunchScript(module.get());

    std::string out = vmoutput.GetAndResetStdOut();
    std::string err = vmoutput.GetAndResetStdErr();
    AssertIsExpectedOutput(out);
    ReleaseAssert(err == "");
}
