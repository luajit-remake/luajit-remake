#include "deegen_jit_impl_creator_base.h"
#include "deegen_ast_inline_cache.h"
#include "deegen_register_pinning_scheme.h"
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
#include "deegen_dfg_jit_impl_creator.h"
#include "deegen_baseline_jit_impl_creator.h"
#include "invoke_clang_helper.h"
#include "llvm_override_option.h"
#include "llvm/Linker/Linker.h"

namespace dast {

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
    if (!DeegenPlaceholderUtils::IsConstantPlaceholderAlreadyDefined(GetModule(), ordinal))
    {
        return CreateConstantPlaceholderForOperand(ordinal, operandTy, lb, ub, insertBefore);
    }

    CallInst* ci = DeegenPlaceholderUtils::GetConstantPlaceholderForOperand(GetModule(), ordinal, operandTy, insertBefore);
    m_stencilRcInserter.WidenRange(ordinal, lb, ub);
    return ci;
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

JitImplCreatorBase::JitImplCreatorBase(BytecodeIrInfo* bii, BytecodeIrComponent& bic)
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
    ReleaseAssert(bii != nullptr);
    m_biiInfo = bii;
    m_bicInfo = &bic;
    m_isSlowPathReturnContinuation = false;
    m_numGenericIcCaptures = 0;
}

JitImplCreatorBase::JitImplCreatorBase(JitImplCreatorBase::SlowPathReturnContinuationTag, BytecodeIrInfo* bii, BytecodeIrComponent& bic)
    : JitImplCreatorBase(bii, bic)
{
    ReleaseAssert(m_processKind == BytecodeIrComponentKind::ReturnContinuation);
    m_isSlowPathReturnContinuation = true;
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

    JitSlowPathDataLayoutBase* jitSlowPathDataLayout = GetJitSlowPathDataLayoutBase();

    ReleaseAssert(m_wrapper == nullptr);

    // Create the wrapper function
    // We can also change the linkage of 'm_impl' back to internal now, since the wrapper function will keep it alive
    //
    ReleaseAssert(m_impl->hasExternalLinkage());
    m_impl->setLinkage(GlobalValue::InternalLinkage);

    bool isReturnContinuation = (m_processKind == BytecodeIrComponentKind::ReturnContinuation);
    SetExecFnContext(ExecutorFunctionContext::Create(GetTier(), !IsJitSlowPath() /*isJitCode*/, isReturnContinuation));

    m_wrapper = GetExecFnContext()->CreateFunction(m_module.get(), m_resultFuncName);

    // The function is temporarily no_return before the control transfer APIs are lowered to tail call
    //
    m_wrapper->addFnAttr(Attribute::NoReturn);
    m_wrapper->addFnAttr(Attribute::NoInline);

    BasicBlock* entryBlock = BasicBlock::Create(ctx, "", m_wrapper);
    BasicBlock* currentBlock = entryBlock;

    if (!isReturnContinuation)
    {
        // Note that we also set parameter names here.
        // These are not required, but just to make dumps more readable
        //
        Value* coroCtx = GetExecFnContext()->GetValueAtEntry<RPV_CoroContext>();
        coroCtx->setName(x_coroutineCtx);
        m_valuePreserver.Preserve(x_coroutineCtx, coroCtx);

        Value* stackBase = GetExecFnContext()->GetValueAtEntry<RPV_StackBase>();
        stackBase->setName(x_stackBase);
        m_valuePreserver.Preserve(x_stackBase, stackBase);

        // Depending on the tier, this is the BaselineCodeBlock or the DfgCodeBlock
        //
        Value* jitCodeBlock = GetExecFnContext()->GetValueAtEntry<RPV_CodeBlock>();
        jitCodeBlock->setName(GetJitCodeBlockLLVMVarName());
        m_valuePreserver.Preserve(GetJitCodeBlockLLVMVarName(), jitCodeBlock);

        // The SlowPathData pointer is only useful (and valid) for slow path
        //
        if (IsJitSlowPath())
        {
            Value* jitSlowPathData = GetExecFnContext()->GetValueAtEntry<RPV_JitSlowPathData>();
            jitSlowPathData->setName(x_jitSlowPathData);
            m_valuePreserver.Preserve(x_jitSlowPathData, jitSlowPathData);
        }
    }
    else
    {
        Value* coroCtx = GetExecFnContext()->GetValueAtEntry<RPV_CoroContext>();
        coroCtx->setName(x_coroutineCtx);
        m_valuePreserver.Preserve(x_coroutineCtx, coroCtx);

        UnreachableInst* tmpInst = new UnreachableInst(ctx, currentBlock);
        Value* calleeStackBase = GetExecFnContext()->GetValueAtEntry<RPV_StackBase>();
        Value* stackBase = CallDeegenCommonSnippet("GetCallerStackBaseFromStackBase", { calleeStackBase }, tmpInst);
        m_valuePreserver.Preserve(x_stackBase, stackBase);
        tmpInst->eraseFromParent();

        Value* retStart = GetExecFnContext()->GetValueAtEntry<RPV_RetValsPtr>();
        retStart->setName(x_retStart);
        m_valuePreserver.Preserve(x_retStart, retStart);

        Value* numRet = GetExecFnContext()->GetValueAtEntry<RPV_NumRetVals>();
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
            Value* val = CreateConstantPlaceholderForOperand(104 /*ordinal*/,
                                                             llvm_type_of<uint64_t>(ctx),
                                                             1,
                                                             m_stencilRcInserter.GetLowAddrRangeUB(),
                                                             currentBlock);
            Value* jitCodeBlockHeapPtr = new IntToPtrInst(val, llvm_type_of<HeapPtr<void>>(ctx), "", currentBlock);
            Value* jitCodeBlock = CreateCallToDeegenCommonSnippet(GetModule(), "SimpleTranslateToRawPointer", { jitCodeBlockHeapPtr }, currentBlock);
            ReleaseAssert(llvm_value_has_type<void*>(jitCodeBlock));
            m_valuePreserver.Preserve(GetJitCodeBlockLLVMVarName(), jitCodeBlock);
        }
    }

    std::unordered_map<uint64_t /*operandOrd*/, uint64_t /*argOrd*/> alreadyDecodedArgs;
    if (m_processKind == BytecodeIrComponentKind::QuickeningSlowPath)
    {
        // Only baseline JIT fast path attempts to pass bytecode operands to slow path
        // DFG does not pass bytecode operands to the quickening slow path since it breaks reg alloc
        //
        if (IsBaselineJIT())
        {
            ReleaseAssert(m_bytecodeDef->HasQuickeningSlowPath());
            alreadyDecodedArgs = TypeBasedHCSHelper::GetQuickeningSlowPathAdditionalArgs(m_bytecodeDef);
        }
    }

    // For DFG JIT fast path, some TValue operands may come directly in register
    //
    std::unordered_set<size_t /*operandOrd*/> operandsInRegister;
    if (!IsJitSlowPath() && IsDfgJIT())
    {
        for (auto& operand : m_bytecodeDef->m_list)
        {
            if (AsDfgJIT()->OperandEligibleForRegAlloc(operand.get()))
            {
                if (AsDfgJIT()->IsOperandRegAllocated(operand.get()))
                {
                    ReleaseAssert(!operandsInRegister.count(operand->OperandOrdinal()));
                    operandsInRegister.insert(operand->OperandOrdinal());
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
        Value* outputSlot = nullptr;
        if (!IsJitSlowPath())
        {
            if (IsDfgJIT() && AsDfgJIT()->IsOutputRegAllocated())
            {
                // The output is reg-allocated, no output slot exists
                //
                outputSlot = nullptr;
            }
            else
            {
                outputSlot = CreateConstantPlaceholderForOperand(100 /*operandOrd*/,
                                                                 llvm_type_of<uint64_t>(ctx),
                                                                 0 /*valueLowerBound*/,
                                                                 1048576 /*valueUpperBound*/,
                                                                 currentBlock);
            }
        }
        else
        {
            // In slow path, the output is always stored to a slot in the stack,
            // even if it is reg alloc'ed in the fast path (in that case it would be stored to the spill slot for that reg)
            //
            outputSlot = jitSlowPathDataLayout->m_outputDest.EmitGetValueLogic(GetJitSlowPathData(), currentBlock);
        }
        if (outputSlot != nullptr)
        {
            ReleaseAssert(llvm_value_has_type<uint64_t>(outputSlot));
            m_valuePreserver.Preserve(x_outputSlot, outputSlot);
            outputSlot->setName(x_outputSlot);
        }
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
            if (!IsDfgJIT())
            {
                JitSlowPathDataJitAddress fallthroughTargetFromSlowPathData = jitSlowPathDataLayout->GetFallthroughJitAddress();
                fallthroughTarget = fallthroughTargetFromSlowPathData.EmitGetValueLogic(GetJitSlowPathData(), currentBlock);
            }
            else
            {
                fallthroughTarget = jitSlowPathDataLayout->AsDfg()->m_dfgFallthroughJitAddr.EmitGetValueLogic(GetJitSlowPathData(), currentBlock);
            }
        }
        ReleaseAssert(llvm_value_has_type<void*>(fallthroughTarget));
        m_valuePreserver.Preserve(x_fallthroughDest, fallthroughTarget);
        fallthroughTarget->setName(x_fallthroughDest);
    }

    if (IsBaselineJIT())
    {
        if (AsBaselineJIT()->HasCondBrTarget())
        {
            // In baseline JIT, the bytecode directly branches to its target
            //
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
                condBrTarget = jitSlowPathDataLayout->AsBaseline()->m_condBrJitAddr.EmitGetValueLogic(GetJitSlowPathData(), currentBlock);
            }
            ReleaseAssert(llvm_value_has_type<void*>(condBrTarget));
            m_valuePreserver.Preserve(x_condBrDest, condBrTarget);
            condBrTarget->setName(x_condBrDest);
        }
    }
    else
    {
        ReleaseAssert(IsDfgJIT());
        if (AsDfgJIT()->HasBranchDecisionOutput())
        {
            // In DFG JIT, whether the branch is taken is outputted as a branch decision stored into a GPR or spilled
            //
            if (AsDfgJIT()->IsBranchDecisionRegAllocated())
            {
               // Branch decision reg-alloc'ed, nothing to do
               //
            }
            else
            {
                Value* brDecision;
                if (!IsJitSlowPath())
                {
                    brDecision = CreateConstantPlaceholderForOperand(105 /*operandOrd*/,
                                                                     llvm_type_of<uint64_t>(ctx),
                                                                     0 /*valueLowerBound*/,
                                                                     1048576 /*valueUpperBound*/,
                                                                     currentBlock);
                }
                else
                {
                    brDecision = jitSlowPathDataLayout->AsDfg()->m_condBrDecisionSlot.EmitGetValueLogic(GetJitSlowPathData(), currentBlock);
                    ReleaseAssert(llvm_value_has_type<uint16_t>(brDecision));
                    brDecision = CastInst::CreateZExtOrBitCast(brDecision, llvm_type_of<uint64_t>(ctx), "", currentBlock);
                }
                ReleaseAssert(llvm_value_has_type<uint64_t>(brDecision));
                m_valuePreserver.Preserve(x_brDecision, brDecision);
                brDecision->setName(x_brDecision);
            }
        }
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
                ReleaseAssert(IsDfgJIT());
                Value* val = AsDfgJIT()->CreatePlaceholderForRegisterAllocatedOperand(operand->OperandOrdinal(), currentBlock);
                usageValues.push_back(val);
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

    ReleaseAssert(usageValues.size() == m_bytecodeDef->m_list.size());

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

JitImplCreatorBase::JitImplCreatorBase(JitImplCreatorBase* other)
    : DeegenBytecodeImplCreatorBase(other)
{
    m_biiInfo = other->m_biiInfo;
    m_bicInfo = other->m_bicInfo;
    m_isSlowPathReturnContinuation = other->m_isSlowPathReturnContinuation;
    if (other->m_impl == nullptr)
    {
        m_impl = nullptr;
    }
    else
    {
        m_impl = m_module->getFunction(other->m_impl->getName());
        ReleaseAssert(m_impl != nullptr);
    }
    if (other->m_wrapper == nullptr)
    {
        m_wrapper = nullptr;
    }
    else
    {
        m_wrapper = m_module->getFunction(other->m_wrapper->getName());
        ReleaseAssert(m_wrapper != nullptr);
    }
    m_resultFuncName = other->m_resultFuncName;
    m_stencilRcInserter = other->m_stencilRcInserter;
    m_stencilRcDefinitions = other->m_stencilRcDefinitions;
    m_numGenericIcCaptures = other->m_numGenericIcCaptures;
    m_generated = other->m_generated;
}

std::string WARN_UNUSED JitImplCreatorBase::CompileToAssemblyFileForStencilGeneration()
{
    using namespace llvm;

    // Compile the function to ASM (.s) file
    // TODO: ideally we should think about properly setting function and loop alignments
    // Currently we simply ignore alignments, which is fine for now since currently our none of our JIT fast path contains loops.
    // But this will break down once any loop is introduced. To fix this, we need to be aware of the aligned BBs and manually generate NOPs.
    //
    return CompileLLVMModuleToAssemblyFileForStencilGeneration(
        m_module.get(),
        llvm::Reloc::Static,
        llvm::CodeModel::Small,
        [](TargetOptions& opt) {
            // This is the option that is equivalent to the clang -fdata-sections flag
            // Put each data symbol into a separate data section so our stencil creation pass can produce more efficient result
            //
            opt.DataSections = true;
        });
}

JitSlowPathDataLayoutBase* WARN_UNUSED JitImplCreatorBase::GetJitSlowPathDataLayoutBase()
{
    if (IsBaselineJIT())
    {
        return AsBaselineJIT()->GetBaselineJitSlowPathDataLayout();
    }
    else
    {
        ReleaseAssert(IsDfgJIT());
        return AsDfgJIT()->GetDfgJitSlowPathDataLayout();
    }
}

bool WARN_UNUSED JitImplCreatorBase::IsLastJitStencilInBytecode()
{
    ReleaseAssert(!IsJitSlowPath());

    if (m_processKind == BytecodeIrComponentKind::Main)
    {
        ReleaseAssert(GetBytecodeIrComponentInfo() == GetBytecodeIrInfo()->m_jitMainComponent.get());
        bool hasJitReturnContinuations = false;
        for (std::unique_ptr<BytecodeIrComponent>& rcBic : GetBytecodeIrInfo()->m_allRetConts)
        {
            if (!rcBic->IsReturnContinuationUsedBySlowPathOnly())
            {
                hasJitReturnContinuations = true;
            }
        }
        return !hasJitReturnContinuations;
    }
    else
    {
        ReleaseAssert(m_processKind == BytecodeIrComponentKind::ReturnContinuation);
        bool foundThisRc = false;
        bool hasMoreJitRcAfterThisOne = false;
        for (std::unique_ptr<BytecodeIrComponent>& rcBic : GetBytecodeIrInfo()->m_allRetConts)
        {
            if (rcBic.get() == GetBytecodeIrComponentInfo())
            {
                ReleaseAssert(!foundThisRc);
                foundThisRc = true;
                ReleaseAssert(!rcBic->IsReturnContinuationUsedBySlowPathOnly());
            }
            else if (foundThisRc && !rcBic->IsReturnContinuationUsedBySlowPathOnly())
            {
                hasMoreJitRcAfterThisOne = true;
            }
        }
        ReleaseAssert(foundThisRc);
        return !hasMoreJitRcAfterThisOne;
    }
}

}   // namespace dast
