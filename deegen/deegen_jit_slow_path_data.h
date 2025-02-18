#pragma once

#include "common_utils.h"
#include "deegen_engine_tier.h"
#include "misc_llvm_helper.h"

namespace dast {

// Describes a field in a piece of JIT SlowPathData
//
class JitSlowPathDataFieldBase
{
public:
    virtual ~JitSlowPathDataFieldBase() = default;

    MAKE_DEFAULT_COPYABLE(JitSlowPathDataFieldBase);
    MAKE_DEFAULT_MOVABLE(JitSlowPathDataFieldBase);

    JitSlowPathDataFieldBase()
        : m_isFieldValid(false)
        , m_fieldOffset(static_cast<size_t>(-1))
        , m_fieldSize(static_cast<size_t>(-1))
    { }

    // If this field is invalid, it means this field doesn't exist at all
    //
    bool IsValid() { return m_isFieldValid; }
    void SetValid() { m_isFieldValid = true; }

    size_t GetFieldOffset() { ReleaseAssert(IsValid() && m_fieldOffset != static_cast<size_t>(-1)); return m_fieldOffset; }
    size_t GetFieldSize() { ReleaseAssert(IsValid() && m_fieldSize != static_cast<size_t>(-1)); return m_fieldSize; }

    void SetFieldOffset(size_t offset)
    {
        ReleaseAssert(offset != static_cast<size_t>(-1) && m_fieldOffset == static_cast<size_t>(-1));
        m_fieldOffset = offset;
    }

    void SetFieldSize(size_t size)
    {
        ReleaseAssert(size != static_cast<size_t>(-1) && m_fieldSize == static_cast<size_t>(-1));
        m_fieldSize = size;
    }

    // Returns a void*, the address of this field
    //
    llvm::Value* WARN_UNUSED EmitGetFieldAddressLogic(llvm::Value* slowPathDataAddr, llvm::Instruction* insertBefore);
    llvm::Value* WARN_UNUSED EmitGetFieldAddressLogic(llvm::Value* slowPathDataAddr, llvm::BasicBlock* insertAtEnd);

    // All subclasses must override this function and change the template parameter in IsEqualDispatch to the subclass name
    //
    virtual bool IsEqual(JitSlowPathDataFieldBase& other)
    {
        return IsEqualDispatch<JitSlowPathDataFieldBase>(other);
    }

protected:
    bool IsBaseEqual(JitSlowPathDataFieldBase& other)
    {
        CHECK(m_isFieldValid == other.m_isFieldValid);
        if (!m_isFieldValid)
        {
            return true;
        }
        CHECK(m_fieldOffset == other.m_fieldOffset);
        CHECK(m_fieldSize == other.m_fieldSize);
        return true;
    }

    bool IsEqualImpl(JitSlowPathDataFieldBase& other)
    {
        return IsBaseEqual(other);
    }

    template<typename T>
    bool IsEqualDispatch(JitSlowPathDataFieldBase& other)
    {
        // If a subclass did not override IsEqual, this assertion will catch it
        //
        ReleaseAssert(typeid(*this) == typeid(T));
        T* tt = dynamic_cast<T*>(this);
        ReleaseAssert(tt != nullptr);
        if (typeid(other) != typeid(T))
        {
            // Different true types are always considered not equal
            //
            return false;
        }
        T* tother = dynamic_cast<T*>(&other);
        ReleaseAssert(tother != nullptr);
        static_assert(std::is_same_v<decltype(&T::IsEqualImpl), bool(T::*)(T&)>);
        return tt->IsEqualImpl(*tother);
    }

private:
    bool m_isFieldValid;
    size_t m_fieldOffset;
    size_t m_fieldSize;
};

// Describes an JIT code address in JIT SlowPathData
//
// Currently the address is simply stored as a int32_t value, because all the code resides in the first 2GB address space.
// We might want to change this assumption to support PIC/PIE in the future, but for now let's stay simple.
//
class JitSlowPathDataJitAddress final : public JitSlowPathDataFieldBase
{
public:
    JitSlowPathDataJitAddress()
    {
        SetFieldSize(4);
    }

    static llvm::Value* WARN_UNUSED EmitGetValueFromFieldAddrLogic(llvm::Value* fieldAddr, llvm::Instruction* insertBefore);

    // Returns a void*, the JIT address
    //
    llvm::Value* WARN_UNUSED EmitGetValueLogic(llvm::Value* slowPathDataAddr, llvm::Instruction* insertBefore);
    llvm::Value* WARN_UNUSED EmitGetValueLogic(llvm::Value* slowPathDataAddr, llvm::BasicBlock* insertAtEnd);

    // 'value' should be a void*
    //
    void EmitSetValueLogic(llvm::Value* slowPathDataAddr, llvm::Value* value, llvm::Instruction* insertBefore);
    void EmitSetValueLogic(llvm::Value* slowPathDataAddr, llvm::Value* value, llvm::BasicBlock* insertAtEnd);

    bool IsEqualImpl(JitSlowPathDataJitAddress& other)
    {
        return IsBaseEqual(other);
    }

    virtual bool IsEqual(JitSlowPathDataFieldBase& other) override
    {
        return IsEqualDispatch<JitSlowPathDataJitAddress>(other);
    }
};

class BcOperand;

// Describes a bytecode operand in JIT SlowPathData
//
// For now for simplicity, we hardcode max 2-byte operands similar to what we have assumed for the bytecode structs.
//
class JitSlowPathDataBcOperand final : public JitSlowPathDataFieldBase
{
public:
    JitSlowPathDataBcOperand()
        : m_operand(nullptr)
    { }

    void SetBcOperand(BcOperand* operand)
    {
        ReleaseAssert(m_operand == nullptr && operand != nullptr);
        m_operand = operand;
    }

    BcOperand* GetBcOperand() { ReleaseAssert(m_operand != nullptr); return m_operand; }

    // Returns the operand value (not the operand usage value!), which type depends on the operand
    // For example, a Slot would give you the size_t slot ordinal, not the pointer into the stack frame
    //
    llvm::Value* WARN_UNUSED EmitGetValueLogic(llvm::Value* slowPathDataAddr, llvm::Instruction* insertBefore);
    llvm::Value* WARN_UNUSED EmitGetValueLogic(llvm::Value* slowPathDataAddr, llvm::BasicBlock* insertAtEnd);

    // Counterpart to the GetValue, similar to above, 'value' should be the operand value, not the usage value.
    //
    void EmitSetValueLogic(llvm::Value* slowPathDataAddr, llvm::Value* value, llvm::Instruction* insertBefore);
    void EmitSetValueLogic(llvm::Value* slowPathDataAddr, llvm::Value* value, llvm::BasicBlock* insertAtEnd);

    // Note that this "IsEqual" only checks that the name and operandOrdinal of the operand is equal
    //
    bool IsEqualImpl(JitSlowPathDataBcOperand& other);

    virtual bool IsEqual(JitSlowPathDataFieldBase& other) override
    {
        return IsEqualDispatch<JitSlowPathDataBcOperand>(other);
    }

private:
    BcOperand* m_operand;
};

// A raw integer value in JIT SlowPathData
//
class JitSlowPathDataRawLiteral : public JitSlowPathDataFieldBase
{
public:
    JitSlowPathDataRawLiteral() = default;

    // Returns the integer stored in the field
    //
    llvm::Value* WARN_UNUSED EmitGetValueLogic(llvm::Value* slowPathDataAddr, llvm::Instruction* insertBefore);
    llvm::Value* WARN_UNUSED EmitGetValueLogic(llvm::Value* slowPathDataAddr, llvm::BasicBlock* insertAtEnd);

    bool IsEqualImpl(JitSlowPathDataRawLiteral& other)
    {
        return IsBaseEqual(other);
    }

    virtual bool IsEqual(JitSlowPathDataFieldBase& other) override
    {
        return IsEqualDispatch<JitSlowPathDataRawLiteral>(other);
    }
};

// A integer with statically known width
//
template<typename T>
class JitSlowPathDataInt : public JitSlowPathDataRawLiteral
{
public:
    static_assert(std::is_integral_v<T> && !std::is_same_v<T, bool>);

    JitSlowPathDataInt()
    {
        SetFieldSize(sizeof(T));
    }

    void EmitSetValueLogic(llvm::Value* slowPathDataAddr, llvm::Value* value, llvm::Instruction* insertBefore)
    {
        using namespace llvm;
        ReleaseAssert(IsValid());
        ReleaseAssert(slowPathDataAddr != nullptr && value != nullptr);
        ReleaseAssert(value->getType()->isIntegerTy(sizeof(T) * 8 /*bitWidth*/));
        ReleaseAssert(GetFieldSize() == sizeof(T));

        Value* addr = EmitGetFieldAddressLogic(slowPathDataAddr, insertBefore);
        new StoreInst(value, addr, false /*isVolatile*/, Align(1), insertBefore);
    }

    void EmitSetValueLogic(llvm::Value* slowPathDataAddr, llvm::Value* value, llvm::BasicBlock* insertAtEnd)
    {
        using namespace llvm;
        UnreachableInst* dummy = new UnreachableInst(slowPathDataAddr->getContext(), insertAtEnd);
        EmitSetValueLogic(slowPathDataAddr, value, dummy);
        dummy->eraseFromParent();
    }

    bool IsEqualImpl(JitSlowPathDataInt<T>& other)
    {
        return JitSlowPathDataRawLiteral::IsEqualImpl(other);
    }

    virtual bool IsEqual(JitSlowPathDataFieldBase& other) override
    {
        return IsEqualDispatch<JitSlowPathDataInt<T>>(other);
    }
};

class BytecodeVariantDefinition;
class DfgJitImplCreator;

// An array of IC sites in JIT SlowPathData
//
class JitSlowPathDataIcSiteArray final : public JitSlowPathDataFieldBase
{
public:
    JitSlowPathDataIcSiteArray()
        : m_numSites(static_cast<size_t>(-1))
        , m_sizePerSite(static_cast<size_t>(-1))
    { }

    void SetInfo(size_t numSites, size_t sizePerSite)
    {
        m_numSites = numSites;
        m_sizePerSite = sizePerSite;
        SetFieldSize(numSites * sizePerSite);
    }

    size_t GetNumSites() { ReleaseAssert(m_numSites != static_cast<size_t>(-1)); return m_numSites; }
    size_t GetSizePerSite() { ReleaseAssert(m_sizePerSite != static_cast<size_t>(-1)); return m_sizePerSite; }

    size_t GetOffsetForSite(size_t siteOrd)
    {
        ReleaseAssert(siteOrd < GetNumSites());
        return GetFieldOffset() + GetSizePerSite() * siteOrd;
    }

    bool IsEqualImpl(JitSlowPathDataIcSiteArray& other)
    {
        CHECK(IsBaseEqual(other));
        if (!IsValid()) { return true; }
        CHECK(GetNumSites() == other.GetNumSites());
        CHECK(GetSizePerSite() == other.GetSizePerSite());
        return true;
    }

    virtual bool IsEqual(JitSlowPathDataFieldBase& other) override
    {
        return IsEqualDispatch<JitSlowPathDataIcSiteArray>(other);
    }

private:
    size_t m_numSites;
    size_t m_sizePerSite;
};

struct BaselineJitSlowPathDataLayout;
struct DfgJitSlowPathDataLayout;

struct JitSlowPathDataLayoutBuilder;
class DeegenBytecodeImplCreatorBase;

// Common data fields utilized by both baseline JIT slow path data and DFG JIT slow path data
//
struct JitSlowPathDataLayoutBase
{
    JitSlowPathDataLayoutBase()
        : m_isLayoutFinalized(false)
        , m_totalLength(static_cast<size_t>(-1))
        , m_totalValidFields(static_cast<size_t>(-1))
    { }

    virtual ~JitSlowPathDataLayoutBase() = default;

    MAKE_NONCOPYABLE(JitSlowPathDataLayoutBase);
    MAKE_NONMOVABLE(JitSlowPathDataLayoutBase);

    bool IsInitialized() { return m_totalValidFields != static_cast<size_t>(-1); }

    virtual DeegenEngineTier GetDeegenEngineTier() = 0;
    bool IsBaselineJIT() { return GetDeegenEngineTier() == DeegenEngineTier::BaselineJIT; }
    bool IsDfgJIT() { return GetDeegenEngineTier() == DeegenEngineTier::DfgJIT; }

    BaselineJitSlowPathDataLayout* WARN_UNUSED AsBaseline();
    DfgJitSlowPathDataLayout* WARN_UNUSED AsDfg();

    JitSlowPathDataBcOperand& GetBytecodeOperand(size_t idx)
    {
        ReleaseAssert(IsInitialized());
        ReleaseAssert(idx < m_operands.size());
        return m_operands[idx];
    }

    size_t GetNumCallIcSites()
    {
        if (!m_callICs.IsValid()) { return 0; }
        return m_callICs.GetNumSites();
    }

    size_t GetNumGenericIcSites()
    {
        if (!m_genericICs.IsValid()) { return 0; }
        return m_genericICs.GetNumSites();
    }

protected:
    // All JitSlowPathData share the same header:
    // - 2 byte opcode (TODO: I don't think we need this, but I don't want to fix it now)
    // - Fast path JIT address (m_jitAddr)
    //
    void SetupHeader(JitSlowPathDataLayoutBuilder& builder /*inout*/);

    // Setup m_operands and m_outputDest
    //
    void SetupOperandsAndOutput(JitSlowPathDataLayoutBuilder& builder /*inout*/, BytecodeVariantDefinition* bvd);

    // Setup call IC site array
    //
    void SetupCallIcSiteArray(JitSlowPathDataLayoutBuilder& builder /*inout*/, size_t numCallIcSites);

    // Setup generic IC site array and m_jitSlowPathAddr / m_jitDataSecAddr
    //
    void SetupGenericIcSiteArray(JitSlowPathDataLayoutBuilder& builder /*inout*/, size_t numGenericIcSites);

public:
    // Returns the JIT address of the fallthrough bytecode, can only be used if SlowPathDataLayout is finalized
    // This must not be used by DFG JIT, since in DFG, the start address of the next node with SlowPathData is not necessarily
    // the end address of the current node, as there may be other operations (reg operations and others) in between!
    //
    JitSlowPathDataJitAddress GetFallthroughJitAddress();

    // Return the JIT address of the fallthrough bytecode, using an external constant to represent the value of SlowPathDataLayoutLength
    //
    // Like above, this function must not be used by DFG JIT
    //
    llvm::Value* WARN_UNUSED GetFallthroughJitAddressUsingPlaceholder(llvm::Module* module, llvm::Value* slowPathDataPtr, llvm::Instruction* insertBefore);
    llvm::Value* WARN_UNUSED GetFallthroughJitAddressUsingPlaceholder(llvm::Module* module, llvm::Value* slowPathDataPtr, llvm::BasicBlock* insertAtEnd);

    static constexpr const char* x_slow_path_data_length_placeholder_name = "__placeholder_deegen_jit_slow_path_data_length";

    bool IsLayoutBaseEqual(JitSlowPathDataLayoutBase& other)
    {
        ReleaseAssert(IsLayoutFinalized() && other.IsLayoutFinalized());
        CHECK(m_jitAddr.IsEqual(other.m_jitAddr));
        CHECK(m_operands.size() == other.m_operands.size());
        for (size_t idx = 0; idx < m_operands.size(); idx++)
        {
            CHECK(m_operands[idx].IsEqual(other.m_operands[idx]));
        }
        CHECK(m_outputDest.IsEqual(other.m_outputDest));
        CHECK(m_callICs.IsEqual(other.m_callICs));
        CHECK(m_genericICs.IsEqual(other.m_genericICs));
        CHECK(m_jitSlowPathAddr.IsEqual(other.m_jitSlowPathAddr));
        CHECK(m_jitDataSecAddr.IsEqual(other.m_jitDataSecAddr));
        CHECK(m_totalLength == other.m_totalLength);
        CHECK(m_totalValidFields == other.m_totalValidFields);
        return true;
    }

    bool IsLayoutFinalized() { ReleaseAssert(IsInitialized()); return m_isLayoutFinalized; }
    void FinalizeLayout() { ReleaseAssert(!IsLayoutFinalized()); m_isLayoutFinalized = true; }

    // The length and #validFields are only accessible after the layout is finalized
    //
    size_t GetTotalLength()
    {
        ReleaseAssert(IsLayoutFinalized());
        ReleaseAssert(m_totalLength != static_cast<size_t>(-1));
        return m_totalLength;
    }

    size_t GetNumValidFields()
    {
        ReleaseAssert(IsLayoutFinalized());
        ReleaseAssert(m_totalValidFields != static_cast<size_t>(-1));
        return m_totalValidFields;
    }

    void EnableExtraFieldIfNotYet(JitSlowPathDataFieldBase& field)
    {
        ReleaseAssert(IsInitialized() && !IsLayoutFinalized());
        if (field.IsValid()) { return; }
        AssignOffsetForExtraField(field);
    }

    void AssignOffsetForExtraField(JitSlowPathDataFieldBase& field)
    {
        ReleaseAssert(IsInitialized() && !IsLayoutFinalized());
        ReleaseAssert(!field.IsValid());
        field.SetValid();
        field.SetFieldOffset(m_totalLength);
        m_totalLength += field.GetFieldSize();
        m_totalValidFields++;
    }

    friend JitSlowPathDataLayoutBuilder;

    // The fast path entry address of the associated JIT code, always at offset x_opcodeSizeBytes (a lot of places expects this)
    //
    JitSlowPathDataJitAddress m_jitAddr;

    // All input operands of this bytecode
    //
    std::vector<JitSlowPathDataBcOperand> m_operands;

    // If this bytecode has an output, the slot where the output shall be stored
    //
    JitSlowPathDataBcOperand m_outputDest;

    // The list of call IC sites and generic IC sites
    //
    JitSlowPathDataIcSiteArray m_callICs;
    JitSlowPathDataIcSiteArray m_genericICs;

    // The JIT slow path address and data section address
    //
    JitSlowPathDataJitAddress m_jitSlowPathAddr;
    JitSlowPathDataJitAddress m_jitDataSecAddr;

    // This is the slowPathDataOffset from the CodeBlock
    // This normally is not useful so it is not enabled in the initial ComputeLayout,
    // but in some cases, generic IC will need this info, in which case it will enable this field later
    //
    JitSlowPathDataInt<uint32_t> m_offsetFromCb;

private:
    // In some cases, we cannot pre-determine all needed fields of SlowPathData before the lowering,
    // and have to add new fields during the lowering.
    // So after ComputeLayout, the layout is still not finalized and the length is not available
    // Only after calling FinalizeLayout() the layout is finalized and one can access the length
    //
    bool m_isLayoutFinalized;

    // The total length of slow path data
    //
    size_t m_totalLength;

    // For assertion purpose to make sure that every field in the SlowPathData has been populated
    //
    size_t m_totalValidFields;
};

// Describes a piece of SlowPathData in baseline JIT
//
struct BaselineJitSlowPathDataLayout final : public JitSlowPathDataLayoutBase
{
    virtual DeegenEngineTier GetDeegenEngineTier() override { return DeegenEngineTier::BaselineJIT; }

    void ComputeLayout(BytecodeVariantDefinition* bvd);

    // If this bytecode has a conditional branch target, records the JIT address to branch to
    //
    JitSlowPathDataJitAddress m_condBrJitAddr;

    // If this bytecode has a conditional branch target, records the bytecode index of the branch destination
    // m_condBrJitAddr and m_condBrBcIndex must be allocated adjacently (m_condBrJitAddr first, m_condBrBcIndex second),
    // as baseline JIT logic expects that.
    //
    JitSlowPathDataInt<uint32_t> m_condBrBcIndex;

    bool IsLayoutEqual(BaselineJitSlowPathDataLayout& other)
    {
        CHECK(IsLayoutBaseEqual(other));
        CHECK(m_condBrJitAddr.IsEqual(other.m_condBrJitAddr));
        CHECK(m_condBrBcIndex.IsEqual(other.m_condBrBcIndex));
        return true;
    }
};

// Describes a piece of SlowPathData in DFG JIT
//
struct DfgJitSlowPathDataLayout final : public JitSlowPathDataLayoutBase
{
    DfgJitSlowPathDataLayout() : JitSlowPathDataLayoutBase() { }

    virtual DeegenEngineTier GetDeegenEngineTier() override { return DeegenEngineTier::DfgJIT; }

    void ComputeLayout(DfgJitImplCreator* ifi);

    // The stack frame slot ordinal where the cond branch decision is stored to
    // This only exists if a cond branch decision is needed. Note that this is not equivalent to that the bytecode has a condBr!
    //
    JitSlowPathDataInt<uint16_t> m_condBrDecisionSlot;

    // Describes the register configuration used for this stencil
    // Only exists if reg alloc is enabled, and Call IC or Generic IC exists
    //
    JitSlowPathDataFieldBase m_compactRegConfig;

    // The fallthrough JIT address of this bytecode
    // Note that we must explicitly store this value in SlowPathData since there may be other logic between the end of this
    // bytecode and the start address of the next bytecode.
    //
    JitSlowPathDataJitAddress m_dfgFallthroughJitAddr;

    bool IsLayoutEqual(DfgJitSlowPathDataLayout& other)
    {
        CHECK(IsLayoutBaseEqual(other));
        CHECK(m_dfgFallthroughJitAddr.IsEqual(other.m_dfgFallthroughJitAddr));
        CHECK(m_condBrDecisionSlot.IsEqual(other.m_condBrDecisionSlot));
        CHECK(m_compactRegConfig.IsEqual(other.m_compactRegConfig));
        return true;
    }
};

inline BaselineJitSlowPathDataLayout* WARN_UNUSED JitSlowPathDataLayoutBase::AsBaseline()
{
    ReleaseAssert(IsBaselineJIT());
    return assert_cast<BaselineJitSlowPathDataLayout*>(this);
}

inline DfgJitSlowPathDataLayout* WARN_UNUSED JitSlowPathDataLayoutBase::AsDfg()
{
    ReleaseAssert(IsDfgJIT());
    return assert_cast<DfgJitSlowPathDataLayout*>(this);
}

}   // namespace dast
