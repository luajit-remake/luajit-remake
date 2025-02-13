#include "deegen_dfg_builtin_nodes.h"
#include "drt/dfg_builtin_nodes.h"
#include "deegen_bytecode_operand.h"
#include "drt/dfg_reg_alloc_register_info.h"

namespace dast {

using namespace dfg;

void DfgBuiltinNodeImplConstant::GenerateImpl(DfgBuiltinNodeImplCreator* impl)
{
    using namespace llvm;
    LLVMContext& ctx = m_llvmCtx;

    using NsdTy = dfg_builtin_node_nsd_t<NodeKind_Constant>;

    ExecutorFunctionContext* funcCtx = impl->CreateFunction(ctx);
    Function* func = funcCtx->GetFunction();
    BasicBlock* bb = BasicBlock::Create(ctx, "", func);

    Value* dfgCb = funcCtx->GetValueAtEntry<RPV_CodeBlock>();
    Value* slotOrd = impl->AddOnlyLiteralOperand<int64_t /*valueTy*/, NsdTy /*storageTy*/>(
        BcOpConstant::x_constantTableOrdLowerBound /*valueLb*/, -1 /*valueUb*/, bb);

    Value* bvPtr = GetElementPtrInst::CreateInBounds(llvm_type_of<uint64_t>(ctx), dfgCb, { slotOrd }, "", bb);
    LoadInst* bv = new LoadInst(llvm_type_of<uint64_t>(ctx), bvPtr, "", bb);
    bv->setAlignment(Align(8));

    ReleaseAssert(!JitGeneratedCodeInterface().IsRegisterUsedInInterface(x_dfg_custom_purpose_temp_reg));

    if (m_toTempReg)
    {
        CallInst* ci = impl->CreateDispatchToFallthrough(nullptr /*outputVal*/, bb);
        RegisterPinningScheme::SetExtraDispatchArgumentWithCastFromI64(ci, x_dfg_custom_purpose_temp_reg, bv);
    }
    else
    {
        impl->CreateDispatchToFallthrough(bv /*outputVal*/, bb);
    }
}

void DfgBuiltinNodeImplUnboxedConstant::GenerateImpl(DfgBuiltinNodeImplCreator* impl)
{
    using namespace llvm;
    LLVMContext& ctx = m_llvmCtx;

    using NsdTy = dfg_builtin_node_nsd_t<NodeKind_UnboxedConstant>;

    ExecutorFunctionContext* funcCtx = impl->CreateFunction(ctx);
    Function* func = funcCtx->GetFunction();
    BasicBlock* bb = BasicBlock::Create(ctx, "", func);

    Value* dfgCb = funcCtx->GetValueAtEntry<RPV_CodeBlock>();
    Value* slotOrd = impl->AddOnlyLiteralOperand<int64_t /*valueTy*/, NsdTy /*storageTy*/>(
        BcOpConstant::x_constantTableOrdLowerBound /*valueLb*/, -1 /*valueUb*/, bb);

    Value* bvPtr = GetElementPtrInst::CreateInBounds(llvm_type_of<uint64_t>(ctx), dfgCb, { slotOrd }, "", bb);
    LoadInst* bv = new LoadInst(llvm_type_of<uint64_t>(ctx), bvPtr, "", bb);
    bv->setAlignment(Align(8));

    if (m_toTempReg)
    {
        CallInst* ci = impl->CreateDispatchToFallthrough(nullptr /*outputVal*/, bb);
        RegisterPinningScheme::SetExtraDispatchArgumentWithCastFromI64(ci, x_dfg_custom_purpose_temp_reg, bv);
    }
    else
    {
        impl->CreateDispatchToFallthrough(bv /*outputVal*/, bb);
    }
}

void DfgBuiltinNodeImplUndefValue::GenerateImpl(DfgBuiltinNodeImplCreator* impl)
{
    using namespace llvm;
    LLVMContext& ctx = m_llvmCtx;

    ExecutorFunctionContext* funcCtx = impl->CreateFunction(ctx);
    Function* func = funcCtx->GetFunction();
    BasicBlock* bb = BasicBlock::Create(ctx, "", func);

    // UndefValue always becomes nil in JIT code
    //
    TValue nilVal = TValue::Create<tNil>();
    Value* outputVal = CreateLLVMConstantInt<uint64_t>(ctx, nilVal.m_value);

    if (m_toTempReg)
    {
        CallInst* ci = impl->CreateDispatchToFallthrough(nullptr /*outputVal*/, bb);
        RegisterPinningScheme::SetExtraDispatchArgumentWithCastFromI64(ci, x_dfg_custom_purpose_temp_reg, outputVal);
    }
    else
    {
        impl->CreateDispatchToFallthrough(outputVal /*outputVal*/, bb);
    }
}

void DfgBuiltinNodeImplArgument::GenerateImpl(DfgBuiltinNodeImplCreator* impl)
{
    using namespace llvm;
    LLVMContext& ctx = m_llvmCtx;

    using NsdTy = dfg_builtin_node_nsd_t<NodeKind_Argument>;

    ExecutorFunctionContext* funcCtx = impl->CreateFunction(ctx);
    Function* func = funcCtx->GetFunction();
    BasicBlock* bb = BasicBlock::Create(ctx, "", func);

    Value* stackBase = funcCtx->GetValueAtEntry<RPV_StackBase>();
    Value* argOrd = impl->AddOnlyLiteralOperand<uint64_t /*valueTy*/, NsdTy /*storageTy*/>(
        0 /*valueLb*/, BcOpSlot::x_localOrdinalUpperBound, bb);

    // Argument k is always at slot k in the DFG stack frame
    //
    Value* bvPtr = GetElementPtrInst::CreateInBounds(llvm_type_of<uint64_t>(ctx), stackBase, { argOrd }, "", bb);
    LoadInst* bv = new LoadInst(llvm_type_of<uint64_t>(ctx), bvPtr, "", bb);
    bv->setAlignment(Align(8));

    if (m_toTempReg)
    {
        CallInst* ci = impl->CreateDispatchToFallthrough(nullptr /*outputVal*/, bb);
        RegisterPinningScheme::SetExtraDispatchArgumentWithCastFromI64(ci, x_dfg_custom_purpose_temp_reg, bv);
    }
    else
    {
        impl->CreateDispatchToFallthrough(bv /*outputVal*/, bb);
    }
}

void DfgBuiltinNodeImplGetNumVariadicArgs::GenerateImpl(DfgBuiltinNodeImplCreator* impl)
{
    using namespace llvm;
    LLVMContext& ctx = m_llvmCtx;

    ExecutorFunctionContext* funcCtx = impl->CreateFunction(ctx);
    Function* func = funcCtx->GetFunction();
    BasicBlock* bb = BasicBlock::Create(ctx, "", func);

    Value* stackBase = funcCtx->GetValueAtEntry<RPV_StackBase>();

    Value* result = CreateCallToDeegenCommonSnippet(impl->GetModule(), "GetNumVariadicArgs", { stackBase }, bb);
    ReleaseAssert(llvm_value_has_type<uint64_t>(result));

    if (m_toTempReg)
    {
        CallInst* ci = impl->CreateDispatchToFallthrough(nullptr /*outputVal*/, bb);
        RegisterPinningScheme::SetExtraDispatchArgumentWithCastFromI64(ci, x_dfg_custom_purpose_temp_reg, result);
    }
    else
    {
        impl->CreateDispatchToFallthrough(result /*outputVal*/, bb);
    }
}

void DfgBuiltinNodeImplGetKthVariadicArg::GenerateImpl(DfgBuiltinNodeImplCreator* impl)
{
    using namespace llvm;
    LLVMContext& ctx = m_llvmCtx;

    using NsdTy = dfg_builtin_node_nsd_t<NodeKind_GetKthVariadicArg>;

    ExecutorFunctionContext* funcCtx = impl->CreateFunction(ctx);
    Function* func = funcCtx->GetFunction();
    BasicBlock* bb = BasicBlock::Create(ctx, "", func);

    Value* stackBase = funcCtx->GetValueAtEntry<RPV_StackBase>();
    Value* argOrd = impl->AddOnlyLiteralOperand<uint64_t /*valueTy*/, NsdTy /*storageTy*/>(
        0 /*valueLb*/, BcOpSlot::x_localOrdinalUpperBound, bb);

    Value* result = CreateCallToDeegenCommonSnippet(impl->GetModule(), "GetKthVariadicArg", { stackBase, argOrd }, bb);
    ReleaseAssert(llvm_value_has_type<uint64_t>(result));

    if (m_toTempReg)
    {
        CallInst* ci = impl->CreateDispatchToFallthrough(nullptr /*outputVal*/, bb);
        RegisterPinningScheme::SetExtraDispatchArgumentWithCastFromI64(ci, x_dfg_custom_purpose_temp_reg, result);
    }
    else
    {
        impl->CreateDispatchToFallthrough(result /*outputVal*/, bb);
    }
}

void DfgBuiltinNodeImplGetFunctionObject::GenerateImpl(DfgBuiltinNodeImplCreator* impl)
{
    using namespace llvm;
    LLVMContext& ctx = m_llvmCtx;

    ExecutorFunctionContext* funcCtx = impl->CreateFunction(ctx);
    Function* func = funcCtx->GetFunction();
    BasicBlock* bb = BasicBlock::Create(ctx, "", func);

    Value* stackBase = funcCtx->GetValueAtEntry<RPV_StackBase>();

    Value* result = CreateCallToDeegenCommonSnippet(impl->GetModule(), "GetFunctionObjectHeapPtrFromStackBase", { stackBase }, bb);
    ReleaseAssert(llvm_value_has_type<HeapPtr<void>>(result));

    result = new PtrToIntInst(result, llvm_type_of<uint64_t>(ctx), "", bb);

    if (m_toTempReg)
    {
        CallInst* ci = impl->CreateDispatchToFallthrough(nullptr /*outputVal*/, bb);
        RegisterPinningScheme::SetExtraDispatchArgumentWithCastFromI64(ci, x_dfg_custom_purpose_temp_reg, result);
    }
    else
    {
        impl->CreateDispatchToFallthrough(result /*outputVal*/, bb);
    }
}

void DfgBuiltinNodeImplGetLocal::GenerateImpl(DfgBuiltinNodeImplCreator* impl)
{
    using namespace llvm;
    LLVMContext& ctx = m_llvmCtx;

    using NsdTy = dfg_builtin_node_nsd_t<NodeKind_GetLocal>;

    ExecutorFunctionContext* funcCtx = impl->CreateFunction(ctx);
    Function* func = funcCtx->GetFunction();
    BasicBlock* bb = BasicBlock::Create(ctx, "", func);

    Value* stackBase = funcCtx->GetValueAtEntry<RPV_StackBase>();
    Value* slotOrd = impl->AddOnlyLiteralOperand<uint64_t /*valueTy*/, NsdTy /*storageTy*/>(
        0 /*valueLb*/, BcOpSlot::x_localOrdinalUpperBound, bb);

    Value* bvPtr = GetElementPtrInst::CreateInBounds(llvm_type_of<uint64_t>(ctx), stackBase, { slotOrd }, "", bb);
    LoadInst* bv = new LoadInst(llvm_type_of<uint64_t>(ctx), bvPtr, "", bb);
    bv->setAlignment(Align(8));

    impl->CreateDispatchToFallthrough(bv /*outputVal*/, bb);
}

void DfgBuiltinNodeImplSetLocal::GenerateImpl(DfgBuiltinNodeImplCreator* impl)
{
    using namespace llvm;
    LLVMContext& ctx = m_llvmCtx;

    using NsdTy = dfg_builtin_node_nsd_t<NodeKind_SetLocal>;

    ExecutorFunctionContext* funcCtx = impl->CreateFunction(ctx);
    Function* func = funcCtx->GetFunction();
    BasicBlock* bb = BasicBlock::Create(ctx, "", func);

    Value* valToStore = impl->EmitGetOperand(llvm_type_of<uint64_t>(ctx), 0 /*opOrd*/, bb);

    Value* stackBase = funcCtx->GetValueAtEntry<RPV_StackBase>();
    Value* slotOrd = impl->AddOnlyLiteralOperand<uint64_t /*valueTy*/, NsdTy /*storageTy*/>(
        0 /*valueLb*/, BcOpSlot::x_localOrdinalUpperBound, bb);

    Value* bvPtr = GetElementPtrInst::CreateInBounds(llvm_type_of<uint64_t>(ctx), stackBase, { slotOrd }, "", bb);
    new StoreInst(valToStore, bvPtr, false /*isVolatile*/, Align(8), bb);

    impl->CreateDispatchToFallthrough(nullptr /*outputVal*/, bb);
}

void DfgBuiltinNodeImplCreateCapturedVar::GenerateImpl(DfgBuiltinNodeImplCreator* impl)
{
    using namespace llvm;
    LLVMContext& ctx = m_llvmCtx;

    ExecutorFunctionContext* funcCtx = impl->CreateFunction(ctx);
    Function* func = funcCtx->GetFunction();
    BasicBlock* bb = BasicBlock::Create(ctx, "", func);

    Value* val = impl->EmitGetOperand(llvm_type_of<uint64_t>(ctx), 0 /*opOrd*/, bb);

    Value* result = CreateCallToDeegenCommonSnippet(impl->GetModule(), "CreateClosedUpvalue", { val }, bb);
    ReleaseAssert(llvm_value_has_type<void*>(result));

    impl->CreateDispatchToFallthrough(val /*outputVal*/, bb);
}

void DfgBuiltinNodeImplGetCapturedVar::GenerateImpl(DfgBuiltinNodeImplCreator* impl)
{
    using namespace llvm;
    LLVMContext& ctx = m_llvmCtx;

    ExecutorFunctionContext* funcCtx = impl->CreateFunction(ctx);
    Function* func = funcCtx->GetFunction();
    BasicBlock* bb = BasicBlock::Create(ctx, "", func);

    Value* capturedVar = impl->EmitGetOperand(llvm_type_of<void*>(ctx), 0 /*opOrd*/, bb);

    Value* result = CreateCallToDeegenCommonSnippet(impl->GetModule(), "GetClosedUpvalueValue", { capturedVar }, bb);
    ReleaseAssert(llvm_value_has_type<uint64_t>(result));

    impl->CreateDispatchToFallthrough(result /*outputVal*/, bb);
}

void DfgBuiltinNodeImplSetCapturedVar::GenerateImpl(DfgBuiltinNodeImplCreator* impl)
{
    using namespace llvm;
    LLVMContext& ctx = m_llvmCtx;

    ExecutorFunctionContext* funcCtx = impl->CreateFunction(ctx);
    Function* func = funcCtx->GetFunction();
    BasicBlock* bb = BasicBlock::Create(ctx, "", func);

    Value* capturedVar = impl->EmitGetOperand(llvm_type_of<void*>(ctx), 0 /*opOrd*/, bb);
    Value* valToStore = impl->EmitGetOperand(llvm_type_of<uint64_t>(ctx), 1 /*opOrd*/, bb);

    CreateCallToDeegenCommonSnippet(impl->GetModule(), "SetClosedUpvalueValue", { capturedVar, valToStore }, bb);

    impl->CreateDispatchToFallthrough(nullptr /*outputVal*/, bb);
}

void DfgBuiltinNodeImplGetKthVariadicRes::GenerateImpl(DfgBuiltinNodeImplCreator* impl)
{
    using namespace llvm;
    LLVMContext& ctx = m_llvmCtx;

    using NsdTy = dfg_builtin_node_nsd_t<NodeKind_GetKthVariadicRes>;

    ExecutorFunctionContext* funcCtx = impl->CreateFunction(ctx);
    Function* func = funcCtx->GetFunction();
    BasicBlock* bb = BasicBlock::Create(ctx, "", func);

    Value* coroCtx = funcCtx->GetValueAtEntry<RPV_CoroContext>();
    Value* slotOrd = impl->AddOnlyLiteralOperand<uint64_t /*valueTy*/, NsdTy /*storageTy*/>(
        0 /*valueLb*/, BcOpSlot::x_localOrdinalUpperBound, bb);

    Value* result = CreateCallToDeegenCommonSnippet(impl->GetModule(), "GetKthVariadicResult", { coroCtx, slotOrd }, bb);

    impl->CreateDispatchToFallthrough(result /*outputVal*/, bb);
}

void DfgBuiltinNodeImplGetNumVariadicRes::GenerateImpl(DfgBuiltinNodeImplCreator* impl)
{
    using namespace llvm;
    LLVMContext& ctx = m_llvmCtx;

    ExecutorFunctionContext* funcCtx = impl->CreateFunction(ctx);
    Function* func = funcCtx->GetFunction();
    BasicBlock* bb = BasicBlock::Create(ctx, "", func);

    Value* coroCtx = funcCtx->GetValueAtEntry<RPV_CoroContext>();

    Value* result = CreateCallToDeegenCommonSnippet(impl->GetModule(), "GetNumVariadicResults", { coroCtx }, bb);
    ReleaseAssert(llvm_value_has_type<uint64_t>(result));

    impl->CreateDispatchToFallthrough(result /*outputVal*/, bb);
}

void DfgBuiltinNodeImplCreateVariadicRes_StoreInfo::GenerateImpl(DfgBuiltinNodeImplCreator* impl)
{
    using namespace llvm;
    LLVMContext& ctx = m_llvmCtx;

    ExecutorFunctionContext* funcCtx = impl->CreateFunction(ctx);
    Function* func = funcCtx->GetFunction();
    BasicBlock* bb = BasicBlock::Create(ctx, "", func);

    Value* coroCtx = funcCtx->GetValueAtEntry<RPV_CoroContext>();
    Value* stackBase = funcCtx->GetValueAtEntry<RPV_StackBase>();
    Value* codeBlock = funcCtx->GetValueAtEntry<RPV_CodeBlock>();

    Value* numFixedVR = impl->AddLiteralOperandForCustomInterface<uint64_t>(0 /*valueLb*/, BcOpSlot::x_localOrdinalUpperBound, bb);
    Value* numNonFixedVR = impl->EmitGetOperand(llvm_type_of<uint64_t>(ctx), 0 /*opOrd*/, bb);

    Value* numVR = CreateUnsignedAddNoOverflow(numFixedVR, numNonFixedVR, bb);

    Value* varResStart = CreateCallToDeegenCommonSnippet(impl->GetModule(), "SetUpVariadicResultInfo", { coroCtx, stackBase, codeBlock, numVR }, bb);
    ReleaseAssert(llvm_value_has_type<void*>(varResStart));

    impl->CreateDispatchToFallthrough(varResStart /*outputVal*/, bb);
}

void DfgBuiltinNodeImplPrependVariadicRes_MoveAndStoreInfo::GenerateImpl(DfgBuiltinNodeImplCreator* impl)
{
    using namespace llvm;
    LLVMContext& ctx = m_llvmCtx;

    ExecutorFunctionContext* funcCtx = impl->CreateFunction(ctx);
    Function* func = funcCtx->GetFunction();
    BasicBlock* bb = BasicBlock::Create(ctx, "", func);

    Value* coroCtx = funcCtx->GetValueAtEntry<RPV_CoroContext>();
    Value* stackBase = funcCtx->GetValueAtEntry<RPV_StackBase>();

    Value* numExtraVals = impl->AddLiteralOperandForCustomInterface<uint64_t>(1 /*valueLb*/, BcOpSlot::x_localOrdinalUpperBound, bb);
    Value* numTotalLocals = impl->AddLiteralOperandForCustomInterface<uint64_t>(1 /*valueLb*/, BcOpSlot::x_localOrdinalUpperBound, bb);
    Value* sumOfExtraValsAndTotalLocals = impl->AddLiteralOperandForCustomInterface<uint64_t>(1 /*valueLb*/, BcOpSlot::x_localOrdinalUpperBound * 2, bb);

    Value* newVarResPtr = CreateCallToDeegenCommonSnippet(
        impl->GetModule(), "MoveVariadicResultsForPrepend", { stackBase, coroCtx, numExtraVals, numTotalLocals, sumOfExtraValsAndTotalLocals }, bb);
    ReleaseAssert(llvm_value_has_type<void*>(newVarResPtr));

    impl->CreateDispatchToFallthrough(newVarResPtr /*outputVal*/, bb);
}

void DfgBuiltinNodeImplCheckU64InBound::GenerateImpl(DfgBuiltinNodeImplCreator* impl)
{
    using namespace llvm;
    LLVMContext& ctx = m_llvmCtx;

    using NsdTy = dfg_builtin_node_nsd_t<NodeKind_CheckU64InBound>;

    ExecutorFunctionContext* funcCtx = impl->CreateFunction(ctx);
    Function* func = funcCtx->GetFunction();
    BasicBlock* bb = BasicBlock::Create(ctx, "", func);

    Value* boundVal = impl->AddOnlyLiteralOperand<uint64_t /*valueTy*/, NsdTy /*storageTy*/>(
        0 /*valueLb*/, BcOpSlot::x_localOrdinalUpperBound, bb);

    Value* inputVal = impl->EmitGetOperand(llvm_type_of<uint64_t>(ctx), 0 /*opOrd*/, bb);

    Value* cmpResult = new ICmpInst(bb, ICmpInst::ICMP_ULE, inputVal, boundVal);

    Function* expectIntrin = Intrinsic::getDeclaration(impl->GetModule(), Intrinsic::expect, { Type::getInt1Ty(ctx) });
    cmpResult = CallInst::Create(expectIntrin, { cmpResult, CreateLLVMConstantInt<bool>(ctx, true) }, "", bb);

    BasicBlock* trueBB = BasicBlock::Create(ctx, "", func);
    BasicBlock* falseBB = BasicBlock::Create(ctx, "", func);
    BranchInst::Create(trueBB, falseBB, cmpResult, bb);

    impl->CreateDispatchToFallthrough(nullptr /*outputVal*/, trueBB);
    impl->CreateDispatchToOsrExit(falseBB);
}

void DfgBuiltinNodeImplI64SubSaturateToZero::GenerateImpl(DfgBuiltinNodeImplCreator* impl)
{
    using namespace llvm;
    LLVMContext& ctx = m_llvmCtx;

    using NsdTy = dfg_builtin_node_nsd_t<NodeKind_I64SubSaturateToZero>;

    ExecutorFunctionContext* funcCtx = impl->CreateFunction(ctx);
    Function* func = funcCtx->GetFunction();
    BasicBlock* bb = BasicBlock::Create(ctx, "", func);

    Value* valToSub = impl->AddOnlyLiteralOperand<int64_t /*valueTy*/, NsdTy /*storageTy*/>(
        -1000000000 /*valueLb*/, 1000000000 /*valueUb*/, bb);

    Value* inputVal = impl->EmitGetOperand(llvm_type_of<uint64_t>(ctx), 0 /*opOrd*/, bb);

    Value* result = CreateCallToDeegenCommonSnippet(impl->GetModule(), "I64SubSaturateToZeroImpl", { inputVal, valToSub }, bb);
    ReleaseAssert(llvm_value_has_type<uint64_t>(result));

    impl->CreateDispatchToFallthrough(result /*outputVal*/, bb);
}

void DfgBuiltinNodeImplCreateFunctionObject_AllocAndSetup::GenerateImpl(DfgBuiltinNodeImplCreator* impl)
{
    using namespace llvm;
    LLVMContext& ctx = m_llvmCtx;

    ExecutorFunctionContext* funcCtx = impl->CreateFunction(ctx);
    Function* func = funcCtx->GetFunction();
    BasicBlock* bb = BasicBlock::Create(ctx, "", func);

    Value* unlinkedCodeBlock = impl->EmitGetOperand(llvm_type_of<void*>(ctx), 0 /*opOrd*/, bb);
    Value* parentFunc = impl->EmitGetOperand(llvm_type_of<HeapPtr<void>>(ctx), 1 /*opOrd*/, bb);

    Value* result = CreateCallToDeegenCommonSnippet(impl->GetModule(), "CreateNewClosureForDfgAndFillUpvaluesFromParent", { unlinkedCodeBlock, parentFunc }, bb);
    ReleaseAssert(llvm_value_has_type<void*>(result));

    ReleaseAssert(x_dfg_reg_alloc_num_gprs > 0);
    X64Reg resultReg = x_dfg_reg_alloc_gprs[0];

    result = new PtrToIntInst(result, llvm_type_of<uint64_t>(ctx), "", bb);
    result = RegisterPinningScheme::EmitCastI64ToArgumentType(result, RegisterPinningScheme::GetArgumentOrdinalForRegister(resultReg), bb);

    CallInst* ci = impl->CreateDispatchToFallthrough(nullptr /*outputVal*/, bb);

    RegisterPinningScheme::SetExtraDispatchArgument(ci, resultReg, result);
}

void DfgBuiltinNodeImplCreateFunctionObject_BoxFunctionObject::GenerateImpl(DfgBuiltinNodeImplCreator* impl)
{
    using namespace llvm;
    LLVMContext& ctx = m_llvmCtx;

    ExecutorFunctionContext* funcCtx = impl->CreateFunction(ctx);
    Function* func = funcCtx->GetFunction();
    BasicBlock* bb = BasicBlock::Create(ctx, "", func);

    Value* funcObjPtr = impl->EmitGetOperand(llvm_type_of<void*>(ctx), 0 /*opOrd*/, bb);

    Value* result = CreateCallToDeegenCommonSnippet(impl->GetModule(), "BoxFunctionObjectRawPointerToTValue", { funcObjPtr }, bb);
    ReleaseAssert(llvm_value_has_type<uint64_t>(result));

    impl->CreateDispatchToFallthrough(result /*outputVal*/, bb);
}

void DfgBuiltinNodeImplCreateFunctionObject_BoxFnObjAndWriteSelfRefUv::GenerateImpl(DfgBuiltinNodeImplCreator* impl)
{
    using namespace llvm;
    LLVMContext& ctx = m_llvmCtx;

    ExecutorFunctionContext* funcCtx = impl->CreateFunction(ctx);
    Function* func = funcCtx->GetFunction();
    BasicBlock* bb = BasicBlock::Create(ctx, "", func);

    Value* funcObjPtr = impl->EmitGetOperand(llvm_type_of<void*>(ctx), 0 /*opOrd*/, bb);
    Value* byteOffset = impl->AddLiteralOperandForCustomInterface<uint64_t>(0 /*valueLb*/, FunctionObject::GetUpvalueAddrByteOffsetFromThisPointer(FunctionObject::x_maxNumUpvalues), bb);

    Value* result = CreateCallToDeegenCommonSnippet(impl->GetModule(), "BoxFunctionObjectRawPointerToTValue", { funcObjPtr }, bb);
    ReleaseAssert(llvm_value_has_type<uint64_t>(result));

    Value* uvAddr = GetElementPtrInst::CreateInBounds(llvm_type_of<uint8_t>(ctx), funcObjPtr, { byteOffset }, "", bb);
    new StoreInst(result, uvAddr, bb);

    impl->CreateDispatchToFallthrough(result /*outputVal*/, bb);
}

void DfgBuiltinNodeImplGetImmutableUpvalue::GenerateImpl(DfgBuiltinNodeImplCreator* impl)
{
    using namespace llvm;
    LLVMContext& ctx = m_llvmCtx;

    using NsdTy = dfg_builtin_node_nsd_t<NodeKind_GetUpvalueImmutable>;

    ExecutorFunctionContext* funcCtx = impl->CreateFunction(ctx);
    Function* func = funcCtx->GetFunction();
    BasicBlock* bb = BasicBlock::Create(ctx, "", func);

    Value* upvalueOrd = impl->AddExtraLiteralOperand<uint64_t /*valueTy*/, &NsdTy::m_ordinal>(
        0 /*valueLb*/, BcOpSlot::x_localOrdinalUpperBound, bb);

    Value* funcObjPtr = impl->EmitGetOperand(llvm_type_of<HeapPtr<void>>(ctx), 0 /*opOrd*/, bb);

    Value* result = CreateCallToDeegenCommonSnippet(impl->GetModule(), "GetImmutableUpvalueValueFromFunctionObject", { funcObjPtr, upvalueOrd }, bb);
    ReleaseAssert(llvm_value_has_type<uint64_t>(result));

    impl->CreateDispatchToFallthrough(result /*outputVal*/, bb);
}

void DfgBuiltinNodeImplGetMutableUpvalue::GenerateImpl(DfgBuiltinNodeImplCreator* impl)
{
    using namespace llvm;
    LLVMContext& ctx = m_llvmCtx;

    using NsdTy = dfg_builtin_node_nsd_t<NodeKind_GetUpvalueImmutable>;

    ExecutorFunctionContext* funcCtx = impl->CreateFunction(ctx);
    Function* func = funcCtx->GetFunction();
    BasicBlock* bb = BasicBlock::Create(ctx, "", func);

    Value* upvalueOrd = impl->AddExtraLiteralOperand<uint64_t /*valueTy*/, &NsdTy::m_ordinal>(
        0 /*valueLb*/, BcOpSlot::x_localOrdinalUpperBound, bb);

    Value* funcObjPtr = impl->EmitGetOperand(llvm_type_of<HeapPtr<void>>(ctx), 0 /*opOrd*/, bb);

    Value* result = CreateCallToDeegenCommonSnippet(impl->GetModule(), "GetMutableUpvalueValueFromFunctionObject", { funcObjPtr, upvalueOrd }, bb);
    ReleaseAssert(llvm_value_has_type<uint64_t>(result));

    impl->CreateDispatchToFallthrough(result /*outputVal*/, bb);
}

void DfgBuiltinNodeImplSetUpvalue::GenerateImpl(DfgBuiltinNodeImplCreator* impl)
{
    using namespace llvm;
    LLVMContext& ctx = m_llvmCtx;

    using NsdTy = dfg_builtin_node_nsd_t<dfg::NodeKind_SetUpvalue>;

    ExecutorFunctionContext* funcCtx = impl->CreateFunction(ctx);
    Function* func = funcCtx->GetFunction();
    BasicBlock* bb = BasicBlock::Create(ctx, "", func);

    Value* upvalueOrd = impl->AddOnlyLiteralOperand<uint64_t /*valueTy*/, NsdTy>(
        0 /*valueLb*/, BcOpSlot::x_localOrdinalUpperBound, bb);

    Value* funcObjPtr = impl->EmitGetOperand(llvm_type_of<HeapPtr<void>>(ctx), 0 /*opOrd*/, bb);
    Value* valToPut = impl->EmitGetOperand(llvm_type_of<uint64_t>(ctx), 1 /*opOrd*/, bb);

    CreateCallToDeegenCommonSnippet(impl->GetModule(), "PutUpvalueFromFunctionObject", { funcObjPtr, upvalueOrd, valToPut }, bb);

    impl->CreateDispatchToFallthrough(nullptr /*outputVal*/, bb);
}

void DfgBuiltinNodeImplReturn_MoveVariadicRes::GenerateImpl(DfgBuiltinNodeImplCreator* impl)
{
    using namespace llvm;
    LLVMContext& ctx = m_llvmCtx;

    ExecutorFunctionContext* funcCtx = impl->CreateFunction(ctx);
    Function* func = funcCtx->GetFunction();
    BasicBlock* bb = BasicBlock::Create(ctx, "", func);

    Value* coroCtx = funcCtx->GetValueAtEntry<RPV_CoroContext>();
    Value* stackBase = funcCtx->GetValueAtEntry<RPV_StackBase>();

    Value* numExtraVals = impl->AddLiteralOperandForCustomInterface<uint64_t>(0 /*valueLb*/, BcOpSlot::x_localOrdinalUpperBound, bb);
    Value* numTotalLocals = impl->AddLiteralOperandForCustomInterface<uint64_t>(1 /*valueLb*/, BcOpSlot::x_localOrdinalUpperBound, bb);
    Value* sumOfExtraValsAndTotalLocals = impl->AddLiteralOperandForCustomInterface<uint64_t>(1 /*valueLb*/, BcOpSlot::x_localOrdinalUpperBound * 2, bb);

    Value* retListAddr = CreateCallToDeegenCommonSnippet(
        impl->GetModule(), "MoveVariadicResultsForReturn", { stackBase, coroCtx, numExtraVals, numTotalLocals, sumOfExtraValsAndTotalLocals }, bb);
    ReleaseAssert(llvm_value_has_type<void*>(retListAddr));

    impl->CreateDispatchToFallthrough(retListAddr /*outputVal*/, bb);
}

void DfgBuiltinNodeImplReturn_RetWithVariadicRes::GenerateImpl(DfgBuiltinNodeImplCreator* impl)
{
    using namespace llvm;
    LLVMContext& ctx = m_llvmCtx;

    ExecutorFunctionContext* funcCtx = impl->CreateFunction(ctx);
    Function* func = funcCtx->GetFunction();
    BasicBlock* bb = BasicBlock::Create(ctx, "", func);

    Value* coroCtx = funcCtx->GetValueAtEntry<RPV_CoroContext>();

    Value* retStart =CreateCallToDeegenCommonSnippet(
        impl->GetModule(), "GetVariadicResultsStart", { coroCtx }, bb);

    Value* numTotalRets = CreateCallToDeegenCommonSnippet(
        impl->GetModule(), "GetNumVariadicResults", { coroCtx }, bb);

    impl->CreateDispatchForGuestLanguageFunctionReturn(retStart, numTotalRets, bb);
}

void DfgBuiltinNodeImplReturn_WriteNil::GenerateImpl(DfgBuiltinNodeImplCreator* impl)
{
    using namespace llvm;
    LLVMContext& ctx = m_llvmCtx;

    ExecutorFunctionContext* funcCtx = impl->CreateFunction(ctx);
    Function* func = funcCtx->GetFunction();
    BasicBlock* bb = BasicBlock::Create(ctx, "", func);

    Value* stackBase = funcCtx->GetValueAtEntry<RPV_StackBase>();
    Value* slotOrd = impl->AddLiteralOperandForCustomInterface<uint64_t>(0 /*valueLb*/, BcOpSlot::x_localOrdinalUpperBound, bb);

    uint64_t nilValue = TValue::Create<tNil>().m_value;

    Value* addr = GetElementPtrInst::CreateInBounds(llvm_type_of<uint64_t>(ctx), stackBase, { slotOrd }, "", bb);
    new StoreInst(CreateLLVMConstantInt<uint64_t>(ctx, nilValue), addr, bb);

    impl->CreateDispatchToFallthrough(nullptr /*outputVal*/, bb);
}

void DfgBuiltinNodeImplReturn_RetNoVariadicRes::GenerateImpl(DfgBuiltinNodeImplCreator* impl)
{
    using namespace llvm;
    LLVMContext& ctx = m_llvmCtx;

    ExecutorFunctionContext* funcCtx = impl->CreateFunction(ctx);
    Function* func = funcCtx->GetFunction();
    BasicBlock* bb = BasicBlock::Create(ctx, "", func);

    Value* stackBase = funcCtx->GetValueAtEntry<RPV_StackBase>();
    Value* retStartOrd = impl->AddLiteralOperandForCustomInterface<uint64_t>(0 /*valueLb*/, BcOpSlot::x_localOrdinalUpperBound, bb);
    Value* numRets = impl->AddLiteralOperandForCustomInterface<uint64_t>(0 /*valueLb*/, BcOpSlot::x_localOrdinalUpperBound, bb);

    Value* retStart = GetElementPtrInst::CreateInBounds(llvm_type_of<uint64_t>(ctx), stackBase, { retStartOrd }, "", bb);
    impl->CreateDispatchForGuestLanguageFunctionReturn(retStart, numRets, bb);
}

void DfgBuiltinNodeImplReturn_Ret1::GenerateImpl(DfgBuiltinNodeImplCreator* impl)
{
    using namespace llvm;
    LLVMContext& ctx = m_llvmCtx;

    ExecutorFunctionContext* funcCtx = impl->CreateFunction(ctx);
    Function* func = funcCtx->GetFunction();
    BasicBlock* bb = BasicBlock::Create(ctx, "", func);

    Value* stackBase = funcCtx->GetValueAtEntry<RPV_StackBase>();

    Value* retSlotOrd = impl->AddLiteralOperandForCustomInterface<uint64_t>(0 /*valueLb*/, BcOpSlot::x_localOrdinalUpperBound, bb);
    Value* retStart = GetElementPtrInst::CreateInBounds(llvm_type_of<uint64_t>(ctx), stackBase, { retSlotOrd }, "", bb);

    uint64_t nilValue = TValue::Create<tNil>().m_value;

    for (size_t i = 1; i < x_minNilFillReturnValues; i++)
    {
        Value* addr = GetElementPtrInst::CreateInBounds(llvm_type_of<uint64_t>(ctx), retStart, { CreateLLVMConstantInt<uint64_t>(ctx, i) }, "", bb);
        new StoreInst(CreateLLVMConstantInt<uint64_t>(ctx, nilValue), addr, bb);
    }

    impl->CreateDispatchForGuestLanguageFunctionReturn(retStart, CreateLLVMConstantInt<uint64_t>(ctx, 1 /*value*/), bb);
}

void DfgBuiltinNodeImplReturn_Ret0::GenerateImpl(DfgBuiltinNodeImplCreator* impl)
{
    using namespace llvm;
    LLVMContext& ctx = m_llvmCtx;

    ExecutorFunctionContext* funcCtx = impl->CreateFunction(ctx);
    Function* func = funcCtx->GetFunction();
    BasicBlock* bb = BasicBlock::Create(ctx, "", func);

    Value* stackBase = funcCtx->GetValueAtEntry<RPV_StackBase>();

    uint64_t nilValue = TValue::Create<tNil>().m_value;
    for (size_t i = 0; i < x_minNilFillReturnValues; i++)
    {
        Value* addr = GetElementPtrInst::CreateInBounds(llvm_type_of<uint64_t>(ctx), stackBase, { CreateLLVMConstantInt<uint64_t>(ctx, i) }, "", bb);
        new StoreInst(CreateLLVMConstantInt<uint64_t>(ctx, nilValue), addr, bb);
    }

    impl->CreateDispatchForGuestLanguageFunctionReturn(stackBase, CreateLLVMConstantInt<uint64_t>(ctx, 0 /*value*/), bb);
}

void DfgBuiltinNodeImplTypeCheck::GenerateImpl(DfgBuiltinNodeImplCreator* impl)
{
    using namespace llvm;
    LLVMContext& ctx = m_srcModule->getContext();

    // Note that SetBaseModule clones the module
    //
    impl->SetBaseModule(m_srcModule);

    Module* m = impl->GetModule();
    ExecutorFunctionContext* funcCtx = impl->CreateFunction(ctx);
    Function* func = funcCtx->GetFunction();
    BasicBlock* bb = BasicBlock::Create(ctx, "", func);

    Function* implFn = m->getFunction(m_implFuncName);
    ReleaseAssert(implFn != nullptr);
    ReleaseAssert(implFn->getReturnType()->isIntegerTy(1 /*bitWidth*/));
    ReleaseAssert(implFn->arg_size() == 1 && llvm_value_has_type<uint64_t>(implFn->getArg(0)));

    Value* val = impl->EmitGetOperand(llvm_type_of<uint64_t>(ctx), 0 /*opOrd*/, bb);

    Function* expectIntrin = Intrinsic::getDeclaration(m, Intrinsic::expect, { Type::getInt1Ty(ctx) });

    CallInst* ci = CallInst::Create(implFn, { val }, "", bb);
    ReleaseAssert(ci->getType()->isIntegerTy(1 /*bitWidth*/));
    ci->addRetAttr(Attribute::ZExt);

    Value* checkPassed = ci;
    if (m_shouldFlipResult)
    {
        checkPassed = BinaryOperator::CreateXor(checkPassed, CreateLLVMConstantInt<bool>(ctx, true), "", bb);
    }

    checkPassed = CallInst::Create(expectIntrin, { checkPassed, CreateLLVMConstantInt<bool>(ctx, true) }, "", bb);

    BasicBlock* trueBB = BasicBlock::Create(ctx, "", func);
    BasicBlock* falseBB = BasicBlock::Create(ctx, "", func);
    BranchInst::Create(trueBB, falseBB, checkPassed, bb);

    impl->CreateDispatchToFallthrough(nullptr /*outputVal*/, trueBB);
    impl->CreateDispatchToOsrExit(falseBB);
}

}   // namespace dast
