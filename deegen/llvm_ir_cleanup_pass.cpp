#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Analysis/BasicAliasAnalysis.h"
#include "llvm/Analysis/CGSCCPassManager.h"
#include "llvm/Analysis/GlobalsModRef.h"
#include "llvm/Analysis/InlineAdvisor.h"
#include "llvm/Analysis/OptimizationRemarkEmitter.h"
#include "llvm/Analysis/ProfileSummaryInfo.h"
#include "llvm/Analysis/ScopedNoAliasAA.h"
#include "llvm/Analysis/TypeBasedAliasAnalysis.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Passes/OptimizationLevel.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/PGOOptions.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Transforms/AggressiveInstCombine/AggressiveInstCombine.h"
#include "llvm/Transforms/IPO/AlwaysInliner.h"
#include "llvm/Transforms/IPO/Annotation2Metadata.h"
#include "llvm/Transforms/IPO/ArgumentPromotion.h"
#include "llvm/Transforms/IPO/Attributor.h"
#include "llvm/Transforms/IPO/CalledValuePropagation.h"
#include "llvm/Transforms/IPO/ConstantMerge.h"
#include "llvm/Transforms/IPO/CrossDSOCFI.h"
#include "llvm/Transforms/IPO/DeadArgumentElimination.h"
#include "llvm/Transforms/IPO/ElimAvailExtern.h"
#include "llvm/Transforms/IPO/ForceFunctionAttrs.h"
#include "llvm/Transforms/IPO/FunctionAttrs.h"
#include "llvm/Transforms/IPO/GlobalDCE.h"
#include "llvm/Transforms/IPO/GlobalOpt.h"
#include "llvm/Transforms/IPO/GlobalSplit.h"
#include "llvm/Transforms/IPO/HotColdSplitting.h"
#include "llvm/Transforms/IPO/IROutliner.h"
#include "llvm/Transforms/IPO/InferFunctionAttrs.h"
#include "llvm/Transforms/IPO/Inliner.h"
#include "llvm/Transforms/IPO/LowerTypeTests.h"
#include "llvm/Transforms/IPO/MergeFunctions.h"
#include "llvm/Transforms/IPO/ModuleInliner.h"
#include "llvm/Transforms/IPO/OpenMPOpt.h"
#include "llvm/Transforms/IPO/PartialInlining.h"
#include "llvm/Transforms/IPO/SCCP.h"
#include "llvm/Transforms/IPO/SampleProfile.h"
#include "llvm/Transforms/IPO/SampleProfileProbe.h"
#include "llvm/Transforms/IPO/SyntheticCountsPropagation.h"
#include "llvm/Transforms/IPO/WholeProgramDevirt.h"
#include "llvm/Transforms/InstCombine/InstCombine.h"
#include "llvm/Transforms/Instrumentation/CGProfile.h"
#include "llvm/Transforms/Instrumentation/ControlHeightReduction.h"
#include "llvm/Transforms/Instrumentation/InstrOrderFile.h"
#include "llvm/Transforms/Instrumentation/InstrProfiling.h"
#include "llvm/Transforms/Instrumentation/MemProfiler.h"
#include "llvm/Transforms/Instrumentation/PGOInstrumentation.h"
#include "llvm/Transforms/Scalar.h"
#include "llvm/Transforms/Scalar/ADCE.h"
#include "llvm/Transforms/Scalar/AlignmentFromAssumptions.h"
#include "llvm/Transforms/Scalar/AnnotationRemarks.h"
#include "llvm/Transforms/Scalar/BDCE.h"
#include "llvm/Transforms/Scalar/CallSiteSplitting.h"
#include "llvm/Transforms/Scalar/ConstraintElimination.h"
#include "llvm/Transforms/Scalar/CorrelatedValuePropagation.h"
#include "llvm/Transforms/Scalar/DFAJumpThreading.h"
#include "llvm/Transforms/Scalar/DeadStoreElimination.h"
#include "llvm/Transforms/Scalar/DivRemPairs.h"
#include "llvm/Transforms/Scalar/EarlyCSE.h"
#include "llvm/Transforms/Scalar/Float2Int.h"
#include "llvm/Transforms/Scalar/GVN.h"
#include "llvm/Transforms/Scalar/IndVarSimplify.h"
#include "llvm/Transforms/Scalar/InstSimplifyPass.h"
#include "llvm/Transforms/Scalar/JumpThreading.h"
#include "llvm/Transforms/Scalar/LICM.h"
#include "llvm/Transforms/Scalar/LoopDeletion.h"
#include "llvm/Transforms/Scalar/LoopDistribute.h"
#include "llvm/Transforms/Scalar/LoopFlatten.h"
#include "llvm/Transforms/Scalar/LoopIdiomRecognize.h"
#include "llvm/Transforms/Scalar/LoopInstSimplify.h"
#include "llvm/Transforms/Scalar/LoopInterchange.h"
#include "llvm/Transforms/Scalar/LoopLoadElimination.h"
#include "llvm/Transforms/Scalar/LoopPassManager.h"
#include "llvm/Transforms/Scalar/LoopRotation.h"
#include "llvm/Transforms/Scalar/LoopSimplifyCFG.h"
#include "llvm/Transforms/Scalar/LoopSink.h"
#include "llvm/Transforms/Scalar/LoopUnrollAndJamPass.h"
#include "llvm/Transforms/Scalar/LoopUnrollPass.h"
#include "llvm/Transforms/Scalar/LowerConstantIntrinsics.h"
#include "llvm/Transforms/Scalar/LowerExpectIntrinsic.h"
#include "llvm/Transforms/Scalar/LowerMatrixIntrinsics.h"
#include "llvm/Transforms/Scalar/MemCpyOptimizer.h"
#include "llvm/Transforms/Scalar/MergedLoadStoreMotion.h"
#include "llvm/Transforms/Scalar/NewGVN.h"
#include "llvm/Transforms/Scalar/Reassociate.h"
#include "llvm/Transforms/Scalar/SCCP.h"
#include "llvm/Transforms/Scalar/SROA.h"
#include "llvm/Transforms/Scalar/SimpleLoopUnswitch.h"
#include "llvm/Transforms/Scalar/SimplifyCFG.h"
#include "llvm/Transforms/Scalar/SpeculativeExecution.h"
#include "llvm/Transforms/Scalar/TailRecursionElimination.h"
#include "llvm/Transforms/Scalar/WarnMissedTransforms.h"
#include "llvm/Transforms/Utils/AddDiscriminators.h"
#include "llvm/Transforms/Utils/AssumeBundleBuilder.h"
#include "llvm/Transforms/Utils/CanonicalizeAliases.h"
#include "llvm/Transforms/Utils/InjectTLIMappings.h"
#include "llvm/Transforms/Utils/LibCallsShrinkWrap.h"
#include "llvm/Transforms/Utils/Mem2Reg.h"
#include "llvm/Transforms/Utils/NameAnonGlobals.h"
#include "llvm/Transforms/Utils/RelLookupTableConverter.h"
#include "llvm/Transforms/Utils/SimplifyCFGOptions.h"
#include "llvm/Transforms/Vectorize/LoopVectorize.h"
#include "llvm/Transforms/Vectorize/SLPVectorizer.h"
#include "llvm/Transforms/Vectorize/VectorCombine.h"

#include "misc_llvm_helper.h"
#include "deegen_desugaring_level.h"
#include "llvm_flatten_select_structure_pass.h"

using namespace llvm;

// Logic stolen from llvm/lib/Passes/PassBuilder.cpp
// function buildModuleSimplificationPipeline
//
static ModulePassManager BuildModuleSimplificationPassesForDesugaring()
{
    ModulePassManager MPM;
    MPM.addPass(InferFunctionAttrsPass());
    FunctionPassManager EarlyFPM;
    EarlyFPM.addPass(LowerExpectIntrinsicPass());
    EarlyFPM.addPass(SimplifyCFGPass());
    EarlyFPM.addPass(SROAPass());
    EarlyFPM.addPass(EarlyCSEPass());
    EarlyFPM.addPass(CallSiteSplittingPass());
    MPM.addPass(createModuleToFunctionPassAdaptor(std::move(EarlyFPM), true /*EagerlyInvalidateAnalyses*/));
    MPM.addPass(IPSCCPPass());
    MPM.addPass(CalledValuePropagationPass());
    MPM.addPass(GlobalOptPass());
    MPM.addPass(createModuleToFunctionPassAdaptor(PromotePass()));
    FunctionPassManager GlobalCleanupPM;
    GlobalCleanupPM.addPass(InstCombinePass());
    GlobalCleanupPM.addPass(SimplifyCFGPass(SimplifyCFGOptions().convertSwitchRangeToICmp(true)));
    MPM.addPass(createModuleToFunctionPassAdaptor(std::move(GlobalCleanupPM), true /*EagerlyInvalidateAnalyses*/));
    return MPM;
}

// This is mostly copied from LLVM PassBuilderPipelines.cpp:buildFunctionSimplificationPipeline except that
// we removed the following passes:
//   1. All the loop transformation passes
//   2. LibCallsShrinkWrapPass
// We made the changes because our purpose is simplification. We do not want to prematurely lower the abstractions,
// because we will do some transformations by ourselves. We will run the full optimization pass in the end anyway.
//
static FunctionPassManager BuildFunctionSimplificationPipelineForDesugaring()
{
    FunctionPassManager FPM;
    FPM.addPass(SROAPass());
    FPM.addPass(EarlyCSEPass(true /* Enable mem-ssa. */));
    FPM.addPass(SpeculativeExecutionPass(/* OnlyIfDivergentTarget =*/true));
    FPM.addPass(JumpThreadingPass());
    FPM.addPass(CorrelatedValuePropagationPass());
    FPM.addPass(SimplifyCFGPass(SimplifyCFGOptions().convertSwitchRangeToICmp(true)));
    FPM.addPass(InstCombinePass());
    FPM.addPass(AggressiveInstCombinePass());
    FPM.addPass(TailCallElimPass());
    FPM.addPass(SimplifyCFGPass(SimplifyCFGOptions().convertSwitchRangeToICmp(true)));
    FPM.addPass(ReassociatePass());
    FPM.addPass(RequireAnalysisPass<OptimizationRemarkEmitterAnalysis, Function>());
    FPM.addPass(SimplifyCFGPass(SimplifyCFGOptions().convertSwitchRangeToICmp(true)));
    FPM.addPass(InstCombinePass());
    FPM.addPass(SROAPass());
    FPM.addPass(MergedLoadStoreMotionPass());
    FPM.addPass(GVNPass());
    FPM.addPass(SCCPPass());
    FPM.addPass(BDCEPass());
    FPM.addPass(InstCombinePass());
    FPM.addPass(JumpThreadingPass());
    FPM.addPass(CorrelatedValuePropagationPass());
    FPM.addPass(ADCEPass());
    FPM.addPass(MemCpyOptPass());
    FPM.addPass(DSEPass());
    FPM.addPass(SimplifyCFGPass(SimplifyCFGOptions()
                                    .convertSwitchRangeToICmp(true)
                                    .hoistCommonInsts(true)
                                    .sinkCommonInsts(true)));
    FPM.addPass(InstCombinePass());
    return FPM;
}

// Code copied from LLVM PassBuilderPipelines.cpp:buildInlinerPipeline
// We have to copy the code because we want to use our 'BuildFunctionSimplificationPipelineForDesugaring' function in the pass,
// not LLVM's buildFunctionSimplificationPipeline function.
//
// Note that I also removed 'PostOrderFunctionAttrsPass', because I'm not confident that
// our transformation won't break the deduced attributes.
//
static ModuleInlinerWrapperPass BuildInlinerPipelineForDesugaring()
{
    InlineParams IP =  getInlineParams(OptimizationLevel::O3.getSpeedupLevel(), OptimizationLevel::O3.getSizeLevel());

    ModuleInlinerWrapperPass MIWP(
        IP, true /*PerformMandatoryInliningsFirst*/,
        InlineContext{ThinOrFullLTOPhase::None, InlinePass::CGSCCInliner},
        InliningAdvisorMode::Default, 4 /*maxDevirtIterations*/);

    MIWP.addModulePass(RequireAnalysisPass<GlobalsAA, Module>());
    MIWP.addModulePass(
        createModuleToFunctionPassAdaptor(InvalidateAnalysisPass<AAManager>()));
    MIWP.addModulePass(RequireAnalysisPass<ProfileSummaryAnalysis, Module>());
    CGSCCPassManager &MainCGPipeline = MIWP.getPM();
    MainCGPipeline.addPass(ArgumentPromotionPass());
    MainCGPipeline.addPass(OpenMPOptCGSCCPass());
    MainCGPipeline.addPass(createCGSCCToFunctionPassAdaptor(
        BuildFunctionSimplificationPipelineForDesugaring(),
        true /*EagerlyInvalidateAnalyses*/, true /*EnableNoRerunSimplificationPipeline*/));
    MIWP.addLateModulePass(createModuleToFunctionPassAdaptor(
        InvalidateAnalysisPass<ShouldNotRunFunctionPassesAnalysis>()));
    return MIWP;
}

namespace dast {

// Most of the heavy-lifting logic below are stolen from llvm/lib/Passes/PassBuilder.cpp function buildPerModuleDefaultPipeline.
// Some adaptions are made to fit our purpose.
//
void DesugarAndSimplifyLLVMModule(Module* module, DesugaringLevel level)
{
    ValidateLLVMModule(module);

    // It turns out that LLVM's SROA pass is not perfect: if function X is already in SSA form,
    // and it gets inlined into some other function Y, SROA often fails to fully decompose the
    // structures in Y into scalars, and produce miserable results. It seems like LLVM's SROA
    // expects that everything is in register form before inlining.
    //
    // Therefore we need to reverse everything back to register form before invoking LLVM's SROA
    // pass (and the rest of the simplification pipeline).
    //
    // It turns out that LLVM's RegToMem pass is designed for this purpose. However, it turns out
    // that the RegToMem pass alone is not sufficient, as it has trouble putting 'select'
    // instruction on structure values back to register form.
    //
    // So we first run our home-brewed pass to rewrite 'select' instruction on structure values
    // back to register form:
    //
    LLVMFlattenSelectOnStructureValueToBranchPass(module);

    // And then run LLVM's RegToMem pass to put everything else back to register form:
    //
    {
        legacy::PassManager Passes;
        Passes.add(createDemoteRegisterToMemoryPass());
        Passes.run(*module);
    }

    PassBuilder passBuilder;
    LoopAnalysisManager LAM;
    FunctionAnalysisManager FAM;
    CGSCCAnalysisManager CGAM;
    ModuleAnalysisManager MAM;

    passBuilder.registerModuleAnalyses(MAM);
    passBuilder.registerCGSCCAnalyses(CGAM);
    passBuilder.registerFunctionAnalyses(FAM);
    passBuilder.registerLoopAnalyses(LAM);
    passBuilder.crossRegisterProxies(LAM, FAM, CGAM, MAM);

    ModulePassManager MPM;
    MPM.addPass(Annotation2MetadataPass());
    MPM.addPass(ForceFunctionAttrsPass());

    if (level == DesugaringLevel::HandleAlwaysInlineButNoSimplify || level == DesugaringLevel::PerFunctionSimplifyOnly || level == DesugaringLevel::PerFunctionSimplifyOnlyAggresive)
    {
        MPM.addPass(AlwaysInlinerPass(true /* InsertLifetimeIntrinsics */));
    }

    if (level >= DesugaringLevel::PerFunctionSimplifyOnly)
    {
        MPM.addPass(BuildModuleSimplificationPassesForDesugaring());
    }

    LLVMRepeatedInliningInhibitor rii(module);

    // The set of function names with 'noinline' attribute
    //
    std::unordered_set<std::string> originalNoInlineFunctions;

    if (level > DesugaringLevel::PerFunctionSimplifyOnlyAggresive)
    {
        // Desugaring will add AlwaysInline and NoInline attributes.
        // It's OK to keep AlwaysInline attributes, since we can only raise desugaring level.
        // However, we need to remove the added NoInline attributes at the end.
        //
        // So now, record all functions with 'noinline' attribute.
        //
        for (Function& func : module->functions())
        {
            if (func.hasFnAttribute(Attribute::AttrKind::NoInline))
            {
                std::string fnName = func.getName().str();
                ReleaseAssert(fnName != "");
                ReleaseAssert(!originalNoInlineFunctions.count(fnName));
                originalNoInlineFunctions.insert(fnName);
            }
        }

        // Add the AlwaysInline and NoInline attributes
        //
        AddLLVMInliningAttributesForDesugaringLevel(module, level);

        // After we have added AlwaysInline and NoInline attributes for our API functions,
        // call the general LLVMRepeatedInliningInhibitor, so that general functions won't get
        // repeatedly inlined across many calls of DesugarAndSimplifyLLVMModule
        //
        rii.PrepareForInliningPass();

        // Create the inlining pass
        //
        MPM.addPass(BuildInlinerPipelineForDesugaring());
    }
    else if (level == DesugaringLevel::PerFunctionSimplifyOnlyAggresive)
    {
        MPM.addPass(createModuleToFunctionPassAdaptor(BuildFunctionSimplificationPipelineForDesugaring(), true /*EagerlyInvalidateAnalyses*/));
    }

    MPM.run(*module, MAM);

    if (level > DesugaringLevel::PerFunctionSimplifyOnlyAggresive)
    {
        // Restore action for LLVMRepeatedInliningInhibitor
        //
        rii.RestoreAfterInliningPass();

        // Remove the added 'NoInline' attributes
        //
        std::unordered_set<std::string> found;
        for (Function& func : module->functions())
        {
            if (func.hasFnAttribute(Attribute::AttrKind::NoInline))
            {
                std::string fnName = func.getName().str();
                ReleaseAssert(fnName != "");
                if (originalNoInlineFunctions.count(fnName))
                {
                    ReleaseAssert(!found.count(fnName));
                    found.insert(fnName);
                }
                else
                {
                    func.removeFnAttr(Attribute::AttrKind::NoInline);
                }
            }
        }
    }

    ValidateLLVMModule(module);
}

}   // namespace dast
