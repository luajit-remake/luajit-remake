#pragma once

#include "common_utils.h"
#include "deegen_dfg_builtin_node_impl_creator.h"

namespace dast {

struct DfgBuiltinNodeImplConstant final : DfgBuiltinNodeCodegenProcessorBase
{
    virtual dfg::NodeKind AssociatedNodeKind() override { return dfg::NodeKind_Constant; }
    virtual size_t NumOperands() override { return 0; }

    virtual void GenerateImpl(DfgBuiltinNodeImplCreator* impl) override;

    DfgBuiltinNodeImplConstant(llvm::LLVMContext& ctx)
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

struct DfgBuiltinNodeImplUnboxedConstant final : DfgBuiltinNodeCodegenProcessorBase
{
    virtual dfg::NodeKind AssociatedNodeKind() override { return dfg::NodeKind_UnboxedConstant; }
    virtual size_t NumOperands() override { return 0; }

    virtual void GenerateImpl(DfgBuiltinNodeImplCreator* impl) override;

    DfgBuiltinNodeImplUnboxedConstant(llvm::LLVMContext& ctx)
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

struct DfgBuiltinNodeImplArgument final : DfgBuiltinNodeCodegenProcessorBase
{
    virtual dfg::NodeKind AssociatedNodeKind() override { return dfg::NodeKind_Argument; }
    virtual size_t NumOperands() override { return 0; }

    virtual void GenerateImpl(DfgBuiltinNodeImplCreator* impl) override;

    DfgBuiltinNodeImplArgument(llvm::LLVMContext& ctx)
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

struct DfgBuiltinNodeImplGetNumVariadicArgs final : DfgBuiltinNodeCodegenProcessorBase
{
    virtual dfg::NodeKind AssociatedNodeKind() override { return dfg::NodeKind_GetNumVariadicArgs; }
    virtual size_t NumOperands() override { return 0; }

    virtual void GenerateImpl(DfgBuiltinNodeImplCreator* impl) override;

    DfgBuiltinNodeImplGetNumVariadicArgs(llvm::LLVMContext& ctx)
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

struct DfgBuiltinNodeImplGetKthVariadicArg final : DfgBuiltinNodeCodegenProcessorBase
{
    virtual dfg::NodeKind AssociatedNodeKind() override { return dfg::NodeKind_GetKthVariadicArg; }
    virtual size_t NumOperands() override { return 0; }

    virtual void GenerateImpl(DfgBuiltinNodeImplCreator* impl) override;

    DfgBuiltinNodeImplGetKthVariadicArg(llvm::LLVMContext& ctx)
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

struct DfgBuiltinNodeImplGetFunctionObject final : DfgBuiltinNodeCodegenProcessorBase
{
    virtual dfg::NodeKind AssociatedNodeKind() override { return dfg::NodeKind_GetFunctionObject; }
    virtual size_t NumOperands() override { return 0; }

    virtual void GenerateImpl(DfgBuiltinNodeImplCreator* impl) override;

    DfgBuiltinNodeImplGetFunctionObject(llvm::LLVMContext& ctx)
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
// Two literal operands: #FixedVR, startAddrSlotOrd
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
    virtual size_t NumOperands() override { return 1; }

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

}   // namespace dast
