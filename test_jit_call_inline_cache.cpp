#include "gtest/gtest.h"
#include "test_lua_file_utils.h"

TEST(BaselineJitCallIc, Sanity_1)
{
    VM* vm = VM::Create();
    Auto(vm->Destroy());
    vm->SetEngineStartingTier(GetVMEngineStartingTierFromEngineTestOption(LuaTestOption::ForceBaselineJit));
    VMOutputInterceptor vmoutput(vm);

    std::unique_ptr<ScriptModule> module = ParseLuaScriptOrFail("luatests/baseline_jit_call_ic_sanity_1.lua", LuaTestOption::ForceBaselineJit);
    vm->LaunchScript(module.get());

    std::string out = vmoutput.GetAndResetStdOut();
    std::string err = vmoutput.GetAndResetStdErr();
    AssertIsExpectedOutput(out);
    ReleaseAssert(err == "");

    // The function 'f' should have one IC site in direct-call mode with 3 entries, sanity check it
    //
    // Note that this check is not a complete check! We do not check disassembly that the JIT'ed IC is actually chained correctly, for simplicity
    //
    // Find the codeblock with 1 arg, which we should find three: 'add1', 'add2', 'add3'
    //
    std::vector<UnlinkedCodeBlock*> addFns;
    for (UnlinkedCodeBlock* ucb : module->m_unlinkedCodeBlocks)
    {
        if (ucb->m_numFixedArguments == 1 && !ucb->m_hasVariadicArguments)
        {
            addFns.push_back(ucb);
        }
    }

    ReleaseAssert(addFns.size() == 3);

    for (UnlinkedCodeBlock* ucb : addFns)
    {
        CodeBlock* cb = ucb->m_defaultCodeBlock;
        ReleaseAssert(cb != nullptr);

        size_t numIC = 0;
        for (JitCallInlineCacheEntry* entry : cb->m_jitCallIcList.elements())
        {
            ReleaseAssert(entry->GetIcTrait()->m_isDirectCallMode);
            numIC++;
        }
        ReleaseAssert(numIC == 1);
    }
}

// Also run the test in interpreter-only mode since it doesn't hurt anything
//
TEST(BaselineJitCallIc, Sanity_1_NoJit)
{
    RunSimpleLuaTest("luatests/baseline_jit_call_ic_sanity_1.lua", LuaTestOption::ForceInterpreter);
}

TEST(BaselineJitCallIc, Sanity_2)
{
    VM* vm = VM::Create();
    Auto(vm->Destroy());
    vm->SetEngineStartingTier(GetVMEngineStartingTierFromEngineTestOption(LuaTestOption::ForceBaselineJit));
    VMOutputInterceptor vmoutput(vm);

    std::unique_ptr<ScriptModule> module = ParseLuaScriptOrFail("luatests/baseline_jit_call_ic_sanity_2.lua", LuaTestOption::ForceBaselineJit);
    vm->LaunchScript(module.get());

    std::string out = vmoutput.GetAndResetStdOut();
    std::string err = vmoutput.GetAndResetStdErr();
    AssertIsExpectedOutput(out);
    ReleaseAssert(err == "");

    // The function 'f' should have one IC site in closure-call mode with 3 entries, sanity check it
    //
    // Note that this check is not a complete check! We do not check disassembly that the JIT'ed IC is actually chained correctly, for simplicity
    //
    // Find the codeblock with 1 arg, which we should find three: the lambdas returned by 'fnFactory1/2/3'
    //
    std::vector<UnlinkedCodeBlock*> lambdaFns;
    for (UnlinkedCodeBlock* ucb : module->m_unlinkedCodeBlocks)
    {
        if (ucb->m_numFixedArguments == 1 && !ucb->m_hasVariadicArguments)
        {
            lambdaFns.push_back(ucb);
        }
    }

    ReleaseAssert(lambdaFns.size() == 3);

    for (UnlinkedCodeBlock* ucb : lambdaFns)
    {
        CodeBlock* cb = ucb->m_defaultCodeBlock;
        ReleaseAssert(cb != nullptr);

        size_t numIC = 0;
        for (JitCallInlineCacheEntry* entry : cb->m_jitCallIcList.elements())
        {
            ReleaseAssert(!entry->GetIcTrait()->m_isDirectCallMode);
            numIC++;
        }
        ReleaseAssert(numIC == 1);
    }
}

TEST(BaselineJitCallIc, Sanity_2_NoJit)
{
    RunSimpleLuaTest("luatests/baseline_jit_call_ic_sanity_2.lua", LuaTestOption::ForceInterpreter);
}

TEST(BaselineJitCallIc, Sanity_3)
{
    VM* vm = VM::Create();
    Auto(vm->Destroy());
    vm->SetEngineStartingTier(GetVMEngineStartingTierFromEngineTestOption(LuaTestOption::ForceBaselineJit));
    VMOutputInterceptor vmoutput(vm);

    std::unique_ptr<ScriptModule> module = ParseLuaScriptOrFail("luatests/baseline_jit_call_ic_sanity_3.lua", LuaTestOption::ForceBaselineJit);
    vm->LaunchScript(module.get());

    std::string out = vmoutput.GetAndResetStdOut();
    std::string err = vmoutput.GetAndResetStdErr();
    AssertIsExpectedOutput(out);
    ReleaseAssert(err == "");

    // The function 'f' should have one IC site in closure-call mode with 3 entries, sanity check it
    //
    // Note that this check is not a complete check! We do not check disassembly that the JIT'ed IC is actually chained correctly, for simplicity
    //
    // Find the codeblock with 1 arg, which we should find three: the lambdas returned by 'fnFactory1/2/3'
    //
    std::vector<UnlinkedCodeBlock*> lambdaFns;
    for (UnlinkedCodeBlock* ucb : module->m_unlinkedCodeBlocks)
    {
        if (ucb->m_numFixedArguments == 1 && !ucb->m_hasVariadicArguments)
        {
            lambdaFns.push_back(ucb);
        }
    }

    ReleaseAssert(lambdaFns.size() == 3);

    for (UnlinkedCodeBlock* ucb : lambdaFns)
    {
        CodeBlock* cb = ucb->m_defaultCodeBlock;
        ReleaseAssert(cb != nullptr);

        size_t numIC = 0;
        for (JitCallInlineCacheEntry* entry : cb->m_jitCallIcList.elements())
        {
            ReleaseAssert(!entry->GetIcTrait()->m_isDirectCallMode);
            numIC++;
        }
        ReleaseAssert(numIC == 1);
    }
}

TEST(BaselineJitCallIc, Sanity_3_NoJit)
{
    RunSimpleLuaTest("luatests/baseline_jit_call_ic_sanity_3.lua", LuaTestOption::ForceInterpreter);
}

TEST(BaselineJitCallIc, Stress_1)
{
    RunSimpleLuaTest("luatests/baseline_jit_call_ic_stress_direct_call_1.lua", LuaTestOption::ForceBaselineJit);
}

TEST(BaselineJitCallIc, Stress_1_NoJit)
{
    RunSimpleLuaTest("luatests/baseline_jit_call_ic_stress_direct_call_1.lua", LuaTestOption::ForceInterpreter);
}

TEST(BaselineJitCallIc, Stress_2)
{
    RunSimpleLuaTest("luatests/baseline_jit_call_ic_stress_direct_call_2.lua", LuaTestOption::ForceBaselineJit);
}

TEST(BaselineJitCallIc, Stress_2_NoJit)
{
    RunSimpleLuaTest("luatests/baseline_jit_call_ic_stress_direct_call_2.lua", LuaTestOption::ForceInterpreter);
}

TEST(BaselineJitCallIc, Stress_3)
{
    RunSimpleLuaTest("luatests/baseline_jit_call_ic_stress_direct_call_3.lua", LuaTestOption::ForceBaselineJit);
}

TEST(BaselineJitCallIc, Stress_3_NoJit)
{
    RunSimpleLuaTest("luatests/baseline_jit_call_ic_stress_direct_call_3.lua", LuaTestOption::ForceInterpreter);
}

TEST(BaselineJitCallIc, Stress_4)
{
    RunSimpleLuaTest("luatests/baseline_jit_call_ic_stress_direct_call_4.lua", LuaTestOption::ForceBaselineJit);
}

TEST(BaselineJitCallIc, Stress_4_NoJit)
{
    RunSimpleLuaTest("luatests/baseline_jit_call_ic_stress_direct_call_4.lua", LuaTestOption::ForceInterpreter);
}

TEST(BaselineJitCallIc, Stress_5)
{
    RunSimpleLuaTest("luatests/baseline_jit_call_ic_stress_closure_call_1.lua", LuaTestOption::ForceBaselineJit);
}

TEST(BaselineJitCallIc, Stress_5_NoJit)
{
    RunSimpleLuaTest("luatests/baseline_jit_call_ic_stress_closure_call_1.lua", LuaTestOption::ForceInterpreter);
}

TEST(BaselineJitCallIc, Stress_6)
{
    RunSimpleLuaTest("luatests/baseline_jit_call_ic_stress_closure_call_2.lua", LuaTestOption::ForceBaselineJit);
}

TEST(BaselineJitCallIc, Stress_6_NoJit)
{
    RunSimpleLuaTest("luatests/baseline_jit_call_ic_stress_closure_call_2.lua", LuaTestOption::ForceInterpreter);
}

TEST(BaselineJitCallIc, Stress_7)
{
    RunSimpleLuaTest("luatests/baseline_jit_call_ic_stress_closure_call_3.lua", LuaTestOption::ForceBaselineJit);
}

TEST(BaselineJitCallIc, Stress_7_NoJit)
{
    RunSimpleLuaTest("luatests/baseline_jit_call_ic_stress_closure_call_3.lua", LuaTestOption::ForceInterpreter);
}

TEST(BaselineJitCallIc, Stress_8)
{
    RunSimpleLuaTest("luatests/baseline_jit_call_ic_stress_closure_call_4.lua", LuaTestOption::ForceBaselineJit);
}

TEST(BaselineJitCallIc, Stress_8_NoJit)
{
    RunSimpleLuaTest("luatests/baseline_jit_call_ic_stress_closure_call_4.lua", LuaTestOption::ForceInterpreter);
}
