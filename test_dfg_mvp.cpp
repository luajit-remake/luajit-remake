#include "gtest/gtest.h"

#include "test_util_helper.h"
#include "dfg_control_flow_and_upvalue_analysis.h"
#include "test_lua_file_utils.h"
#include "dfg_frontend.h"
#include "dfg_ir_dump.h"
#include "dfg_ir_validator.h"
#include "dfg_speculative_inliner.h"
#include "dfg_phantom_insertion.h"
#include "dfg_prediction_propagation.h"
#include "dfg_speculation_assignment.h"
#include "dfg_stack_layout_planning.h"
#include "dfg_register_bank_assignment.h"
#include "dfg_backend.h"

using namespace dfg;

// 'm' must be a freshly parsed module that has not been executed yet
// Run a minimal end-to-end DFG pipeline that compiles each function to DFG code without speculative inlining or value profile
//
inline void RunMinimalDfgCompilationPipeline(ScriptModule* m)
{
    for (UnlinkedCodeBlock* ucb : m->m_unlinkedCodeBlocks)
    {
        CodeBlock* cb = ucb->m_defaultCodeBlock;
        ReleaseAssert(cb != nullptr);
        arena_unique_ptr<Graph> graph = RunDfgFrontend(cb);
        ReleaseAssert(ValidateDfgIrGraph(graph.get()));

        TempArenaAllocator alloc;
        std::ignore = RunPredictionPropagationWithoutValueProfile(alloc, graph.get());

        RunSpeculationAssignmentPass(graph.get());

        RunPhantomInsertionPass(graph.get());
        StackLayoutPlanningResult slp = RunStackLayoutPlanningPass(alloc, graph.get());
        RunRegisterBankAssignmentPass(graph.get());
        ReleaseAssert(ValidateDfgIrGraph(graph.get()));
        DfgBackendResult backendResult = RunDfgBackend(alloc, graph.get(), slp);
        ReleaseAssert(ValidateDfgIrGraph(graph.get()));

        DfgCodeBlock* dcb = backendResult.m_dfgCodeBlock;
        ReleaseAssert(dcb != nullptr);

        // fprintf(stderr, "New function:\n");
        // fprintf(stderr, "cb stackSlots = %d, dcb stackSlots = %d\n", static_cast<int>(cb->m_stackFrameNumSlots), static_cast<int>(dcb->m_stackFrameNumSlots));
        // fprintf(stderr, "%s\n", backendResult.m_codegenLogDump);
        // fprintf(stderr, "JIT: [%llx, %llx)\n\n", reinterpret_cast<unsigned long long>(dcb->m_jitCodeEntry), reinterpret_cast<unsigned long long>(dcb->m_jitRegionStart) + dcb->m_jitRegionSize);

        ReleaseAssert(cb->m_dfgCodeBlock == nullptr);
        cb->m_dfgCodeBlock = dcb;
        cb->UpdateBestEntryPoint(dcb->m_jitCodeEntry);

        ReleaseAssert(cb->m_bestEntryPoint == dcb->m_jitCodeEntry);
    }
}

inline void RunSimpleLuaTestWithDfgMvp(const std::string& filename, const std::string originTestSuite, size_t manualStackSize = static_cast<size_t>(-1))
{
    VM* vm = VM::Create();
    Auto(vm->Destroy());
    vm->SetEngineStartingTier(VM::EngineStartingTier::BaselineJIT);
    vm->SetEngineMaxTier(VM::EngineMaxTier::BaselineJIT);
    VMOutputInterceptor vmoutput(vm);

    std::unique_ptr<ScriptModule> module = ParseLuaScriptOrFail(filename, LuaTestOption::ForceBaselineJit);

    RunMinimalDfgCompilationPipeline(module.get());

    if (manualStackSize != static_cast<size_t>(-1))
    {
        CoroutineRuntimeContext* rc = vm->GetRootCoroutine();
        rc->m_stackBegin = new TValue[manualStackSize];
    }

    vm->LaunchScript(module.get());

    std::string out = vmoutput.GetAndResetStdOut();
    std::string err = vmoutput.GetAndResetStdErr();

    std::string caseName = ::testing::UnitTest::GetInstance()->current_test_info()->name();
    std::string expectedOutputFileName = GetExpectedOutputFileNameForTestCase(originTestSuite, caseName, "" /*suffix*/);
    AssertOutputAgreesWithExpectedOutputFile(out, expectedOutputFileName);
    ReleaseAssert(err == "");

    FreeScriptModuleJITMemory(module.get());
}

// TODO: the following tests are currently excluded since they require non-standard test logic
//     TestPrint,
//     TestTableSizeHint,
//     LuaTest_ForPairs_Impl,
//     LuaTest_ForPairsPoisonNext_Impl,
//     LuaTest_ForPairsSlowNext_Impl,
//     LuaTest_BooleanAsTableIndex_1,
//     LuaTest_BooleanAsTableIndex_2,
//

TEST(DfgMvp, Fib)
{
    RunSimpleLuaTestWithDfgMvp("luatests/fib.lua", "LuaTest");
}

TEST(DfgMvp, TestTableDup)
{
    RunSimpleLuaTestWithDfgMvp("luatests/table_dup.lua", "LuaTest");
}

TEST(DfgMvp, TestTableDup2)
{
    RunSimpleLuaTestWithDfgMvp("luatests/table_dup2.lua", "LuaTest");
}

TEST(DfgMvp, TestTableDup3)
{
    RunSimpleLuaTestWithDfgMvp("luatests/table_dup3.lua", "LuaTest");
}

TEST(DfgMvp, Upvalue)
{
    RunSimpleLuaTestWithDfgMvp("luatests/upvalue.lua", "LuaTest");
}

TEST(DfgMvp, Fib_upvalue)
{
    RunSimpleLuaTestWithDfgMvp("luatests/fib_upvalue.lua", "LuaTest");
}

TEST(DfgMvp, LinearSieve)
{
    RunSimpleLuaTestWithDfgMvp("luatests/linear_sieve.lua", "LuaTest");
}

TEST(DfgMvp, NaNEdgeCase)
{
    RunSimpleLuaTestWithDfgMvp("luatests/nan_edge_case.lua", "LuaTest");
}

TEST(DfgMvp, ForLoopCoercion)
{
    RunSimpleLuaTestWithDfgMvp("luatests/for_loop_coercion.lua", "LuaTest");
}

TEST(DfgMvp, ForLoopEdgeCases)
{
    RunSimpleLuaTestWithDfgMvp("luatests/for_loop_edge_cases.lua", "LuaTest");
}

TEST(DfgMvp, PrimitiveConstants)
{
    RunSimpleLuaTestWithDfgMvp("luatests/primitive_constant.lua", "LuaTest");
}

TEST(DfgMvp, LogicalOpSanity)
{
    RunSimpleLuaTestWithDfgMvp("luatests/logical_op_sanity.lua", "LuaTest");
}

TEST(DfgMvp, PositiveAndNegativeInf)
{
    RunSimpleLuaTestWithDfgMvp("luatests/pos_and_neg_inf.lua", "LuaTest");
}

TEST(DfgMvp, LogicalNot)
{
    RunSimpleLuaTestWithDfgMvp("luatests/logical_not.lua", "LuaTest");
}

TEST(DfgMvp, LengthOperator)
{
    RunSimpleLuaTestWithDfgMvp("luatests/length_operator.lua", "LuaTest");
}

TEST(DfgMvp, TailCall)
{
    RunSimpleLuaTestWithDfgMvp("luatests/tail_call.lua", "LuaTest", 200 /*manualStackSize*/);
}

TEST(DfgMvp, VariadicTailCall_1)
{
    RunSimpleLuaTestWithDfgMvp("luatests/variadic_tail_call_1.lua", "LuaTest", 200 /*manualStackSize*/);
}

TEST(DfgMvp, VariadicTailCall_2)
{
    RunSimpleLuaTestWithDfgMvp("luatests/variadic_tail_call_2.lua", "LuaTest", 200 /*manualStackSize*/);
}

TEST(DfgMvp, VariadicTailCall_3)
{
    RunSimpleLuaTestWithDfgMvp("luatests/variadic_tail_call_3.lua", "LuaTest", 200 /*manualStackSize*/);
}

TEST(DfgMvp, OpcodeKNIL)
{
    RunSimpleLuaTestWithDfgMvp("luatests/test_knil.lua", "LuaTest");
}

TEST(DfgMvp, IterativeForLoop)
{
    RunSimpleLuaTestWithDfgMvp("luatests/iter_for.lua", "LuaTest");
}

TEST(DfgMvp, NegativeZeroAsIndex)
{
    RunSimpleLuaTestWithDfgMvp("luatests/negative_zero_as_index.lua", "LuaTest");
}

TEST(DfgMvp, ForPairsPoisonPairs)
{
    RunSimpleLuaTestWithDfgMvp("luatests/for_pairs_poison_pairs.lua", "LuaTest");
}

TEST(DfgMvp, ForPairsEmpty)
{
    RunSimpleLuaTestWithDfgMvp("luatests/for_pairs_empty.lua", "LuaTest");
}

TEST(DfgMvp, ForPairsNextButNotNil)
{
    RunSimpleLuaTestWithDfgMvp("luatests/for_pairs_next_but_not_nil.lua", "LuaTest");
}

TEST(DfgMvp, BooleanAsTableIndex_3)
{
    RunSimpleLuaTestWithDfgMvp("luatests/boolean_as_table_index_3.lua", "LuaTest");
}

TEST(DfgMvp, ArithmeticSanity)
{
    RunSimpleLuaTestWithDfgMvp("luatests/arithmetic_sanity.lua", "LuaTest");
}

TEST(DfgMvp, StringConcat)
{
    RunSimpleLuaTestWithDfgMvp("luatests/string_concat.lua", "LuaTest");
}

TEST(DfgMvp, TableVariadicPut)
{
    RunSimpleLuaTestWithDfgMvp("luatests/table_variadic_put.lua", "LuaTest");
}

TEST(DfgMvp, TableVariadicPut_2)
{
    RunSimpleLuaTestWithDfgMvp("luatests/table_variadic_put_2.lua", "LuaTest");
}

TEST(DfgMvp, UpvalueClosedOnException)
{
    RunSimpleLuaTestWithDfgMvp("luatests/upvalue_closed_on_exception.lua", "LuaTest");
}

TEST(DfgMvp, NBody)
{
    RunSimpleLuaTestWithDfgMvp("luatests/n-body.lua", "LuaBenchmark");
}

TEST(DfgMvp, Ack)
{
    RunSimpleLuaTestWithDfgMvp("luatests/ack.lua", "LuaBenchmark", 1000000 /*manualStackSize*/);
}

TEST(DfgMvp, BinaryTrees_1)
{
    RunSimpleLuaTestWithDfgMvp("luatests/binary-trees-1.lua", "LuaBenchmark");
}

TEST(DfgMvp, BinaryTrees_2)
{
    RunSimpleLuaTestWithDfgMvp("luatests/binary-trees-2.lua", "LuaBenchmark");
}

TEST(DfgMvp, Fannkuch_Redux)
{
    RunSimpleLuaTestWithDfgMvp("luatests/fannkuch-redux.lua", "LuaBenchmark");
}

TEST(DfgMvp, Fixpoint_Fact)
{
    RunSimpleLuaTestWithDfgMvp("luatests/fixpoint-fact.lua", "LuaBenchmark");
}

TEST(DfgMvp, Mandel_NoMetatable)
{
    RunSimpleLuaTestWithDfgMvp("luatests/mandel-nometatable.lua", "LuaBenchmark");
}

TEST(DfgMvp, Mandel)
{
    RunSimpleLuaTestWithDfgMvp("luatests/mandel.lua", "LuaBenchmark");
}

TEST(DfgMvp, QuadTree)
{
    RunSimpleLuaTestWithDfgMvp("luatests/qt.lua", "LuaBenchmark");
}

TEST(DfgMvp, Queen)
{
    RunSimpleLuaTestWithDfgMvp("luatests/queen.lua", "LuaBenchmark");
}

TEST(DfgMvp, NlgN_Sieve)
{
    RunSimpleLuaTestWithDfgMvp("luatests/nlgn_sieve.lua", "LuaBenchmark");
}

TEST(DfgMvp, Spectral_Norm)
{
    RunSimpleLuaTestWithDfgMvp("luatests/spectral-norm.lua", "LuaBenchmark");
}

TEST(DfgMvp, chameneos)
{
    RunSimpleLuaTestWithDfgMvp("luatests/chameneos.lua", "LuaBenchmark");
}

TEST(DfgMvp, xpcall_1)
{
    RunSimpleLuaTestWithDfgMvp("luatests/xpcall_1.lua", "LuaTest");
}

TEST(DfgMvp, xpcall_2)
{
    RunSimpleLuaTestWithDfgMvp("luatests/xpcall_2.lua", "LuaTest");
}

TEST(DfgMvp, xpcall_3)
{
    RunSimpleLuaTestWithDfgMvp("luatests/xpcall_3.lua", "LuaTest");
}

TEST(DfgMvp, xpcall_4)
{
    RunSimpleLuaTestWithDfgMvp("luatests/xpcall_4.lua", "LuaTest");
}

TEST(DfgMvp, xpcall_5)
{
    RunSimpleLuaTestWithDfgMvp("luatests/xpcall_5.lua", "LuaTest");
}

TEST(DfgMvp, xpcall_6)
{
    RunSimpleLuaTestWithDfgMvp("luatests/xpcall_6.lua", "LuaTest");
}

TEST(DfgMvp, pcall_1)
{
    RunSimpleLuaTestWithDfgMvp("luatests/pcall_1.lua", "LuaTest");
}

TEST(DfgMvp, pcall_2)
{
    RunSimpleLuaTestWithDfgMvp("luatests/pcall_2.lua", "LuaTest");
}

TEST(DfgMvp, GetSetMetatable)
{
    RunSimpleLuaTestWithDfgMvp("luatests/get_set_metatable.lua", "LuaTest");
}

TEST(DfgMvp, getsetmetatable_2)
{
    RunSimpleLuaTestWithDfgMvp("luatests/getsetmetatable_2.lua", "LuaTest");
}

TEST(DfgMvp, metatable_call_1)
{
    RunSimpleLuaTestWithDfgMvp("luatests/metatable_call_1.lua", "LuaTest");
}

TEST(DfgMvp, metatable_call_2)
{
    RunSimpleLuaTestWithDfgMvp("luatests/metatable_call_2.lua", "LuaTest");
}

TEST(DfgMvp, metatable_call_3)
{
    RunSimpleLuaTestWithDfgMvp("luatests/metatable_call_3.lua", "LuaTest");
}

TEST(DfgMvp, metatable_call_4)
{
    RunSimpleLuaTestWithDfgMvp("luatests/metatable_call_4.lua", "LuaTest");
}

TEST(DfgMvp, metatable_call_5)
{
    RunSimpleLuaTestWithDfgMvp("luatests/metatable_call_5.lua", "LuaTest");
}

TEST(DfgMvp, xpcall_metatable)
{
    RunSimpleLuaTestWithDfgMvp("luatests/xpcall_metatable.lua", "LuaTest");
}

TEST(DfgMvp, pcall_metatable)
{
    RunSimpleLuaTestWithDfgMvp("luatests/pcall_metatable.lua", "LuaTest");
}

TEST(DfgMvp, metatable_add_1)
{
    RunSimpleLuaTestWithDfgMvp("luatests/metatable_add_1.lua", "LuaTest");
}

TEST(DfgMvp, metatable_add_2)
{
    RunSimpleLuaTestWithDfgMvp("luatests/metatable_add_2.lua", "LuaTest");
}

TEST(DfgMvp, metatable_add_3)
{
    RunSimpleLuaTestWithDfgMvp("luatests/metatable_add_3.lua", "LuaTest");
}

TEST(DfgMvp, metatable_sub)
{
    RunSimpleLuaTestWithDfgMvp("luatests/metatable_sub.lua", "LuaTest");
}

TEST(DfgMvp, metatable_mul)
{
    RunSimpleLuaTestWithDfgMvp("luatests/metatable_mul.lua", "LuaTest");
}

TEST(DfgMvp, metatable_div)
{
    RunSimpleLuaTestWithDfgMvp("luatests/metatable_div.lua", "LuaTest");
}

TEST(DfgMvp, metatable_mod)
{
    RunSimpleLuaTestWithDfgMvp("luatests/metatable_mod.lua", "LuaTest");
}

TEST(DfgMvp, metatable_pow)
{
    RunSimpleLuaTestWithDfgMvp("luatests/metatable_pow.lua", "LuaTest");
}

TEST(DfgMvp, metatable_unm)
{
    RunSimpleLuaTestWithDfgMvp("luatests/metatable_unm.lua", "LuaTest");
}

TEST(DfgMvp, metatable_len)
{
    RunSimpleLuaTestWithDfgMvp("luatests/metatable_len.lua", "LuaTest");
}

TEST(DfgMvp, metatable_concat)
{
    RunSimpleLuaTestWithDfgMvp("luatests/metatable_concat.lua", "LuaTest");
}

TEST(DfgMvp, metatable_concat_2)
{
    RunSimpleLuaTestWithDfgMvp("luatests/metatable_concat_2.lua", "LuaTest");
}

TEST(DfgMvp, metatable_concat_3)
{
    RunSimpleLuaTestWithDfgMvp("luatests/metatable_concat_3.lua", "LuaTest");
}

TEST(DfgMvp, metatable_eq_1)
{
    RunSimpleLuaTestWithDfgMvp("luatests/metatable_eq_1.lua", "LuaTest");
}

TEST(DfgMvp, metatable_eq_2)
{
    RunSimpleLuaTestWithDfgMvp("luatests/metatable_eq_2.lua", "LuaTest");
}

TEST(DfgMvp, metatable_lt)
{
    RunSimpleLuaTestWithDfgMvp("luatests/metatable_lt.lua", "LuaTest");
}

TEST(DfgMvp, metatable_le)
{
    RunSimpleLuaTestWithDfgMvp("luatests/metatable_le.lua", "LuaTest");
}

TEST(DfgMvp, metatable_eq_3)
{
    RunSimpleLuaTestWithDfgMvp("luatests/metatable_eq_3.lua", "LuaTest");
}

TEST(DfgMvp, getbyid_metatable)
{
    RunSimpleLuaTestWithDfgMvp("luatests/getbyid_metatable.lua", "LuaTest");
}

TEST(DfgMvp, globalget_metatable)
{
    RunSimpleLuaTestWithDfgMvp("luatests/globalget_metatable.lua", "LuaTest");
}

TEST(DfgMvp, getbyval_metatable)
{
    RunSimpleLuaTestWithDfgMvp("luatests/getbyval_metatable.lua", "LuaTest");
}

TEST(DfgMvp, getbyintegerval_metatable)
{
    RunSimpleLuaTestWithDfgMvp("luatests/getbyintegerval_metatable.lua", "LuaTest");
}

TEST(DfgMvp, rawget_and_rawset)
{
    RunSimpleLuaTestWithDfgMvp("luatests/rawget_rawset.lua", "LuaTest");
}

TEST(DfgMvp, putbyid_metatable)
{
    RunSimpleLuaTestWithDfgMvp("luatests/putbyid_metatable.lua", "LuaTest");
}

TEST(DfgMvp, globalput_metatable)
{
    RunSimpleLuaTestWithDfgMvp("luatests/globalput_metatable.lua", "LuaTest");
}

TEST(DfgMvp, putbyintegerval_metatable)
{
    RunSimpleLuaTestWithDfgMvp("luatests/putbyintegerval_metatable.lua", "LuaTest");
}

TEST(DfgMvp, putbyval_metatable)
{
    RunSimpleLuaTestWithDfgMvp("luatests/putbyval_metatable.lua", "LuaTest");
}

TEST(DfgMvp, GlobalGetInterpreterIC)
{
    RunSimpleLuaTestWithDfgMvp("luatests/globalget_interpreter_ic.lua", "LuaTest");
}

TEST(DfgMvp, TableGetByIdInterpreterIC)
{
    RunSimpleLuaTestWithDfgMvp("luatests/table_getbyid_interpreter_ic.lua", "LuaTest");
}

TEST(DfgMvp, GetByImmInterpreterIC_1)
{
    RunSimpleLuaTestWithDfgMvp("luatests/get_by_imm_interpreter_ic_1.lua", "LuaTest");
}

TEST(DfgMvp, GetByImmInterpreterIC_2)
{
    RunSimpleLuaTestWithDfgMvp("luatests/get_by_imm_interpreter_ic_2.lua", "LuaTest");
}

TEST(DfgMvp, GetByValInterpreterIC_1)
{
    RunSimpleLuaTestWithDfgMvp("luatests/get_by_val_interpreter_ic_1.lua", "LuaTest");
}

TEST(DfgMvp, GetByValInterpreterIC_2)
{
    RunSimpleLuaTestWithDfgMvp("luatests/get_by_val_interpreter_ic_2.lua", "LuaTest");
}

TEST(DfgMvp, GetByValInterpreterIC_3)
{
    RunSimpleLuaTestWithDfgMvp("luatests/get_by_val_interpreter_ic_3.lua", "LuaTest");
}

TEST(DfgMvp, GetByValInterpreterIC_4)
{
    RunSimpleLuaTestWithDfgMvp("luatests/get_by_val_interpreter_ic_4.lua", "LuaTest");
}

TEST(DfgMvp, GetByValInterpreterIC_5)
{
    RunSimpleLuaTestWithDfgMvp("luatests/get_by_val_interpreter_ic_5.lua", "LuaTest");
}

TEST(DfgMvp, GetByValInterpreterIC_6)
{
    RunSimpleLuaTestWithDfgMvp("luatests/get_by_val_interpreter_ic_6.lua", "LuaTest");
}

TEST(DfgMvp, GlobalPutInterpreterIC_1)
{
    RunSimpleLuaTestWithDfgMvp("luatests/globalput_interpreter_ic_1.lua", "LuaTest");
}

TEST(DfgMvp, GlobalPutInterpreterIC_2)
{
    RunSimpleLuaTestWithDfgMvp("luatests/globalput_interpreter_ic_2.lua", "LuaTest");
}

TEST(DfgMvp, GlobalPutInterpreterIC_3)
{
    RunSimpleLuaTestWithDfgMvp("luatests/globalput_interpreter_ic_3.lua", "LuaTest");
}

TEST(DfgMvp, GlobalPutInterpreterIC_4)
{
    RunSimpleLuaTestWithDfgMvp("luatests/globalput_interpreter_ic_4.lua", "LuaTest");
}

TEST(DfgMvp, PutByIdInterpreterIC_1)
{
    RunSimpleLuaTestWithDfgMvp("luatests/putbyid_interpreter_ic_1.lua", "LuaTest");
}

TEST(DfgMvp, PutByIdInterpreterIC_2)
{
    RunSimpleLuaTestWithDfgMvp("luatests/putbyid_interpreter_ic_2.lua", "LuaTest");
}

TEST(DfgMvp, PutByIdInterpreterIC_3)
{
    RunSimpleLuaTestWithDfgMvp("luatests/putbyid_interpreter_ic_3.lua", "LuaTest");
}

TEST(DfgMvp, PutByIdInterpreterIC_4)
{
    RunSimpleLuaTestWithDfgMvp("luatests/putbyid_interpreter_ic_4.lua", "LuaTest");
}

TEST(DfgMvp, PutByIdInterpreterIC_5)
{
    RunSimpleLuaTestWithDfgMvp("luatests/putbyid_interpreter_ic_5.lua", "LuaTest");
}

TEST(DfgMvp, PutByIdInterpreterIC_6)
{
    RunSimpleLuaTestWithDfgMvp("luatests/putbyid_interpreter_ic_6.lua", "LuaTest");
}

TEST(DfgMvp, PutByIdInterpreterIC_7)
{
    RunSimpleLuaTestWithDfgMvp("luatests/putbyid_interpreter_ic_7.lua", "LuaTest");
}

TEST(DfgMvp, PutByIdInterpreterIC_8)
{
    RunSimpleLuaTestWithDfgMvp("luatests/putbyid_interpreter_ic_8.lua", "LuaTest");
}

TEST(DfgMvp, PutByImmInterpreterIC_1)
{
    RunSimpleLuaTestWithDfgMvp("luatests/putbyimm_interpreter_ic_1.lua", "LuaTest");
}

TEST(DfgMvp, PutByImmInterpreterIC_2)
{
    RunSimpleLuaTestWithDfgMvp("luatests/putbyimm_interpreter_ic_2.lua", "LuaTest");
}

TEST(DfgMvp, PutByImmInterpreterIC_3)
{
    RunSimpleLuaTestWithDfgMvp("luatests/putbyimm_interpreter_ic_3.lua", "LuaTest");
}

TEST(DfgMvp, PutByImmInterpreterIC_4)
{
    RunSimpleLuaTestWithDfgMvp("luatests/putbyimm_interpreter_ic_4.lua", "LuaTest");
}

TEST(DfgMvp, PutByValInterpreterIC_1)
{
    RunSimpleLuaTestWithDfgMvp("luatests/putbyval_interpreter_ic_1.lua", "LuaTest");
}

TEST(DfgMvp, PutByValInterpreterIC_2)
{
    RunSimpleLuaTestWithDfgMvp("luatests/putbyval_interpreter_ic_2.lua", "LuaTest");
}

TEST(DfgMvp, PutByValInterpreterIC_3)
{
    RunSimpleLuaTestWithDfgMvp("luatests/putbyval_interpreter_ic_3.lua", "LuaTest");
}

TEST(DfgMvp, PutByValInterpreterIC_4)
{
    RunSimpleLuaTestWithDfgMvp("luatests/putbyval_interpreter_ic_4.lua", "LuaTest");
}

TEST(DfgMvp, PutByValInterpreterIC_5)
{
    RunSimpleLuaTestWithDfgMvp("luatests/putbyval_interpreter_ic_5.lua", "LuaTest");
}

TEST(DfgMvp, istc_conditional_copy)
{
    RunSimpleLuaTestWithDfgMvp("luatests/istc_conditional_copy.lua", "LuaTest");
}

TEST(DfgMvp, isfc_conditional_copy)
{
    RunSimpleLuaTestWithDfgMvp("luatests/isfc_conditional_copy.lua", "LuaTest");
}

TEST(DfgMvp, le_use_lt_metamethod)
{
    RunSimpleLuaTestWithDfgMvp("luatests/le_use_lt_metamethod.lua", "LuaTest");
}

TEST(DfgMvp, upvalue_mutability)
{
    RunSimpleLuaTestWithDfgMvp("luatests/upvalue_mutability.lua", "LuaTest");
}

TEST(DfgMvp, base_assert)
{
    RunSimpleLuaTestWithDfgMvp("luatests/lib_base_assert.lua", "LuaLib");
}

TEST(DfgMvp, base_assert_2)
{
    RunSimpleLuaTestWithDfgMvp("luatests/base_lib_assert_2.lua", "LuaLib");
}

TEST(DfgMvp, RawsetReturnsOriginalTable)
{
    RunSimpleLuaTestWithDfgMvp("luatests/rawset_returns_original_table.lua", "LuaLib");
}

TEST(DfgMvp, InitEnvironment)
{
    RunSimpleLuaTestWithDfgMvp("luatests/init_environment.lua", "LuaLib");
}

TEST(DfgMvp, math_sqrt)
{
    RunSimpleLuaTestWithDfgMvp("luatests/math_sqrt.lua", "LuaLib");
}

TEST(DfgMvp, math_constants)
{
    RunSimpleLuaTestWithDfgMvp("luatests/math_constants.lua", "LuaLib");
}

TEST(DfgMvp, math_unary_fn)
{
    RunSimpleLuaTestWithDfgMvp("luatests/math_lib_unary.lua", "LuaLib");
}

TEST(DfgMvp, math_misc_fn)
{
    RunSimpleLuaTestWithDfgMvp("luatests/math_lib_misc.lua", "LuaLib");
}

TEST(DfgMvp, math_min_max)
{
    RunSimpleLuaTestWithDfgMvp("luatests/math_lib_min_max.lua", "LuaLib");
}

TEST(DfgMvp, math_random)
{
    RunSimpleLuaTestWithDfgMvp("luatests/math_lib_random.lua", "LuaLib");
}

TEST(DfgMvp, coroutine_1)
{
    RunSimpleLuaTestWithDfgMvp("luatests/coroutine_1.lua", "LuaLib");
}

TEST(DfgMvp, coroutine_2)
{
    RunSimpleLuaTestWithDfgMvp("luatests/coroutine_2.lua", "LuaLib");
}

TEST(DfgMvp, coroutine_3)
{
    RunSimpleLuaTestWithDfgMvp("luatests/coroutine_3.lua", "LuaLib");
}

TEST(DfgMvp, coroutine_4)
{
    RunSimpleLuaTestWithDfgMvp("luatests/coroutine_4.lua", "LuaLib");
}

TEST(DfgMvp, coroutine_5)
{
    RunSimpleLuaTestWithDfgMvp("luatests/coroutine_5.lua", "LuaLib");
}

TEST(DfgMvp, coroutine_ring)
{
    RunSimpleLuaTestWithDfgMvp("luatests/coroutine_ring.lua", "LuaLib");
}

TEST(DfgMvp, coroutine_error_1)
{
    RunSimpleLuaTestWithDfgMvp("luatests/coroutine_error_1.lua", "LuaLib");
}

TEST(DfgMvp, coroutine_error_2)
{
    RunSimpleLuaTestWithDfgMvp("luatests/coroutine_error_2.lua", "LuaLib");
}

TEST(DfgMvp, coroutine_error_3)
{
    RunSimpleLuaTestWithDfgMvp("luatests/coroutine_error_3.lua", "LuaLib");
}

TEST(DfgMvp, base_ipairs)
{
    RunSimpleLuaTestWithDfgMvp("luatests/base_lib_ipairs.lua", "LuaLib");
}

TEST(DfgMvp, base_ipairs_2)
{
    RunSimpleLuaTestWithDfgMvp("luatests/base_lib_ipairs_2.lua", "LuaLib");
}

TEST(DfgMvp, base_rawequal)
{
    RunSimpleLuaTestWithDfgMvp("luatests/base_lib_rawequal.lua", "LuaLib");
}

TEST(DfgMvp, base_select_1)
{
    RunSimpleLuaTestWithDfgMvp("luatests/base_lib_select_1.lua", "LuaLib");
}

TEST(DfgMvp, base_select_2)
{
    RunSimpleLuaTestWithDfgMvp("luatests/base_lib_select_2.lua", "LuaLib");
}

TEST(DfgMvp, base_lib_type)
{
    RunSimpleLuaTestWithDfgMvp("luatests/base_lib_type.lua", "LuaLib");
}

TEST(DfgMvp, base_lib_next)
{
    RunSimpleLuaTestWithDfgMvp("luatests/base_lib_next.lua", "LuaLib");
}

TEST(DfgMvp, base_lib_pairs)
{
    RunSimpleLuaTestWithDfgMvp("luatests/base_lib_pairs.lua", "LuaLib");
}

TEST(DfgMvp, base_lib_pcall)
{
    RunSimpleLuaTestWithDfgMvp("luatests/base_lib_pcall.lua", "LuaLib");
}

TEST(DfgMvp, base_lib_tonumber)
{
    RunSimpleLuaTestWithDfgMvp("luatests/base_lib_tonumber.lua", "LuaLib");
}

TEST(DfgMvp, base_lib_tonumber_2)
{
    RunSimpleLuaTestWithDfgMvp("luatests/base_lib_tonumber_2.lua", "LuaLib");
}

TEST(DfgMvp, base_lib_tostring)
{
    RunSimpleLuaTestWithDfgMvp("luatests/base_lib_tostring.lua", "LuaLib");
}

TEST(DfgMvp, base_lib_tostring_2)
{
    RunSimpleLuaTestWithDfgMvp("luatests/base_lib_tostring_2.lua", "LuaLib");
}

TEST(DfgMvp, base_lib_tostring_3)
{
    RunSimpleLuaTestWithDfgMvp("luatests/base_lib_tostring_3.lua", "LuaLib");
}

TEST(DfgMvp, base_lib_tostring_4)
{
    RunSimpleLuaTestWithDfgMvp("luatests/base_lib_tostring_4.lua", "LuaLib");
}

TEST(DfgMvp, base_lib_tostring_5)
{
    RunSimpleLuaTestWithDfgMvp("luatests/base_lib_tostring_5.lua", "LuaLib");
}

TEST(DfgMvp, base_lib_tostring_6)
{
    RunSimpleLuaTestWithDfgMvp("luatests/base_lib_tostring_6.lua", "LuaLib");
}

TEST(DfgMvp, base_lib_print)
{
    RunSimpleLuaTestWithDfgMvp("luatests/base_lib_print.lua", "LuaLib");
}

TEST(DfgMvp, base_lib_print_2)
{
    RunSimpleLuaTestWithDfgMvp("luatests/base_lib_print_2.lua", "LuaLib");
}

TEST(DfgMvp, base_lib_unpack)
{
    RunSimpleLuaTestWithDfgMvp("luatests/base_lib_unpack.lua", "LuaLib");
}

TEST(DfgMvp, string_lib_byte)
{
    RunSimpleLuaTestWithDfgMvp("luatests/string_lib_byte.lua", "LuaLib");
}

TEST(DfgMvp, string_lib_byte_2)
{
    RunSimpleLuaTestWithDfgMvp("luatests/string_lib_byte_2.lua", "LuaLib");
}

TEST(DfgMvp, string_lib_char)
{
    RunSimpleLuaTestWithDfgMvp("luatests/string_lib_char.lua", "LuaLib");
}

TEST(DfgMvp, string_lib_char_2)
{
    RunSimpleLuaTestWithDfgMvp("luatests/string_lib_char_2.lua", "LuaLib");
}

TEST(DfgMvp, string_lib_rep)
{
    RunSimpleLuaTestWithDfgMvp("luatests/string_lib_rep.lua", "LuaLib");
}

TEST(DfgMvp, string_lib_rep_2)
{
    RunSimpleLuaTestWithDfgMvp("luatests/string_lib_rep_2.lua", "LuaLib");
}

TEST(DfgMvp, string_lib_sub)
{
    RunSimpleLuaTestWithDfgMvp("luatests/string_lib_sub.lua", "LuaLib");
}

TEST(DfgMvp, string_lib_sub_2)
{
    RunSimpleLuaTestWithDfgMvp("luatests/string_lib_sub_2.lua", "LuaLib");
}

TEST(DfgMvp, string_format)
{
    RunSimpleLuaTestWithDfgMvp("luatests/string_format.lua", "LuaLib");
}

TEST(DfgMvp, string_lib_len)
{
    RunSimpleLuaTestWithDfgMvp("luatests/string_lib_len.lua", "LuaLib");
}

TEST(DfgMvp, string_lib_reverse)
{
    RunSimpleLuaTestWithDfgMvp("luatests/string_lib_reverse.lua", "LuaLib");
}

TEST(DfgMvp, string_lib_lower_upper)
{
    RunSimpleLuaTestWithDfgMvp("luatests/string_lib_lower_upper.lua", "LuaLib");
}

TEST(DfgMvp, string_lib_lower_upper_2)
{
    RunSimpleLuaTestWithDfgMvp("luatests/string_lib_lower_upper_2.lua", "LuaLib");
}

TEST(DfgMvp, string_lib_misc)
{
    RunSimpleLuaTestWithDfgMvp("luatests/string_lib_misc.lua", "LuaLib");
}

TEST(DfgMvp, table_sort_1)
{
    RunSimpleLuaTestWithDfgMvp("luatests/table_sort_1.lua", "LuaLib");
}

TEST(DfgMvp, table_sort_2)
{
    RunSimpleLuaTestWithDfgMvp("luatests/table_sort_2.lua", "LuaLib");
}

TEST(DfgMvp, table_sort_3)
{
    RunSimpleLuaTestWithDfgMvp("luatests/table_sort_3.lua", "LuaLib");
}

TEST(DfgMvp, table_sort_4)
{
    RunSimpleLuaTestWithDfgMvp("luatests/table_sort_4.lua", "LuaLib");
}

TEST(DfgMvp, table_lib_concat)
{
    RunSimpleLuaTestWithDfgMvp("luatests/table_lib_concat.lua", "LuaLib");
}

TEST(DfgMvp, table_concat_overflow)
{
    RunSimpleLuaTestWithDfgMvp("luatests/table_concat_overflow.lua", "LuaLib");
}

TEST(DfgMvp, array3d)
{
    RunSimpleLuaTestWithDfgMvp("luatests/array3d.lua", "LuaBenchmark");
}

TEST(DfgMvp, life)
{
    RunSimpleLuaTestWithDfgMvp("luatests/life.lua", "LuaBenchmark");
}

TEST(DfgMvp, mandel2)
{
    RunSimpleLuaTestWithDfgMvp("luatests/mandel2.lua", "LuaBenchmark");
}

TEST(DfgMvp, heapsort)
{
    RunSimpleLuaTestWithDfgMvp("luatests/heapsort.lua", "LuaBenchmark");
}

TEST(DfgMvp, nsieve)
{
    RunSimpleLuaTestWithDfgMvp("luatests/nsieve.lua", "LuaBenchmark");
}

TEST(DfgMvp, quadtree2)
{
    RunSimpleLuaTestWithDfgMvp("luatests/quadtree2.lua", "LuaBenchmark");
}

TEST(DfgMvp, ray)
{
    RunSimpleLuaTestWithDfgMvp("luatests/ray.lua", "LuaBenchmark");
}

TEST(DfgMvp, ray2)
{
    RunSimpleLuaTestWithDfgMvp("luatests/ray2.lua", "LuaBenchmark");
}

TEST(DfgMvp, series)
{
    RunSimpleLuaTestWithDfgMvp("luatests/series.lua", "LuaBenchmark");
}

TEST(DfgMvp, scimark_fft)
{
    RunSimpleLuaTestWithDfgMvp("luatests/scimark_fft.lua", "LuaBenchmark");
}

TEST(DfgMvp, scimark_lu)
{
    RunSimpleLuaTestWithDfgMvp("luatests/scimark_lu.lua", "LuaBenchmark");
}

TEST(DfgMvp, scimark_sor)
{
    RunSimpleLuaTestWithDfgMvp("luatests/scimark_sor.lua", "LuaBenchmark");
}

TEST(DfgMvp, scimark_sparse)
{
    RunSimpleLuaTestWithDfgMvp("luatests/scimark_sparse.lua", "LuaBenchmark");
}

TEST(DfgMvp, table_sort)
{
    RunSimpleLuaTestWithDfgMvp("luatests/table_sort.lua", "LuaBenchmark");
}

TEST(DfgMvp, table_sort_cmp)
{
    RunSimpleLuaTestWithDfgMvp("luatests/table_sort_cmp.lua", "LuaBenchmark");
}

TEST(DfgMvp, base_loadstring)
{
    RunSimpleLuaTestWithDfgMvp("luatests/base_loadstring.lua", "LuaLib");
}

TEST(DfgMvp, base_load)
{
    RunSimpleLuaTestWithDfgMvp("luatests/base_load.lua", "LuaLib");
}

TEST(DfgMvp, base_loadfile)
{
    RunSimpleLuaTestWithDfgMvp("luatests/base_loadfile.lua", "LuaLib");
}

TEST(DfgMvp, base_loadfile_nonexistent)
{
    RunSimpleLuaTestWithDfgMvp("luatests/base_loadfile_nonexistent.lua", "LuaLib");
}

TEST(DfgMvp, base_lib_dofile)
{
    RunSimpleLuaTestWithDfgMvp("luatests/base_lib_dofile.lua", "LuaLib");
}

TEST(DfgMvp, base_lib_dofile_nonexistent)
{
    RunSimpleLuaTestWithDfgMvp("luatests/base_lib_dofile_nonexistent.lua", "LuaLib");
}

TEST(DfgMvp, base_lib_dofile_bad_syntax)
{
    RunSimpleLuaTestWithDfgMvp("luatests/base_lib_dofile_bad_syntax.lua", "LuaLib");
}

TEST(DfgMvp, base_lib_dofile_throw)
{
    RunSimpleLuaTestWithDfgMvp("luatests/base_lib_dofile_throw.lua", "LuaLib");
}

TEST(DfgMvp, fasta)
{
    RunSimpleLuaTestWithDfgMvp("luatests/fasta.lua", "LuaBenchmark");
}

TEST(DfgMvp, pidigits)
{
    RunSimpleLuaTestWithDfgMvp("luatests/pidigits-nogmp.lua", "LuaBenchmark");
}

TEST(DfgMvp, revcomp)
{
    RunSimpleLuaTestWithDfgMvp("luatests/revcomp.lua", "LuaBenchmark");
}

TEST(DfgMvp, knucleotide)
{
    RunSimpleLuaTestWithDfgMvp("luatests/k-nucleotide.lua", "LuaBenchmark");
}

TEST(DfgMvp, comparison_one_side_constant)
{
    RunSimpleLuaTestWithDfgMvp("luatests/comparison_one_side_constant.lua", "LuaTest");
}

TEST(DfgMvp, bounce)
{
    RunSimpleLuaTestWithDfgMvp("luatests/bounce.lua", "LuaBenchmark");
}

TEST(DfgMvp, cd)
{
    RunSimpleLuaTestWithDfgMvp("luatests/cd.lua", "LuaBenchmark");
}

TEST(DfgMvp, deltablue)
{
    RunSimpleLuaTestWithDfgMvp("luatests/deltablue.lua", "LuaBenchmark");
}

TEST(DfgMvp, havlak)
{
    RunSimpleLuaTestWithDfgMvp("luatests/havlak.lua", "LuaBenchmark", 100000 /*manualStackSize*/);
}

TEST(DfgMvp, json)
{
    RunSimpleLuaTestWithDfgMvp("luatests/json.lua", "LuaBenchmark");
}

TEST(DfgMvp, list)
{
    RunSimpleLuaTestWithDfgMvp("luatests/list.lua", "LuaBenchmark");
}

TEST(DfgMvp, permute)
{
    RunSimpleLuaTestWithDfgMvp("luatests/permute.lua", "LuaBenchmark");
}

TEST(DfgMvp, richard)
{
    RunSimpleLuaTestWithDfgMvp("luatests/richard.lua", "LuaBenchmark");
}

TEST(DfgMvp, storage)
{
    RunSimpleLuaTestWithDfgMvp("luatests/storage.lua", "LuaBenchmark");
}

TEST(DfgMvp, towers)
{
    RunSimpleLuaTestWithDfgMvp("luatests/towers.lua", "LuaBenchmark");
}

