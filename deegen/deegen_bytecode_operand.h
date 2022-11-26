#pragma once

#include "misc_llvm_helper.h"

#include "api_define_bytecode.h"
#include "deegen_bytecode_metadata.h"
#include "deegen_call_inline_cache.h"

namespace dast {

enum class BcOperandKind
{
    Slot,
    Constant,
    Literal,
    SpecializedLiteral,
    BytecodeRangeBase,
    InlinedMetadata
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

    // Retunr true if this operand is specialized to have a constant value so it doesn't have to sit in the bytecode struct
    //
    virtual bool WARN_UNUSED IsElidedFromBytecodeStruct()
    {
        return false;
    }

    // Retunr true if this operand cannot be decoded trivially by a load from the bytecode struct
    //
    virtual bool WARN_UNUSED IsNotTriviallyDecodeableFromBytecodeStruct()
    {
        return false;
    }

    // Return the # of bytes for the full representation of this operand
    //
    virtual size_t WARN_UNUSED ValueFullByteLength() = 0;

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

    llvm::Type* WARN_UNUSED GetSourceValueFullRepresentationType(llvm::LLVMContext& ctx)
    {
        ReleaseAssert(!IsNotTriviallyDecodeableFromBytecodeStruct());
        return llvm::Type::getIntNTy(ctx, static_cast<uint32_t>(ValueFullByteLength() * 8));
    }

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
        ReleaseAssert(m_sizeInBytecodeStruct <= ValueFullByteLength());
        return m_sizeInBytecodeStruct;
    }

    void InvalidateBytecodeStructOffsetAndSize()
    {
        m_offsetInBytecodeStruct = static_cast<size_t>(-1);
        m_sizeInBytecodeStruct = 0;
    }

    void AssignOrChangeBytecodeStructOffsetAndSize(size_t offset, size_t size)
    {
        size_t maxSize = ValueFullByteLength();
        if (IsElidedFromBytecodeStruct())
        {
            ReleaseAssert(offset == static_cast<size_t>(-1));
            ReleaseAssert(size == 0);
        }
        else
        {
            ReleaseAssert(offset > 0 && size > 0);
        }
        ReleaseAssert(size <= maxSize);
        m_offsetInBytecodeStruct = offset;
        m_sizeInBytecodeStruct = size;
    }

    bool WARN_UNUSED SupportsGetOperandValueFromBytecodeStruct();

    // May only be called if SupportsGetOperandValueFromBytecodeStruct() is true
    // Emit decoding logic into 'targetBB', and return the decoded operand value in the bytecode struct
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

    virtual size_t WARN_UNUSED ValueFullByteLength() override
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

    virtual bool WARN_UNUSED IsElidedFromBytecodeStruct() override
    {
        // Since the 'nil' type has only one possible value, we can elide it from the bytecode struct
        // Note that even though some other types have only one possible value as well (e.g., DoubleNaN), we only do this optimization for 'nil'
        // as it is the important case (... == nil is pretty common) and the 'nil' constant is readily avaiable (thanks to the tag register).
        //
        return m_typeMask == x_typeSpeculationMaskFor<tNil>;
    }

    virtual size_t WARN_UNUSED ValueFullByteLength() override
    {
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

    virtual size_t WARN_UNUSED ValueFullByteLength() override final
    {
        return m_numBytes;
    }

    virtual bool WARN_UNUSED IsSignedValue() override
    {
        return m_isSigned;
    }

    virtual llvm::Type* WARN_UNUSED GetUsageType(llvm::LLVMContext& ctx) override final
    {
        return llvm::Type::getIntNTy(ctx, static_cast<uint32_t>(m_numBytes * 8));
    }

    virtual llvm::Value* WARN_UNUSED EmitUsageValueFromBytecodeValue(InterpreterBytecodeImplCreator* ifi, llvm::BasicBlock* targetBB /*out*/, llvm::Value* bytecodeValue) override;

    DeegenBytecodeOperandType GetLiteralType()
    {
        if (m_isSigned)
        {
            if (m_numBytes == 1)
            {
                return DeegenBytecodeOperandType::Int8;
            }
            else if (m_numBytes == 2)
            {
                return DeegenBytecodeOperandType::Int16;
            }
            else
            {
                ReleaseAssert(m_numBytes == 4);
                return DeegenBytecodeOperandType::Int32;
            }
        }
        else
        {
            if (m_numBytes == 1)
            {
                return DeegenBytecodeOperandType::UInt8;
            }
            else if (m_numBytes == 2)
            {
                return DeegenBytecodeOperandType::UInt16;
            }
            else
            {
                ReleaseAssert(m_numBytes == 4);
                return DeegenBytecodeOperandType::UInt32;
            }
        }
    }

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

    virtual bool WARN_UNUSED IsElidedFromBytecodeStruct() override final
    {
        // This is a specialized constant, so it doesn't have to live in the bytecode struct
        //
        return true;
    }

    virtual bool WARN_UNUSED IsSignedValue() override
    {
        return m_isSigned;
    }

    virtual llvm::Value* WARN_UNUSED EmitUsageValueFromBytecodeValue(InterpreterBytecodeImplCreator* ifi, llvm::BasicBlock* targetBB /*out*/, llvm::Value* bytecodeValue) override;

    uint64_t m_concreteValue;
};

// A bytecode operand that is a bytecode range starting point
//
class BcOpBytecodeRangeBase final : public BcOperand
{
public:
    BcOpBytecodeRangeBase(const std::string& name, bool isReadOnly)
        : BcOperand(name)
        , m_isReadOnly(isReadOnly)
        , m_hasRangeLimit(false)
        , m_isRangeLimitConstant(false)
        , m_constantRangeLimit(0)
        , m_operandRangeLimit(nullptr)
    { }

    virtual void dump(std::stringstream& ss) override
    {
        ss << "Arg '" << OperandName() << "': BytecodeRangeBase [ ";
        if (m_isReadOnly)
        {
            ss << "readonly";
        }
        else
        {
            ss << "readwrite";
        }
        ss << ", ";
        if (m_hasRangeLimit)
        {
            if (m_isRangeLimitConstant)
            {
                ss << "len = " << m_constantRangeLimit;
            }
            else
            {
                ss << "len = " << m_operandRangeLimit->OperandName();
            }
        }
        else
        {
            ss << "len = unlimited";
        }
        ss << " ]" << std::endl;
    }

    virtual BcOperandKind GetKind() override { return BcOperandKind::BytecodeRangeBase; }

    virtual size_t WARN_UNUSED ValueFullByteLength() override
    {
        return 8;
    }

    virtual bool WARN_UNUSED IsSignedValue() override
    {
        return false;
    }

    virtual llvm::Type* WARN_UNUSED GetUsageType(llvm::LLVMContext& ctx) override
    {
        return llvm_type_of<void*>(ctx);
    }

    virtual llvm::Value* WARN_UNUSED EmitUsageValueFromBytecodeValue(InterpreterBytecodeImplCreator* ifi, llvm::BasicBlock* targetBB /*out*/, llvm::Value* bytecodeValue) override;

    bool m_isReadOnly;
    bool m_hasRangeLimit;
    bool m_isRangeLimitConstant;
    size_t m_constantRangeLimit;
    BcOperand* m_operandRangeLimit;
};

// A bytecode operand that is an inlined bytecode metadata
//
class BcOpInlinedMetadata final : public BcOperand
{
public:
    BcOpInlinedMetadata(size_t size)
        : BcOperand("inlined_bytecode_metadata")
        , m_size(size)
    { }

    virtual void dump(std::stringstream& ss) override
    {
        ss << "Bytecode inlined metadata: size = " << m_size << " bytes" << std::endl;
    }

    virtual BcOperandKind GetKind() override { return BcOperandKind::InlinedMetadata; }

    virtual bool WARN_UNUSED IsNotTriviallyDecodeableFromBytecodeStruct() override
    {
        return true;
    }

    virtual size_t WARN_UNUSED ValueFullByteLength() override
    {
        return m_size;
    }

    virtual bool WARN_UNUSED IsSignedValue() override
    {
        return false;
    }

    virtual llvm::Type* WARN_UNUSED GetUsageType(llvm::LLVMContext& ctx) override
    {
        return llvm_type_of<void*>(ctx);
    }

    virtual llvm::Value* WARN_UNUSED EmitUsageValueFromBytecodeValue(InterpreterBytecodeImplCreator* ifi, llvm::BasicBlock* targetBB /*out*/, llvm::Value* bytecodeValue) override;

    size_t m_size;
};

enum class BytecodeQuickeningKind
{
    // This variant is a normal variant
    //
    NoQuickening,
    // This variant is used to implement default quickening assumptions (hot-cold splitting)
    // That is, the logic of this variant is same as 'Quickened', except that it will never de-quicken itself.
    //
    LockedQuickening,
    // This is the initial quickening state. The code is quickened to execute the first quickening variant, but if at runtime it encounters
    // input that cannot be handled by the first quickening variant, it will quicken itself to a different quickened variant (transitions to 'Quickened') as necessary.
    //
    // This also imples that for best performance, the user should list the most probable quickening variant as the first quickening variant.
    //
    QuickeningSelector,
    // This is a variant quickened for some specific quickening assumptions (other than the first one).
    // When the quickening assumption check failed, it will de-quicken itself to the de-quickened variant.
    //
    Quickened,
    // This means a quickening has happened but subsequently encountered unexpected input. It will never attempt to quicken itself to anything else.
    // The logic of this variant is same as 'NoQuickening'.
    //
    Dequickened
};

struct BytecodeOperandQuickeningDescriptor
{
    size_t m_operandOrd;
    TypeSpeculationMask m_speculatedMask;
};

class BytecodeVariantDefinition
{
public:
    void SetMaxOperandWidthBytes(size_t maxWidthBytesInput)
    {
        ReleaseAssert(!m_hasDecidedOperandWidth);
        m_hasDecidedOperandWidth = true;
        size_t currentOffset = x_opcodeSizeBytes;
        auto update = [&](BcOperand* operand, size_t maxWidthBytes)
        {
            if (operand->IsElidedFromBytecodeStruct())
            {
                operand->AssignOrChangeBytecodeStructOffsetAndSize(static_cast<size_t>(-1) /*offset*/, 0 /*size*/);
            }
            else
            {
                size_t operandMaxWidth = operand->ValueFullByteLength();
                size_t width = std::min(operandMaxWidth, maxWidthBytes);
                operand->AssignOrChangeBytecodeStructOffsetAndSize(currentOffset, width);
                currentOffset += width;
            }
        };
        for (auto& operand : m_list)
        {
            update(operand.get(), maxWidthBytesInput);
        }
        if (m_hasOutputValue)
        {
            update(m_outputOperand.get(), maxWidthBytesInput);
        }
        if (m_hasConditionalBranchTarget)
        {
            // Currently the frontend locked condBr's width to 2 bytes, we can improve this later
            //
            update(m_condBrTarget.get(), 2 /*maxWidth*/);
        }
        m_bytecodeStructLength = currentOffset;
    }

    static std::vector<std::vector<std::unique_ptr<BytecodeVariantDefinition>>> WARN_UNUSED ParseAllFromModule(llvm::Module* module);
    static void RemoveUsedAttributeOfBytecodeDefinitionGlobalSymbol(llvm::Module* module);
    static void AssertBytecodeDefinitionGlobalSymbolHasBeenRemoved(llvm::Module* module);

    static llvm::Value* WARN_UNUSED DecodeBytecodeOpcode(llvm::Value* bytecode, llvm::Instruction* insertBefore);

    bool HasQuickeningSlowPath()
    {
        return m_quickeningKind == BytecodeQuickeningKind::LockedQuickening ||
               m_quickeningKind == BytecodeQuickeningKind::QuickeningSelector ||
               m_quickeningKind == BytecodeQuickeningKind::Quickened;
    }

    void AddBytecodeMetadata(std::unique_ptr<BytecodeMetadataStruct> s)
    {
        if (m_bytecodeMetadataMaybeNull.get() == nullptr)
        {
            m_bytecodeMetadataMaybeNull = std::make_unique<BytecodeMetadataStruct>();
        }
        m_bytecodeMetadataMaybeNull->AddStruct(std::move(s));
    }

    bool IsBytecodeStructLengthFinalized() { return m_bytecodeStructLengthFinalized; }
    bool IsBytecodeStructLengthTentativelyFinalized() { return m_bytecodeStructLengthTentativelyFinalized; }

    void TentativelyFinalizeBytecodeStructLength()
    {
        ReleaseAssert(m_hasDecidedOperandWidth);
        ReleaseAssert(!m_bytecodeStructLengthTentativelyFinalized);
        m_bytecodeStructLengthTentativelyFinalized = true;
        if (m_bytecodeMetadataMaybeNull.get() != nullptr)
        {
            BytecodeMetadataStruct::StructInfo info = m_bytecodeMetadataMaybeNull->FinalizeStructAndAssignOffsets();
            // We cannot store the metadata inline if it contains pointers that need to be visited by GC
            // Therefore, we currently conservatively inline only if the metadata struct is < 4 bytes, since each
            // pointer is at least 4 bytes. The alignment also must be 1 since the bytecode is not aligned.
            //
            // TODO: we could have done better, for example, we may want to make the decision on a per-IC basis,
            // or even on a per-member basis.
            //
            ReleaseAssert(m_metadataPtrOffset.get() == nullptr && m_inlinedMetadata.get() == nullptr);
            if (info.alignment == 1 && info.allocSize < 4)
            {
                m_inlinedMetadata = std::make_unique<BcOpInlinedMetadata>(info.allocSize);
                m_inlinedMetadata->AssignOrChangeBytecodeStructOffsetAndSize(m_bytecodeStructLength, info.allocSize /*size*/);
                m_bytecodeStructLength += info.allocSize;
            }
            else
            {
                AssignMetadataStructInfo(info);
                m_metadataPtrOffset = std::make_unique<BcOpLiteral>("bytecodeMetadata", false /*isSigned*/, 4 /*numBytes*/);
                m_metadataPtrOffset->AssignOrChangeBytecodeStructOffsetAndSize(m_bytecodeStructLength, 4 /*size*/);
                m_bytecodeStructLength += 4;
            }
        }
    }

    size_t GetTentativeBytecodeStructLength()
    {
        ReleaseAssert(IsBytecodeStructLengthTentativelyFinalized());
        return m_bytecodeStructLength;
    }

    void FinalizeBytecodeStructLength(size_t finalBytecodeStructLength)
    {
        ReleaseAssert(!IsBytecodeStructLengthFinalized());
        ReleaseAssert(IsBytecodeStructLengthTentativelyFinalized());
        ReleaseAssert(finalBytecodeStructLength >= m_bytecodeStructLength);
        m_bytecodeStructLengthFinalized = true;
        m_bytecodeStructLength = finalBytecodeStructLength;
    }

    bool HasBytecodeMetadata()
    {
        ReleaseAssert(m_bytecodeStructLengthFinalized);
        return m_bytecodeMetadataMaybeNull.get() != nullptr;
    }

    bool IsBytecodeMetadataInlined()
    {
        ReleaseAssert(HasBytecodeMetadata());
        ReleaseAssert(m_inlinedMetadata.get() != nullptr ^ m_metadataPtrOffset.get() != nullptr);
        return m_inlinedMetadata.get() != nullptr;
    }

    size_t GetBytecodeStructLength()
    {
        ReleaseAssert(m_bytecodeStructLengthFinalized);
        return m_bytecodeStructLength;
    }

    BytecodeMetadataStructBase::StructInfo GetMetadataStructInfo()
    {
        ReleaseAssert(m_metadataStructInfoAssigned);
        return m_metadataStructInfo;
    }

    bool HasInterpreterCallIC()
    {
        ReleaseAssert(IsBytecodeStructLengthFinalized());
        return m_interpreterCallIcMetadata.IcExists();
    }

    InterpreterCallIcMetadata& GetInterpreterCallIc()
    {
        ReleaseAssert(HasInterpreterCallIC());
        return m_interpreterCallIcMetadata;
    }

    // If this function is called, this bytecode will have a interpreter call inline cache
    //
    void AddInterpreterCallIc()
    {
        ReleaseAssert(!IsBytecodeStructLengthFinalized());
        ReleaseAssert(!m_interpreterCallIcMetadata.IcExists());
        std::pair<std::unique_ptr<BytecodeMetadataStruct>, InterpreterCallIcMetadata> res = InterpreterCallIcMetadata::Create();
        AddBytecodeMetadata(std::move(res.first));
        m_interpreterCallIcMetadata = res.second;
        ReleaseAssert(m_interpreterCallIcMetadata.IcExists());
    }

    // For now we have a fixed 2-byte opcode header for simplicity
    // We can probably improve compactness by making the most common opcodes use 1-byte opcode in the future
    //
    static constexpr size_t x_opcodeSizeBytes = 2;

    static constexpr const char* x_defListSymbolName = "x_deegen_impl_all_bytecode_defs_in_this_tu";
    static constexpr const char* x_nameListSymbolName = "x_deegen_impl_all_bytecode_names_in_this_tu";
    static constexpr const char* x_sameLengthConstraintListSymbolName = "x_deegen_impl_all_bytecode_same_length_constraints_in_this_tu";

    size_t m_bytecodeOrdInTU;
    size_t m_variantOrd;
    std::string m_bytecodeName;
    std::string m_implFunctionName;
    std::vector<std::string> m_opNames;
    std::vector<DeegenBytecodeOperandType> m_originalOperandTypes;
    std::vector<std::unique_ptr<BcOperand>> m_list;

    bool m_hasDecidedOperandWidth;
    bool m_bytecodeStructLengthTentativelyFinalized;
    bool m_bytecodeStructLengthFinalized;

    bool m_hasOutputValue;
    bool m_hasConditionalBranchTarget;
    std::unique_ptr<BcOpSlot> m_outputOperand;
    std::unique_ptr<BcOpLiteral> m_condBrTarget;

    // If the bytecode has metadata, exactly one of the two fields above will be non-nullptr
    // If 'm_metadataPtrOffset' is not nullptr, the bytecode metadata is outlined, and its ptr offset is stored by this member.
    // If 'm_inlinedMetadata' is not nullptr, the bytecode metadata is inlined with size and offset described by this member.
    //
    std::unique_ptr<BcOpLiteral> m_metadataPtrOffset;
    std::unique_ptr<BcOpInlinedMetadata> m_inlinedMetadata;

    bool m_metadataStructInfoAssigned;

    // If this is not null, it means that this variant has bytecode metadata, which is described by this field
    //
    std::unique_ptr<BytecodeMetadataStruct> m_bytecodeMetadataMaybeNull;

    bool m_isInterpreterCallIcExplicitlyDisabled;
    bool m_isInterpreterCallIcEverUsed;

    BytecodeQuickeningKind m_quickeningKind;
    // Populated if m_quickeningKind == LockedQuickening or Quickened or QuickeningSelector
    // For QuickeningSelector, this holds the first quickening.
    //
    std::vector<BytecodeOperandQuickeningDescriptor> m_quickening;
    // Populated if m_quickeningKind == QuickeningSelector, holds all except the first quickening
    //
    std::vector<std::vector<BytecodeOperandQuickeningDescriptor>> m_allOtherQuickenings;

    // The length of this bytecode is enforced to be the maximum length of all bytecode variants in this list
    //
    std::vector<BytecodeVariantDefinition*> m_sameLengthConstraintList;

private:
    void AssignMetadataStructInfo(BytecodeMetadataStructBase::StructInfo info)
    {
        ReleaseAssert(!m_metadataStructInfoAssigned);
        m_metadataStructInfoAssigned = true;
        m_metadataStructInfo = info;
    }

    size_t m_bytecodeStructLength;
    BytecodeMetadataStructBase::StructInfo m_metadataStructInfo;

    // If the bytecode has a Call IC, this holds the metadata (InterpreterCallIcMetadata::IcExists() tell whether the IC exists or not)
    //
    InterpreterCallIcMetadata m_interpreterCallIcMetadata;
};

}  // namespace dast
