#pragma once

#include "misc_llvm_helper.h"

#include "bytecode_definition_utils.h"

// TODO: fix this
#include "bytecode.h"

namespace dast {

enum class BcOperandKind
{
    Slot,
    Constant,
    Literal,
    SpecializedLiteral
};

class InterpreterBytecodeImplCreator;

// The base class for a bytecode operand
//
class BcOperand
{
public:
    BcOperand(const std::string& name)
        : m_name(name)
        , m_operandOrdinal(static_cast<size_t>(-1))
        , m_offsetInBytecodeStruct(static_cast<size_t>(-1))
        , m_sizeInBytecodeStruct(0)
    { }

    virtual ~BcOperand() {}

    virtual void dump(std::stringstream& ss) = 0;

    virtual BcOperandKind GetKind() = 0;

    // Return 0 if this operand is specialized to have a constant value so it doesn't have to sit in the bytecode struct
    // Otherwise, return the max # of bytes needed to represent this operand
    //
    virtual size_t WARN_UNUSED ValueByteLength() = 0;

    // Whether this operand should be treated as a signed value or an unsigned value
    //
    virtual bool WARN_UNUSED IsSignedValue() = 0;

    // The LLVM type that the implementation function expects for this operand
    //
    virtual llvm::Type* WARN_UNUSED GetUsageType(llvm::LLVMContext& ctx) = 0;

    // Get the LLVM value to be passed to the implementation function.
    // 'targetBB' is the basic block where the logic should be appended to.
    // 'bytecodeValue' is an i64 denoting the value of the operand in the bytecode struct (or void if it is eliminated from bytecode struct).
    //
    virtual llvm::Value* WARN_UNUSED EmitUsageValueFromBytecodeValue(InterpreterBytecodeImplCreator* ifi, llvm::BasicBlock* targetBB /*out*/, llvm::Value* bytecodeValue) = 0;

    // Do custom transformation to the implementation function
    //
    virtual void TransformImplementationBeforeSimplificationPhase() {}
    virtual void TransformImplementationAfterSimplificationPhase() {}

    size_t OperandOrdinal() const
    {
        ReleaseAssert(m_operandOrdinal != static_cast<size_t>(-1));
        return m_operandOrdinal;
    }
    void SetOperandOrdinal(size_t ordinal) { m_operandOrdinal = ordinal; }
    void AssertOrSetOperandOrdinal(size_t ordinal)
    {
        ReleaseAssert(m_operandOrdinal == static_cast<size_t>(-1) || m_operandOrdinal == ordinal);
        m_operandOrdinal = ordinal;
    }

    std::string OperandName() { return m_name; }

    size_t GetOffsetInBytecodeStruct()
    {
        ReleaseAssert(m_offsetInBytecodeStruct != static_cast<size_t>(-1));
        return m_offsetInBytecodeStruct;
    }

    size_t GetSizeInBytecodeStruct()
    {
        ReleaseAssert(m_offsetInBytecodeStruct != static_cast<size_t>(-1));
        return m_sizeInBytecodeStruct;
    }

    void InvalidateBytecodeStructOffsetAndSize()
    {
        m_offsetInBytecodeStruct = static_cast<size_t>(-1);
        m_sizeInBytecodeStruct = 0;
    }

    void AssignOrChangeBytecodeStructOffsetAndSize(size_t offset, size_t size)
    {
        size_t maxSize = ValueByteLength();
        ReleaseAssertIff(maxSize == 0, offset == static_cast<size_t>(-1));
        ReleaseAssertIff(maxSize == 0, size == 0);
        ReleaseAssert(size <= maxSize);
        m_offsetInBytecodeStruct = offset;
        m_sizeInBytecodeStruct = size;
    }

    // If m_offsetInBytecodeStruct == -1, return nullptr
    // Otherwise emit decoding logic into 'targetBB', and return the decoded operand value in the bytecode struct
    // 'bytecodeStruct' must have type i8*
    //
    llvm::Value* WARN_UNUSED GetOperandValueFromBytecodeStruct(InterpreterBytecodeImplCreator* ifi, llvm::BasicBlock* targetBB);

private:
    std::string m_name;
    // The ordinal of this operand in the bytecode definition
    //
    size_t m_operandOrdinal;

    // The offset of this bytecode in the bytecode struct
    //
    size_t m_offsetInBytecodeStruct;
    size_t m_sizeInBytecodeStruct;
};

// A bytecode operand that refers to a bytecode slot
//
class BcOpSlot final : public BcOperand
{
public:
    BcOpSlot(const std::string& name)
        : BcOperand(name)
    { }

    virtual void dump(std::stringstream& ss) override
    {
        ss << "Arg '" << OperandName() << "': Slot" << std::endl;
    }

    virtual BcOperandKind GetKind() override { return BcOperandKind::Slot; }

    virtual size_t WARN_UNUSED ValueByteLength() override
    {
        return 8;
    }

    virtual bool WARN_UNUSED IsSignedValue() override
    {
        return false;
    }

    virtual llvm::Type* WARN_UNUSED GetUsageType(llvm::LLVMContext& ctx) override
    {
        // TValue, which is i64
        //
        return llvm_type_of<uint64_t>(ctx);
    }

    virtual llvm::Value* WARN_UNUSED EmitUsageValueFromBytecodeValue(InterpreterBytecodeImplCreator* ifi, llvm::BasicBlock* targetBB /*out*/, llvm::Value* bytecodeValue) override;
};

// A bytecode operand that refers to a constant in the constant table
//
class BcOpConstant final : public BcOperand
{
public:
    BcOpConstant(const std::string& name, TypeSpeculationMask mask)
        : BcOperand(name)
        , m_typeMask(mask)
    { }

    virtual void dump(std::stringstream& ss) override
    {
        ss << "Arg '" << OperandName() << "': Constant [ " << DumpHumanReadableTypeSpeculation(m_typeMask) << " ]" << std::endl;
    }

    virtual BcOperandKind GetKind() override { return BcOperandKind::Constant; }

    virtual size_t WARN_UNUSED ValueByteLength() override
    {
        // TODO: this can be improved: if the type is known to have only one value, the constant can be eliminated
        //
        return 8;
    }

    virtual bool WARN_UNUSED IsSignedValue() override
    {
        // The constant ordinal is always a negative value due to how we designed the in-memory representation
        //
        return true;
    }

    virtual llvm::Type* WARN_UNUSED GetUsageType(llvm::LLVMContext& ctx) override
    {
        // TValue, which is i64
        //
        return llvm_type_of<uint64_t>(ctx);
    }

    virtual llvm::Value* WARN_UNUSED EmitUsageValueFromBytecodeValue(InterpreterBytecodeImplCreator* ifi, llvm::BasicBlock* targetBB /*out*/, llvm::Value* bytecodeValue) override;

    // The statically-known type mask of this constant
    //
    TypeSpeculationMask m_typeMask;
};

// A bytecode operand that is a literal integer
//
class BcOpLiteral : public BcOperand
{
public:
    BcOpLiteral(const std::string& name, bool isSigned, size_t numBytes)
        : BcOperand(name)
        , m_isSigned(isSigned)
        , m_numBytes(numBytes)
    { }

    virtual void dump(std::stringstream& ss) override
    {
        ss << "Arg '" << OperandName() << "': Literal [ " << (m_isSigned ? "" : "u") << "int_" << m_numBytes * 8 << " ]" << std::endl;
    }

    virtual BcOperandKind GetKind() override { return BcOperandKind::Literal; }

    virtual bool WARN_UNUSED IsSpecializedToConcreteValue()
    {
        return false;
    }

    virtual size_t WARN_UNUSED ValueByteLength() override
    {
        return m_numBytes;
    }

    virtual bool WARN_UNUSED IsSignedValue() override
    {
        return m_isSigned;
    }

    virtual llvm::Type* WARN_UNUSED GetUsageType(llvm::LLVMContext& ctx) override final
    {
        return llvm::Type::getIntNTy(ctx, static_cast<uint32_t>(m_numBytes * 4));
    }

    virtual llvm::Value* WARN_UNUSED EmitUsageValueFromBytecodeValue(InterpreterBytecodeImplCreator* ifi, llvm::BasicBlock* targetBB /*out*/, llvm::Value* bytecodeValue) override;

    // The sign and width of this literal
    //
    bool m_isSigned;
    size_t m_numBytes;
};

class BcOpSpecializedLiteral final : public BcOpLiteral
{
public:
    BcOpSpecializedLiteral(const std::string& name, bool isSigned, size_t numBytes, uint64_t concreteValue)
        : BcOpLiteral(name, isSigned, numBytes)
        , m_concreteValue(concreteValue)
    {
        ReleaseAssert(is_power_of_2(numBytes));
        if (numBytes < 8)
        {
            if (isSigned)
            {
                ReleaseAssert(-(1LL << (numBytes * 8 - 1)) <= static_cast<int64_t>(concreteValue));
                ReleaseAssert(static_cast<int64_t>(concreteValue) < (1LL << (numBytes * 8 - 1)));
            }
            else
            {
                ReleaseAssert(concreteValue < (1LL << (numBytes * 8)));
            }
        }
    }

    virtual void dump(std::stringstream& ss) override
    {
        ss << "Arg '" << OperandName() << "': Literal [ value = ";
        if (m_isSigned)
        {
            ss << static_cast<int64_t>(m_concreteValue);
        }
        else
        {
            ss << static_cast<uint64_t>(m_concreteValue);
        }
        ss << " ]" << std::endl;
    }

    virtual BcOperandKind GetKind() override { return BcOperandKind::SpecializedLiteral; }

    virtual bool WARN_UNUSED IsSpecializedToConcreteValue() override
    {
        return true;
    }

    virtual size_t WARN_UNUSED ValueByteLength() override
    {
        // This is a specialized constant, so it doesn't have to live in the bytecode struct
        //
        return 0;
    }

    virtual bool WARN_UNUSED IsSignedValue() override
    {
        return m_isSigned;
    }

    virtual llvm::Value* WARN_UNUSED EmitUsageValueFromBytecodeValue(InterpreterBytecodeImplCreator* ifi, llvm::BasicBlock* targetBB /*out*/, llvm::Value* bytecodeValue) override;

    uint64_t m_concreteValue;
};

class BytecodeVariantDefinition
{
public:
    void SetMaxOperandWidthBytes(size_t maxWidthBytes)
    {
        size_t currentOffset = x_opcodeSizeBytes;
        auto update = [&](BcOperand* operand)
        {
            size_t operandMaxWidth = operand->ValueByteLength();
            if (operandMaxWidth == 0)
            {
                return;
            }
            size_t width = std::min(operandMaxWidth, maxWidthBytes);
            operand->AssignOrChangeBytecodeStructOffsetAndSize(currentOffset, width);
            currentOffset += width;
        };
        for (auto& operand : m_list)
        {
            update(operand.get());
        }
        if (m_hasOutputValue)
        {
            update(m_outputOperand.get());
        }
        if (m_hasConditionalBranchTarget)
        {
            update(m_condBrTarget.get());
        }
        m_bytecodeStructLength = currentOffset;
    }

    static std::vector<std::vector<std::unique_ptr<BytecodeVariantDefinition>>> WARN_UNUSED ParseAllFromModule(llvm::Module* module);
    static void RemoveUsedAttributeOfBytecodeDefinitionGlobalSymbol(llvm::Module* module);
    static void AssertBytecodeDefinitionGlobalSymbolHasBeenRemoved(llvm::Module* module);

    static llvm::Value* WARN_UNUSED DecodeBytecodeOpcode(llvm::Value* bytecode, llvm::Instruction* insertBefore);

    // For now we have a fixed 2-byte opcode header for simplicity
    // We can probably improve compactness by making the most common opcodes use 1-byte opcode in the future
    //
    static constexpr size_t x_opcodeSizeBytes = 2;

    static constexpr const char* x_defListSymbolName = "x_deegen_impl_all_bytecode_defs_in_this_tu";
    static constexpr const char* x_nameListSymbolName = "x_deegen_impl_all_bytecode_names_in_this_tu";

    size_t m_bytecodeOrdInTU;
    size_t m_variantOrd;
    std::string m_bytecodeName;
    std::string m_implFunctionName;
    std::vector<std::string> m_opNames;
    std::vector<DeegenBytecodeOperandType> m_originalOperandTypes;
    std::vector<std::unique_ptr<BcOperand>> m_list;
    size_t m_bytecodeStructLength;

    bool m_hasOutputValue;
    bool m_hasConditionalBranchTarget;
    std::unique_ptr<BcOpSlot> m_outputOperand;
    std::unique_ptr<BcOpLiteral> m_condBrTarget;
};

}  // namespace dast
