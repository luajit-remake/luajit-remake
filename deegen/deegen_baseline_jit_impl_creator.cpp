#include "deegen_baseline_jit_impl_creator.h"
#include "deegen_ast_inline_cache.h"
#include "deegen_interpreter_function_interface.h"
#include "deegen_ast_slow_path.h"
#include "deegen_jit_slow_path_data.h"
#include "deegen_stencil_reserved_placeholder_ords.h"
#include "llvm_fcmp_extra_optimizations.h"
#include "tvalue_typecheck_optimization.h"
#include "deegen_type_based_hcs_helper.h"
#include "tag_register_optimization.h"
#include "deegen_ast_return.h"
#include "deegen_ast_make_call.h"
#include "deegen_ast_simple_lowering_utils.h"
#include "deegen_ast_return_value_accessor.h"
#include "deegen_stencil_lowering_pass.h"
#include "invoke_clang_helper.h"
#include "llvm_override_option.h"
#include "llvm/Linker/Linker.h"
#include "vm_base_pointer_optimization.h"

namespace dast {

void BaselineJitImplCreator::DoLowering(BytecodeIrInfo* bii, const DeegenGlobalBytecodeTraitAccessor& gbta)
{
    using namespace llvm;

    LLVMContext& ctx = m_module->getContext();

    ReleaseAssert(!m_generated);
    m_generated = true;

    ReleaseAssert(m_bytecodeDef->IsBytecodeStructLengthFinalized());

    // Create the wrapper function 'm_wrapper'
    //
    CreateWrapperFunction();
    ReleaseAssert(m_wrapper != nullptr);

    // Inline 'm_impl' into 'm_wrapper'
    //
    if (m_impl->hasFnAttribute(Attribute::AttrKind::NoInline))
    {
        m_impl->removeFnAttr(Attribute::AttrKind::NoInline);
    }
    m_impl->addFnAttr(Attribute::AttrKind::AlwaysInline);
    m_impl->setLinkage(GlobalValue::InternalLinkage);

    DesugarAndSimplifyLLVMModule(m_module.get(), DesugaringLevel::PerFunctionSimplifyOnlyAggresive);
    RunLLVMDeadGlobalElimination(m_module.get());
    m_impl = nullptr;

    m_valuePreserver.RefreshAfterTransform();

    std::vector<AstInlineCache::BaselineJitLLVMLoweringResult> icLLRes;

    if (IsMainComponent())
    {
        std::vector<AstInlineCache> allIcUses = AstInlineCache::GetAllUseInFunction(m_wrapper);
        size_t globalIcTraitOrdBase = gbta.GetGenericIcEffectTraitBaseOrdinal(m_bytecodeDef->GetBytecodeIdName());
        for (size_t i = 0; i < allIcUses.size(); i++)
        {
            icLLRes.push_back(allIcUses[i].DoLoweringForBaselineJit(this, i, globalIcTraitOrdBase));
            globalIcTraitOrdBase += allIcUses[i].m_totalEffectKinds;
        }
        ReleaseAssert(globalIcTraitOrdBase == gbta.GetGenericIcEffectTraitBaseOrdinal(m_bytecodeDef->GetBytecodeIdName()) + gbta.GetNumTotalGenericIcEffectKinds(m_bytecodeDef->GetBytecodeIdName()));
    }

    // Now we can do the lowerings
    //
    AstBytecodeReturn::DoLoweringForBaselineJIT(this, m_wrapper);
    AstMakeCall::LowerForBaselineJIT(this, m_wrapper);
    AstReturnValueAccessor::LowerForInterpreterOrBaselineJIT(this, m_wrapper);
    DeegenAllSimpleApiLoweringPasses::LowerAllForBaselineJIT(this, m_wrapper);
    AstSlowPath::LowerAllForInterpreterOrBaselineJIT(this, m_wrapper);

    // Lower the remaining function APIs from the generic IC
    //
    if (!IsJitSlowPath())
    {
        LowerInterpreterGetBytecodePtrInternalAPI(this, m_wrapper);
        AstInlineCache::LowerIcPtrGetterFunctionForBaselineJit(this, m_wrapper);
    }

    // All lowerings are complete.
    // Remove the NoReturn attribute since all pseudo no-return API calls have been replaced to dispatching tail calls
    //
    m_wrapper->removeFnAttr(Attribute::AttrKind::NoReturn);
    if (m_processKind == BytecodeIrComponentKind::Main && m_bytecodeDef->HasQuickeningSlowPath())
    {
        std::string slowPathFnName = BytecodeIrInfo::GetQuickeningSlowPathFuncName(m_bytecodeDef);
        Function* slowPathFn = m_module->getFunction(slowPathFnName);
        ReleaseAssert(slowPathFn != nullptr);
        ReleaseAssert(slowPathFn->hasFnAttribute(Attribute::AttrKind::NoReturn));
        slowPathFn->removeFnAttr(Attribute::AttrKind::NoReturn);
    }

    // Remove the value preserver annotations so optimizer can work fully
    //
    m_valuePreserver.Cleanup();

    // Now, having lowered everything, we can run the tag register optimization pass
    //
    // The tag register optimization pass is supposed to be run after all API calls have been inlined, so lower all the API calls
    //
    DesugarAndSimplifyLLVMModule(m_module.get(), DesugaringLevel::Top);

    // Now, run the tag register optimization pass
    //
    RunTagRegisterOptimizationPass(m_wrapper);

    // Now, run the vm base pointer optimization pass
    //
    RunVMBasePointerOptimizationPass(m_wrapper);

    DesugarAndSimplifyLLVMModule(m_module.get(), DesugaringLevel::PerFunctionSimplifyOnlyAggresive);

    // Run the stencil runtime constant insertion pass if this function is for the JIT
    //
    if (!IsJitSlowPath())
    {
        m_stencilRcDefinitions = m_stencilRcInserter.RunOnFunction(m_wrapper);
    }

    // Run LLVM optimization pass
    //
    RunLLVMOptimizePass(m_module.get());

    // Run our homebrewed simple rewrite passes (targetting some insufficiencies of LLVM's optimizations of FCmp) after the main LLVM optimization pass
    //
    DeegenExtraLLVMOptPass_FuseTwoNaNChecksIntoOne(m_module.get());
    DeegenExtraLLVMOptPass_FuseNaNAndCmpCheckIntoOne(m_module.get());

    ReleaseAssert(m_module->getFunction(m_resultFuncName) == m_wrapper);

    if (icLLRes.size() > 0)
    {
        std::string fallthroughPlaceholderName = DeegenPlaceholderUtils::FindFallthroughPlaceholderSymbolName(GetStencilRcDefinitions());
        if (fallthroughPlaceholderName != "")
        {
            AstInlineCache::AttemptIrRewriteToManuallyTailDuplicateSimpleIcCases(m_wrapper, fallthroughPlaceholderName);
        }
    }

    ValidateLLVMModule(m_module.get());

    // After the optimization pass, change the linkage of everything to 'external' before extraction
    // This is fine: for AOT slow path, our caller will fix up the linkage for us.
    // For JIT, we will extract the target function into a stencil, so the linkage doesn't matter.
    //
    for (Function& func : *m_module.get())
    {
        func.setLinkage(GlobalValue::ExternalLinkage);
        func.setComdat(nullptr);
    }

    // Sanity check that 'm_wrapper' is there
    //
    ReleaseAssert(m_module->getFunction(m_resultFuncName) == m_wrapper);
    ReleaseAssert(!m_wrapper->empty());

    // Extract all the IC body functions
    //
    std::unique_ptr<Module> icBodyModule;
    if (icLLRes.size() > 0)
    {
        std::vector<std::string> fnNames;
        for (auto& item : icLLRes)
        {
            ReleaseAssert(m_module->getFunction(item.m_bodyFnName) != nullptr);
            fnNames.push_back(item.m_bodyFnName);
        }
        icBodyModule = ExtractFunctions(m_module.get(), fnNames);
    }

    m_module = ExtractFunction(m_module.get(), m_resultFuncName);

    // After the extract, 'm_wrapper' is invalidated since a new module is returned. Refresh its value.
    //
    m_wrapper = m_module->getFunction(m_resultFuncName);
    ReleaseAssert(m_wrapper != nullptr);

    if (IsJitSlowPath())
    {
        // For AOT slow path, we are done at this point. For JIT, we need to do further processing.
        //
        return;
    }

    // Run the stencil lowering pass in preparation for stencil generation
    //
    DeegenStencilLoweringPass slPass = DeegenStencilLoweringPass::RunIrRewritePhase(m_wrapper, GetRcPlaceholderNameForFallthrough());

    // Compile the function to ASM (.s) file
    // TODO: ideally we should think about properly setting function and loop alignments
    // Currently we simply ignore alignments, which is fine for now since currently our none of our JIT fast path contains loops.
    // But this will break down once any loop is introduced. To fix this, we need to be aware of the aligned BBs and manually generate NOPs.
    //
    std::string asmFile = CompileLLVMModuleToAssemblyFileForStencilGeneration(
        m_module.get(),
        llvm::Reloc::Static,
        llvm::CodeModel::Small,
        [](TargetOptions& opt) {
            // This is the option that is equivalent to the clang -fdata-sections flag
            // Put each data symbol into a separate data section so our stencil creation pass can produce more efficient result
            //
            opt.DataSections = true;
        });

    // Run the ASM phase of the stencil lowering pass
    //
    slPass.RunAsmRewritePhase(asmFile);
    asmFile = slPass.m_primaryPostTransformAsmFile;

    m_stencilPreTransformAsmFile = std::move(slPass.m_rawInputFileForAudit);

    m_stencilPostTransformAsmFile = asmFile;

    // Compile the final ASM file to object file
    //
    m_stencilObjectFile = CompileAssemblyFileToObjectFile(asmFile, " -fno-pic -fno-pie ");

    // Parse object file into copy-and-patch stencil
    //
    m_stencil = DeegenStencil::ParseMainLogic(ctx, m_stencilObjectFile);

    m_genericIcLoweringResult = AstInlineCache::DoLoweringAfterAsmTransform(bii,
                                                                            this,
                                                                            std::move(icBodyModule),
                                                                            icLLRes,
                                                                            slPass.m_genericIcLoweringResults,
                                                                            m_stencil /*mainStencil*/,
                                                                            gbta);

    m_callIcInfo = slPass.m_callIcLoweringResults;

    // Note that we cannot further lower the main logic stencil to concrete copy-and-patch logic yet, because at this stage
    // we cannot determine if we are allowed to eliminate the tail jump to fallthrough. So our lowering ends here.
    //
    return;
}

}   // namespace dast
