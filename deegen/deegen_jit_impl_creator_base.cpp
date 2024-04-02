#include "deegen_jit_impl_creator_base.h"
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

namespace dast {

static std::string WARN_UNUSED GetRawRuntimeConstantPlaceholderName(size_t ordinal)
{
    return std::string("__deegen_constant_placeholder_bytecode_operand_") + std::to_string(ordinal);
}

llvm::CallInst* WARN_UNUSED DeegenPlaceholderUtils::CreateConstantPlaceholderForOperand(llvm::Module* module, size_t ordinal, llvm::Type* operandTy, llvm::Instruction* insertBefore)
{
    using namespace llvm;
    std::string placeholderName = GetRawRuntimeConstantPlaceholderName(ordinal);
    ReleaseAssert(module->getNamedValue(placeholderName) == nullptr);
    FunctionType* fty = FunctionType::get(operandTy, {}, false /*isVarArg*/);
    Function* func = Function::Create(fty, GlobalValue::ExternalLinkage, placeholderName, module);
    ReleaseAssert(func->getName() == placeholderName);
    func->addFnAttr(Attribute::AttrKind::NoUnwind);
    func->addFnAttr(Attribute::AttrKind::ReadNone);
    func->addFnAttr(Attribute::AttrKind::WillReturn);
    return CallInst::Create(func, { }, "", insertBefore);
}

llvm::CallInst* WARN_UNUSED JitImplCreatorBase::CreateConstantPlaceholderForOperand(size_t ordinal, llvm::Type* operandTy, int64_t lb, int64_t ub, llvm::Instruction* insertBefore)
{
    using namespace llvm;
    CallInst* ci = DeegenPlaceholderUtils::CreateConstantPlaceholderForOperand(GetModule(), ordinal, operandTy, insertBefore);
    m_stencilRcInserter.AddRawRuntimeConstant(ordinal, lb, ub);
    return ci;
}

llvm::CallInst* WARN_UNUSED JitImplCreatorBase::CreateConstantPlaceholderForOperand(size_t ordinal, llvm::Type* operandTy, int64_t lb, int64_t ub, llvm::BasicBlock* insertAtEnd)
{
    using namespace llvm;
    UnreachableInst* dummy = new UnreachableInst(operandTy->getContext(), insertAtEnd);
    CallInst* res = CreateConstantPlaceholderForOperand(ordinal, operandTy, lb, ub, dummy);
    dummy->eraseFromParent();
    return res;
}

llvm::CallInst* WARN_UNUSED JitImplCreatorBase::CreateOrGetConstantPlaceholderForOperand(size_t ordinal, llvm::Type* operandTy, int64_t lb, int64_t ub, llvm::Instruction* insertBefore)
{
    using namespace llvm;
    std::string placeholderName = GetRawRuntimeConstantPlaceholderName(ordinal);
    if (GetModule()->getNamedValue(placeholderName) == nullptr)
    {
        return CreateConstantPlaceholderForOperand(ordinal, operandTy, lb, ub, insertBefore);
    }

    Function* func = GetModule()->getFunction(placeholderName);
    ReleaseAssert(func != nullptr);
    ReleaseAssert(func->getReturnType() == operandTy);
    ReleaseAssert(func->arg_size() == 0);

    m_stencilRcInserter.WidenRange(ordinal, lb, ub);
    return CallInst::Create(func, { }, "", insertBefore);
}

std::pair<llvm::CallInst*, size_t /*ord*/> WARN_UNUSED JitImplCreatorBase::CreateGenericIcStateCapturePlaceholder(llvm::Type* ty, int64_t lb, int64_t ub, llvm::BasicBlock* insertAtEnd)
{
    using namespace llvm;
    UnreachableInst* dummy = new UnreachableInst(ty->getContext(), insertAtEnd);
    auto res = CreateGenericIcStateCapturePlaceholder(ty, lb, ub, dummy);
    dummy->eraseFromParent();
    return res;
}

std::pair<llvm::CallInst*, size_t /*ord*/> WARN_UNUSED JitImplCreatorBase::CreateGenericIcStateCapturePlaceholder(llvm::Type* ty, int64_t lb, int64_t ub, llvm::Instruction* insertBefore)
{
    size_t resultOrd = m_numGenericIcCaptures;
    m_numGenericIcCaptures++;
    llvm::CallInst* inst = CreateConstantPlaceholderForOperand(resultOrd + 200, ty, lb, ub, insertBefore);
    return std::make_pair(inst, resultOrd);
}

llvm::CallInst* WARN_UNUSED JitImplCreatorBase::CreateOrGetConstantPlaceholderForOperand(size_t ordinal, llvm::Type* operandTy, int64_t lb, int64_t ub, llvm::BasicBlock* insertAtEnd)
{
    using namespace llvm;
    UnreachableInst* dummy = new UnreachableInst(operandTy->getContext(), insertAtEnd);
    CallInst* res = CreateOrGetConstantPlaceholderForOperand(ordinal, operandTy, lb, ub, dummy);
    dummy->eraseFromParent();
    return res;
}

llvm::Value* WARN_UNUSED JitImplCreatorBase::GetSlowPathDataOffsetFromJitFastPath(llvm::Instruction* insertBefore, bool useAliasStencilOrd)
{
    using namespace llvm;
    ReleaseAssert(!IsJitSlowPath());
    size_t ordinalToUse = useAliasStencilOrd ? CP_PLACEHOLDER_JIT_SLOW_PATH_DATA_OFFSET : 103 /*ordinal*/;
    return CreateOrGetConstantPlaceholderForOperand(ordinalToUse,
                                                    llvm_type_of<uint64_t>(insertBefore->getContext()),
                                                    1 /*lowerBound*/,
                                                    StencilRuntimeConstantInserter::GetLowAddrRangeUB(),
                                                    insertBefore);
}

llvm::Value* WARN_UNUSED JitImplCreatorBase::GetSlowPathDataOffsetFromJitFastPath(llvm::BasicBlock* insertAtEnd, bool useAliasStencilOrd)
{
    using namespace llvm;
    UnreachableInst* dummy = new UnreachableInst(insertAtEnd->getContext(), insertAtEnd);
    Value* res = GetSlowPathDataOffsetFromJitFastPath(dummy, useAliasStencilOrd);
    dummy->eraseFromParent();
    return res;
}

JitImplCreatorBase::JitImplCreatorBase(BytecodeIrComponent& bic)
    : DeegenBytecodeImplCreatorBase(bic.m_bytecodeDef, bic.m_processKind)
{
    using namespace llvm;
    m_module = CloneModule(*bic.m_module.get());
    m_impl = m_module->getFunction(bic.m_impl->getName());
    ReleaseAssert(m_impl != nullptr);
    ReleaseAssert(m_impl->getName() == bic.m_impl->getName());
    m_wrapper = nullptr;
    m_resultFuncName = bic.m_identFuncName;
    m_generated = false;
    m_isSlowPathReturnContinuation = false;
    m_numGenericIcCaptures = 0;
}

JitImplCreatorBase::JitImplCreatorBase(JitImplCreatorBase::SlowPathReturnContinuationTag, BytecodeIrComponent& bic)
    : JitImplCreatorBase(bic)
{
    ReleaseAssert(m_processKind == BytecodeIrComponentKind::ReturnContinuation);
    m_isSlowPathReturnContinuation = true;
}

size_t WARN_UNUSED DeegenPlaceholderUtils::FindFallthroughPlaceholderOrd(const std::vector<CPRuntimeConstantNodeBase*>& rcDef)
{
    bool found = false;
    size_t ord = static_cast<size_t>(-1);
    for (size_t i = 0; i < rcDef.size(); i++)
    {
        CPRuntimeConstantNodeBase* def = rcDef[i];
        if (def->IsRawRuntimeConstant() && dynamic_cast<CPRawRuntimeConstant*>(def)->m_label == 101 /*fallthroughTarget*/)
        {
            ReleaseAssert(!found);
            found = true;
            ord = i;
        }
    }
    if (!found) { return static_cast<size_t>(-1); }
    return ord;
}

std::string WARN_UNUSED DeegenPlaceholderUtils::FindFallthroughPlaceholderSymbolName(const std::vector<CPRuntimeConstantNodeBase*>& rcDef)
{
    size_t ord = FindFallthroughPlaceholderOrd(rcDef);
    if (ord == static_cast<size_t>(-1))
    {
        return "";
    }
    return std::string("__deegen_cp_placeholder_") + std::to_string(ord);
}

std::string WARN_UNUSED JitImplCreatorBase::GetRcPlaceholderNameForFallthrough()
{
    ReleaseAssert(!IsJitSlowPath());
    return DeegenPlaceholderUtils::FindFallthroughPlaceholderSymbolName(m_stencilRcDefinitions);
}

void JitImplCreatorBase::CreateWrapperFunction()
{
    using namespace llvm;
    LLVMContext& ctx = m_module->getContext();

    JitSlowPathDataLayoutBase* jitSlowPathDataLayout;
    if (IsBaselineJIT())
    {
        jitSlowPathDataLayout = m_bytecodeDef->GetBaselineJitSlowPathDataLayout();
    }
    else
    {
        ReleaseAssert(IsDfgJIT());
        jitSlowPathDataLayout = m_bytecodeDef->GetDfgJitSlowPathDataLayout();
    }

    ReleaseAssert(m_wrapper == nullptr);

    // Create the wrapper function
    // We can also change the linkage of 'm_impl' back to internal now, since the wrapper function will keep it alive
    //
    ReleaseAssert(m_impl->hasExternalLinkage());
    m_impl->setLinkage(GlobalValue::InternalLinkage);
    m_wrapper = InterpreterFunctionInterface::CreateFunction(m_module.get(), m_resultFuncName);
    ReleaseAssert(m_wrapper->arg_size() == 16);

    m_wrapper->addFnAttr(Attribute::AttrKind::NoReturn);
    m_wrapper->addFnAttr(Attribute::AttrKind::NoUnwind);
    m_wrapper->addFnAttr(Attribute::AttrKind::NoInline);
    CopyFunctionAttributes(m_wrapper /*dst*/, m_impl /*src*/);

    BasicBlock* entryBlock = BasicBlock::Create(ctx, "", m_wrapper);
    BasicBlock* currentBlock = entryBlock;

    if (m_processKind != BytecodeIrComponentKind::ReturnContinuation)
    {
        // Note that we also set parameter names here.
        // These are not required, but just to make dumps more readable
        //
        Value* coroCtx = m_wrapper->getArg(0);
        coroCtx->setName(x_coroutineCtx);
        m_valuePreserver.Preserve(x_coroutineCtx, coroCtx);

        Value* stackBase = m_wrapper->getArg(1);
        stackBase->setName(x_stackBase);
        m_valuePreserver.Preserve(x_stackBase, stackBase);

        Value* baselineCodeBlock = m_wrapper->getArg(3);
        baselineCodeBlock->setName(GetJitCodeBlockLLVMVarName());
        m_valuePreserver.Preserve(GetJitCodeBlockLLVMVarName(), baselineCodeBlock);

        // The SlowPathData pointer is only useful (and valid) for slow path
        //
        if (IsJitSlowPath())
        {
            Value* jitSlowPathData = m_wrapper->getArg(2);
            jitSlowPathData->setName(x_jitSlowPathData);
            m_valuePreserver.Preserve(x_jitSlowPathData, jitSlowPathData);
        }
    }
    else
    {
        m_valuePreserver.Preserve(x_coroutineCtx, m_wrapper->getArg(0));

        UnreachableInst* tmpInst = new UnreachableInst(ctx, currentBlock);
        Value* calleeStackBase = m_wrapper->getArg(1);
        Value* stackBase = CallDeegenCommonSnippet("GetCallerStackBaseFromStackBase", { calleeStackBase }, tmpInst);
        m_valuePreserver.Preserve(x_stackBase, stackBase);
        tmpInst->eraseFromParent();

        Value* retStart = m_wrapper->getArg(5);
        retStart->setName(x_retStart);
        m_valuePreserver.Preserve(x_retStart, retStart);

        Value* numRet = m_wrapper->getArg(6);
        numRet->setName(x_numRet);
        m_valuePreserver.Preserve(x_numRet, numRet);

        if (IsJitSlowPath())
        {
            if (GetTier() == DeegenEngineTier::BaselineJIT)
            {
                // Decode the BaselineCodeBlock from the stack frame header
                //
                Instruction* baselineCodeBlock = CreateCallToDeegenCommonSnippet(GetModule(), "GetBaselineCodeBlockFromStackBase", { GetStackBase() }, currentBlock);
                ReleaseAssert(llvm_value_has_type<void*>(baselineCodeBlock));

                m_valuePreserver.Preserve(GetJitCodeBlockLLVMVarName(), baselineCodeBlock);
            }
            else
            {
                // Mostly a mirror of the above branch, except that we need to get the DFG Codeblock
                //
                ReleaseAssert(GetTier() == DeegenEngineTier::DfgJIT);
                Instruction* dfgCodeBlock = CreateCallToDeegenCommonSnippet(GetModule(), "GetDfgCodeBlockFromStackBase", { GetStackBase() }, currentBlock);
                ReleaseAssert(llvm_value_has_type<void*>(dfgCodeBlock));

                m_valuePreserver.Preserve(GetJitCodeBlockLLVMVarName(), dfgCodeBlock);
            }

            // Decode the SlowPathData pointer, which is only useful (and valid) for slow path
            // Furthermore, since this is the return continuation, we must decode this value from the stack frame header
            //
            // Note that the 'm_callerBytecodePtr' is stored in the callee's stack frame header, so we should pass 'calleeStackBase' here
            //
            // Note that the logic here is the same regardless of whether the CodeBlock is a BaselineCodeBlock or a DfgCodeBlock
            //
            Instruction* slowPathDataPtr = CreateCallToDeegenCommonSnippet(
                GetModule(),
                "GetJitSlowpathDataAfterSlowCall",
                { calleeStackBase, GetJitCodeBlock() },
                currentBlock);
            ReleaseAssert(llvm_value_has_type<void*>(slowPathDataPtr));
            m_valuePreserver.Preserve(x_jitSlowPathData, slowPathDataPtr);
        }
        else
        {
            // For the JIT fast path, we only need to set up the BaselineCodeBlock or DfgCodeBlock, which is hardcoded as a stencil hole
            // For now since we don't support encoding 64-bit constant, just translate from HeapPtr..
            //
            Value* jitCodeBlockU64 = CreateConstantPlaceholderForOperand(104 /*ordinal*/,
                                                                         llvm_type_of<uint64_t>(ctx),
                                                                         1,
                                                                         m_stencilRcInserter.GetLowAddrRangeUB(),
                                                                         currentBlock);
            Value* jitCodeBlockU32 = new TruncInst(jitCodeBlockU64, llvm_type_of<uint32_t>(ctx), "", currentBlock);
            Value* jitCodeBlock = CreateCallToDeegenCommonSnippet(GetModule(), "GetCbFromU32", { jitCodeBlockU32 }, currentBlock);
            ReleaseAssert(llvm_value_has_type<void*>(jitCodeBlock));
            m_valuePreserver.Preserve(GetJitCodeBlockLLVMVarName(), jitCodeBlock);
        }
    }

    std::unordered_map<uint64_t /*operandOrd*/, uint64_t /*argOrd*/> alreadyDecodedArgs;
    if (m_processKind == BytecodeIrComponentKind::QuickeningSlowPath && m_bytecodeDef->HasQuickeningSlowPath())
    {
        alreadyDecodedArgs = TypeBasedHCSHelper::GetQuickeningSlowPathAdditionalArgs(m_bytecodeDef);
    }

    // For DFG JIT fast path, some TValue operands may come directly in register
    //
    std::unordered_map<uint64_t /*operandOrd*/, uint64_t /*argOrd*/> operandsInRegister;
    if (!IsJitSlowPath() && GetTier() == DeegenEngineTier::DfgJIT)
    {
        for (auto& operand : m_bytecodeDef->m_list)
        {
            if (operand->GetKind() == BcOperandKind::Slot || operand->GetKind() == BcOperandKind::Constant)
            {
                uint64_t argOrd = GetDfgRegisterSpecilaizationInfo(operand->OperandOrdinal());
                if (argOrd != static_cast<uint64_t>(-1))
                {
                    ReleaseAssert(!operandsInRegister.count(operand->OperandOrdinal()));
                    operandsInRegister[operand->OperandOrdinal()] = argOrd;
                }
            }
        }
    }

    std::vector<Value*> opcodeValues;

    for (auto& operand : m_bytecodeDef->m_list)
    {
        if (alreadyDecodedArgs.count(operand->OperandOrdinal()))
        {
            opcodeValues.push_back(nullptr);
        }
        else if (operandsInRegister.count(operand->OperandOrdinal()))
        {
            opcodeValues.push_back(nullptr);
        }
        else if (!operand->SupportsGetOperandValueFromBytecodeStruct())
        {
            opcodeValues.push_back(nullptr);
        }
        else if (!IsJitSlowPath())
        {
            // This is the fast path JIT'ed code, the operands shall be represented as a runtime constant hole
            // Note that this check has to happen after the SupportsGetOperandValueFromBytecodeStruct() check,
            // since if the operand is already known to be a static constant value, we should not use a runtime constant hole
            //
            llvm::Value* val = CreateConstantPlaceholderForOperand(operand->OperandOrdinal(),
                                                                   operand->GetSourceValueFullRepresentationType(ctx),
                                                                   operand->ValueLowerBound(),
                                                                   operand->ValueUpperBound(),
                                                                   currentBlock);
            opcodeValues.push_back(val);
        }
        else
        {
            // This is the AOT slow path, operands should be decoded from the SlowPathData
            //
            JitSlowPathDataBcOperand& info = jitSlowPathDataLayout->GetBytecodeOperand(operand->OperandOrdinal());
            opcodeValues.push_back(info.EmitGetValueLogic(GetJitSlowPathData(), currentBlock));
        }
    }

    if (m_bytecodeDef->m_hasOutputValue)
    {
        Value* outputSlot;
        if (!IsJitSlowPath())
        {
            outputSlot = CreateConstantPlaceholderForOperand(100 /*operandOrd*/,
                                                             llvm_type_of<uint64_t>(ctx),
                                                             0 /*valueLowerBound*/,
                                                             1048576 /*valueUpperBound*/,
                                                             currentBlock);
        }
        else
        {
            outputSlot = jitSlowPathDataLayout->m_outputDest.EmitGetValueLogic(GetJitSlowPathData(), currentBlock);
        }
        ReleaseAssert(llvm_value_has_type<uint64_t>(outputSlot));
        m_valuePreserver.Preserve(x_outputSlot, outputSlot);
        outputSlot->setName(x_outputSlot);
    }

    {
        Value* fallthroughTarget;
        if (!IsJitSlowPath())
        {
            fallthroughTarget = CreateConstantPlaceholderForOperand(101 /*operandOrd*/,
                                                                    llvm_type_of<void*>(ctx),
                                                                    1 /*valueLowerBound*/,
                                                                    m_stencilRcInserter.GetLowAddrRangeUB(),
                                                                    currentBlock);
        }
        else
        {
            JitSlowPathDataJitAddress fallthroughTargetFromSlowPathData = jitSlowPathDataLayout->GetFallthroughJitAddress();
            fallthroughTarget = fallthroughTargetFromSlowPathData.EmitGetValueLogic(GetJitSlowPathData(), currentBlock);
        }
        ReleaseAssert(llvm_value_has_type<void*>(fallthroughTarget));
        m_valuePreserver.Preserve(x_fallthroughDest, fallthroughTarget);
        fallthroughTarget->setName(x_fallthroughDest);
    }

    if (m_bytecodeDef->m_hasConditionalBranchTarget)
    {
        Value* condBrTarget;
        if (!IsJitSlowPath())
        {
            condBrTarget = CreateConstantPlaceholderForOperand(102 /*operandOrd*/,
                                                               llvm_type_of<void*>(ctx),
                                                               1 /*valueLowerBound*/,
                                                               m_stencilRcInserter.GetLowAddrRangeUB(),
                                                               currentBlock);
        }
        else
        {
            condBrTarget = jitSlowPathDataLayout->m_condBrJitAddr.EmitGetValueLogic(GetJitSlowPathData(), currentBlock);
        }
        ReleaseAssert(llvm_value_has_type<void*>(condBrTarget));
        m_valuePreserver.Preserve(x_condBrDest, condBrTarget);
        condBrTarget->setName(x_condBrDest);
    }

    ReleaseAssert(m_bytecodeDef->IsBytecodeStructLengthFinalized());

    std::vector<Value*> usageValues;
    {
        size_t ord = 0;
        for (auto& operand : m_bytecodeDef->m_list)
        {
            ReleaseAssert(operand->OperandOrdinal() == ord);
            if (alreadyDecodedArgs.count(operand->OperandOrdinal()))
            {
                size_t argOrd = alreadyDecodedArgs[operand->OperandOrdinal()];
                Value* arg = TypeBasedHCSHelper::GetBytecodeOperandUsageValueFromAlreadyDecodedArgs(m_wrapper, argOrd, currentBlock);
                usageValues.push_back(arg);
            }
            else if (operandsInRegister.count(operand->OperandOrdinal()))
            {
                size_t argOrd = operandsInRegister[operand->OperandOrdinal()];
                Value* arg = InterpreterFunctionInterface::GetArgumentAsInt64Value(m_wrapper, argOrd, currentBlock);
                usageValues.push_back(arg);
            }
            else
            {
                usageValues.push_back(operand->EmitUsageValueFromBytecodeValue(this, currentBlock, opcodeValues[ord]));
            }
            // Set name to make dump a bit more readable
            //
            usageValues.back()->setName(std::string("bc_operand_") + operand->OperandName());
            ord++;
        }
        ReleaseAssert(ord == opcodeValues.size() && ord == usageValues.size());
    }

    if (m_processKind == BytecodeIrComponentKind::SlowPath)
    {
        std::vector<Value*> extraArgs = AstSlowPath::CreateCallArgsInSlowPathWrapperFunction(static_cast<uint32_t>(usageValues.size()), m_impl, currentBlock);
        for (Value* val : extraArgs)
        {
            usageValues.push_back(val);
        }
    }

    {
        FunctionType* fty = m_impl->getFunctionType();
        ReleaseAssert(llvm_type_has_type<void>(fty->getReturnType()));
        ReleaseAssert(fty->getNumParams() == usageValues.size());
        for (size_t i = 0; i < usageValues.size(); i++)
        {
            ReleaseAssert(fty->getParamType(static_cast<uint32_t>(i)) == usageValues[i]->getType());
        }
    }

    if (m_processKind == BytecodeIrComponentKind::Main && m_bytecodeDef->HasQuickeningSlowPath())
    {
        // If we are the main function and we are a quickening bytecode, we need to check that the quickening condition holds.
        // We can only run 'm_impl' if the condition holds. If not, we must transfer control to the quickening slow path.
        //
        TypeBasedHCSHelper::GenerateCheckConditionLogic(this, usageValues, currentBlock /*inout*/);

        // At the end of 'currentBlock', we've passed all checks. Now we can call our specialized fastpath 'm_impl' which assumes these checks are true
        //
        CallInst::Create(m_impl, usageValues, "", currentBlock);
        new UnreachableInst(ctx, currentBlock);
    }
    else
    {
        // Otherwise, it's as simple as calling 'm_impl'
        //
        CallInst::Create(m_impl, usageValues, "", currentBlock);
        new UnreachableInst(ctx, currentBlock);
    }

    ValidateLLVMFunction(m_wrapper);
}

}   // namespace dast
