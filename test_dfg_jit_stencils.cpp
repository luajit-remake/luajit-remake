#include "gtest/gtest.h"

#include "deegen_dfg_jit_impl_creator.h"
#include "json_utils.h"
#include "json_parse_dump.h"
#include "read_file.h"
#include "misc_llvm_helper.h"
#include "vm.h"
#include "deegen_jit_codegen_logic_creator.h"
#include "deegen_dfg_builtin_nodes.h"

using namespace llvm;
using namespace dast;

TEST(DfgBuiltinNode, Constant)
{
#if 0
    std::unique_ptr<LLVMContext> ctxHolder(new LLVMContext);
    LLVMContext& ctx = *ctxHolder.get();

    {
        DfgBuiltinNodeImplReturn_MoveVariadicRes impl(ctx);
        fprintf(stderr, "%s\n", impl.m_regAllocAuditLog.c_str());
        fprintf(stderr, "%s\n", impl.m_jitCodeAuditLog.c_str());
    }

    {
        DfgBuiltinNodeImplCreateFunctionObject_BoxFunctionObject impl(ctx);
        fprintf(stderr, "%s\n", impl.m_regAllocAuditLog.c_str());
        fprintf(stderr, "%s\n", impl.m_jitCodeAuditLog.c_str());
    }
    {
        DfgBuiltinNodeImplGetImmutableUpvalue impl(ctx);
        fprintf(stderr, "%s\n", impl.m_regAllocAuditLog.c_str());
        fprintf(stderr, "%s\n", impl.m_jitCodeAuditLog.c_str());
    }
    {
        DfgBuiltinNodeImplGetMutableUpvalue impl(ctx);
        fprintf(stderr, "%s\n", impl.m_regAllocAuditLog.c_str());
        fprintf(stderr, "%s\n", impl.m_jitCodeAuditLog.c_str());
    }
    {
        DfgBuiltinNodeImplSetUpvalue impl(ctx);
        fprintf(stderr, "%s\n", impl.m_regAllocAuditLog.c_str());
        fprintf(stderr, "%s\n", impl.m_jitCodeAuditLog.c_str());
    }
    {
        DfgBuiltinNodeImplReturn_MoveVariadicRes impl(ctx);
        fprintf(stderr, "%s\n", impl.m_regAllocAuditLog.c_str());
        fprintf(stderr, "%s\n", impl.m_jitCodeAuditLog.c_str());
    }
    {
        DfgBuiltinNodeImplReturn_RetWithVariadicRes impl(ctx);
        fprintf(stderr, "%s\n", impl.m_regAllocAuditLog.c_str());
        fprintf(stderr, "%s\n", impl.m_jitCodeAuditLog.c_str());
    }
    {
        DfgBuiltinNodeImplReturn_WriteNil impl(ctx);
        fprintf(stderr, "%s\n", impl.m_regAllocAuditLog.c_str());
        fprintf(stderr, "%s\n", impl.m_jitCodeAuditLog.c_str());
    }
    {
        DfgBuiltinNodeImplReturn_RetNoVariadicRes impl(ctx);
        fprintf(stderr, "%s\n", impl.m_regAllocAuditLog.c_str());
        fprintf(stderr, "%s\n", impl.m_jitCodeAuditLog.c_str());
    }
    {
        DfgBuiltinNodeImplReturn_Ret1 impl(ctx);
        fprintf(stderr, "%s\n", impl.m_regAllocAuditLog.c_str());
        fprintf(stderr, "%s\n", impl.m_jitCodeAuditLog.c_str());
    }
    {
        DfgBuiltinNodeImplReturn_Ret0 impl(ctx);
        fprintf(stderr, "%s\n", impl.m_regAllocAuditLog.c_str());
        fprintf(stderr, "%s\n", impl.m_jitCodeAuditLog.c_str());
    }
#endif
#if 0
    {
        DfgBuiltinNodeImplConstant impl(ctx);
        fprintf(stderr, "%s\n", impl.m_regAllocAuditLog.c_str());
        fprintf(stderr, "%s\n", impl.m_jitCodeAuditLog.c_str());
    }
    {
        DfgBuiltinNodeImplUnboxedConstant impl(ctx);
        fprintf(stderr, "%s\n", impl.m_regAllocAuditLog.c_str());
        fprintf(stderr, "%s\n", impl.m_jitCodeAuditLog.c_str());
    }
    {
        DfgBuiltinNodeImplArgument impl(ctx);
        fprintf(stderr, "%s\n", impl.m_regAllocAuditLog.c_str());
        fprintf(stderr, "%s\n", impl.m_jitCodeAuditLog.c_str());
    }
    {
        DfgBuiltinNodeImplGetNumVariadicArgs impl(ctx);
        fprintf(stderr, "%s\n", impl.m_regAllocAuditLog.c_str());
        fprintf(stderr, "%s\n", impl.m_jitCodeAuditLog.c_str());
    }
    {
        DfgBuiltinNodeImplGetKthVariadicArg impl(ctx);
        fprintf(stderr, "%s\n", impl.m_regAllocAuditLog.c_str());
        fprintf(stderr, "%s\n", impl.m_jitCodeAuditLog.c_str());
    }
    {
        DfgBuiltinNodeImplGetFunctionObject impl(ctx);
        fprintf(stderr, "%s\n", impl.m_regAllocAuditLog.c_str());
        fprintf(stderr, "%s\n", impl.m_jitCodeAuditLog.c_str());
    }
    {
        DfgBuiltinNodeImplGetLocal impl(ctx);
        fprintf(stderr, "%s\n", impl.m_regAllocAuditLog.c_str());
        fprintf(stderr, "%s\n", impl.m_jitCodeAuditLog.c_str());
    }
    {
        DfgBuiltinNodeImplSetLocal impl(ctx);
        fprintf(stderr, "%s\n", impl.m_regAllocAuditLog.c_str());
        fprintf(stderr, "%s\n", impl.m_jitCodeAuditLog.c_str());
    }
    {
        DfgBuiltinNodeImplCreateCapturedVar impl(ctx);
        fprintf(stderr, "%s\n", impl.m_regAllocAuditLog.c_str());
        fprintf(stderr, "%s\n", impl.m_jitCodeAuditLog.c_str());
    }
    {
        DfgBuiltinNodeImplGetCapturedVar impl(ctx);
        fprintf(stderr, "%s\n", impl.m_regAllocAuditLog.c_str());
        fprintf(stderr, "%s\n", impl.m_jitCodeAuditLog.c_str());
    }
    {
        DfgBuiltinNodeImplSetCapturedVar impl(ctx);
        fprintf(stderr, "%s\n", impl.m_regAllocAuditLog.c_str());
        fprintf(stderr, "%s\n", impl.m_jitCodeAuditLog.c_str());
    }
    {
        DfgBuiltinNodeImplGetKthVariadicRes impl(ctx);
        fprintf(stderr, "%s\n", impl.m_regAllocAuditLog.c_str());
        fprintf(stderr, "%s\n", impl.m_jitCodeAuditLog.c_str());
    }
    {
        DfgBuiltinNodeImplCreateVariadicRes_StoreInfo impl(ctx);
        fprintf(stderr, "%s\n", impl.m_regAllocAuditLog.c_str());
        fprintf(stderr, "%s\n", impl.m_jitCodeAuditLog.c_str());
    }
    {
        DfgBuiltinNodeImplPrependVariadicRes_MoveAndStoreInfo impl(ctx);
        fprintf(stderr, "%s\n", impl.m_regAllocAuditLog.c_str());
        fprintf(stderr, "%s\n", impl.m_jitCodeAuditLog.c_str());
    }
    {
        DfgBuiltinNodeImplCheckU64InBound impl(ctx);
        fprintf(stderr, "%s\n", impl.m_regAllocAuditLog.c_str());
        fprintf(stderr, "%s\n", impl.m_jitCodeAuditLog.c_str());
    }
    {
        DfgBuiltinNodeImplI64SubSaturateToZero impl(ctx);
        fprintf(stderr, "%s\n", impl.m_regAllocAuditLog.c_str());
        fprintf(stderr, "%s\n", impl.m_jitCodeAuditLog.c_str());
    }
    {
        DfgBuiltinNodeImplCreateFunctionObject_AllocAndSetup impl(ctx);
        fprintf(stderr, "%s\n", impl.m_regAllocAuditLog.c_str());
        fprintf(stderr, "%s\n", impl.m_jitCodeAuditLog.c_str());
    }
#endif
}

#if 0
TEST(DfgJitStencil, Test)
{
    json_t bytecodeTraitTableJson = ParseJsonFromFileName("bytecode_opcode_trait_table.json");
    DeegenGlobalBytecodeTraitAccessor gbta = DeegenGlobalBytecodeTraitAccessor::LoadFromJson(bytecodeTraitTableJson);
    json_t inputJson;
    {
        AutoTimer t;
        inputJson = ParseJsonFromFileName("__generated__/release/generated/bytecode_info.interpreter.arithmetic_bytecodes.cpp.json");
    }

    std::unique_ptr<LLVMContext> ctxHolder(new LLVMContext);
    LLVMContext& ctx = *ctxHolder.get();

    ReleaseAssert(inputJson.count("all-dfg-variant-info"));
    json_t& bytecodeInfoListJson = inputJson["all-dfg-variant-info"];
    ReleaseAssert(bytecodeInfoListJson.is_array());

    json_t& bytecodeInfoJson = bytecodeInfoListJson[0]["dfg_variants"][1];
    BytecodeIrInfo bii(ctx, bytecodeInfoJson);

    // bii.m_jitMainComponent->m_module->dump();

    {
        AutoTimer t;
        DfgJitImplCreator* j = new DfgJitImplCreator(*bii.m_jitMainComponent.get(), &bii, nullptr /*gbta*/);

        std::vector<DfgJitImplCreator::RAVariant*> variants;
        bool success = j->TryGenerateAllRegAllocVariants(variants /*out*/);
        ReleaseAssert(success);

        std::vector<std::unique_ptr<DfgJitImplCreator::RASubVariant>> r = variants[0]->GenerateSubVariants(j);

        JitCodeGenLogicInfo cgi;
        cgi.SetBII(&bii);

        cgi.GenerateLogic(r[2]->m_impl.get());

        cgi.m_cgMod->dump();
    }


}

TEST(DfgJitStencil, Test2)
{
    json_t bytecodeTraitTableJson = ParseJsonFromFileName("bytecode_opcode_trait_table.json");
    DeegenGlobalBytecodeTraitAccessor gbta = DeegenGlobalBytecodeTraitAccessor::LoadFromJson(bytecodeTraitTableJson);
    json_t inputJson = ParseJsonFromFileName("bytecode_info.interpreter.table_dup.cpp.json");

    std::unique_ptr<LLVMContext> ctxHolder(new LLVMContext);
    LLVMContext& ctx = *ctxHolder.get();

    ReleaseAssert(inputJson.count("all-bytecode-info"));
    json_t& bytecodeInfoListJson = inputJson["all-bytecode-info"];
    ReleaseAssert(bytecodeInfoListJson.is_array());

    json_t& bytecodeInfoJson = bytecodeInfoListJson[1];
    BytecodeIrInfo bii(ctx, bytecodeInfoJson);

          // bii.m_jitMainComponent->m_module->dump();

    {
        AutoTimer t;
        DfgJitImplCreator* j = new DfgJitImplCreator(*bii.m_jitMainComponent.get(), &bii, &gbta);
        j->SetIsFastPathRegAllocAlwaysDisabled(false);
        j->SetNumGprPassthru(1);
        j->SetNumFprPassthru(6);
        j->SetNumGprPassthruGroup1(0);
        j->SetOperandRegAllocKind(j->GetBytecodeDef()->m_list[0].get(), DfgJitImplCreator::RegAllocKind::GPR);
        j->SetOperandRegAllocSubKind(j->GetBytecodeDef()->m_list[0].get(), DfgJitImplCreator::RegAllocSubKind::Group1);
        j->GetOutputRegAllocInfo().m_kind = DfgJitImplCreator::OutputRegAllocKind::GPR;
        j->GetOutputRegAllocInfo().m_subKind = DfgJitImplCreator::RegAllocSubKind::Group1;
        j->SetEnableRegAlloc();
        j->ComputeDfgJitSlowPathDataLayout();
        j->DoIrLowering(false, 0);

        j->GetModule()->dump();
    }


}


TEST(DfgJitStencil, Test3)
{
    json_t bytecodeTraitTableJson = ParseJsonFromFileName("bytecode_opcode_trait_table.json");
    DeegenGlobalBytecodeTraitAccessor gbta = DeegenGlobalBytecodeTraitAccessor::LoadFromJson(bytecodeTraitTableJson);
    json_t inputJson = ParseJsonFromFileName("bytecode_info.interpreter.table_get_by_id.cpp.json");

    std::unique_ptr<LLVMContext> ctxHolder(new LLVMContext);
    LLVMContext& ctx = *ctxHolder.get();

    ReleaseAssert(inputJson.count("all-bytecode-info"));
    json_t& bytecodeInfoListJson = inputJson["all-bytecode-info"];
    ReleaseAssert(bytecodeInfoListJson.is_array());

    json_t& bytecodeInfoJson = bytecodeInfoListJson[0];
    BytecodeIrInfo bii(ctx, bytecodeInfoJson);

          // bii.m_jitMainComponent->m_module->dump();

    {
        AutoTimer t;
        DfgJitImplCreator* j = new DfgJitImplCreator(*bii.m_jitMainComponent.get(), &bii, &gbta);
        j->SetIsFastPathRegAllocAlwaysDisabled(false);
        j->SetNumGprPassthru(1);
        j->SetNumFprPassthru(6);
        j->SetNumGprPassthruGroup1(1);
        j->SetOperandRegAllocKind(j->GetBytecodeDef()->m_list[0].get(), DfgJitImplCreator::RegAllocKind::GPR);
        j->SetOperandRegAllocSubKind(j->GetBytecodeDef()->m_list[0].get(), DfgJitImplCreator::RegAllocSubKind::Group2);
        j->SetOperandRegAllocKind(j->GetBytecodeDef()->m_list[1].get(), DfgJitImplCreator::RegAllocKind::GPR);
        j->SetOperandRegAllocSubKind(j->GetBytecodeDef()->m_list[1].get(), DfgJitImplCreator::RegAllocSubKind::Group2);
        j->GetOutputRegAllocInfo().m_kind = DfgJitImplCreator::OutputRegAllocKind::GPR;
        j->GetOutputRegAllocInfo().m_subKind = DfgJitImplCreator::RegAllocSubKind::Group1;
        j->SetEnableRegAlloc();
        j->ComputeDfgJitSlowPathDataLayout();
        j->DoIrLowering(false, 0);

        //j->GetModule()->dump();

        fprintf(stderr, "%s\n", j->GetGenericIcLoweringResult().m_disasmForAudit.c_str());
        fprintf(stderr, "%s\n", j->GetStencilPostTransformAsmFile().c_str());
    }


}


TEST(DfgJitStencil, Test4)
{
    json_t bytecodeTraitTableJson = ParseJsonFromFileName("bytecode_opcode_trait_table.json");
    DeegenGlobalBytecodeTraitAccessor gbta = DeegenGlobalBytecodeTraitAccessor::LoadFromJson(bytecodeTraitTableJson);
    json_t inputJson = ParseJsonFromFileName("bytecode_info.interpreter.table_put_by_val.cpp.json");

    std::unique_ptr<LLVMContext> ctxHolder(new LLVMContext);
    LLVMContext& ctx = *ctxHolder.get();

    ReleaseAssert(inputJson.count("all-bytecode-info"));
    json_t& bytecodeInfoListJson = inputJson["all-bytecode-info"];
    ReleaseAssert(bytecodeInfoListJson.is_array());

    json_t& bytecodeInfoJson = bytecodeInfoListJson[0];
    BytecodeIrInfo bii(ctx, bytecodeInfoJson);

          // bii.m_jitMainComponent->m_module->dump();

    {
        AutoTimer t;
        DfgJitImplCreator* j = new DfgJitImplCreator(*bii.m_jitMainComponent.get(), &bii, &gbta);
        j->SetIsFastPathRegAllocAlwaysDisabled(false);
        j->SetNumGprPassthru(2);
        j->SetNumFprPassthru(5);
        j->SetNumGprPassthruGroup1(2);
        j->SetOperandRegAllocKind(j->GetBytecodeDef()->m_list[0].get(), DfgJitImplCreator::RegAllocKind::GPR);
        j->SetOperandRegAllocSubKind(j->GetBytecodeDef()->m_list[0].get(), DfgJitImplCreator::RegAllocSubKind::Group2);
        j->SetOperandRegAllocKind(j->GetBytecodeDef()->m_list[1].get(), DfgJitImplCreator::RegAllocKind::FPR);
        j->SetOperandRegAllocKind(j->GetBytecodeDef()->m_list[2].get(), DfgJitImplCreator::RegAllocKind::GPR);
        j->SetOperandRegAllocSubKind(j->GetBytecodeDef()->m_list[2].get(), DfgJitImplCreator::RegAllocSubKind::Group2);
        j->SetEnableRegAlloc();
        j->ComputeDfgJitSlowPathDataLayout();
        j->DoIrLowering(false, 0);

        //j->GetModule()->dump();

        fprintf(stderr, "%s\n", j->GetGenericIcLoweringResult().m_disasmForAudit.c_str());
        fprintf(stderr, "%s\n", j->GetStencilPostTransformAsmFile().c_str());
        //j->GetGenericIcLoweringResult().m_icBodyModule->dump();
    }


}


TEST(DfgJitStencil, Test6)
{
    const char* files[] = {
                           "__generated__/release/generated/bytecode_info.interpreter.arithmetic_bytecodes.cpp.json",
                           "__generated__/release/generated/bytecode_info.interpreter.bytecode_mov.cpp.json",
                           "__generated__/release/generated/bytecode_info.interpreter.call_bytecodes.cpp.json",
                           "__generated__/release/generated/bytecode_info.interpreter.comparison_bytecodes.cpp.json",
                           "__generated__/release/generated/bytecode_info.interpreter.concat.cpp.json",
                           "__generated__/release/generated/bytecode_info.interpreter.equality_bytecodes.cpp.json",
                           "__generated__/release/generated/bytecode_info.interpreter.global_get.cpp.json",
                           "__generated__/release/generated/bytecode_info.interpreter.global_put.cpp.json",
                           "__generated__/release/generated/bytecode_info.interpreter.iterative_for_loop.cpp.json",
                           "__generated__/release/generated/bytecode_info.interpreter.iterative_for_loop_kv.cpp.json",
                           "__generated__/release/generated/bytecode_info.interpreter.length_operator.cpp.json",
                           "__generated__/release/generated/bytecode_info.interpreter.logical_not.cpp.json",
                           "__generated__/release/generated/bytecode_info.interpreter.new_closure.cpp.json",
                           "__generated__/release/generated/bytecode_info.interpreter.numeric_for_loop.cpp.json",
                           "__generated__/release/generated/bytecode_info.interpreter.range_fill_nils.cpp.json",
                           "__generated__/release/generated/bytecode_info.interpreter.return_bytecodes.cpp.json",
                           "__generated__/release/generated/bytecode_info.interpreter.set_constant_value.cpp.json",
                           "__generated__/release/generated/bytecode_info.interpreter.table_dup.cpp.json",
                           "__generated__/release/generated/bytecode_info.interpreter.table_get_by_id.cpp.json",
                           "__generated__/release/generated/bytecode_info.interpreter.table_get_by_imm.cpp.json",
                           "__generated__/release/generated/bytecode_info.interpreter.table_get_by_val.cpp.json",
                           "__generated__/release/generated/bytecode_info.interpreter.table_new.cpp.json",
                           "__generated__/release/generated/bytecode_info.interpreter.table_put_by_id.cpp.json",
                           "__generated__/release/generated/bytecode_info.interpreter.table_put_by_imm.cpp.json",
                           "__generated__/release/generated/bytecode_info.interpreter.table_put_by_val.cpp.json",
                           "__generated__/release/generated/bytecode_info.interpreter.table_variadic_put_by_seq.cpp.json",
                           "__generated__/release/generated/bytecode_info.interpreter.tail_call_bytecodes.cpp.json",
                           "__generated__/release/generated/bytecode_info.interpreter.test_and_branch_bytecodes.cpp.json",
                           "__generated__/release/generated/bytecode_info.interpreter.unary_minus.cpp.json",
                           "__generated__/release/generated/bytecode_info.interpreter.unconditional_branch.cpp.json",
                           "__generated__/release/generated/bytecode_info.interpreter.upvalue_bytecodes.cpp.json",
                           "__generated__/release/generated/bytecode_info.interpreter.variadic_args_accessor.cpp.json"
    };

    json_t bytecodeTraitTableJson = ParseJsonFromFileName("bytecode_opcode_trait_table.json");
    DeegenGlobalBytecodeTraitAccessor gbta = DeegenGlobalBytecodeTraitAccessor::LoadFromJson(bytecodeTraitTableJson);

    std::unique_ptr<LLVMContext> ctxHolder(new LLVMContext);
    LLVMContext& ctx = *ctxHolder.get();


    size_t totalSv = 0;

    for (const char* file : files)
    {
        json_t inputJson = ParseJsonFromFileName(file);
        ReleaseAssert(inputJson.count("all-dfg-variant-info"));
        json_t& bytecodeInfoListJson = inputJson["all-dfg-variant-info"];
        ReleaseAssert(bytecodeInfoListJson.is_array());

        for (json_t& bcInfo : bytecodeInfoListJson)
        {
            ReleaseAssert(bcInfo.is_object());
            ReleaseAssert(bcInfo.count("dfg_variants"));
            json_t& dfgVariantList = bcInfo["dfg_variants"];

            for (json_t& bytecodeInfoJson : dfgVariantList)
            {
                BytecodeIrInfo bii(ctx, bytecodeInfoJson);

                DfgJitImplCreator* j = new DfgJitImplCreator(*bii.m_jitMainComponent.get(), &bii, nullptr);
                fprintf(stderr, "%s\n", j->GetResultFunctionName().c_str());

                std::vector<DfgJitImplCreator::RAVariant*> variants;
                bool success = j->TryGenerateAllRegAllocVariants(variants /*out*/);

                fprintf(stderr, "success = %s, log:\n%s\n", (success ? "true" : "false"), j->GetRegAllocInfoAuditLog().c_str());

                if (success)
                {
                    for (DfgJitImplCreator::RAVariant* variant : variants)
                    {
                        size_t numSV = 0;
                        std::vector<std::unique_ptr<DfgJitImplCreator::RASubVariant>> r = variant->GenerateSubVariants(j);
                        for (auto& it : r)
                        {
                            if (it.get() != nullptr)
                            {
                                numSV++;
                                JitCodeGenLogicInfo cgi;
                                cgi.SetBII(&bii);
                                cgi.GenerateLogic(it->m_impl.get());
                            }
                        }
                        ReleaseAssert(numSV > 0);
                        fprintf(stderr, "Variant %s: numSubVariants = %d\n",
                                variant->GetVariantRegConfigDescForAudit().c_str(), static_cast<int>(numSV));
                        totalSv += numSV;
                    }
                }
                else
                {
                    JitCodeGenLogicInfo cgi;
                    cgi.SetBII(&bii);
                    cgi.GenerateLogic(j);
                }
            }
        }
    }

    fprintf(stderr, "%d\n", static_cast<int>(totalSv));
}

TEST(DfgJitStencil, Test7)
{
    json_t bytecodeTraitTableJson = ParseJsonFromFileName("bytecode_opcode_trait_table.json");
    DeegenGlobalBytecodeTraitAccessor gbta = DeegenGlobalBytecodeTraitAccessor::LoadFromJson(bytecodeTraitTableJson);
    json_t inputJson = ParseJsonFromFileName("bytecode_info.interpreter.table_put_by_val.cpp.json");

    std::unique_ptr<LLVMContext> ctxHolder(new LLVMContext);
    LLVMContext& ctx = *ctxHolder.get();

    ReleaseAssert(inputJson.count("all-bytecode-info"));
    json_t& bytecodeInfoListJson = inputJson["all-bytecode-info"];
    ReleaseAssert(bytecodeInfoListJson.is_array());

    json_t& bytecodeInfoJson = bytecodeInfoListJson[0];
    BytecodeIrInfo bii(ctx, bytecodeInfoJson);

          // bii.m_jitMainComponent->m_module->dump();

    {
        AutoTimer t;
        DfgJitImplCreator* j = new DfgJitImplCreator(*bii.m_jitMainComponent.get(), &bii, &gbta);
        j->SetIsFastPathRegAllocAlwaysDisabled(false);
        j->SetOperandRegAllocKind(j->GetBytecodeDef()->m_list[0].get(), DfgJitImplCreator::RegAllocKind::GPR);
        j->SetOperandRegAllocKind(j->GetBytecodeDef()->m_list[1].get(), DfgJitImplCreator::RegAllocKind::FPR);
        j->SetOperandRegAllocKind(j->GetBytecodeDef()->m_list[2].get(), DfgJitImplCreator::RegAllocKind::GPR);
        j->DetermineRegisterDemandInfo();
    }


}
#endif
