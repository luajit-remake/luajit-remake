#include "deegen_jit_slow_path_data.h"
#include "deegen_bytecode_operand.h"
#include "deegen_dfg_jit_impl_creator.h"
#include "drt/dfg_slowpath_register_config_helper.h"

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

    return EmitGetValueFromFieldAddrLogic(gep, insertBefore);
}

llvm::Value* WARN_UNUSED JitSlowPathDataJitAddress::EmitGetValueFromFieldAddrLogic(llvm::Value* fieldAddr, llvm::Instruction* insertBefore)
{
    using namespace llvm;
    LLVMContext& ctx = fieldAddr->getContext();
    ReleaseAssert(llvm_value_has_type<void*>(fieldAddr));

    Value* addr32 = new LoadInst(llvm_type_of<int32_t>(ctx), fieldAddr, "", false /*isVolatile*/, Align(1), insertBefore);

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

bool JitSlowPathDataBcOperand::IsEqualImpl(JitSlowPathDataBcOperand& other)
{
    CHECK(IsBaseEqual(other));
    if (!IsValid()) { return true; }
    CHECK(GetBcOperand()->OperandName() == other.GetBcOperand()->OperandName());
    CHECK(GetBcOperand()->HasOperandOrdinal() == other.GetBcOperand()->HasOperandOrdinal());
    if (GetBcOperand()->HasOperandOrdinal())
    {
        CHECK(GetBcOperand()->OperandOrdinal() == other.GetBcOperand()->OperandOrdinal());
    }
    return true;
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
    ReleaseAssert(!IsDfgJIT() && "this function must not be used by DFG!");
    // Cannot get length of the SlowPathData unless the layout has been finalized
    //
    ReleaseAssert(IsInitialized() && IsLayoutFinalized());
    size_t length = GetTotalLength();
    // Note that the jitAddr of every SlowPathData is always at offset x_opcodeSizeBytes,
    // so we can decode without knowing anything about the next SlowPathData
    //
    JitSlowPathDataJitAddress addr;
    addr.SetValid();
    addr.SetFieldOffset(length + BytecodeVariantDefinition::x_opcodeSizeBytes);
    return addr;
}

llvm::Value* WARN_UNUSED JitSlowPathDataLayoutBase::GetFallthroughJitAddressUsingPlaceholder(llvm::Module* module, llvm::Value* slowPathDataPtr, llvm::Instruction* insertBefore)
{
    using namespace llvm;
    LLVMContext& ctx = module->getContext();

    ReleaseAssert(!IsDfgJIT() && "this function must not be used by DFG!");
    ReleaseAssert(llvm_value_has_type<void*>(slowPathDataPtr));

    if (module->getNamedValue(x_slow_path_data_length_placeholder_name) == nullptr)
    {
        GlobalVariable* gv = new GlobalVariable(
            *module,
            llvm_type_of<uint64_t>(ctx),
            true /*isConstant*/,
            GlobalValue::ExternalLinkage,
            nullptr /*initializer*/,
            x_slow_path_data_length_placeholder_name);
        ReleaseAssert(gv->getName() == x_slow_path_data_length_placeholder_name);
    }

    GlobalVariable* gv = module->getGlobalVariable(x_slow_path_data_length_placeholder_name);
    ReleaseAssert(gv != nullptr);
    ReleaseAssert(llvm_type_has_type<uint64_t>(gv->getValueType()));

    Value* slowPathDataLen = new LoadInst(llvm_type_of<uint64_t>(ctx), gv, "", insertBefore);
    Value* offset = CreateUnsignedAddNoOverflow(slowPathDataLen, CreateLLVMConstantInt<uint64_t>(ctx, BytecodeVariantDefinition::x_opcodeSizeBytes), insertBefore);

    Value* fieldAddr = GetElementPtrInst::CreateInBounds(
        llvm_type_of<uint8_t>(ctx), slowPathDataPtr, { offset }, "", insertBefore);

    Value* jitAddr = JitSlowPathDataJitAddress::EmitGetValueFromFieldAddrLogic(fieldAddr, insertBefore);
    ReleaseAssert(llvm_value_has_type<void*>(jitAddr));
    return jitAddr;
}

llvm::Value* WARN_UNUSED JitSlowPathDataLayoutBase::GetFallthroughJitAddressUsingPlaceholder(llvm::Module* module, llvm::Value* slowPathDataPtr, llvm::BasicBlock* insertAtEnd)
{
    using namespace llvm;
    UnreachableInst* dummy = new UnreachableInst(module->getContext(), insertAtEnd);
    Value* res = GetFallthroughJitAddressUsingPlaceholder(module, slowPathDataPtr, dummy);
    dummy->eraseFromParent();
    return res;
}

// TODO: this class should be removed, and use the facilities in JitSlowPathDataLayoutBase directly
//
struct JitSlowPathDataLayoutBuilder
{
    JitSlowPathDataLayoutBuilder()
        : m_currentOffset(0)
        , m_totalValidFields(0)
    { }

    void AssignOffsetAndAdvance(JitSlowPathDataFieldBase& field)
    {
        ReleaseAssert(!field.IsValid());
        field.SetValid();
        field.SetFieldOffset(m_currentOffset);
        m_currentOffset += field.GetFieldSize();
        m_totalValidFields++;
    }

    void FinishSetup(JitSlowPathDataLayoutBase& layout)
    {
        ReleaseAssert(!layout.IsInitialized());
        layout.m_totalLength = m_currentOffset;
        layout.m_totalValidFields = m_totalValidFields;
        ReleaseAssert(layout.IsInitialized() && !layout.IsLayoutFinalized());
    }

    size_t m_currentOffset;
    size_t m_totalValidFields;
};

void JitSlowPathDataLayoutBase::SetupHeader(JitSlowPathDataLayoutBuilder& builder /*inout*/)
{
    ReleaseAssert(builder.m_currentOffset == 0);
    builder.m_currentOffset = BytecodeVariantDefinition::x_opcodeSizeBytes;
    builder.AssignOffsetAndAdvance(m_jitAddr);
}

void JitSlowPathDataLayoutBase::SetupOperandsAndOutput(JitSlowPathDataLayoutBuilder& builder /*inout*/, BytecodeVariantDefinition* bvd)
{
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
        builder.AssignOffsetAndAdvance(target);
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
}

void JitSlowPathDataLayoutBase::SetupCallIcSiteArray(JitSlowPathDataLayoutBuilder& builder /*inout*/, size_t numCallIcSites)
{
    ReleaseAssert(!m_callICs.IsValid());
    if (numCallIcSites > 0)
    {
        // Reserve space for each Call IC site
        //
        m_callICs.SetInfo(numCallIcSites, sizeof(JitCallInlineCacheSite));
        builder.AssignOffsetAndAdvance(m_callICs);
    }
}

void JitSlowPathDataLayoutBase::SetupGenericIcSiteArray(JitSlowPathDataLayoutBuilder& builder /*inout*/, size_t numGenericIcSites)
{
    ReleaseAssert(!m_genericICs.IsValid());
    if (numGenericIcSites > 0)
    {
        // Reserve space for each generic IC site, and if generic IC exists, we also need to record address for JIT slow path and data section
        //
        builder.AssignOffsetAndAdvance(m_jitSlowPathAddr);
        builder.AssignOffsetAndAdvance(m_jitDataSecAddr);

        m_genericICs.SetInfo(numGenericIcSites, sizeof(JitGenericInlineCacheSite));
        builder.AssignOffsetAndAdvance(m_genericICs);
    }
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

    JitSlowPathDataLayoutBuilder builder;

    SetupHeader(builder /*inout*/);

    // Reserve space for condBrJitAddr and condBrBytecodeIndex, if needed
    //
    if (bvd->m_hasConditionalBranchTarget)
    {
        // Must be allocated adjacently in this order, baseline JIT expects that
        //
        builder.AssignOffsetAndAdvance(m_condBrJitAddr);
        builder.AssignOffsetAndAdvance(m_condBrBcIndex);
        ReleaseAssert(m_condBrJitAddr.GetFieldSize() == 4 && m_condBrBcIndex.GetFieldSize() == 4);
        ReleaseAssert(m_condBrJitAddr.GetFieldOffset() + 4 == m_condBrBcIndex.GetFieldOffset());
    }

    SetupOperandsAndOutput(builder /*inout*/, bvd);

    SetupCallIcSiteArray(builder /*inout*/, bvd->GetNumCallICsInBaselineJitTier());
    SetupGenericIcSiteArray(builder /*inout*/, bvd->GetNumGenericICsInJitTier());

    builder.FinishSetup(*this);
}

void DfgJitSlowPathDataLayout::ComputeLayout(DfgJitImplCreator* ifi)
{
    ReleaseAssert(ifi->GetBytecodeDef()->IsBytecodeStructLengthFinalized());
    ReleaseAssert(!IsInitialized());

    JitSlowPathDataLayoutBuilder builder;

    SetupHeader(builder /*inout*/);

    SetupOperandsAndOutput(builder /*inout*/, ifi->GetBytecodeDef());

    // Call IC is currently always disabled in DFG
    //
    SetupCallIcSiteArray(builder /*inout*/, 0);

    // TODO: we need to adapt this when we support fully-inlined monomorphic IC
    //
    SetupGenericIcSiteArray(builder /*inout*/, ifi->GetBytecodeDef()->GetNumGenericICsInJitTier());

    builder.AssignOffsetAndAdvance(m_dfgFallthroughJitAddr);

    if (ifi->HasBranchDecisionOutput())
    {
        builder.AssignOffsetAndAdvance(m_condBrDecisionSlot);
    }

    if (!ifi->IsFastPathRegAllocAlwaysDisabled())
    {
        if ((m_callICs.IsValid() && m_callICs.GetNumSites() > 0) || (m_genericICs.IsValid() && m_genericICs.GetNumSites() > 0))
        {
            m_compactRegConfig.SetFieldSize(dfg::DfgSlowPathRegConfigDataTraits::x_slowPathDataCompactRegConfigInfoSizeBytes);
            builder.AssignOffsetAndAdvance(m_compactRegConfig);
        }
    }

    builder.FinishSetup(*this);
}

}   // namespace dast
