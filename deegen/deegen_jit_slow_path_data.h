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

    // Returns a void*, the JIT address
    //
    llvm::Value* WARN_UNUSED EmitGetValueLogic(llvm::Value* slowPathDataAddr, llvm::Instruction* insertBefore);
    llvm::Value* WARN_UNUSED EmitGetValueLogic(llvm::Value* slowPathDataAddr, llvm::BasicBlock* insertAtEnd);

    // 'value' should be a void*
    //
    void EmitSetValueLogic(llvm::Value* slowPathDataAddr, llvm::Value* value, llvm::Instruction* insertBefore);
    void EmitSetValueLogic(llvm::Value* slowPathDataAddr, llvm::Value* value, llvm::BasicBlock* insertAtEnd);
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
};

class BytecodeVariantDefinition;

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

private:
    size_t m_numSites;
    size_t m_sizePerSite;
};

struct BaselineJitSlowPathDataLayout;
struct DfgJitSlowPathDataLayout;

// Common data fields utilized by both baseline JIT slow path data and DFG JIT slow path data
//
struct JitSlowPathDataLayoutBase
{
    JitSlowPathDataLayoutBase()
        : m_totalLength(static_cast<size_t>(-1))
        , m_totalValidFields(static_cast<size_t>(-1))
    { }

    virtual ~JitSlowPathDataLayoutBase() = default;

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

    // Returns the JIT address of the fallthrough bytecode
    //
    JitSlowPathDataJitAddress GetFallthroughJitAddress();

    // The fast path entry address of the associated JIT code, always at offset x_opcodeSizeBytes (a lot of places expects this)
    //
    JitSlowPathDataJitAddress m_jitAddr;

    // If this bytecode has a conditional branch target, records the JIT address to branch to
    //
    JitSlowPathDataJitAddress m_condBrJitAddr;

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

    // If this bytecode has a conditional branch target, records the bytecode index of the branch destination
    // m_condBrJitAddr and m_condBrBcIndex must be allocated adjacently (m_condBrJitAddr first, m_condBrBcIndex second),
    // as baseline JIT logic expects that.
    //
    JitSlowPathDataInt<uint32_t> m_condBrBcIndex;
};

// Describes a piece of SlowPathData in DFG JIT
//
struct DfgJitSlowPathDataLayout final : public JitSlowPathDataLayoutBase
{
    virtual DeegenEngineTier GetDeegenEngineTier() override { return DeegenEngineTier::DfgJIT; }

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
