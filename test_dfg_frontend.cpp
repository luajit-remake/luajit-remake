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

using namespace dfg;

TEST(DfgFrontend, UpvalueAnalysis_1)
{
    VM* vm = VM::Create();
    Auto(vm->Destroy());

    vm->SetEngineStartingTier(VM::EngineStartingTier::BaselineJIT);
    vm->SetEngineMaxTier(VM::EngineMaxTier::BaselineJIT);
    std::unique_ptr<ScriptModule> module = ParseLuaScriptOrFail("luatests/opt_jit_frontend_capture_analysis_1.lua", LuaTestOption::ForceBaselineJit);

    ReleaseAssert(module->m_unlinkedCodeBlocks.size() == 6);

    UnlinkedCodeBlock* ucb = module->m_unlinkedCodeBlocks.back();
    CodeBlock* cb = ucb->m_defaultCodeBlock;

    ReleaseAssert(cb->m_stackFrameNumSlots == 4);

    TempArenaAllocator alloc;
    DfgControlFlowAndUpvalueAnalysisResult res = RunControlFlowAndUpvalueAnalysis(alloc, cb);

    for (size_t i = 0; i < res.m_basicBlocks.size(); i++)
    {
        ReleaseAssert(res.m_basicBlocks[i]->m_ord == i);
    }

    ReleaseAssert(res.m_basicBlocks.size() == 8);
    ReleaseAssert(res.m_basicBlocks[0]->m_numSuccessors == 2);
    ReleaseAssert(res.m_basicBlocks[0]->m_isLocalCapturedAtHead.m_data[0] == 0);
    ReleaseAssert(res.m_basicBlocks[0]->m_isLocalCapturedAtTail.m_data[0] == 0);

    BasicBlockUpvalueInfo* b1 = res.m_basicBlocks[0]->m_successors[0];
    BasicBlockUpvalueInfo* b2 = res.m_basicBlocks[0]->m_successors[1];

    ReleaseAssert(b1->m_numSuccessors == 1);
    ReleaseAssert(b2->m_numSuccessors == 1);
    ReleaseAssert(b1->m_successors[0] == b2->m_successors[0]);

    BasicBlockUpvalueInfo* b3 = b1->m_successors[0];

    ReleaseAssert(b1->m_isLocalCapturedAtHead.m_data[0] == 0);
    ReleaseAssert(b2->m_isLocalCapturedAtHead.m_data[0] == 0);

    {
        uint64_t t1 = b1->m_isLocalCapturedAtTail.m_data[0];
        uint64_t t2 = b2->m_isLocalCapturedAtTail.m_data[0];
        if (t1 > t2) { std::swap(t1, t2); }
        ReleaseAssert(t1 == 1 && t2 == 2);
    }

    ReleaseAssert(b3->m_isLocalCapturedAtHead.m_data[0] == 3);
    ReleaseAssert(b3->m_isLocalCapturedAtTail.m_data[0] == 7);

    ReleaseAssert(b3->m_numSuccessors == 2);
    BasicBlockUpvalueInfo* b4 = b3->m_successors[0];
    BasicBlockUpvalueInfo* b5 = b3->m_successors[1];

    ReleaseAssert(b4->m_numSuccessors == 1);
    ReleaseAssert(b5->m_numSuccessors == 1);
    ReleaseAssert(b4->m_successors[0] == b5->m_successors[0]);

    BasicBlockUpvalueInfo* b6 = b4->m_successors[0];

    ReleaseAssert(b4->m_isLocalCapturedAtHead.m_data[0] == 7);
    ReleaseAssert(b4->m_isLocalCapturedAtTail.m_data[0] == 7);

    ReleaseAssert(b5->m_isLocalCapturedAtHead.m_data[0] == 7);
    ReleaseAssert(b5->m_isLocalCapturedAtTail.m_data[0] == 7);

    ReleaseAssert(b6->m_isLocalCapturedAtHead.m_data[0] == 7);
    ReleaseAssert(b6->m_isLocalCapturedAtTail.m_data[0] == 0);

    ReleaseAssert(b6->m_numSuccessors == 1);
    BasicBlockUpvalueInfo* b7 = b6->m_successors[0];

    ReleaseAssert(b7->m_isLocalCapturedAtHead.m_data[0] == 0);
    ReleaseAssert(b7->m_isLocalCapturedAtTail.m_data[0] == 0);
    ReleaseAssert(b7->m_numSuccessors == 0);
}

TEST(DfgFrontend, UpvalueAnalysis_2)
{
    VM* vm = VM::Create();
    Auto(vm->Destroy());

    vm->SetEngineStartingTier(VM::EngineStartingTier::BaselineJIT);
    vm->SetEngineMaxTier(VM::EngineMaxTier::BaselineJIT);
    std::unique_ptr<ScriptModule> module = ParseLuaScriptOrFail("luatests/opt_jit_frontend_capture_analysis_2.lua", LuaTestOption::ForceBaselineJit);

    ReleaseAssert(module->m_unlinkedCodeBlocks.size() == 2);

    UnlinkedCodeBlock* ucb = module->m_unlinkedCodeBlocks.back();
    CodeBlock* cb = ucb->m_defaultCodeBlock;

    TempArenaAllocator alloc;
    DfgControlFlowAndUpvalueAnalysisResult res = RunControlFlowAndUpvalueAnalysis(alloc, cb);

    ReleaseAssert(res.m_basicBlocks.size() == 7);

    BasicBlockUpvalueInfo* b0 = res.m_basicBlocks[0];
    ReleaseAssert(b0->m_numSuccessors == 1);
    ReleaseAssert(b0->m_isLocalCapturedAtHead.m_data[0] == 0);
    ReleaseAssert(b0->m_isLocalCapturedAtTail.m_data[0] == 0);

    BasicBlockUpvalueInfo* b1 = b0->m_successors[0];
    ReleaseAssert(b1->m_numSuccessors == 2);
    ReleaseAssert(b1->m_isLocalCapturedAtHead.m_data[0] == 1);
    ReleaseAssert(b1->m_isLocalCapturedAtTail.m_data[0] == 1);

    BasicBlockUpvalueInfo* b2 = b1->m_successors[0];
    ReleaseAssert(b2->m_numSuccessors == 2);
    ReleaseAssert(b2->m_isLocalCapturedAtHead.m_data[0] == 1);
    ReleaseAssert(b2->m_isLocalCapturedAtTail.m_data[0] == 3);

    BasicBlockUpvalueInfo* b3 = b2->m_successors[0];
    BasicBlockUpvalueInfo* b4 = b2->m_successors[1];

    if (b3->m_numSuccessors != 1 || b3->m_successors[0] != b4)
    {
        std::swap(b3, b4);
    }

    ReleaseAssert(b3->m_numSuccessors == 1 && b3->m_successors[0] == b4);
    ReleaseAssert(b3->m_isLocalCapturedAtHead.m_data[0] == 3);
    ReleaseAssert(b3->m_isLocalCapturedAtTail.m_data[0] == 3);

    ReleaseAssert(b4->m_numSuccessors == 1);
    ReleaseAssert(b4->m_successors[0] == b1);
    ReleaseAssert(b4->m_isLocalCapturedAtHead.m_data[0] == 3);
    ReleaseAssert(b4->m_isLocalCapturedAtTail.m_data[0] == 1);

    BasicBlockUpvalueInfo* b5 = b1->m_successors[1];
    ReleaseAssert(b5->m_numSuccessors == 1);
    ReleaseAssert(b5->m_isLocalCapturedAtHead.m_data[0] == 1);
    ReleaseAssert(b5->m_isLocalCapturedAtTail.m_data[0] == 0);

    BasicBlockUpvalueInfo* b6 = b5->m_successors[0];
    ReleaseAssert(b6->m_numSuccessors == 0);
    ReleaseAssert(b6->m_isLocalCapturedAtHead.m_data[0] == 0);
    ReleaseAssert(b6->m_isLocalCapturedAtTail.m_data[0] == 0);
}

TEST(DfgFrontend, UpvalueAnalysis_3)
{
    VM* vm = VM::Create();
    Auto(vm->Destroy());

    vm->SetEngineStartingTier(VM::EngineStartingTier::BaselineJIT);
    vm->SetEngineMaxTier(VM::EngineMaxTier::BaselineJIT);
    std::unique_ptr<ScriptModule> module = ParseLuaScriptOrFail("luatests/opt_jit_frontend_capture_analysis_3.lua", LuaTestOption::ForceBaselineJit);

    ReleaseAssert(module->m_unlinkedCodeBlocks.size() == 3);

    UnlinkedCodeBlock* ucb = module->m_unlinkedCodeBlocks[1];
    CodeBlock* cb = ucb->m_defaultCodeBlock;

    TempArenaAllocator alloc;
    DfgControlFlowAndUpvalueAnalysisResult res = RunControlFlowAndUpvalueAnalysis(alloc, cb);

    ReleaseAssert(res.m_basicBlocks.size() == 3);

    BasicBlockUpvalueInfo* b0 = res.m_basicBlocks[0];
    ReleaseAssert(b0->m_numSuccessors == 2);
    ReleaseAssert(b0->m_isLocalCapturedAtHead.m_data[0] == 1);
    ReleaseAssert(b0->m_isLocalCapturedAtTail.m_data[0] == 1);

    BasicBlockUpvalueInfo* b1 = b0->m_successors[0];
    if (b1 == b0)
    {
        b1 = b0->m_successors[1];
    }
    else
    {
        ReleaseAssert(b0->m_successors[1] == b0);
    }

    ReleaseAssert(b1 != b0);
    ReleaseAssert(b1->m_numSuccessors == 1);
    ReleaseAssert(b1->m_isLocalCapturedAtHead.m_data[0] == 1);
    ReleaseAssert(b1->m_isLocalCapturedAtTail.m_data[0] == 0);

    BasicBlockUpvalueInfo* b2 = b1->m_successors[0];
    ReleaseAssert(b2->m_numSuccessors == 0);
    ReleaseAssert(b2->m_isLocalCapturedAtHead.m_data[0] == 0);
    ReleaseAssert(b2->m_isLocalCapturedAtTail.m_data[0] == 0);
}

const char* const x_lua_files_for_parser_test[] = {
    "arithmetic_sanity.lua",
    "array3d.lua",
    "base_lib_assert_2.lua",
    "base_lib_dofile_file.lua",
    "base_lib_dofile.lua",
    "base_lib_dofile_nonexistent.lua",
    "base_lib_dofile_throw.lua",
    "base_lib_ipairs_2.lua",
    "base_lib_ipairs.lua",
    "base_lib_next.lua",
    "base_lib_pairs.lua",
    "base_lib_pcall.lua",
    "base_lib_print_2.lua",
    "base_lib_print.lua",
    "base_lib_rawequal.lua",
    "base_lib_select_1.lua",
    "base_lib_select_2.lua",
    "base_lib_tonumber_2.lua",
    "base_lib_tonumber.lua",
    "base_lib_tostring_2.lua",
    "base_lib_tostring_3.lua",
    "base_lib_tostring_4.lua",
    "base_lib_tostring_5.lua",
    "base_lib_tostring_6.lua",
    "base_lib_tostring.lua",
    "base_lib_type.lua",
    "base_lib_unpack.lua",
    "baseline_jit_call_ic_sanity_1.lua",
    "baseline_jit_call_ic_sanity_2.lua",
    "baseline_jit_call_ic_sanity_3.lua",
    "baseline_jit_call_ic_stress_closure_call_1.lua",
    "baseline_jit_call_ic_stress_closure_call_2.lua",
    "baseline_jit_call_ic_stress_closure_call_3.lua",
    "baseline_jit_call_ic_stress_closure_call_4.lua",
    "baseline_jit_call_ic_stress_direct_call_1.lua",
    "baseline_jit_call_ic_stress_direct_call_2.lua",
    "baseline_jit_call_ic_stress_direct_call_3.lua",
    "baseline_jit_call_ic_stress_direct_call_4.lua",
    "base_loadfile_file.lua",
    "base_loadfile.lua",
    "base_loadfile_nonexistent.lua",
    "base_load.lua",
    "base_loadstring.lua",
    "binary-trees-1.lua",
    "binary-trees-2.lua",
    "boolean_as_table_index_1.lua",
    "boolean_as_table_index_2.lua",
    "boolean_as_table_index_3.lua",
    "bounce.lua",
    "cd.lua",
    "chameneos.lua",
    "comparison_one_side_constant.lua",
    "coroutine_1.lua",
    "coroutine_2.lua",
    "coroutine_3.lua",
    "coroutine_4.lua",
    "coroutine_5.lua",
    "coroutine_error_1.lua",
    "coroutine_error_2.lua",
    "coroutine_error_3.lua",
    "coroutine_ring.lua",
    "deltablue.lua",
    "fannkuch-redux.lua",
    "fasta.lua",
    "fib.lua",
    "fib_upvalue.lua",
    "fixpoint-fact.lua",
    "for_loop_coercion.lua",
    "for_loop_edge_cases.lua",
    "for_pairs_empty.lua",
    "for_pairs.lua",
    "for_pairs_next_but_not_nil.lua",
    "for_pairs_poison_next.lua",
    "for_pairs_poison_pairs.lua",
    "for_pairs_slow_next.lua",
    "getbyid_metatable.lua",
    "get_by_imm_interpreter_ic_1.lua",
    "get_by_imm_interpreter_ic_2.lua",
    "getbyintegerval_metatable.lua",
    "get_by_val_interpreter_ic_1.lua",
    "get_by_val_interpreter_ic_2.lua",
    "get_by_val_interpreter_ic_3.lua",
    "get_by_val_interpreter_ic_4.lua",
    "get_by_val_interpreter_ic_5.lua",
    "get_by_val_interpreter_ic_6.lua",
    "getbyval_metatable.lua",
    "getsetmetatable_2.lua",
    "get_set_metatable.lua",
    "globalget_interpreter_ic.lua",
    "globalget_metatable.lua",
    "globalput_interpreter_ic_1.lua",
    "globalput_interpreter_ic_2.lua",
    "globalput_interpreter_ic_3.lua",
    "globalput_interpreter_ic_4.lua",
    "globalput_metatable.lua",
    "havlak.lua",
    "heapsort.lua",
    "init_environment.lua",
    "interp_to_baseline_osr_entry_iterative_for_loop.lua",
    "interp_to_baseline_osr_entry_kv_loop_1.lua",
    "interp_to_baseline_osr_entry_kv_loop_2.lua",
    "interp_to_baseline_osr_entry_kv_loop_3.lua",
    "interp_to_baseline_osr_entry_kv_loop_4.lua",
    "interp_to_baseline_osr_entry_numeric_for_loop.lua",
    "interp_to_baseline_osr_entry_repeat_loop_1.lua",
    "interp_to_baseline_osr_entry_repeat_loop_2.lua",
    "interp_to_baseline_osr_entry_while_loop_1.lua",
    "interp_to_baseline_osr_entry_while_loop_2.lua",
    "interp_to_baseline_tier_up_1.lua",
    "interp_to_baseline_tier_up_2.lua",
    "interp_to_baseline_tier_up_3.lua",
    "isfc_conditional_copy.lua",
    "istc_conditional_copy.lua",
    "iter_for.lua",
    "json.lua",
    "k-nucleotide.lua",
    "length_operator.lua",
    "le_use_lt_metamethod.lua",
    "lib_base_assert.lua",
    "life.lua",
    "linear_sieve.lua",
    "list.lua",
    "logical_not.lua",
    "logical_op_sanity.lua",
    "mandel2.lua",
    "mandel.lua",
    "mandel-nometatable.lua",
    "math_constants.lua",
    "math_lib_min_max.lua",
    "math_lib_misc.lua",
    "math_lib_random.lua",
    "math_lib_unary.lua",
    "math_sqrt.lua",
    "metatable_add_1.lua",
    "metatable_add_2.lua",
    "metatable_add_3.lua",
    "metatable_call_1.lua",
    "metatable_call_2.lua",
    "metatable_call_3.lua",
    "metatable_call_4.lua",
    "metatable_call_5.lua",
    "metatable_concat_2.lua",
    "metatable_concat_3.lua",
    "metatable_concat.lua",
    "metatable_div.lua",
    "metatable_eq_1.lua",
    "metatable_eq_2.lua",
    "metatable_eq_3.lua",
    "metatable_le.lua",
    "metatable_len.lua",
    "metatable_lt.lua",
    "metatable_mod.lua",
    "metatable_mul.lua",
    "metatable_pow.lua",
    "metatable_sub.lua",
    "metatable_unm.lua",
    "nan_edge_case.lua",
    "n-body.lua",
    "negative_zero_as_index.lua",
    "nlgn_sieve.lua",
    "nsieve.lua",
    "partialsums.lua",
    "pcall_1.lua",
    "pcall_2.lua",
    "pcall_metatable.lua",
    "permute.lua",
    "pidigits-nogmp.lua",
    "pos_and_neg_inf.lua",
    "primitive_constant.lua",
    "putbyid_interpreter_ic_1.lua",
    "putbyid_interpreter_ic_2.lua",
    "putbyid_interpreter_ic_3.lua",
    "putbyid_interpreter_ic_4.lua",
    "putbyid_interpreter_ic_5.lua",
    "putbyid_interpreter_ic_6.lua",
    "putbyid_interpreter_ic_7.lua",
    "putbyid_interpreter_ic_8.lua",
    "putbyid_metatable.lua",
    "putbyimm_interpreter_ic_1.lua",
    "putbyimm_interpreter_ic_2.lua",
    "putbyimm_interpreter_ic_3.lua",
    "putbyimm_interpreter_ic_4.lua",
    "putbyintegerval_metatable.lua",
    "putbyval_interpreter_ic_1.lua",
    "putbyval_interpreter_ic_2.lua",
    "putbyval_interpreter_ic_3.lua",
    "putbyval_interpreter_ic_4.lua",
    "putbyval_interpreter_ic_5.lua",
    "putbyval_metatable.lua",
    "qt.lua",
    "quadtree2.lua",
    "queen.lua",
    "rawget_rawset.lua",
    "rawset_returns_original_table.lua",
    "ray2.lua",
    "ray.lua",
    "revcomp.lua",
    "richard.lua",
    "scimark_fft.lua",
    "scimark_lu.lua",
    "scimark_sor.lua",
    "scimark_sparse.lua",
    "series.lua",
    "spectral-norm.lua",
    "storage.lua",
    "string_concat.lua",
    "string_format.lua",
    "string_lib_byte_2.lua",
    "string_lib_byte.lua",
    "string_lib_char_2.lua",
    "string_lib_char.lua",
    "string_lib_len.lua",
    "string_lib_lower_upper_2.lua",
    "string_lib_lower_upper.lua",
    "string_lib_misc.lua",
    "string_lib_rep_2.lua",
    "string_lib_rep.lua",
    "string_lib_reverse.lua",
    "string_lib_sub_2.lua",
    "string_lib_sub.lua",
    "table_concat_overflow.lua",
    "table_dup2.lua",
    "table_dup3.lua",
    "table_dup.lua",
    "table_getbyid_interpreter_ic.lua",
    "table_lib_concat.lua",
    "table_size_hint.lua",
    "table_sort_1.lua",
    "table_sort_2.lua",
    "table_sort_3.lua",
    "table_sort_4.lua",
    "table_sort_cmp.lua",
    "table_sort.lua",
    "table_variadic_put_2.lua",
    "table_variadic_put.lua",
    "tail_call.lua",
    "test_knil.lua",
    "test_print.lua",
    "towers.lua",
    "upvalue.lua",
    "variadic_tail_call_1.lua",
    "variadic_tail_call_2.lua",
    "variadic_tail_call_3.lua",
    "xpcall_1.lua",
    "xpcall_2.lua",
    "xpcall_3.lua",
    "xpcall_4.lua",
    "xpcall_5.lua",
    "xpcall_6.lua",
    "xpcall_metatable.lua"
};

// Test DFG frontend without speculative inlining
// Note that this only tests that no internal asserts are fired and that the generated DFG IR pass validation
//
TEST(DfgFrontend, Parser_Stress_1)
{
    VM* vm = VM::Create();
    Auto(vm->Destroy());

    vm->SetEngineStartingTier(VM::EngineStartingTier::BaselineJIT);
    vm->SetEngineMaxTier(VM::EngineMaxTier::BaselineJIT);

    constexpr size_t numFiles = std::extent_v<decltype(x_lua_files_for_parser_test)>;

    for (size_t i = 0; i < numFiles; i++)
    {
        std::unique_ptr<ScriptModule> module = ParseLuaScriptOrFail(std::string("luatests/") + x_lua_files_for_parser_test[i], LuaTestOption::ForceBaselineJit);

        for (UnlinkedCodeBlock* ucb : module->m_unlinkedCodeBlocks)
        {
            CodeBlock* cb = ucb->m_defaultCodeBlock;
            ReleaseAssert(cb != nullptr);
            arena_unique_ptr<Graph> graph = RunDfgFrontend(cb);
            ReleaseAssert(ValidateDfgIrGraph(graph.get()));

            TempArenaAllocator alloc;
            std::ignore = RunPredictionPropagationWithoutValueProfile(alloc, graph.get());

            RunPhantomInsertionPass(graph.get());
            ReleaseAssert(ValidateDfgIrGraph(graph.get()));
        }
    }
}

// Test DFG frontend with speculative inlining.
// Call IC info are produced by actually running each test in baseline JIT.
// Note that this only tests that no internal asserts are fired and that the generated DFG IR pass validation
//
static void TestDfgFrontendWithRealCallIcInfo(std::string fileName, int randomlyMakeFnVariadicLevel)
{
    // Blacklist some files that take a relatively long time to run
    //
    constexpr const char* blacklist_files[] = {
        "havlak.lua",
        "putbyval_interpreter_ic_1.lua",
        "putbyval_interpreter_ic_2.lua",
        "putbyval_interpreter_ic_3.lua",
        "putbyval_interpreter_ic_4.lua",
        "putbyval_interpreter_ic_5.lua",
        "putbyimm_interpreter_ic_1.lua",
        "putbyimm_interpreter_ic_2.lua",
        "putbyimm_interpreter_ic_3.lua",
        "putbyimm_interpreter_ic_4.lua",
        "table_sort_1.lua",
        "table_sort_2.lua",
        "table_sort_3.lua",
        "table_sort_4.lua"
    };

    for (size_t idx = 0; idx < std::extent_v<decltype(blacklist_files)>; idx++)
    {
        if (fileName == blacklist_files[idx])
        {
            return;
        }
    }

    VM* vm = VM::Create();
    Auto(vm->Destroy());
    VMOutputInterceptor vmOutput(vm);

    vm->SetEngineStartingTier(VM::EngineStartingTier::BaselineJIT);
    vm->SetEngineMaxTier(VM::EngineMaxTier::BaselineJIT);

    DfgAlloc()->Reset();

    std::unique_ptr<ScriptModule> module = ParseLuaScriptOrFail(std::string("luatests/") + fileName, LuaTestOption::ForceBaselineJit);
    vm->LaunchScript(module.get());

    for (UnlinkedCodeBlock* ucb : module->m_unlinkedCodeBlocks)
    {
        CodeBlock* cb = ucb->m_defaultCodeBlock;
        ReleaseAssert(cb != nullptr);
        BaselineCodeBlock* bcb = cb->m_baselineCodeBlock;
        ReleaseAssert(bcb != nullptr);
        if ((randomlyMakeFnVariadicLevel == 1 && rand() % 2 == 0) || (randomlyMakeFnVariadicLevel == 2))
        {
            ucb->m_hasVariadicArguments = true;
            cb->m_hasVariadicArguments = true;
        }
        if (cb->m_hasVariadicArguments)
        {
            // TODO: currently we inject profiling info about the runtime-seen max number of variadic
            // args, since the implementation for this part is undone yet.
            //
            bcb->m_maxObservedNumVariadicArgs = static_cast<uint32_t>(rand()) % 10;
        }
    }
    for (UnlinkedCodeBlock* ucb : module->m_unlinkedCodeBlocks)
    {
        CodeBlock* cb = ucb->m_defaultCodeBlock;
        ReleaseAssert(cb != nullptr);
        arena_unique_ptr<Graph> graph = RunDfgFrontend(cb);
        if (!ValidateDfgIrGraph(graph.get()))
        {
            fprintf(stderr, "File %s failed test!\n", fileName.c_str());
            DumpDfgIrGraph(stderr, graph.get());
            ReleaseAssert(false);
        }
        RunPhantomInsertionPass(graph.get());
        ReleaseAssert(ValidateDfgIrGraph(graph.get()));
    }
}

TEST(DfgFrontend, Inlining_Stress_1)
{
    constexpr size_t numFiles = std::extent_v<decltype(x_lua_files_for_parser_test)>;
    for (size_t i = 0; i < numFiles; i++)
    {
        TestDfgFrontendWithRealCallIcInfo(x_lua_files_for_parser_test[i], 0 /*doNotRandomlyMakeFnVariadic*/);
    }
}

TEST(DfgFrontend, Inlining_Stress_2)
{
    constexpr size_t numFiles = std::extent_v<decltype(x_lua_files_for_parser_test)>;
    for (size_t i = 0; i < numFiles; i++)
    {
        TestDfgFrontendWithRealCallIcInfo(x_lua_files_for_parser_test[i], 1 /*randomlyMakeFnVariadic*/);
    }
}

TEST(DfgFrontend, Inlining_Stress_3)
{
    constexpr size_t numFiles = std::extent_v<decltype(x_lua_files_for_parser_test)>;
    for (size_t i = 0; i < numFiles; i++)
    {
        TestDfgFrontendWithRealCallIcInfo(x_lua_files_for_parser_test[i], 2 /*alwaysMakeFnVariadic*/);
    }
}

// Test DFG frontend with speculative inlining.
// Call IC info are "produced" by injecting random information.
// It also allows parsing multiple files, to add more entropy to the injected call IC info
// Note that this only tests that no internal asserts are fired and that the generated DFG IR pass validation
//
static void TestDfgFrontendWithRandomCallIcInfo(VM* vm, std::vector<std::string> fileNames, int randomlyMakeFnVariadicLevel)
{
    DfgAlloc()->Reset();

    std::vector<std::unique_ptr<ScriptModule>> allModules;
    for (const std::string& fileName : fileNames)
    {
        std::unique_ptr<ScriptModule> module = ParseLuaScriptOrFail(std::string("luatests/") + fileName, LuaTestOption::ForceBaselineJit);
        allModules.push_back(std::move(module));
    }

    std::vector<UnlinkedCodeBlock*> allUcbs;
    for (auto& module : allModules)
    {
        for (UnlinkedCodeBlock* ucb : module->m_unlinkedCodeBlocks)
        {
            allUcbs.push_back(ucb);
            CodeBlock* cb = ucb->m_defaultCodeBlock;
            ReleaseAssert(cb != nullptr);
            BaselineCodeBlock* bcb = cb->m_baselineCodeBlock;
            ReleaseAssert(bcb != nullptr);
            if ((randomlyMakeFnVariadicLevel == 1 && rand() % 2 == 0) || (randomlyMakeFnVariadicLevel == 2))
            {
                ucb->m_hasVariadicArguments = true;
                cb->m_hasVariadicArguments = true;
            }
            if (cb->m_hasVariadicArguments)
            {
                bcb->m_maxObservedNumVariadicArgs = static_cast<uint32_t>(rand()) % 10;
            }
        }
    }

    for (UnlinkedCodeBlock* ucb : allUcbs)
    {
        CodeBlock* cb = ucb->m_defaultCodeBlock;
        ReleaseAssert(cb != nullptr);
        BaselineCodeBlock* bcb = cb->m_baselineCodeBlock;
        ReleaseAssert(bcb != nullptr);
        if (cb->m_hasVariadicArguments)
        {
            bcb->m_maxObservedNumVariadicArgs = static_cast<uint32_t>(rand()) % 10;
        }
        const BytecodeSpeculativeInliningInfo* infoArray = SpeculativeInliner::GetBytecodeSpeculativeInliningInfoArray();
        DeegenBytecodeBuilder::BytecodeDecoder decoder(cb);
        for (size_t bcIndex = 0; bcIndex < bcb->m_numBytecodes; bcIndex++)
        {
            uint8_t* slowPathData = bcb->GetSlowPathDataAtBytecodeIndex(bcIndex);
            size_t bcOffset = bcb->GetBytecodeOffsetFromBytecodeIndex(bcIndex);
            size_t opcode = decoder.GetCanonicalizedOpcodeAtPosition(bcOffset);
            ReleaseAssert(opcode < decoder.GetTotalBytecodeKinds());
            ReleaseAssert(infoArray[opcode].m_isInitialized);
            if (infoArray[opcode].m_info == nullptr)
            {
                continue;
            }

            const BytecodeSpeculativeInliningInfo& siInfo = infoArray[opcode];
            ReleaseAssert(siInfo.m_isInitialized && siInfo.m_numCallSites > 0 && siInfo.m_info != nullptr);

            JitCallInlineCacheSite* icSiteList = reinterpret_cast<JitCallInlineCacheSite*>(slowPathData + siInfo.m_callIcOffsetInSlowPathData);
            ReleaseAssert(siInfo.m_numCallSites > 0);
            for (size_t siteOrd = 0; siteOrd < siInfo.m_numCallSites; siteOrd++)
            {
                ReleaseAssert(icSiteList[siteOrd].ObservedNoTarget());
            }

            size_t targetIcSiteOrd = static_cast<size_t>(rand()) % siInfo.m_numCallSites;
            JitCallInlineCacheSite* targetIcSite = icSiteList + targetIcSiteOrd;

            // For simplicity, always inject in closure call mode, since direct call requires a valid FunctionObject
            // Note that this also makes the IC code stubs and the linked list out of sync, but it's fine since we are never running the script
            //
            targetIcSite->m_mode = JitCallInlineCacheSite::Mode::ClosureCall;
            ReleaseAssert(targetIcSite->m_numEntries == 0);
            CodeBlock* targetCb = allUcbs[static_cast<size_t>(rand()) % allUcbs.size()]->m_defaultCodeBlock;
            TValue fakeFnObj = TValue::CreatePointer(FunctionObject::Create(vm, targetCb));
            std::ignore = targetIcSite->InsertInClosureCallMode(0 /*fakeDcIcTraitKind*/, fakeFnObj);

            ReleaseAssert(targetIcSite->m_numEntries == 1);
        }
    }

    for (UnlinkedCodeBlock* ucb : allUcbs)
    {
        CodeBlock* cb = ucb->m_defaultCodeBlock;
        ReleaseAssert(cb != nullptr);
        arena_unique_ptr<Graph> graph = RunDfgFrontend(cb);
        if (!ValidateDfgIrGraph(graph.get(), IRValidateOptions().SetAllowUnreachableBlocks()))
        {
            fprintf(stderr, "Test failed! File(s) = ");
            for (const std::string& fileName : fileNames)
            {
                fprintf(stderr, "%s ", fileName.c_str());
            }
            fprintf(stderr, "\n");
            ReleaseAssert(false);
        }
        RunPhantomInsertionPass(graph.get());
        ReleaseAssert(ValidateDfgIrGraph(graph.get()));
    }
}

static void TestDfgFrontendWithRandomCallIcInfoWithConfig(size_t desiredFileNamesSetSize, int randomlyMakeFnVariadicLevel)
{
    using namespace dfg;

    VM* vm = VM::Create();
    Auto(vm->Destroy());
    VMOutputInterceptor vmOutput(vm);

    vm->SetEngineStartingTier(VM::EngineStartingTier::BaselineJIT);
    vm->SetEngineMaxTier(VM::EngineMaxTier::BaselineJIT);

    constexpr size_t numFiles = std::extent_v<decltype(x_lua_files_for_parser_test)>;
    for (size_t i = 0; i < numFiles; i++)
    {
        std::unordered_set<std::string> fileNamesSet;
        fileNamesSet.insert(x_lua_files_for_parser_test[i]);
        while (fileNamesSet.size() < desiredFileNamesSetSize)
        {
            size_t k = static_cast<size_t>(rand()) % numFiles;
            if (!fileNamesSet.count(x_lua_files_for_parser_test[k]))
            {
                fileNamesSet.insert(x_lua_files_for_parser_test[k]);
            }
        }
        std::vector<std::string> fileNames;
        for (const std::string& file : fileNamesSet)
        {
            fileNames.push_back(file);
        }
        TestDfgFrontendWithRandomCallIcInfo(vm, fileNames, randomlyMakeFnVariadicLevel);
    }
}

// Test DFG frontend with speculative inlining.
// Call IC info are "produced" by injecting random information.
// Note that this only tests that no internal asserts are fired and that the generated DFG IR pass validation
//
TEST(DfgFrontend, Inlining_Stress_4)
{
    TestDfgFrontendWithRandomCallIcInfoWithConfig(1 /*fileNamesSetSize*/, 0 /*randomlyMakeFnVariadicLevel*/);
}

TEST(DfgFrontend, Inlining_Stress_5)
{
    TestDfgFrontendWithRandomCallIcInfoWithConfig(1 /*fileNamesSetSize*/, 1 /*randomlyMakeFnVariadicLevel*/);
}

TEST(DfgFrontend, Inlining_Stress_6)
{
    TestDfgFrontendWithRandomCallIcInfoWithConfig(1 /*fileNamesSetSize*/, 2 /*randomlyMakeFnVariadicLevel*/);
}

TEST(DfgFrontend, Inlining_Stress_7)
{
    TestDfgFrontendWithRandomCallIcInfoWithConfig(3 /*fileNamesSetSize*/, 0 /*randomlyMakeFnVariadicLevel*/);
}

TEST(DfgFrontend, Inlining_Stress_8)
{
    TestDfgFrontendWithRandomCallIcInfoWithConfig(3 /*fileNamesSetSize*/, 1 /*randomlyMakeFnVariadicLevel*/);
}

TEST(DfgFrontend, Inlining_Stress_9)
{
    TestDfgFrontendWithRandomCallIcInfoWithConfig(3 /*fileNamesSetSize*/, 2 /*randomlyMakeFnVariadicLevel*/);
}

TEST(DfgFrontend, SpeculativelyInlineADeadLoop)
{
    VM* vm = VM::Create();
    Auto(vm->Destroy());
    VMOutputInterceptor vmOutput(vm);

    vm->SetEngineStartingTier(VM::EngineStartingTier::BaselineJIT);
    vm->SetEngineMaxTier(VM::EngineMaxTier::BaselineJIT);

    DfgAlloc()->Reset();

    std::unique_ptr<ScriptModule> module = ParseLuaScriptOrFail(std::string("luatests/speculatively_inline_dead_loop.lua"), LuaTestOption::ForceBaselineJit);
    vm->LaunchScript(module.get());

    UnlinkedCodeBlock* targetUcb = nullptr;
    for (UnlinkedCodeBlock* ucb : module->m_unlinkedCodeBlocks)
    {
        if (ucb->m_numFixedArguments == 2)
        {
            ReleaseAssert(targetUcb == nullptr);
            targetUcb = ucb;
        }
    }
    ReleaseAssert(targetUcb != nullptr);
    CodeBlock* cb = targetUcb->m_defaultCodeBlock;
    ReleaseAssert(cb != nullptr);
    arena_unique_ptr<Graph> graph = RunDfgFrontend(cb);
    ReleaseAssert(ValidateDfgIrGraph(graph.get()));

    // We should see 2 basic blocks where BB0->BB1 and BB1->BB1
    //
    ReleaseAssert(graph->m_blocks.size() == 2);
    ReleaseAssert(graph->m_blocks[0]->GetNumSuccessors() == 1);
    ReleaseAssert(graph->m_blocks[0]->GetSuccessor(0) == graph->m_blocks[1]);
    ReleaseAssert(graph->m_blocks[1]->GetNumSuccessors() == 1);
    ReleaseAssert(graph->m_blocks[1]->GetSuccessor(0) == graph->m_blocks[1]);

    // In the entry block, we should see exactly 2 SetLocals
    //
    size_t numSetLocals = 0;
    for (Node* node : graph->GetEntryBB()->m_nodes)
    {
        if (node->IsSetLocalNode())
        {
            numSetLocals++;
        }
    }
    ReleaseAssert(numSetLocals == 2);
}

TEST(DfgFrontend, Dump_1)
{
    VM* vm = VM::Create();
    Auto(vm->Destroy());

    vm->SetEngineStartingTier(VM::EngineStartingTier::BaselineJIT);
    vm->SetEngineMaxTier(VM::EngineMaxTier::BaselineJIT);

    std::unique_ptr<ScriptModule> module = ParseLuaScriptOrFail("luatests/opt_jit_frontend_capture_analysis_1.lua", LuaTestOption::ForceBaselineJit);

    UnlinkedCodeBlock* ucb = module->m_unlinkedCodeBlocks.back();
    CodeBlock* cb = ucb->m_defaultCodeBlock;

    arena_unique_ptr<Graph> graph = RunDfgFrontend(cb);
    DumpDfgIrGraph(stdout, graph.get());
}

TEST(DfgUtils, BatchedInsertion)
{
    VM* vm = VM::Create();
    Auto(vm->Destroy());

    DfgAlloc()->Reset();

    size_t numElements = 100;
    std::vector<Node*> nodeForOriginalVector;
    for (size_t i = 0; i < numElements; i++)
    {
        nodeForOriginalVector.push_back(Node::CreateNoopNode());
    }

    size_t maxInsertions = 1000;
    std::vector<Node*> nodeForInsertion;
    for (size_t i = 0; i < maxInsertions; i++)
    {
        nodeForInsertion.push_back(Node::CreateNoopNode());
    }

    auto doTest = [&](size_t numInsertions, size_t randMode, bool ordered)
    {
        ReleaseAssert(numInsertions <= maxInsertions);

        BasicBlock* bb = DfgAlloc()->AllocateObject<BasicBlock>();
        for (size_t i = 0; i < numElements; i++)
        {
            bb->m_nodes.push_back(nodeForOriginalVector[i]);
        }

        std::vector<std::pair<size_t, Node*>> insertions;
        for (size_t i = 0; i < numInsertions; i++)
        {
            size_t insertBefore;
            if (randMode == 0)
            {
                insertBefore = static_cast<size_t>(rand()) % (numElements + 1);
            }
            else if (randMode == 1)
            {
                insertBefore = static_cast<size_t>(rand()) % (numElements / 10);
            }
            else if (randMode == 2)
            {
                insertBefore = static_cast<size_t>(rand()) % (numElements / 10) + (numElements + 1 - numElements / 10);
            }
            else if (randMode == 3)
            {
                insertBefore = static_cast<size_t>(rand()) % (numElements / 3) + (numElements / 3);
            }
            else
            {
                insertBefore = static_cast<size_t>(rand()) % (numElements / 10 + 1) * 10;
            }
            ReleaseAssert(insertBefore <= numElements);
            insertions.push_back(std::make_pair(insertBefore, nodeForInsertion[i]));
        }

        if (ordered)
        {
            std::sort(insertions.begin(), insertions.end());
        }

        std::vector<std::vector<Node*>> expectedInsertions;
        expectedInsertions.resize(numElements + 1);
        for (auto& it : insertions)
        {
            size_t insertBefore = it.first;
            Node* node = it.second;
            ReleaseAssert(insertBefore <= numElements);
            expectedInsertions[insertBefore].push_back(node);
        }

        std::vector<Node*> expectedResult;
        for (size_t i = 0; i <= numElements; i++)
        {
            for (Node* node : expectedInsertions[i])
            {
                expectedResult.push_back(node);
            }
            if (i < numElements)
            {
                expectedResult.push_back(bb->m_nodes[i]);
            }
        }
        ReleaseAssert(expectedResult.size() == numElements + numInsertions);

        TempArenaAllocator alloc;
        BatchedInsertions inserter(alloc);
        inserter.Reset(bb);
        for (auto& it : insertions)
        {
            size_t insertBefore = it.first;
            Node* node = it.second;
            inserter.Add(insertBefore, node);
        }
        inserter.Commit();

        ReleaseAssert(bb->m_nodes.size() == numElements + numInsertions);
        for (size_t i = 0; i < bb->m_nodes.size(); i++)
        {
            ReleaseAssert(bb->m_nodes[i] == expectedResult[i]);
        }
    };

    for (size_t testIter = 0; testIter < 40; testIter++)
    {
        for (size_t numInsertionsMode = 0; numInsertionsMode < 5; numInsertionsMode++)
        {
            size_t numInsertions;
            if (numInsertionsMode == 0)
            {
                numInsertions = static_cast<size_t>(rand()) % 15;
            }
            else if (numInsertionsMode == 1)
            {
                numInsertions = static_cast<size_t>(rand()) % 80;
            }
            else if (numInsertionsMode == 2)
            {
                numInsertions = static_cast<size_t>(rand()) % 100 + 50;
            }
            else if (numInsertionsMode == 3)
            {
                numInsertions = static_cast<size_t>(rand()) % 200 + 200;
            }
            else
            {
                numInsertions = static_cast<size_t>(rand()) % 500 + 500;
            }
            for (size_t randMode = 0; randMode < 5; randMode++)
            {
                for (bool ordered : {false, true})
                {
                    doTest(numInsertions, randMode, ordered);
                }
            }
        }
    }
}
