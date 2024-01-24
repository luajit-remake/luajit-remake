#include "deegen_jit_slow_path_data.h"
#include "deegen_bytecode_operand.h"

namespace dast {

llvm::Value* WARN_UNUSED JitSlowPathDataFieldBase::EmitGetFieldAddressLogic(llvm::Value* slowPathDataAddr, llvm::BasicBlock* insertAtEnd)
{
    using namespace llvm;
    UnreachableInst* dummy = new UnreachableInst(slowPathDataAddr->getContext(), insertAtEnd);
    Value* res = EmitGetFieldAddressLogic(slowPathDataAddr, dummy);
    dummy->eraseFromParent();
    return res;
}

llvm::Value* WARN_UNUSED JitSlowPathDataFieldBase::EmitGetFieldAddressLogic(llvm::Value* slowPathDataAddr, llvm::Instruction* insertBefore)
{
    using namespace llvm;
    ReleaseAssert(IsValid());
    LLVMContext& ctx = slowPathDataAddr->getContext();
    size_t offset = GetFieldOffset();
    ReleaseAssert(llvm_value_has_type<void*>(slowPathDataAddr));
    GetElementPtrInst* gep = GetElementPtrInst::CreateInBounds(llvm_type_of<uint8_t>(ctx), slowPathDataAddr,
                                                               { CreateLLVMConstantInt<uint64_t>(ctx, offset) }, "", insertBefore);
    return gep;
}

llvm::Value* WARN_UNUSED JitSlowPathDataJitAddress::EmitGetValueLogic(llvm::Value* slowPathDataAddr, llvm::BasicBlock* insertAtEnd)
{
    using namespace llvm;
    UnreachableInst* dummy = new UnreachableInst(slowPathDataAddr->getContext(), insertAtEnd);
    Value* res = EmitGetValueLogic(slowPathDataAddr, dummy);
    dummy->eraseFromParent();
    return res;
}

llvm::Value* WARN_UNUSED JitSlowPathDataJitAddress::EmitGetValueLogic(llvm::Value* slowPathDataAddr, llvm::Instruction* insertBefore)
{
    using namespace llvm;
    ReleaseAssert(IsValid());
    LLVMContext& ctx = slowPathDataAddr->getContext();
    size_t offset = GetFieldOffset();
    ReleaseAssert(llvm_value_has_type<void*>(slowPathDataAddr));
    GetElementPtrInst* gep = GetElementPtrInst::CreateInBounds(llvm_type_of<uint8_t>(ctx), slowPathDataAddr,
                                                               { CreateLLVMConstantInt<uint64_t>(ctx, offset) }, "", insertBefore);

    ReleaseAssert(llvm_value_has_type<void*>(gep));
    Value* addr32 = new LoadInst(llvm_type_of<int32_t>(ctx), gep, "", false /*isVolatile*/, Align(1), insertBefore);

    // ZExt/SExt doesn't matter because the address is < 2GB
    //
    Value* addr64 = new ZExtInst(addr32, llvm_type_of<uint64_t>(ctx), "", insertBefore);

    Value* ptr = new IntToPtrInst(addr64, llvm_type_of<void*>(ctx), "", insertBefore);
    return ptr;
}

void JitSlowPathDataJitAddress::EmitSetValueLogic(llvm::Value* slowPathDataAddr, llvm::Value* value, llvm::Instruction* insertBefore)
{
    using namespace llvm;
    ReleaseAssert(IsValid());
    LLVMContext& ctx = slowPathDataAddr->getContext();

    ReleaseAssert(llvm_value_has_type<void*>(slowPathDataAddr));
    ReleaseAssert(llvm_value_has_type<void*>(value));
    size_t offset = GetFieldOffset();
    ReleaseAssert(GetFieldSize() == 4);
    Value* addr = GetElementPtrInst::CreateInBounds(llvm_type_of<uint8_t>(ctx), slowPathDataAddr,
                                                    { CreateLLVMConstantInt<uint64_t>(ctx, offset) }, "", insertBefore);

    Value* val64 = new PtrToIntInst(value, llvm_type_of<uint64_t>(ctx), "", insertBefore);
    Value* val32 = new TruncInst(val64, llvm_type_of<uint32_t>(ctx), "", insertBefore);
    new StoreInst(val32, addr, false /*isVolatile*/, Align(1), insertBefore);
}

void JitSlowPathDataJitAddress::EmitSetValueLogic(llvm::Value* slowPathDataAddr, llvm::Value* value, llvm::BasicBlock* insertAtEnd)
{
    using namespace llvm;
    UnreachableInst* dummy = new UnreachableInst(slowPathDataAddr->getContext(), insertAtEnd);
    EmitSetValueLogic(slowPathDataAddr, value, dummy);
    dummy->eraseFromParent();
}

llvm::Value* WARN_UNUSED JitSlowPathDataBcOperand::EmitGetValueLogic(llvm::Value* slowPathDataAddr, llvm::BasicBlock* insertAtEnd)
{
    using namespace llvm;
    UnreachableInst* dummy = new UnreachableInst(slowPathDataAddr->getContext(), insertAtEnd);
    Value* res = EmitGetValueLogic(slowPathDataAddr, dummy);
    dummy->eraseFromParent();
    return res;
}

llvm::Value* WARN_UNUSED JitSlowPathDataBcOperand::EmitGetValueLogic(llvm::Value* slowPathDataAddr, llvm::Instruction* insertBefore)
{
    ReleaseAssert(IsValid());
    BcOperand* operand = GetBcOperand();
    size_t offset = GetFieldOffset();
    size_t size = GetFieldSize();
    return operand->GetOperandValueFromStorage(slowPathDataAddr, offset, size, insertBefore);
}

void JitSlowPathDataBcOperand::EmitSetValueLogic(llvm::Value* slowPathDataAddr, llvm::Value* value, llvm::Instruction* insertBefore)
{
    using namespace llvm;
    ReleaseAssert(IsValid());
    ReleaseAssert(value != nullptr);

    LLVMContext& ctx = slowPathDataAddr->getContext();
    size_t offset = GetFieldOffset();
    size_t size = GetFieldSize();
    ReleaseAssert(size > 0);

    ReleaseAssert(llvm_value_has_type<void*>(slowPathDataAddr));

    Value* src = value;
    ReleaseAssert(src->getType()->isIntegerTy());
    size_t srcBitWidth = src->getType()->getIntegerBitWidth();
    ReleaseAssert(srcBitWidth >= size * 8);

    Type* dstTy = Type::getIntNTy(ctx, static_cast<unsigned int>(size * 8));
    Value* dst;
    if (srcBitWidth == size * 8)
    {
        dst = src;
    }
    else
    {
        dst = new TruncInst(src, dstTy, "", insertBefore);
    }
    ReleaseAssert(dst->getType() == dstTy);

    Value* addr = GetElementPtrInst::CreateInBounds(llvm_type_of<uint8_t>(ctx), slowPathDataAddr,
                                                    { CreateLLVMConstantInt<uint64_t>(ctx, offset) }, "", insertBefore);
    new StoreInst(dst, addr, false /*isVolatile*/, Align(1), insertBefore);
}

void JitSlowPathDataBcOperand::EmitSetValueLogic(llvm::Value* slowPathDataAddr, llvm::Value* value, llvm::BasicBlock* insertAtEnd)
{
    using namespace llvm;
    UnreachableInst* dummy = new UnreachableInst(slowPathDataAddr->getContext(), insertAtEnd);
    EmitSetValueLogic(slowPathDataAddr, value, dummy);
    dummy->eraseFromParent();
}

llvm::Value* WARN_UNUSED JitSlowPathDataRawLiteral::EmitGetValueLogic(llvm::Value* slowPathDataAddr, llvm::BasicBlock* insertAtEnd)
{
    using namespace llvm;
    UnreachableInst* dummy = new UnreachableInst(slowPathDataAddr->getContext(), insertAtEnd);
    Value* res = EmitGetValueLogic(slowPathDataAddr, dummy);
    dummy->eraseFromParent();
    return res;
}

llvm::Value* WARN_UNUSED JitSlowPathDataRawLiteral::EmitGetValueLogic(llvm::Value* slowPathDataAddr, llvm::Instruction* insertBefore)
{
    using namespace llvm;
    LLVMContext& ctx = slowPathDataAddr->getContext();
    ReleaseAssert(IsValid());
    ReleaseAssert(llvm_value_has_type<void*>(slowPathDataAddr));
    size_t offset = GetFieldOffset();
    size_t size = GetFieldSize();
    ReleaseAssert(size > 0 && size <= 8 && is_power_of_2(size));
    Type* valTy = Type::getIntNTy(ctx, static_cast<unsigned int>(size * 8));
    GetElementPtrInst* gep = GetElementPtrInst::CreateInBounds(llvm_type_of<uint8_t>(ctx), slowPathDataAddr,
                                                               { CreateLLVMConstantInt<uint64_t>(ctx, offset) }, "", insertBefore);
    Value* val = new LoadInst(valTy, gep, "", false /*isVolatile*/, Align(1), insertBefore);
    return val;
}

JitSlowPathDataJitAddress JitSlowPathDataLayoutBase::GetFallthroughJitAddress()
{
    ReleaseAssert(IsInitialized());
    ReleaseAssert(m_totalLength != static_cast<size_t>(-1));
    // Note that the jitAddr of every SlowPathData is always at offset x_opcodeSizeBytes,
    // so we can decode without knowing anything about the next SlowPathData
    //
    JitSlowPathDataJitAddress addr;
    addr.SetValid();
    addr.SetFieldOffset(m_totalLength + BytecodeVariantDefinition::x_opcodeSizeBytes);
    return addr;
}

// Currently the baseline JIT slow path data is layouted as follow:
//     2-byte opcode
//     4-byte jitAddr -- the JIT'ed fast path address for this bytecode
//     4-byte condBrJitAddr -- exists if this bytecode can branch, the JIT'ed address to branch to
//     4-byte condBrBytecodeIndex -- exists if this bytecode can branch, the index of the bytecode target
//     All the bytecode input operands
//     Bytecode output operand, if exists
//     Call IC sites, if exists
//     jitSlowPathAddr and jitDataSecAddr, if generic IC sites exist
//     Generic IC sites, if exist
//
void BaselineJitSlowPathDataLayout::ComputeLayout(BytecodeVariantDefinition* bvd)
{
    ReleaseAssert(bvd->IsBytecodeStructLengthFinalized());
    ReleaseAssert(!IsInitialized());

    size_t currentOffset = BytecodeVariantDefinition::x_opcodeSizeBytes;
    size_t totalValidFields = 0;

    auto assignOffsetAndAdvance = [&](JitSlowPathDataFieldBase& field)
    {
        field.SetValid();
        field.SetFieldOffset(currentOffset);
        currentOffset += field.GetFieldSize();
        totalValidFields++;
    };

    // Reserve space for jitAddr
    //
    // Must come first, a lot of places expect that
    //
    assignOffsetAndAdvance(m_jitAddr);

    // Reserve space for condBrJitAddr and condBrBytecodeIndex, if needed
    //
    if (bvd->m_hasConditionalBranchTarget)
    {
        // Must be allocated adjacently in this order, baseline JIT expects that
        //
        assignOffsetAndAdvance(m_condBrJitAddr);
        assignOffsetAndAdvance(m_condBrBcIndex);
        ReleaseAssert(m_condBrJitAddr.GetFieldSize() == 4 && m_condBrBcIndex.GetFieldSize() == 4);
        ReleaseAssert(m_condBrJitAddr.GetFieldOffset() + 4 == m_condBrBcIndex.GetFieldOffset());
    }

    auto assignForBcOperand = [&](JitSlowPathDataBcOperand& target, BcOperand* operand, size_t maxWidthBytes)
    {
        if (operand->IsElidedFromBytecodeStruct())
        {
            return;
        }

        size_t operandMaxWidth = operand->ValueFullByteLength();
        size_t width = std::min(operandMaxWidth, maxWidthBytes);

        target.SetBcOperand(operand);
        target.SetFieldSize(width);
        assignOffsetAndAdvance(target);
    };

    // For now for simplicity, just hardcode 2-byte operands similar to what we have assumed for the bytecode structs.
    //
    m_operands.resize(bvd->m_list.size());
    for (auto& operand : bvd->m_list)
    {
        size_t ord = operand->OperandOrdinal();
        ReleaseAssert(ord < m_operands.size());
        assignForBcOperand(m_operands[ord], operand.get(), 2 /*maxWidthBytes*/);
    }

    if (bvd->m_hasOutputValue)
    {
        assignForBcOperand(m_outputDest, bvd->m_outputOperand.get(), 2 /*maxWidthBytes*/);
    }

    // Reserve space for each Call IC site
    //
    {
        size_t numCallICs = bvd->GetNumCallICsInJitTier();
        if (numCallICs > 0)
        {
            m_callICs.SetInfo(numCallICs, sizeof(JitCallInlineCacheSite));
            assignOffsetAndAdvance(m_callICs);
        }
    }

    // Reserve space for each generic IC site, and if generic IC exists, we also need to record address for JIT slow path and data section
    //
    {
        size_t numGenericICs = bvd->GetNumGenericICsInJitTier();
        if (numGenericICs > 0)
        {
            assignOffsetAndAdvance(m_jitSlowPathAddr);
            assignOffsetAndAdvance(m_jitDataSecAddr);

            m_genericICs.SetInfo(numGenericICs, sizeof(JitGenericInlineCacheSite));
            assignOffsetAndAdvance(m_genericICs);
        }
    }

    m_totalLength = currentOffset;
    m_totalValidFields = totalValidFields;
}

}   // namespace dast
