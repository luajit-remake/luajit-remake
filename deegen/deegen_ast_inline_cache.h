#pragma once

#include "common.h"
#include "misc_llvm_helper.h"
#include "cxx_symbol_demangler.h"
#include "api_inline_cache.h"
#include "deegen_bytecode_metadata.h"

namespace dast {

class BaselineJitImplCreator;
struct X64AsmFile;
struct DeegenStencil;
class DeegenGlobalBytecodeTraitAccessor;
struct BytecodeIrInfo;
class JitImplCreatorBase;

class AstInlineCache
{
public:
    struct Effect
    {
        struct SpecializationInfo
        {
            // The LLVM value in the body function that is being specialized
            //
            llvm::Value* m_valueInBodyFn;
            // Whether this specialization is full coverage
            //
            bool m_isFullCoverage;
            // The list of each specialization
            //
            std::vector<llvm::Constant*> m_specializations;
        };

        struct IcStateValueInfo
        {
            llvm::Value* m_valueInBodyFn;
            // Always populated, but only matters for JIT lowering
            //
            int64_t m_lbInclusive;
            int64_t m_ubInclusive;
            // Only used in JIT lowering
            //
            size_t m_placeholderOrd;
        };

        // The place in the IC body where this effect is called
        //
        llvm::CallInst* m_origin;

        // The IC pointer in the IC body
        //
        llvm::Value* m_icPtr;

        // The list of specializations
        //
        std::vector<SpecializationInfo> m_specializations;

        // The list of wrapped effect function that is callable from the main function
        //
        std::vector<llvm::Function*> m_effectFnMain;

        // The captures of 'm_effectFnMain' in the main function
        //
        std::vector<llvm::Value*> m_effectFnMainArgs;

        // The lambda and capture used to call the effect from the IC body
        //
        llvm::Function* m_effectFnBody;
        llvm::AllocaInst* m_effectFnBodyCapture;

        // The placeholder functions describing how to encode/decode the IC state
        //
        llvm::Function* m_icStateEncoder;
        llvm::Function* m_icStateDecoder;

        // The LLVM values (defined in the IC body) of the IC state
        //
        std::vector<IcStateValueInfo> m_icStateVals;

        // The effect functions in this effect should occupy ordinal [m_effectStartOrdinal, m_effectStartOrdinal + m_effectFnMain.size())
        //
        size_t m_effectStartOrdinal;

        size_t m_ordinalInEffectArray;
    };

    static bool WARN_UNUSED IsIcPtrGetterFunctionCall(llvm::CallInst* inst);

    static void PreprocessModule(llvm::Module* module);

    // Perform trivial lowering for all inline caches in 'func'
    //
    // For testing and debugging purpose only.
    //
    static void TriviallyLowerAllInlineCaches(llvm::Function* func);

    // Parse out all the inline caches defined in the given function
    // Note that unlike many GetAllUseInFunction() in other classes, this GetAllUseInFunction() is not idempotent.
    // That is, it can be only called once on each function.
    //
    static std::vector<AstInlineCache> WARN_UNUSED GetAllUseInFunction(llvm::Function* func);

    // This lowers everything except the 'MakeInlineCache()' call (the m_icPtrOrigin, which returns the IC pointer)
    // The main motivation is to separate the IC lowering logic and the interpreter-specific logic. Such design also
    // makes unit testing easier.
    //
    // This function also populates 'm_icStruct', the state definition of this IC.
    //
    void DoLoweringForInterpreter();

    struct JitLLVMLoweringResult
    {
        std::string m_bodyFnName;

        // Describes one IC effect codegen function
        //
        struct Item
        {
            // The global ordinal, which is also the suffix of the function
            //
            size_t m_globalOrd;
            // The parameters passed to the codegen function corresponds to placeholder ordinal [start, start + num)
            //
            size_t m_placeholderStart;
            size_t m_numPlaceholders;
        };

        std::vector<Item> m_effectPlaceholderDesc;
    };

    // Do lowering for baseline/DFG JIT
    // Each bytecode in thoery may employ multiple IC sites, so 'icUsageOrdInBytecode' is the ordinal of this IC
    // The 'globalIcEffectTraitBaseOrd' is the effect trait table base index for this IC,
    // so to get trait for effect kind 'k' in this IC, the index is globalIcEffectTraitBaseOrd + k
    //
    // Note that for DFG JIT, for simplicity, we currently do not globally precompute the globalIcEffectTraitBaseOrd,
    // and the passed in globalIcEffectTraitBaseOrd is always 0
    //
    JitLLVMLoweringResult WARN_UNUSED DoLoweringForJit(JitImplCreatorBase* ifi, size_t icUsageOrdInBytecode, size_t globalIcEffectTraitBaseOrd);

    static void LowerIcPtrGetterFunctionForJit(JitImplCreatorBase* ifi, llvm::Function* func);

    struct JitAsmTransformResult
    {
        // The label for the SMC region
        //
        std::string m_labelForSMCRegion;
        // The entry label for each effect of this IC, in the order of the oridinal
        //
        std::vector<std::string> m_labelForEffects;
        // Label for the slow path
        // The patchable jump always jumps to here initially
        //
        std::string m_labelForIcMissLogic;
        // The ordinal of this IC in the bytecode
        //
        uint64_t m_uniqueOrd;

        // Special symbols which stores the results of label offset / distance computation
        //
        std::string m_symbolNameForSMCLabelOffset;
        std::string m_symbolNameForSMCRegionLength;
        std::string m_symbolNameForIcMissLogicLabelOffset;
    };

    static std::vector<JitAsmTransformResult> WARN_UNUSED DoAsmTransformForJit(X64AsmFile* file);

    // Final result after all ASM-level lowering, produced by stencil lowering pipeline
    //
    struct JitAsmLoweringResult
    {
        // Assembly files for each of the extracted IC case logic
        //
        std::vector<std::string> m_icLogicAsm;

        // The # of lines of assembly of each IC case, in the same order
        //
        std::vector<size_t> m_icLogicAsmLineCount;

        // Special symbol name storing the various measured values
        //
        std::string m_symbolNameForSMCLabelOffset;
        std::string m_symbolNameForSMCRegionLength;
        std::string m_symbolNameForIcMissLogicLabelOffset;

        // The ordinal of this IC in the bytecode
        //
        uint64_t m_uniqueOrd;
    };

    struct JitCodegenResult
    {
        std::unique_ptr<llvm::Module> m_module;
        std::string m_resultFnName;
        size_t m_icSize;
        std::string m_disasmForAudit;
    };

    struct InlineSlabInfo
    {
        // If true, this IC has inline slab, so we need to generate different SMC patching
        // logic depending on whether the inline slab is already used
        //
        bool m_hasInlineSlab;

        // The offset and size of the SMC region
        //
        size_t m_smcRegionOffset;
        size_t m_smcRegionLength;

        // If the inline slab is already used, the patchable jump ends at fastpathAddr + m_inlineSlabPatchableJumpEndOffsetInFastPath
        // Otherwise, the patchable jump ends at fastpathAddr + m_smcRegionOffset + 5
        //
        size_t m_inlineSlabPatchableJumpEndOffsetInFastPath;

        // The offset of the IC miss logic in the slow path
        //
        size_t m_icMissLogicOffsetInSlowPath;
    };

    static JitCodegenResult WARN_UNUSED CreateJitIcCodegenImplementation(JitImplCreatorBase* ifi,
                                                                         JitLLVMLoweringResult::Item icInfo,
                                                                         DeegenStencil& stencil,
                                                                         InlineSlabInfo inlineSlabInfo,
                                                                         size_t icUsageOrdInBytecode,
                                                                         bool isCodegenForInlineSlab);

    struct JitFinalLoweringResult
    {
        // At the time we codegen the implementation, the length of the SlowPathData has not yet been finalized,
        // so we were using an external constant to represent the length (and LLVM optimization passes were not run).
        // This function replaces the external constant with the actual length and run the LLVM optimization passes.
        //
        void LateFixSlowPathDataLength(size_t length);

        struct TraitDesc
        {
            size_t m_ordInTraitTable;
            size_t m_allocationLength;
        };

        // Must call LateFixSlowPathDataLength to get the final module
        //
        std::unique_ptr<llvm::Module> m_icBodyModule;
        std::vector<std::string> m_icBodyFunctionNames;
        std::vector<TraitDesc> m_icTraitInfo;
        std::vector<InlineSlabInfo> m_inlineSlabInfo;
        uint64_t m_fpuUsedMask;
        std::string m_disasmForAudit;
    };

    static JitFinalLoweringResult WARN_UNUSED DoLoweringAfterAsmTransform(BytecodeIrInfo* bii,
                                                                          JitImplCreatorBase* ifi,
                                                                          std::unique_ptr<llvm::Module> icBodyModule,
                                                                          std::vector<JitLLVMLoweringResult>& icLLRes,
                                                                          std::vector<JitAsmLoweringResult>& icAsmRes,
                                                                          DeegenStencil& mainStencil,
                                                                          size_t icEffectTraitBaseOrd,
                                                                          size_t expectedNumTotalIcEffects);

    // Perform trivial lowering: the execution semantics of this inline cache is preserved,
    // but no inling caching ever happens (i.e., execution simply unconditionally execute the IC body).
    //
    // For testing and debugging purpose only.
    //
    void DoTrivialLowering();

    // LLVM's tail duplication already works completely fine normally.
    // However, if the IC logic itself is *too* simple, LLVM would treat the cost of tail duplication as too high
    // (as it doesn't understand that we will extract the IC into a separate piece of code) and won't do the tail duplication.
    //
    // The overhead of not doing the tail duplication is only one or two extra jumps, so it's really not a big deal in performance.
    // But if we can get slightly better code with a riskless simple pass, why not?
    //
    // This pass is a really crude pass that fixes this problem.
    // It simply checks that: if an IC entry BB branches directly to a *small* terminal BB that dispatches to the next bytecode,
    // it would manually duplicate the terminal BB into the IC BB.
    // The strict check is intentional, as tail duplication makes the IC logic larger, thus higher memory consumption..
    //
    static void AttemptIrRewriteToManuallyTailDuplicateSimpleIcCases(llvm::Function* func,
                                                                     std::string fallthroughPlaceholderName);

    // The CallInst that retrieves the IC pointer in the main function
    //
    llvm::CallInst* m_icPtrOrigin;
    // The CallInst that invokes the IC body in the main function
    //
    llvm::CallInst* m_origin;
    // Whether 'FuseICIntoInterpreterOpcode()' is specified
    //
    bool m_shouldFuseICIntoInterpreterOpcode;
    // The transformed body function (at this stage, the body lambda has been transformed into a normal function)
    //
    llvm::Function* m_bodyFn;
    // The arguments of the body function
    //
    std::vector<llvm::Value*> m_bodyFnArgs;
    // The arg ordinal of the body function that passes over the IC pointer
    //
    uint32_t m_bodyFnIcPtrArgOrd;
    // The arg ordinal of the body function that passes over the current IC key
    //
    uint32_t m_bodyFnIcKeyArgOrd;
    // The arg ordinal of the body function that passes over the bytecode ptr, only exists if m_shouldFuseICIntoInterpreterOpcode is true
    //
    uint32_t m_bodyFnBytecodePtrArgOrd;
    // The key and impossible value (if no impossible value is specified, the field is nullptr)
    //
    llvm::Value* m_icKey;
    llvm::ConstantInt* m_icKeyImpossibleValueMaybeNull;
    // The list of effect lambdas of this IC
    //
    std::vector<Effect> m_effects;
    size_t m_totalEffectKinds;
    // The list of other misc API calls used in this IC
    //
    std::vector<llvm::CallInst*> m_setUncacheableApiCalls;

    // This is populated by 'DoLoweringForInterpreter'
    // The IC state definition
    //
    std::unique_ptr<BytecodeMetadataStruct> m_icStruct;
};

constexpr const char* x_get_bytecode_ptr_placeholder_fn_name = "__DeegenImpl_GetInterpreterBytecodePtrPlaceholder";
constexpr const char* x_jit_codegen_ic_impl_placeholder_fn_prefix = "__deegen_jit_codegen_generic_ic_effect_";
constexpr const char* x_jit_check_generic_ic_fits_in_inline_slab_placeholder_fn_prefix = "__deegen_jit_check_generic_ic_fits_in_inline_slab_";

class DeegenBytecodeImplCreatorBase;

void LowerInterpreterGetBytecodePtrInternalAPI(DeegenBytecodeImplCreatorBase* ifi, llvm::Function* func);

// Always takes i1 and returns i1
// Only used if 'FuseICIntoInterpreterOpcode' is true
//
constexpr const char* x_adapt_ic_hit_check_behavior_placeholder_fn = "__deegen_internal_adapt_ic_hit_check_behavior_fn";

// Always takes i8 and returns i8
// Only used if 'FuseICIntoInterpreterOpcode' is true
//
constexpr const char* x_adapt_get_ic_effect_ord_behavior_placeholder_fn = "__deegen_internal_adapt_get_ic_effect_ord_behavior_fn";

}   // namespace dast
