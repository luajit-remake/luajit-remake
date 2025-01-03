#pragma once

#include "common_utils.h"
#include "deegen_dfg_reg_alloc_variants.h"
#include "deegen_jit_register_patch_analyzer.h"
#include "deegen_stencil_creator.h"
#include "deegen_stencil_runtime_constant_insertion_pass.h"
#include "misc_llvm_helper.h"
#include "deegen_register_pinning_scheme.h"
#include "deegen_jit_runtime_constant_utils.h"
#include "deegen_dfg_jit_regalloc_rt_call_wrapper.h"
#include "drt/dfg_builtin_nodes.h"

namespace dast {

struct DfgBuiltinNodeVariantCodegenInfo
{
    size_t m_fastPathLength;
    size_t m_slowPathLength;
    size_t m_dataSecLength;
    size_t m_dataSecAlignment;
    std::unique_ptr<llvm::Module> m_cgMod;
    std::string m_cgFnName;
    std::unique_ptr<llvm::Module> m_implMod;    // for audit purpose
    std::vector<DfgRegAllocCCallWrapperRequest> m_ccwRequests;
    size_t m_variantOrd;
    std::string m_jitCodeAuditLog;
    bool m_mayProvideNullNsd;
    bool m_mayProvideNullPhySlotInfo;
    bool m_isCustomCodegenInterface;
};

// Common utility class to generate the code generator functions for the DFG builtin nodes
//
class DfgBuiltinNodeImplCreator
{
    MAKE_NONCOPYABLE(DfgBuiltinNodeImplCreator);
    MAKE_NONMOVABLE(DfgBuiltinNodeImplCreator);

public:
    DfgBuiltinNodeImplCreator(dfg::NodeKind associatedNodeKind)
        : m_associatedNodeKind(associatedNodeKind)
        , m_numOperands(static_cast<size_t>(-1))
        , m_variantOrd(static_cast<size_t>(-1))
        , m_shouldUseCustomInterface(false)
        , m_isRegAllocEnabled(false)
        , m_hasOutput(false)
        , m_hasHiddenOutput(false)
        , m_isSingletonLiteralOp(false)
        , m_regPurposeCtx(nullptr)
        , m_funcCtx(nullptr)
        , m_module(nullptr)
    {
        ReleaseAssert(associatedNodeKind < dfg::NodeKind_FirstAvailableGuestLanguageNodeKind);
        m_nodeName = dfg::GetDfgBuiltinNodeKindName(associatedNodeKind);
        if (dfg::DfgBuiltinNodeUseCustomCodegenImpl(associatedNodeKind))
        {
            SetShouldUseCustomCodegenInterface();
        }
    }

    // Similar to above, but gives a custom name, instead of the default name (which is just the name of the NodeKind)
    //
    DfgBuiltinNodeImplCreator(dfg::NodeKind associatedNodeKind, const std::string& nodeName)
        : DfgBuiltinNodeImplCreator(associatedNodeKind)
    {
        m_nodeName = nodeName;
    }

private:
    // By default, we generate the codegen function with prototype CodegenImplFn, which does end-to-end codegen from the DFG Node
    //
    // But for some complex cases, we need to generate codegen function with prototype CustomBuiltinNodeCodegenImplFn,
    // which gets used by higher-level logic that this class is not responsible for.
    //
    // This is controlled by the m_associatedNodeKind (the 'shouldUseCustomCodegenImpl' trait of the built-in node)
    //
    void SetShouldUseCustomCodegenInterface()
    {
        ReleaseAssert(!m_shouldUseCustomInterface);
        m_shouldUseCustomInterface = true;
    }

public:
    dfg::NodeKind GetAssociatedNodeKind() { return m_associatedNodeKind; }

    bool ShouldUseCustomCodegenInterface() { return m_shouldUseCustomInterface; }

    void SetupWithRegAllocDisabled(size_t numOperands, bool hasOutput);

    // Set up the reg config using a DfgNodeRegAllocSubVariant
    //
    void SetupForRegAllocSubVariant(size_t numOperands, DfgNodeRegAllocSubVariant* sv);

    // Set up the reg config using a DfgNodeRegAllocVariant
    //
    void SetupForRegAllocVariant(size_t numOperands,
                                 DfgNodeRegAllocVariant* variant,
                                 size_t numGprPassthrus,
                                 size_t numFprPassthrus,
                                 bool outputReusesInputReg);

    // Caller should populate implementation into the function
    //
    ExecutorFunctionContext* WARN_UNUSED CreateFunction(llvm::LLVMContext& ctx);

    // Use heuristic to automatically determine the values of maxGprPt, maxFprPt and outputWorthReuseInputReg
    // 'implGen' should create the implementation function (by calling CreateFunction and fill in the implementation)
    //
    static void DetermineRegDemandForRegAllocVariant(dfg::NodeKind associatedNodeKind,
                                                     std::string nodeName,
                                                     size_t numOperands,
                                                     DfgNodeRegAllocVariant* variant /*inout*/,
                                                     const std::function<void(DfgBuiltinNodeImplCreator*)>& implGen,
                                                     std::string* auditLogResult = nullptr /*out*/);

    // Clones the module so the input module is unchanged
    // This function is needed because sometimes the node implementation needs to call the guest language type checkers,
    // in which case we need to import those implementations so they can be called.
    //
    void SetBaseModule(llvm::Module* module);

    StencilRegisterFileContext* GetRegContext() { ReleaseAssert(m_regPurposeCtx.get() != nullptr); return m_regPurposeCtx.get(); }
    ExecutorFunctionContext* GetFuncCtx() { ReleaseAssert(m_funcCtx.get() != nullptr); return m_funcCtx.get(); }
    llvm::Function* GetFunction() { return GetFuncCtx()->GetFunction(); }

    llvm::Module* GetModule() { ReleaseAssert(m_module.get() != nullptr); return m_module.get(); }

    void DoLowering(bool forRegisterDemandTest);

    llvm::Value* WARN_UNUSED EmitGetOperand(llvm::Type* desiredTy, size_t operandOrd, llvm::Instruction* insertBefore);
    llvm::Value* WARN_UNUSED EmitGetOperand(llvm::Type* desiredTy, size_t operandOrd, llvm::BasicBlock* insertAtEnd);

    // Our reg alloc scheme normally assumes that if reg alloc is enabled, output is always stored in a reg as well
    // But for builtin node sometimes we want to allow some operands are reg-allocated while the output is not.
    // In this case, the RegAllocRootInfo will say that there's no output, but we can still call this function to store it.
    //
    void UnsafeEmitStoreHiddenOutputToStack(llvm::Value* outputVal, llvm::Instruction* insertBefore);
    void UnsafeEmitStoreHiddenOutputToStack(llvm::Value* outputVal, llvm::BasicBlock* insertAtEnd);

    // Pass in outputVal = nullptr if there's no output for this bytecode
    //
    llvm::CallInst* CreateDispatchToFallthrough(llvm::Value* outputVal, llvm::Instruction* insertBefore);
    llvm::CallInst* CreateDispatchToFallthrough(llvm::Value* outputVal, llvm::BasicBlock* insertAtEnd);

    // Create the dispatch for guest language function return
    //
    llvm::CallInst* CreateDispatchForGuestLanguageFunctionReturn(llvm::Value* retStart, llvm::Value* numRets, llvm::Instruction* insertBefore);
    llvm::CallInst* CreateDispatchForGuestLanguageFunctionReturn(llvm::Value* retStart, llvm::Value* numRets, llvm::BasicBlock* insertAtEnd);

    void CreateDispatchToOsrExit(llvm::Instruction* insertBefore);
    void CreateDispatchToOsrExit(llvm::BasicBlock* insertAtEnd);

    bool IsSetupComplete() { return m_numOperands != static_cast<size_t>(-1); }

    bool IsRegAllocEnabled() { ReleaseAssert(IsSetupComplete()); return m_isRegAllocEnabled; }
    size_t GetNumOperands() { ReleaseAssert(IsSetupComplete()); return m_numOperands; }

    void SetVariantOrd(size_t ord) { ReleaseAssert(m_variantOrd == static_cast<size_t>(-1) && ord != static_cast<size_t>(-1)); m_variantOrd = ord; }
    size_t GetVariantOrd() { ReleaseAssert(m_variantOrd != static_cast<size_t>(-1)); return m_variantOrd; }

    X64Reg GetOutputReg()
    {
        ReleaseAssert(IsSetupComplete() && m_isRegAllocEnabled && m_hasOutput);
        return m_outputReg;
    }

    bool IsOperandInReg(size_t opOrd)
    {
        ReleaseAssert(opOrd < GetNumOperands());
        return m_operandsReg.count(opOrd);
    }

    X64Reg GetOperandReg(size_t opOrd)
    {
        ReleaseAssert(IsOperandInReg(opOrd));
        return m_operandsReg[opOrd];
    }

private:
    template<typename LiteralTy>
    llvm::Value* WARN_UNUSED AddLiteralOperandImpl(size_t offsetInNsd, size_t sizeInNsd, int64_t valueLb, int64_t valueUb, llvm::Instruction* insertBefore)
    {
        using namespace llvm;
        LLVMContext& ctx = GetModule()->getContext();

        static_assert(std::is_integral_v<LiteralTy> && !std::is_same_v<LiteralTy, bool> && sizeof(LiteralTy) <= 8);
        ReleaseAssert(IntegerCanBeRepresentedIn<LiteralTy>(valueLb));
        ReleaseAssert(IntegerCanBeRepresentedIn<LiteralTy>(valueUb));
        ReleaseAssert(valueLb <= valueUb);

        ReleaseAssert(is_power_of_2(sizeInNsd) && sizeInNsd <= 8);

        size_t litOpOrd = m_extraLiteralOps.size() + GetNumOperands();
        ReleaseAssert(litOpOrd < 100);
        m_stencilRcInserter.AddRawRuntimeConstant(litOpOrd, valueLb, valueUb);

        Value* result = DeegenPlaceholderUtils::CreateConstantPlaceholderForOperand(GetModule(), litOpOrd, llvm_type_of<LiteralTy>(ctx), insertBefore);

        bool shouldSignExtend = (valueLb < 0);
        m_extraLiteralOps.push_back({
            .m_shouldSignExt = shouldSignExtend,
            .m_offsetInNsd = offsetInNsd,
            .m_sizeInNsd = sizeInNsd
        });

        // Sanity check that the NodeSpecificData representation is valid
        //
        {
            std::vector<bool> used;
            for (LiteralInfo& info : m_extraLiteralOps)
            {
                size_t offset = info.m_offsetInNsd;
                size_t size = info.m_sizeInNsd;
                if (offset + size > used.size())
                {
                    used.resize(offset + size, false /*value*/);
                }
                for (size_t k = offset; k < offset + size; k++)
                {
                    ReleaseAssert(!used[k]);
                    used[k] = true;
                }
            }
        }

        return result;
    }

public:
    // Add an literal operand that is a member of the NodeSpecificData struct
    //
    template<typename LiteralTy, auto memberObjPtr>
    llvm::Value* WARN_UNUSED AddExtraLiteralOperand(int64_t valueLb, int64_t valueUb, llvm::Instruction* insertBefore)
    {
        static_assert(std::is_member_object_pointer_v<decltype(memberObjPtr)>);
        using MemTy = typeof_member_t<memberObjPtr>;
        static_assert(std::is_integral_v<MemTy> && !std::is_same_v<MemTy, bool>);
        ReleaseAssert(!m_shouldUseCustomInterface);
        ReleaseAssert(IntegerCanBeRepresentedIn<MemTy>(valueLb));
        ReleaseAssert(IntegerCanBeRepresentedIn<MemTy>(valueUb));
        // Catch bugs where a wrong Nsd type is passed in
        //
        using ClassTy = classof_member_t<memberObjPtr>;
        ReleaseAssert(dfg::DfgNodeIsBuiltinNodeWithNsdType<ClassTy>(GetAssociatedNodeKind()));
        ReleaseAssert(!m_isSingletonLiteralOp);
        size_t offsetInNsd = offsetof_member_v<memberObjPtr>;
        size_t sizeInNsd = sizeof(MemTy);
        return AddLiteralOperandImpl<LiteralTy>(offsetInNsd, sizeInNsd, valueLb, valueUb, insertBefore);
    }

    template<typename LiteralTy, auto memberObjPtr>
    llvm::Value* WARN_UNUSED AddExtraLiteralOperand(int64_t valueLb, int64_t valueUb, llvm::BasicBlock* insertAtEnd)
    {
        using namespace llvm;
        LLVMContext& ctx = GetModule()->getContext();
        UnreachableInst* dummy = new UnreachableInst(ctx, insertAtEnd);
        Value* res = AddExtraLiteralOperand<LiteralTy, memberObjPtr>(valueLb, valueUb, dummy);
        dummy->eraseFromParent();
        return res;
    }

    // Add the only literal operand, for the simple case that the built-in node only has one literal operand
    // and it's provided directly as an integer value instead of as a struct
    //
    template<typename LiteralTy, typename StorageTy>
    llvm::Value* WARN_UNUSED AddOnlyLiteralOperand(int64_t valueLb, int64_t valueUb, llvm::Instruction* insertBefore)
    {
        static_assert(std::is_integral_v<StorageTy> && !std::is_same_v<StorageTy, bool>);
        ReleaseAssert(!m_shouldUseCustomInterface);
        ReleaseAssert(IntegerCanBeRepresentedIn<StorageTy>(valueLb));
        ReleaseAssert(IntegerCanBeRepresentedIn<StorageTy>(valueUb));
        // Catch bugs where a wrong Nsd type is passed in
        //
        ReleaseAssert(dfg::DfgNodeIsBuiltinNodeWithNsdType<StorageTy>(GetAssociatedNodeKind()));
        size_t sizeInNsd = sizeof(StorageTy);
        ReleaseAssert(m_extraLiteralOps.empty());
        ReleaseAssert(!m_isSingletonLiteralOp);
        m_isSingletonLiteralOp = true;
        return AddLiteralOperandImpl<LiteralTy>(0 /*offsetInNsd*/, sizeInNsd, valueLb, valueUb, insertBefore);
    }

    template<typename LiteralTy, typename StorageTy>
    llvm::Value* WARN_UNUSED AddOnlyLiteralOperand(int64_t valueLb, int64_t valueUb, llvm::BasicBlock* insertAtEnd)
    {
        using namespace llvm;
        LLVMContext& ctx = GetModule()->getContext();
        UnreachableInst* dummy = new UnreachableInst(ctx, insertAtEnd);
        Value* res = AddOnlyLiteralOperand<LiteralTy, StorageTy>(valueLb, valueUb, dummy);
        dummy->eraseFromParent();
        return res;
    }

    // If the codegen function uses custom interface, it does not take in the nsd struct pointer,
    // instead, it takes in a uint64_t* pointer where each value represents the value of a literal operand
    //
    template<typename LiteralTy>
    llvm::Value* WARN_UNUSED AddLiteralOperandForCustomInterface(int64_t valueLb, int64_t valueUb, llvm::Instruction* insertBefore)
    {
        ReleaseAssert(m_shouldUseCustomInterface);
        size_t offset = m_extraLiteralOps.size() * 8;
        return AddLiteralOperandImpl<LiteralTy>(offset, 8 /*sizeInNsd*/, valueLb, valueUb, insertBefore);
    }

    template<typename LiteralTy>
    llvm::Value* WARN_UNUSED AddLiteralOperandForCustomInterface(int64_t valueLb, int64_t valueUb, llvm::BasicBlock* insertAtEnd)
    {
        using namespace llvm;
        LLVMContext& ctx = GetModule()->getContext();
        UnreachableInst* dummy = new UnreachableInst(ctx, insertAtEnd);
        Value* res = AddLiteralOperandForCustomInterface<LiteralTy>(valueLb, valueUb, dummy);
        dummy->eraseFromParent();
        return res;
    }

    DfgBuiltinNodeVariantCodegenInfo& GetCodegenResult()
    {
        ReleaseAssert(m_codegenResult.m_cgMod.get() != nullptr);
        return m_codegenResult;
    }

    static constexpr const char* x_implFuncName = "deegen_builtin_node_impl";

private:
    static bool IsReasonableStorageType(llvm::Type* ty)
    {
        return llvm_type_has_type<uint64_t>(ty) ||
               llvm_type_has_type<void*>(ty) ||
               llvm_type_has_type<HeapPtr<void>>(ty) ||
               llvm_type_has_type<double>(ty);
    }

    llvm::Value* WARN_UNUSED EmitCastStorageTypeToI64(llvm::Value* value, llvm::Instruction* insertBefore)
    {
        using namespace llvm;
        ReleaseAssert(value != nullptr);
        LLVMContext& ctx = value->getContext();

        ReleaseAssert(IsReasonableStorageType(value->getType()));
        if (llvm_value_has_type<uint64_t>(value))
        {
            return value;
        }
        if (llvm_value_has_type<void*>(value) || llvm_value_has_type<HeapPtr<void>>(value))
        {
            return new PtrToIntInst(value, llvm_type_of<uint64_t>(ctx), "", insertBefore);
        }
        ReleaseAssert(llvm_value_has_type<double>(value));
        return new BitCastInst(value, llvm_type_of<uint64_t>(ctx), "", insertBefore);
    }

    void SetOperandReg(size_t opOrd, X64Reg reg)
    {
        ReleaseAssert(opOrd < GetNumOperands());
        ReleaseAssert(m_isRegAllocEnabled);
        ReleaseAssert(!m_operandsReg.count(opOrd));
        m_operandsReg[opOrd] = reg;
    }

    std::string GetCCallWrapperPrefix();

    llvm::CallInst* WARN_UNUSED CreateOrGetRuntimeConstant(llvm::Type* ty, size_t ord, int64_t lb, int64_t ub, llvm::Instruction* insertBefore);

    // The runtime constant ordinal 'ord' should be a uint64_t runtime constant that represents the stack slot ordinal
    //
    llvm::Value* WARN_UNUSED EmitGetStackSlotAddrFromRuntimeConstantOrd(size_t placeholderOrd, llvm::Instruction* insertBefore);

    struct LiteralInfo
    {
        bool m_shouldSignExt;
        size_t m_offsetInNsd;
        size_t m_sizeInNsd;
    };

    dfg::NodeKind m_associatedNodeKind;
    std::string m_nodeName;
    size_t m_numOperands;
    size_t m_variantOrd;
    bool m_shouldUseCustomInterface;
    bool m_isRegAllocEnabled;
    bool m_hasOutput;
    bool m_hasHiddenOutput;
    bool m_isSingletonLiteralOp;
    X64Reg m_outputReg;
    std::unordered_map<size_t /*operandOrd*/, X64Reg> m_operandsReg;
    std::unique_ptr<StencilRegisterFileContext> m_regPurposeCtx;
    std::unique_ptr<ExecutorFunctionContext> m_funcCtx;
    std::unique_ptr<llvm::Module> m_module;
    StencilRuntimeConstantInserter m_stencilRcInserter;
    std::vector<CPRuntimeConstantNodeBase*> m_stencilRcDefinitions;
    std::vector<DfgCCallFuncInfo> m_rtCCallWrappers;
    RegisterDemandEstimationMetric m_stencilCodeMetrics;
    DeegenStencil m_stencil;
    DfgBuiltinNodeVariantCodegenInfo m_codegenResult;
    // The placeholder oridinal for index 'idx' is always m_numOperands + idx
    //
    std::vector<LiteralInfo> m_extraLiteralOps;
};

struct DfgBuiltinNodeCodegenProcessorBase
{
    virtual ~DfgBuiltinNodeCodegenProcessorBase() = default;

    MAKE_NONCOPYABLE(DfgBuiltinNodeCodegenProcessorBase);
    MAKE_NONMOVABLE(DfgBuiltinNodeCodegenProcessorBase);

    DfgBuiltinNodeCodegenProcessorBase()
        : m_processed(false)
    { }

    virtual dfg::NodeKind AssociatedNodeKind() = 0;

    // This name is only used for distinguish purposes and becomes part of the generated C++ names
    //
    virtual std::string NodeName() { return dfg::GetDfgBuiltinNodeKindName(AssociatedNodeKind()); }

    // The total number of operands (including those not register-allocated)
    //
    virtual size_t NumOperands() = 0;

    virtual void GenerateImpl(DfgBuiltinNodeImplCreator* impl) = 0;

    // Note that using this function means that all DFG registers are invalidated after the logic!
    //
    void ProcessWithRegAllocDisabled(bool hasOutput);
    void ProcessWithRegAllocEnabled(std::unique_ptr<DfgNodeRegAllocRootInfo> rootInfo);

    bool IsProcessed() { return m_processed; }

private:
    bool m_processed;

public:
    bool m_isRegAllocEnabled;
    std::unique_ptr<DfgNodeRegAllocRootInfo> m_rootInfo;
    std::unordered_map<DfgNodeRegAllocVariant*, std::vector<std::unique_ptr<DfgNodeRegAllocSubVariant>>> m_svMap;
    std::unordered_map<DfgNodeRegAllocSubVariant*, DfgBuiltinNodeVariantCodegenInfo> m_cgInfoMap;
    std::unique_ptr<DfgBuiltinNodeVariantCodegenInfo> m_cgInfoForRegDisabled;
    std::string m_regAllocAuditLog;
    std::string m_jitCodeAuditLog;
};

}   // namespace dast
