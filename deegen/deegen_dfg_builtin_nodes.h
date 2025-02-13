#pragma once

#include "common_utils.h"
#include "deegen_dfg_builtin_node_impl_creator.h"

namespace dast {

// Common abstraction for processing constant-like node
// Constant-like node always has no input operands, and produces an output operand.
// There is one special requirement: DFG backend may want us to materialize the constant into a non-reg-alloc temp register
// while preserving all reg-alloc registers. 'm_toTempReg' indicates if we are generating this special variant.
//
struct DfgConstantLikeNodeCodegenProcessorBase : DfgBuiltinNodeCodegenProcessorBase
{
    virtual std::string NodeName() override final
    {
        dfg::NodeKind nodeKind = AssociatedNodeKind();
        ReleaseAssert(dfg::IsDfgNodeKindConstantLikeNodeKind(nodeKind));
        std::string name = dfg::GetDfgBuiltinNodeKindName(nodeKind);
        if (m_toTempReg) { name += "_to_temp_reg"; }
        return name;
    }

    virtual size_t NumOperands() override final { return 0; }

    DfgConstantLikeNodeCodegenProcessorBase(bool toTempReg, llvm::LLVMContext& ctx)
        : DfgBuiltinNodeCodegenProcessorBase()
        , m_toTempReg(toTempReg)
        , m_llvmCtx(ctx)
    { }

    // Unfortunately we cannot run this in the constructor, since the derived class isn't constructed yet so we cannot call their virtual method
    // So this needs to be called from the constructor of the derived class
    //
    void ProcessNode()
    {
        std::unique_ptr<DfgNodeRegAllocRootInfo> info(new DfgNodeRegAllocRootInfo());
        if (m_toTempReg)
        {
            // The output is stored hackily into a specific non-reg-alloc reg, so no output on paper
            // Note that reg alloc is still enabled, since we must preserve all registers!
            //
            info->m_hasOutput = false;
        }
        else
        {
            info->m_hasOutput = true;
            // All constant-like nodes must be materializable in both GPR and FPR
            //
            info->m_outputInfo.m_allowGPR = true;
            info->m_outputInfo.m_allowFPR = true;
            info->m_outputInfo.m_preferGPR = true;
        }

        // This function will assert that all registers (except the output, of course) can be preserved for
        // constant-like nodes, which is expected by DFG backend
        //
        ProcessWithRegAllocEnabled(std::move(info));
    }

    // All constant-like nodes have a special variant that materializes the constant into a specific temp reg that does not
    // participate in reg alloc (currently RDX) while preserving all reg alloc registers, which is needed by our
    // codegen backend sometimes.
    //
    bool m_toTempReg;
    llvm::LLVMContext& m_llvmCtx;
};

struct DfgBuiltinNodeImplConstant final : DfgConstantLikeNodeCodegenProcessorBase
{
    virtual dfg::NodeKind AssociatedNodeKind() override { return dfg::NodeKind_Constant; }

    virtual void GenerateImpl(DfgBuiltinNodeImplCreator* impl) override;

    DfgBuiltinNodeImplConstant(bool toTempReg, llvm::LLVMContext& ctx)
        : DfgConstantLikeNodeCodegenProcessorBase(toTempReg, ctx)
    {
        ProcessNode();
    }
};

struct DfgBuiltinNodeImplUnboxedConstant final : DfgConstantLikeNodeCodegenProcessorBase
{
    virtual dfg::NodeKind AssociatedNodeKind() override { return dfg::NodeKind_UnboxedConstant; }

    virtual void GenerateImpl(DfgBuiltinNodeImplCreator* impl) override;

    DfgBuiltinNodeImplUnboxedConstant(bool toTempReg, llvm::LLVMContext& ctx)
        : DfgConstantLikeNodeCodegenProcessorBase(toTempReg, ctx)
    {
        ProcessNode();
    }
};

struct DfgBuiltinNodeImplUndefValue final : DfgConstantLikeNodeCodegenProcessorBase
{
    virtual dfg::NodeKind AssociatedNodeKind() override { return dfg::NodeKind_UndefValue; }

    virtual void GenerateImpl(DfgBuiltinNodeImplCreator* impl) override;

    DfgBuiltinNodeImplUndefValue(bool toTempReg, llvm::LLVMContext& ctx)
        : DfgConstantLikeNodeCodegenProcessorBase(toTempReg, ctx)
    {
        ProcessNode();
    }
};

struct DfgBuiltinNodeImplArgument final : DfgConstantLikeNodeCodegenProcessorBase
{
    virtual dfg::NodeKind AssociatedNodeKind() override { return dfg::NodeKind_Argument; }

    virtual void GenerateImpl(DfgBuiltinNodeImplCreator* impl) override;

    DfgBuiltinNodeImplArgument(bool toTempReg, llvm::LLVMContext& ctx)
        : DfgConstantLikeNodeCodegenProcessorBase(toTempReg, ctx)
    {
        ProcessNode();
    }
};

struct DfgBuiltinNodeImplGetNumVariadicArgs final : DfgConstantLikeNodeCodegenProcessorBase
{
    virtual dfg::NodeKind AssociatedNodeKind() override { return dfg::NodeKind_GetNumVariadicArgs; }

    virtual void GenerateImpl(DfgBuiltinNodeImplCreator* impl) override;

    DfgBuiltinNodeImplGetNumVariadicArgs(bool toTempReg, llvm::LLVMContext& ctx)
        : DfgConstantLikeNodeCodegenProcessorBase(toTempReg, ctx)
    {
        ProcessNode();
    }
};

struct DfgBuiltinNodeImplGetKthVariadicArg final : DfgConstantLikeNodeCodegenProcessorBase
{
    virtual dfg::NodeKind AssociatedNodeKind() override { return dfg::NodeKind_GetKthVariadicArg; }

    virtual void GenerateImpl(DfgBuiltinNodeImplCreator* impl) override;

    DfgBuiltinNodeImplGetKthVariadicArg(bool toTempReg, llvm::LLVMContext& ctx)
        : DfgConstantLikeNodeCodegenProcessorBase(toTempReg, ctx)
    {
        ProcessNode();
    }
};

struct DfgBuiltinNodeImplGetFunctionObject final : DfgConstantLikeNodeCodegenProcessorBase
{
    virtual dfg::NodeKind AssociatedNodeKind() override { return dfg::NodeKind_GetFunctionObject; }

    virtual void GenerateImpl(DfgBuiltinNodeImplCreator* impl) override;

    DfgBuiltinNodeImplGetFunctionObject(bool toTempReg, llvm::LLVMContext& ctx)
        : DfgConstantLikeNodeCodegenProcessorBase(toTempReg, ctx)
    {
        ProcessNode();
    }
};

struct DfgBuiltinNodeImplGetLocal final : DfgBuiltinNodeCodegenProcessorBase
{
    virtual dfg::NodeKind AssociatedNodeKind() override { return dfg::NodeKind_GetLocal; }
    virtual size_t NumOperands() override { return 0; }

    virtual void GenerateImpl(DfgBuiltinNodeImplCreator* impl) override;

    DfgBuiltinNodeImplGetLocal(llvm::LLVMContext& ctx)
        : DfgBuiltinNodeCodegenProcessorBase()
        , m_llvmCtx(ctx)
    {
        std::unique_ptr<DfgNodeRegAllocRootInfo> info(new DfgNodeRegAllocRootInfo());
        info->m_hasOutput = true;
        info->m_outputInfo.m_allowFPR = true;
        info->m_outputInfo.m_allowGPR = true;
        info->m_outputInfo.m_preferGPR = false;

        ProcessWithRegAllocEnabled(std::move(info));
    }

    llvm::LLVMContext& m_llvmCtx;
};

struct DfgBuiltinNodeImplSetLocal final : DfgBuiltinNodeCodegenProcessorBase
{
    virtual dfg::NodeKind AssociatedNodeKind() override { return dfg::NodeKind_SetLocal; }
    virtual size_t NumOperands() override { return 1; }

    virtual void GenerateImpl(DfgBuiltinNodeImplCreator* impl) override;

    DfgBuiltinNodeImplSetLocal(llvm::LLVMContext& ctx)
        : DfgBuiltinNodeCodegenProcessorBase()
        , m_llvmCtx(ctx)
    {
        std::unique_ptr<DfgNodeRegAllocRootInfo> info(new DfgNodeRegAllocRootInfo());
        info->m_hasOutput = false;

        DfgNodeRegAllocRootInfo::OpInfo opInfo;
        opInfo.m_opOrd = 0;
        opInfo.m_allowFPR = true;
        opInfo.m_allowGPR = true;
        opInfo.m_preferGPR = false;
        info->m_operandInfo.push_back(opInfo);

        ProcessWithRegAllocEnabled(std::move(info));
    }

    llvm::LLVMContext& m_llvmCtx;
};

struct DfgBuiltinNodeImplCreateCapturedVar final : DfgBuiltinNodeCodegenProcessorBase
{
    virtual dfg::NodeKind AssociatedNodeKind() override { return dfg::NodeKind_CreateCapturedVar; }
    virtual size_t NumOperands() override { return 1; }

    virtual void GenerateImpl(DfgBuiltinNodeImplCreator* impl) override;

    DfgBuiltinNodeImplCreateCapturedVar(llvm::LLVMContext& ctx)
        : DfgBuiltinNodeCodegenProcessorBase()
        , m_llvmCtx(ctx)
    {
        std::unique_ptr<DfgNodeRegAllocRootInfo> info(new DfgNodeRegAllocRootInfo());
        info->m_hasOutput = true;
        info->m_outputInfo.m_allowFPR = false;
        info->m_outputInfo.m_allowGPR = true;
        info->m_outputInfo.m_preferGPR = true;

        DfgNodeRegAllocRootInfo::OpInfo opInfo;
        opInfo.m_opOrd = 0;
        opInfo.m_allowFPR = true;
        opInfo.m_allowGPR = true;
        opInfo.m_preferGPR = false;
        info->m_operandInfo.push_back(opInfo);

        ProcessWithRegAllocEnabled(std::move(info));
    }

    llvm::LLVMContext& m_llvmCtx;
};

struct DfgBuiltinNodeImplGetCapturedVar final : DfgBuiltinNodeCodegenProcessorBase
{
    virtual dfg::NodeKind AssociatedNodeKind() override { return dfg::NodeKind_GetCapturedVar; }
    virtual size_t NumOperands() override { return 1; }

    virtual void GenerateImpl(DfgBuiltinNodeImplCreator* impl) override;

    DfgBuiltinNodeImplGetCapturedVar(llvm::LLVMContext& ctx)
        : DfgBuiltinNodeCodegenProcessorBase()
        , m_llvmCtx(ctx)
    {
        std::unique_ptr<DfgNodeRegAllocRootInfo> info(new DfgNodeRegAllocRootInfo());
        info->m_hasOutput = true;
        info->m_outputInfo.m_allowFPR = true;
        info->m_outputInfo.m_allowGPR = true;
        info->m_outputInfo.m_preferGPR = false;

        DfgNodeRegAllocRootInfo::OpInfo opInfo;
        opInfo.m_opOrd = 0;
        opInfo.m_allowFPR = false;
        opInfo.m_allowGPR = true;
        opInfo.m_preferGPR = true;
        info->m_operandInfo.push_back(opInfo);

        ProcessWithRegAllocEnabled(std::move(info));
    }

    llvm::LLVMContext& m_llvmCtx;
};

struct DfgBuiltinNodeImplSetCapturedVar final : DfgBuiltinNodeCodegenProcessorBase
{
    virtual dfg::NodeKind AssociatedNodeKind() override { return dfg::NodeKind_SetCapturedVar; }
    virtual size_t NumOperands() override { return 2; }

    virtual void GenerateImpl(DfgBuiltinNodeImplCreator* impl) override;

    DfgBuiltinNodeImplSetCapturedVar(llvm::LLVMContext& ctx)
        : DfgBuiltinNodeCodegenProcessorBase()
        , m_llvmCtx(ctx)
    {
        std::unique_ptr<DfgNodeRegAllocRootInfo> info(new DfgNodeRegAllocRootInfo());
        info->m_hasOutput = false;
        {
            DfgNodeRegAllocRootInfo::OpInfo opInfo;
            opInfo.m_opOrd = 0;
            opInfo.m_allowFPR = false;
            opInfo.m_allowGPR = true;
            opInfo.m_preferGPR = true;
            info->m_operandInfo.push_back(opInfo);
        }
        {
            DfgNodeRegAllocRootInfo::OpInfo opInfo;
            opInfo.m_opOrd = 1;
            opInfo.m_allowFPR = true;
            opInfo.m_allowGPR = true;
            opInfo.m_preferGPR = false;
            info->m_operandInfo.push_back(opInfo);
        }
        ProcessWithRegAllocEnabled(std::move(info));
    }

    llvm::LLVMContext& m_llvmCtx;
};

// TODO: reg alloc the variadic res pointer and number
//
struct DfgBuiltinNodeImplGetKthVariadicRes final : DfgBuiltinNodeCodegenProcessorBase
{
    virtual dfg::NodeKind AssociatedNodeKind() override { return dfg::NodeKind_GetKthVariadicRes; }
    virtual size_t NumOperands() override { return 0; }

    virtual void GenerateImpl(DfgBuiltinNodeImplCreator* impl) override;

    DfgBuiltinNodeImplGetKthVariadicRes(llvm::LLVMContext& ctx)
        : DfgBuiltinNodeCodegenProcessorBase()
        , m_llvmCtx(ctx)
    {
        std::unique_ptr<DfgNodeRegAllocRootInfo> info(new DfgNodeRegAllocRootInfo());
        info->m_hasOutput = true;
        info->m_outputInfo.m_allowFPR = true;
        info->m_outputInfo.m_allowGPR = true;
        info->m_outputInfo.m_preferGPR = false;

        ProcessWithRegAllocEnabled(std::move(info));
    }

    llvm::LLVMContext& m_llvmCtx;
};

struct DfgBuiltinNodeImplGetNumVariadicRes final : DfgBuiltinNodeCodegenProcessorBase
{
    virtual dfg::NodeKind AssociatedNodeKind() override { return dfg::NodeKind_GetNumVariadicRes; }
    virtual size_t NumOperands() override { return 0; }

    virtual void GenerateImpl(DfgBuiltinNodeImplCreator* impl) override;

    DfgBuiltinNodeImplGetNumVariadicRes(llvm::LLVMContext& ctx)
        : DfgBuiltinNodeCodegenProcessorBase()
        , m_llvmCtx(ctx)
    {
        std::unique_ptr<DfgNodeRegAllocRootInfo> info(new DfgNodeRegAllocRootInfo());
        info->m_hasOutput = true;
        info->m_outputInfo.m_allowFPR = false;
        info->m_outputInfo.m_allowGPR = true;
        info->m_outputInfo.m_preferGPR = true;

        ProcessWithRegAllocEnabled(std::move(info));
    }

    llvm::LLVMContext& m_llvmCtx;
};

// Store the info about the start address and number of variadic results for CreateVariadicRes
// One SSA operand: #NonFixedVR (must reg-alloc), an unboxed uint64_t
// One literal operands: #FixedVR
// Return the start address of the variadic results
//
struct DfgBuiltinNodeImplCreateVariadicRes_StoreInfo final : DfgBuiltinNodeCodegenProcessorBase
{
    virtual dfg::NodeKind AssociatedNodeKind() override { return dfg::NodeKind_CreateVariadicRes; }
    virtual std::string NodeName() override { return "CreateVariadicRes_StoreInfo"; }
    virtual size_t NumOperands() override { return 1; }

    virtual void GenerateImpl(DfgBuiltinNodeImplCreator* impl) override;

    DfgBuiltinNodeImplCreateVariadicRes_StoreInfo(llvm::LLVMContext& ctx)
        : DfgBuiltinNodeCodegenProcessorBase()
        , m_llvmCtx(ctx)
    {
        std::unique_ptr<DfgNodeRegAllocRootInfo> info(new DfgNodeRegAllocRootInfo());
        info->m_hasOutput = true;
        info->m_outputInfo.m_allowFPR = false;
        info->m_outputInfo.m_allowGPR = true;
        info->m_outputInfo.m_preferGPR = true;

        DfgNodeRegAllocRootInfo::OpInfo opInfo;
        opInfo.m_opOrd = 0;
        opInfo.m_allowFPR = false;
        opInfo.m_allowGPR = true;
        opInfo.m_preferGPR = true;
        info->m_operandInfo.push_back(opInfo);

        ProcessWithRegAllocEnabled(std::move(info));
    }

    llvm::LLVMContext& m_llvmCtx;
};

// Store the info about the start address and number of variadic results for PrependVariadicRes
// No SSA operand, three literal operand: #ExtraVals, #TotalLocals, #ExtraVals+#TotalLocals
//    (We have to pass in the sum explicitly as another operand as LLVM otherwise generates slightly bloated code..)
// Move the current variadic result if needed, to make space for preprending #ExtraVals.
//    (Note that if there is already enough space because current VRStart >= StackBase + #TotalLocals + #ExtraVals, we don't need to move).
// Updates the variadic result info to reflect the new variadic results
// Returns the pointer to the new variadic results
//
struct DfgBuiltinNodeImplPrependVariadicRes_MoveAndStoreInfo final : DfgBuiltinNodeCodegenProcessorBase
{
    virtual dfg::NodeKind AssociatedNodeKind() override { return dfg::NodeKind_PrependVariadicRes; }
    virtual std::string NodeName() override { return "PrependVariadicRes_MoveAndStoreInfo"; }
    virtual size_t NumOperands() override { return 0; }

    virtual void GenerateImpl(DfgBuiltinNodeImplCreator* impl) override;

    DfgBuiltinNodeImplPrependVariadicRes_MoveAndStoreInfo(llvm::LLVMContext& ctx)
        : DfgBuiltinNodeCodegenProcessorBase()
          , m_llvmCtx(ctx)
    {
        std::unique_ptr<DfgNodeRegAllocRootInfo> info(new DfgNodeRegAllocRootInfo());
        info->m_hasOutput = true;
        info->m_outputInfo.m_allowFPR = false;
        info->m_outputInfo.m_allowGPR = true;
        info->m_outputInfo.m_preferGPR = true;

        ProcessWithRegAllocEnabled(std::move(info));
    }

    llvm::LLVMContext& m_llvmCtx;
};

struct DfgBuiltinNodeImplCheckU64InBound final : DfgBuiltinNodeCodegenProcessorBase
{
    virtual dfg::NodeKind AssociatedNodeKind() override { return dfg::NodeKind_CheckU64InBound; }
    virtual size_t NumOperands() override { return 1; }

    virtual void GenerateImpl(DfgBuiltinNodeImplCreator* impl) override;

    DfgBuiltinNodeImplCheckU64InBound(llvm::LLVMContext& ctx)
        : DfgBuiltinNodeCodegenProcessorBase()
        , m_llvmCtx(ctx)
    {
        std::unique_ptr<DfgNodeRegAllocRootInfo> info(new DfgNodeRegAllocRootInfo());
        info->m_hasOutput = false;

        DfgNodeRegAllocRootInfo::OpInfo opInfo;
        opInfo.m_opOrd = 0;
        opInfo.m_allowFPR = false;
        opInfo.m_allowGPR = true;
        opInfo.m_preferGPR = true;
        info->m_operandInfo.push_back(opInfo);

        ProcessWithRegAllocEnabled(std::move(info));
    }

    llvm::LLVMContext& m_llvmCtx;
};

struct DfgBuiltinNodeImplI64SubSaturateToZero final : DfgBuiltinNodeCodegenProcessorBase
{
    virtual dfg::NodeKind AssociatedNodeKind() override { return dfg::NodeKind_I64SubSaturateToZero; }
    virtual size_t NumOperands() override { return 1; }

    virtual void GenerateImpl(DfgBuiltinNodeImplCreator* impl) override;

    DfgBuiltinNodeImplI64SubSaturateToZero(llvm::LLVMContext& ctx)
        : DfgBuiltinNodeCodegenProcessorBase()
        , m_llvmCtx(ctx)
    {
        std::unique_ptr<DfgNodeRegAllocRootInfo> info(new DfgNodeRegAllocRootInfo());
        info->m_hasOutput = true;
        info->m_outputInfo.m_allowFPR = false;
        info->m_outputInfo.m_allowGPR = true;
        info->m_outputInfo.m_preferGPR = true;

        DfgNodeRegAllocRootInfo::OpInfo opInfo;
        opInfo.m_opOrd = 0;
        opInfo.m_allowFPR = false;
        opInfo.m_allowGPR = true;
        opInfo.m_preferGPR = true;
        info->m_operandInfo.push_back(opInfo);

        ProcessWithRegAllocEnabled(std::move(info));
    }

    llvm::LLVMContext& m_llvmCtx;
};

// Allocate the function object and populate upvalues that comes from the enclosing function's upvalues
// Two SSA operands: the UnlinkedCodeBlock (raw pointer), and the parent function object (HeapPtr)
// One return value: the newly created function object (raw pointer)
// This function disables reg alloc, but the return value is always returned in x_dfg_reg_alloc_gprs[0]
//
struct DfgBuiltinNodeImplCreateFunctionObject_AllocAndSetup final : DfgBuiltinNodeCodegenProcessorBase
{
    virtual dfg::NodeKind AssociatedNodeKind() override { return dfg::NodeKind_CreateFunctionObject; }
    virtual std::string NodeName() override { return "CreateFunctionObject_AllocAndSetup"; }
    virtual size_t NumOperands() override { return 2; }

    virtual void GenerateImpl(DfgBuiltinNodeImplCreator* impl) override;

    DfgBuiltinNodeImplCreateFunctionObject_AllocAndSetup(llvm::LLVMContext& ctx)
        : DfgBuiltinNodeCodegenProcessorBase()
        , m_llvmCtx(ctx)
    {
        // hasOutput is false because we hackily return the result in a reg
        //
        ProcessWithRegAllocDisabled(false /*hasOutput*/);
    }

    llvm::LLVMContext& m_llvmCtx;
};

// Box the raw function object pointer to a TValue
// One SSA operand: the function object (raw pointer)
// One return value: the boxed TValue
//
struct DfgBuiltinNodeImplCreateFunctionObject_BoxFunctionObject final : DfgBuiltinNodeCodegenProcessorBase
{
    virtual dfg::NodeKind AssociatedNodeKind() override { return dfg::NodeKind_CreateFunctionObject; }
    virtual std::string NodeName() override { return "CreateFunctionObject_BoxFunctionObject"; }
    virtual size_t NumOperands() override { return 1; }

    virtual void GenerateImpl(DfgBuiltinNodeImplCreator* impl) override;

    DfgBuiltinNodeImplCreateFunctionObject_BoxFunctionObject(llvm::LLVMContext& ctx)
        : DfgBuiltinNodeCodegenProcessorBase()
        , m_llvmCtx(ctx)
    {
        std::unique_ptr<DfgNodeRegAllocRootInfo> info(new DfgNodeRegAllocRootInfo());
        info->m_hasOutput = true;
        info->m_outputInfo.m_allowFPR = false;
        info->m_outputInfo.m_allowGPR = true;
        info->m_outputInfo.m_preferGPR = true;

        DfgNodeRegAllocRootInfo::OpInfo opInfo;
        opInfo.m_opOrd = 0;
        opInfo.m_allowFPR = false;
        opInfo.m_allowGPR = true;
        opInfo.m_preferGPR = true;
        info->m_operandInfo.push_back(opInfo);

        ProcessWithRegAllocEnabled(std::move(info));
    }

    llvm::LLVMContext& m_llvmCtx;
};

// Box the raw function object pointer to a TValue, and write the self reference into the corresponding upvalue location
// One SSA operand: the function object (raw pointer)
// One literal operand: the *byte* offset of the address of the upvalue from the function object pointer
// One return value: the boxed TValue
//
// This logic fragment is used to simplify some temporary value liveness management issue in the backend
// (without this, the backend will need to keep the raw function object pointer live after boxing to write the self-reference upvalue)
//
struct DfgBuiltinNodeImplCreateFunctionObject_BoxFnObjAndWriteSelfRefUv final : DfgBuiltinNodeCodegenProcessorBase
{
    virtual dfg::NodeKind AssociatedNodeKind() override { return dfg::NodeKind_CreateFunctionObject; }
    virtual std::string NodeName() override { return "CreateFunctionObject_BoxFnObjAndWriteSelfRefUv"; }
    virtual size_t NumOperands() override { return 1; }

    virtual void GenerateImpl(DfgBuiltinNodeImplCreator* impl) override;

    DfgBuiltinNodeImplCreateFunctionObject_BoxFnObjAndWriteSelfRefUv(llvm::LLVMContext& ctx)
        : DfgBuiltinNodeCodegenProcessorBase()
        , m_llvmCtx(ctx)
    {
        std::unique_ptr<DfgNodeRegAllocRootInfo> info(new DfgNodeRegAllocRootInfo());
        info->m_hasOutput = true;
        info->m_outputInfo.m_allowFPR = false;
        info->m_outputInfo.m_allowGPR = true;
        info->m_outputInfo.m_preferGPR = true;

        DfgNodeRegAllocRootInfo::OpInfo opInfo;
        opInfo.m_opOrd = 0;
        opInfo.m_allowFPR = false;
        opInfo.m_allowGPR = true;
        opInfo.m_preferGPR = true;
        info->m_operandInfo.push_back(opInfo);

        ProcessWithRegAllocEnabled(std::move(info));
    }

    llvm::LLVMContext& m_llvmCtx;
};

struct DfgBuiltinNodeImplGetImmutableUpvalue final : DfgBuiltinNodeCodegenProcessorBase
{
    virtual dfg::NodeKind AssociatedNodeKind() override { return dfg::NodeKind_GetUpvalueImmutable; }
    virtual size_t NumOperands() override { return 1; }

    virtual void GenerateImpl(DfgBuiltinNodeImplCreator* impl) override;

    DfgBuiltinNodeImplGetImmutableUpvalue(llvm::LLVMContext& ctx)
        : DfgBuiltinNodeCodegenProcessorBase()
        , m_llvmCtx(ctx)
    {
        std::unique_ptr<DfgNodeRegAllocRootInfo> info(new DfgNodeRegAllocRootInfo());
        info->m_hasOutput = true;
        info->m_outputInfo.m_allowFPR = true;
        info->m_outputInfo.m_allowGPR = true;
        info->m_outputInfo.m_preferGPR = false;

        DfgNodeRegAllocRootInfo::OpInfo opInfo;
        opInfo.m_opOrd = 0;
        opInfo.m_allowFPR = false;
        opInfo.m_allowGPR = true;
        opInfo.m_preferGPR = true;
        info->m_operandInfo.push_back(opInfo);

        ProcessWithRegAllocEnabled(std::move(info));
    }

    llvm::LLVMContext& m_llvmCtx;
};

struct DfgBuiltinNodeImplGetMutableUpvalue final : DfgBuiltinNodeCodegenProcessorBase
{
    virtual dfg::NodeKind AssociatedNodeKind() override { return dfg::NodeKind_GetUpvalueMutable; }
    virtual size_t NumOperands() override { return 1; }

    virtual void GenerateImpl(DfgBuiltinNodeImplCreator* impl) override;

    DfgBuiltinNodeImplGetMutableUpvalue(llvm::LLVMContext& ctx)
        : DfgBuiltinNodeCodegenProcessorBase()
        , m_llvmCtx(ctx)
    {
        std::unique_ptr<DfgNodeRegAllocRootInfo> info(new DfgNodeRegAllocRootInfo());
        info->m_hasOutput = true;
        info->m_outputInfo.m_allowFPR = true;
        info->m_outputInfo.m_allowGPR = true;
        info->m_outputInfo.m_preferGPR = false;

        DfgNodeRegAllocRootInfo::OpInfo opInfo;
        opInfo.m_opOrd = 0;
        opInfo.m_allowFPR = false;
        opInfo.m_allowGPR = true;
        opInfo.m_preferGPR = true;
        info->m_operandInfo.push_back(opInfo);

        ProcessWithRegAllocEnabled(std::move(info));
    }

    llvm::LLVMContext& m_llvmCtx;
};

struct DfgBuiltinNodeImplSetUpvalue final : DfgBuiltinNodeCodegenProcessorBase
{
    virtual dfg::NodeKind AssociatedNodeKind() override { return dfg::NodeKind_SetUpvalue; }
    virtual size_t NumOperands() override { return 2; }

    virtual void GenerateImpl(DfgBuiltinNodeImplCreator* impl) override;

    DfgBuiltinNodeImplSetUpvalue(llvm::LLVMContext& ctx)
        : DfgBuiltinNodeCodegenProcessorBase()
        , m_llvmCtx(ctx)
    {
        std::unique_ptr<DfgNodeRegAllocRootInfo> info(new DfgNodeRegAllocRootInfo());
        info->m_hasOutput = false;

        {
            DfgNodeRegAllocRootInfo::OpInfo opInfo;
            opInfo.m_opOrd = 0;
            opInfo.m_allowFPR = false;
            opInfo.m_allowGPR = true;
            opInfo.m_preferGPR = true;
            info->m_operandInfo.push_back(opInfo);
        }
        {
            DfgNodeRegAllocRootInfo::OpInfo opInfo;
            opInfo.m_opOrd = 1;
            opInfo.m_allowFPR = true;
            opInfo.m_allowGPR = true;
            opInfo.m_preferGPR = false;
            info->m_operandInfo.push_back(opInfo);
        }

        ProcessWithRegAllocEnabled(std::move(info));
    }

    llvm::LLVMContext& m_llvmCtx;
};

// Very similar to PreprendVariadicResult, except for return
// No SSA operand, three literal operand: #ExtraVals, #TotalLocals, #ExtraVals+#TotalLocals
//    (We have to pass in the sum explicitly as another operand as LLVM otherwise generates slightly bloated code..)
// Move the current variadic result if needed, to make space for preprending #ExtraVals before it.
//    (Note that if there is already enough space because current VRStart >= StackBase + #TotalLocals + #ExtraVals, we don't need to move).
// Also, nils are appended at the end to satisfy our internal ABI that nil must be appended if there's less than x_minNilFillReturnValues ret values.
// Returns the pointer to the start of the return region
//
struct DfgBuiltinNodeImplReturn_MoveVariadicRes final : DfgBuiltinNodeCodegenProcessorBase
{
    virtual dfg::NodeKind AssociatedNodeKind() override { return dfg::NodeKind_Return; }
    virtual std::string NodeName() override { return "Return_MoveVariadicRes"; }
    virtual size_t NumOperands() override { return 0; }

    virtual void GenerateImpl(DfgBuiltinNodeImplCreator* impl) override;

    DfgBuiltinNodeImplReturn_MoveVariadicRes(llvm::LLVMContext& ctx)
        : DfgBuiltinNodeCodegenProcessorBase()
        , m_llvmCtx(ctx)
    {
        std::unique_ptr<DfgNodeRegAllocRootInfo> info(new DfgNodeRegAllocRootInfo());
        info->m_hasOutput = true;
        info->m_outputInfo.m_allowFPR = false;
        info->m_outputInfo.m_allowGPR = true;
        info->m_outputInfo.m_preferGPR = true;

        ProcessWithRegAllocEnabled(std::move(info));
    }

    llvm::LLVMContext& m_llvmCtx;
};

// The actual return logic with variadic results
// (note that Return_MoveVariadicRes already stores the retStart and numRets as variadic results)
//
struct DfgBuiltinNodeImplReturn_RetWithVariadicRes final : DfgBuiltinNodeCodegenProcessorBase
{
    virtual dfg::NodeKind AssociatedNodeKind() override { return dfg::NodeKind_Return; }
    virtual std::string NodeName() override { return "Return_RetWithVariadicRes"; }
    virtual size_t NumOperands() override { return 0; }

    virtual void GenerateImpl(DfgBuiltinNodeImplCreator* impl) override;

    DfgBuiltinNodeImplReturn_RetWithVariadicRes(llvm::LLVMContext& ctx)
        : DfgBuiltinNodeCodegenProcessorBase()
        , m_llvmCtx(ctx)
    {
        ProcessWithRegAllocDisabled(false /*hasOutput*/);
    }

    llvm::LLVMContext& m_llvmCtx;
};

struct DfgBuiltinNodeImplReturn_WriteNil final : DfgBuiltinNodeCodegenProcessorBase
{
    virtual dfg::NodeKind AssociatedNodeKind() override { return dfg::NodeKind_Return; }
    virtual std::string NodeName() override { return "Return_WriteNil"; }
    virtual size_t NumOperands() override { return 0; }

    virtual void GenerateImpl(DfgBuiltinNodeImplCreator* impl) override;

    DfgBuiltinNodeImplReturn_WriteNil(llvm::LLVMContext& ctx)
        : DfgBuiltinNodeCodegenProcessorBase()
        , m_llvmCtx(ctx)
    {
        std::unique_ptr<DfgNodeRegAllocRootInfo> info(new DfgNodeRegAllocRootInfo());
        info->m_hasOutput = false;

        ProcessWithRegAllocEnabled(std::move(info));
    }

    llvm::LLVMContext& m_llvmCtx;
};

// The actual return logic, no variadic results
// No SSA operand, two literal operands: the slotOrd for return values, # of return values
//
struct DfgBuiltinNodeImplReturn_RetNoVariadicRes final : DfgBuiltinNodeCodegenProcessorBase
{
    virtual dfg::NodeKind AssociatedNodeKind() override { return dfg::NodeKind_Return; }
    virtual std::string NodeName() override { return "Return_RetNoVariadicRes"; }
    virtual size_t NumOperands() override { return 0; }

    virtual void GenerateImpl(DfgBuiltinNodeImplCreator* impl) override;

    DfgBuiltinNodeImplReturn_RetNoVariadicRes(llvm::LLVMContext& ctx)
        : DfgBuiltinNodeCodegenProcessorBase()
        , m_llvmCtx(ctx)
    {
        ProcessWithRegAllocDisabled(false /*hasOutput*/);
    }

    llvm::LLVMContext& m_llvmCtx;
};

// A specialized common case: return one return value
// No SSA operand, one literal operands: the slotOrd for return value
//
struct DfgBuiltinNodeImplReturn_Ret1 final : DfgBuiltinNodeCodegenProcessorBase
{
    virtual dfg::NodeKind AssociatedNodeKind() override { return dfg::NodeKind_Return; }
    virtual std::string NodeName() override { return "Return_Ret1"; }
    virtual size_t NumOperands() override { return 0; }

    virtual void GenerateImpl(DfgBuiltinNodeImplCreator* impl) override;

    DfgBuiltinNodeImplReturn_Ret1(llvm::LLVMContext& ctx)
        : DfgBuiltinNodeCodegenProcessorBase()
        , m_llvmCtx(ctx)
    {
        ProcessWithRegAllocDisabled(false /*hasOutput*/);
    }

    llvm::LLVMContext& m_llvmCtx;
};

// A specialized common case: return no return value
// No SSA operand, no literal operand
//
struct DfgBuiltinNodeImplReturn_Ret0 final : DfgBuiltinNodeCodegenProcessorBase
{
    virtual dfg::NodeKind AssociatedNodeKind() override { return dfg::NodeKind_Return; }
    virtual std::string NodeName() override { return "Return_Ret0"; }
    virtual size_t NumOperands() override { return 0; }

    virtual void GenerateImpl(DfgBuiltinNodeImplCreator* impl) override;

    DfgBuiltinNodeImplReturn_Ret0(llvm::LLVMContext& ctx)
        : DfgBuiltinNodeCodegenProcessorBase()
        , m_llvmCtx(ctx)
    {
        ProcessWithRegAllocDisabled(false /*hasOutput*/);
    }

    llvm::LLVMContext& m_llvmCtx;
};

// Generate a type check implementation
//
struct DfgBuiltinNodeImplTypeCheck final : DfgBuiltinNodeCodegenProcessorBase
{
    // TypeCheck nodes identify themselves by NodeKind_FirstAvailableGuestLanguageNodeKind
    //
    virtual dfg::NodeKind AssociatedNodeKind() override { return dfg::NodeKind_FirstAvailableGuestLanguageNodeKind; }

    virtual std::string NodeName() override
    {
        return "TypeCheck_" + std::to_string(m_checkMask.m_mask) + "_precond_" + std::to_string(m_precondMask.m_mask) + (m_shouldFlipResult ? "_flip" : "");
    }

    virtual size_t NumOperands() override { return 1; }

    virtual void GenerateImpl(DfgBuiltinNodeImplCreator* impl) override;

    DfgBuiltinNodeImplTypeCheck(TypeMask checkMask,
                                TypeMask precondMask,
                                bool shouldFlipResult,
                                bool allowGPR,
                                bool allowFPR,
                                llvm::Module* module,
                                std::string implFuncName)
        : DfgBuiltinNodeCodegenProcessorBase()
        , m_checkMask(checkMask)
        , m_precondMask(precondMask)
        , m_shouldFlipResult(shouldFlipResult)
        , m_srcModule(module)
        , m_implFuncName(implFuncName)
    {
        ReleaseAssert(m_precondMask.SupersetOf(m_checkMask));

        std::unique_ptr<DfgNodeRegAllocRootInfo> info(new DfgNodeRegAllocRootInfo());
        info->m_hasOutput = false;

        {
            DfgNodeRegAllocRootInfo::OpInfo opInfo;
            opInfo.m_opOrd = 0;
            opInfo.m_allowFPR = allowFPR;
            opInfo.m_allowGPR = allowGPR;
            opInfo.m_preferGPR = allowGPR;
            info->m_operandInfo.push_back(opInfo);
        }

        ProcessWithRegAllocEnabled(std::move(info));
    }

    TypeMask m_checkMask;
    TypeMask m_precondMask;
    bool m_shouldFlipResult;
    llvm::Module* m_srcModule;
    std::string m_implFuncName;
};

}   // namespace dast
