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

using namespace llvm;

// Logic stolen from llvm/lib/Passes/PassBuilder.cpp
// function buildModuleSimplificationPipeline
//
static ModulePassManager CreateSimplificationPasses()
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

namespace dast {

// Logic stolen from llvm/lib/Passes/PassBuilder.cpp
// function buildPerModuleDefaultPipeline
//
void DesugarAndSimplifyLLVMModule(Module* module, DesugaringLevel level)
{
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

    if (level == DesugaringLevel::AlwaysInline || level == DesugaringLevel::PerFunctionSimplifyOnly)
    {
        MPM.addPass(AlwaysInlinerPass(true /* InsertLifetimeIntrinsics */));
    }

    if (level >= DesugaringLevel::PerFunctionSimplifyOnly)
    {
        MPM.addPass(CreateSimplificationPasses());
    }

    // The set of function names with 'noinline' attribute
    //
    std::unordered_set<std::string> originalNoInlineFunctions;

    if (level > DesugaringLevel::PerFunctionSimplifyOnly)
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

        // Create the inlining pass
        //
        MPM.addPass(passBuilder.buildInlinerPipeline(OptimizationLevel::O3, ThinOrFullLTOPhase::None));
    }

    MPM.run(*module, MAM);

    if (level > DesugaringLevel::PerFunctionSimplifyOnly)
    {
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
        ReleaseAssert(found.size() == originalNoInlineFunctions.size());
    }
}

}   // namespace dast
