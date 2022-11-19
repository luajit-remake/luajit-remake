#pragma once

#include "bytecode_builder_utils.h"
#include "runtime_utils.h"

#include "generated/all_bytecode_builder_apis.h"

namespace DeegenBytecodeBuilder {

#define macro(e) , DeegenGenerated_BytecodeBuilder_ ## e ## _BytecodeMetadataInfo
using BytecodeMetadataTypeListInfo = deduplicate_tuple<tuple_cat_t<std::tuple<> PP_FOR_EACH(macro, GENERATED_ALL_BYTECODE_BUILDER_BYTECODE_NAMES)>>;
#undef macro

#define macro(e) e ,
enum class BCKind : uint8_t
{
    PP_FOR_EACH(macro, GENERATED_ALL_BYTECODE_BUILDER_BYTECODE_NAMES)
    X_END_OF_ENUM
};
#undef macro

#define macro(e) , public DeegenGenerated_BytecodeBuilder_ ## e<BytecodeBuilderImpl>
class BytecodeBuilderImpl : public BytecodeBuilderBase<BytecodeMetadataTypeListInfo> PP_FOR_EACH(macro, GENERATED_ALL_BYTECODE_BUILDER_BYTECODE_NAMES) {
#undef macro

protected:

#define macro(e) friend class DeegenGenerated_BytecodeBuilder_ ## e<BytecodeBuilderImpl>;
PP_FOR_EACH(macro, GENERATED_ALL_BYTECODE_BUILDER_BYTECODE_NAMES)
#undef macro

    template<typename T>
    static constexpr size_t GetBytecodeOpcodeBase()
    {
        size_t res = 0;
#define macro(e)                                                                                    \
    if constexpr(std::is_same_v<T, DeegenGenerated_BytecodeBuilder_ ## e<BytecodeBuilderImpl>>) {   \
        return res;                                                                                 \
    } else {                                                                                        \
        res += GetNumVariantsOfBytecode<DeegenGenerated_BytecodeBuilder_ ## e>();

        PP_FOR_EACH(macro, GENERATED_ALL_BYTECODE_BUILDER_BYTECODE_NAMES)
#undef macro

        std::ignore = res;
        static_assert(type_dependent_false<T>::value, "bad type T!");
        return static_cast<size_t>(-1);

#define macro(e) }
        PP_FOR_EACH(macro, GENERATED_ALL_BYTECODE_BUILDER_BYTECODE_NAMES)
#undef macro
    }

    template<template<typename> class T>
    static constexpr size_t GetNumVariantsOfBytecode()
    {
        static_assert(std::is_base_of_v<T<BytecodeBuilderImpl>, BytecodeBuilderImpl>);
        return T<BytecodeBuilderImpl>::GetNumVariants();
    }
};

class BytecodeBuilder final : public BytecodeBuilderImpl
{
    MAKE_NONCOPYABLE(BytecodeBuilder);
    MAKE_NONMOVABLE(BytecodeBuilder);

    friend class DeegenInterpreterDispatchTableBuilder;

public:
    BytecodeBuilder() = default;

    // Returns whether the bytecode at offset 'bcPos' is a non-quickening bytecode.
    // 'bcPos' must be a valid offset that points at the beginning of a bytecode.
    //
    // This function is only used for assertion purpose as BytecodeBuilder can only emit non-quickening bytecode.
    //
    bool WARN_UNUSED IsPrimitiveBytecode(size_t bcPos)
    {
        BytecodeOpcodeTy opcode = GetOpcodeAtPosition(bcPos);
        return x_isPrimitiveBcArray[opcode];
    }

    // Get the kind of the bytecode at offset 'bcPos' of the bytecode stream.
    // 'bcPos' must be a valid offset that points at the beginning of a bytecode.
    //
    BCKind WARN_UNUSED GetBytecodeKind(size_t bcPos)
    {
        assert(IsPrimitiveBytecode(bcPos));
        BytecodeOpcodeTy opcode = GetOpcodeAtPosition(bcPos);
        return x_bcKindArray[opcode];
    }

    // Set the output operand of the bytecode at 'bcPos', overwriting the old value.
    //
    void SetOutputOperand(size_t bcPos, size_t value)
    {
        assert(IsPrimitiveBytecode(bcPos));
        BytecodeOpcodeTy opcode = GetOpcodeAtPosition(bcPos);
        uint8_t offset = x_bcOutputOperandOffsetArray[opcode];
        assert(offset != 255);
        // Currently the output operand slot is hardcoded to 2 bytes
        //
        assert(value <= 32767);
        // Output operand is always a BcSlot, so we should just store the slot value
        //
        UnalignedStore<uint16_t>(GetBytecodeStart() + bcPos + offset, static_cast<uint16_t>(value));
    }

    // Update the branch target of the bytecode at 'bcPos', overwriting the old value.
    // Return false if a overflow happened (currently our bytecode branch operand is only int16_t)
    //
    bool WARN_UNUSED SetBranchTarget(size_t bcPos, size_t destBcPos)
    {
        assert(IsPrimitiveBytecode(bcPos));
        BytecodeOpcodeTy opcode = GetOpcodeAtPosition(bcPos);
        uint8_t branchOperandOffsetInBc = x_bcBranchOperandOffsetArray[opcode];
        assert(branchOperandOffsetInBc != 255);

        int64_t diff = static_cast<int64_t>(destBcPos - bcPos);
        // TODO: we likely need to support larger offset size in the future, but for now just stick with int16_t
        //
        using ValueType = int16_t;
        if (unlikely(diff < std::numeric_limits<ValueType>::min() || diff > std::numeric_limits<ValueType>::max()))
        {
            return false;
        }
        ValueType val = static_cast<ValueType>(diff);
        UnalignedStore<ValueType>(GetBytecodeStart() + bcPos + branchOperandOffsetInBc, val);
        return true;
    }

    // The bytecode target to jump to is bcPos + offset
    //
    ssize_t WARN_UNUSED GetBranchTargetOffset(size_t bcPos)
    {
        assert(IsPrimitiveBytecode(bcPos));
        BytecodeOpcodeTy opcode = GetOpcodeAtPosition(bcPos);
        uint8_t branchOperandOffsetInBc = x_bcBranchOperandOffsetArray[opcode];
        assert(branchOperandOffsetInBc != 255);

        using ValueType = int16_t;
        ValueType offset = UnalignedLoad<ValueType>(GetBytecodeStart() + bcPos + branchOperandOffsetInBc);
        return offset;
    }

    size_t WARN_UNUSED GetBranchTarget(size_t bcPos)
    {
        ssize_t offset = GetBranchTargetOffset(bcPos);
        ssize_t dest = static_cast<ssize_t>(bcPos) + offset;
        assert(dest >= 0);
        return static_cast<size_t>(dest);
    }

private:
    // We should refactor this definition to a unified place..
    //
    using BytecodeOpcodeTy = uint16_t;

    BytecodeOpcodeTy GetOpcodeAtPosition(size_t bcPos)
    {
        BytecodeOpcodeTy opcode = UnalignedLoad<BytecodeOpcodeTy>(GetBytecodeStart() + bcPos);
        assert(opcode < x_bcKindArray.size());
        return opcode;
    }

    size_t WARN_UNUSED GetLengthOfSpecifiedBytecode(size_t bcPos)
    {
        assert(IsPrimitiveBytecode(bcPos));
        BytecodeOpcodeTy opcode = GetOpcodeAtPosition(bcPos);
        uint8_t res = x_bcLengthArray[opcode];
        assert(res != 255);
        return res;
    }

    static constexpr size_t x_numTotalVariants = []() {
        size_t result = 0;
#define macro(e) result += GetNumVariantsOfBytecode<DeegenGenerated_BytecodeBuilder_ ## e>();
        PP_FOR_EACH(macro, GENERATED_ALL_BYTECODE_BUILDER_BYTECODE_NAMES)
#undef macro
        return result;
    }();

    static constexpr std::array<bool, x_numTotalVariants> x_isPrimitiveBcArray = []() {
        std::array<bool, x_numTotalVariants> res;
#define macro(e)                                                                                                                \
        {                                                                                                                       \
            constexpr size_t variantOrdBase = GetBytecodeOpcodeBase<DeegenGenerated_BytecodeBuilder_ ## e>();                   \
            constexpr size_t numVariants = GetNumVariantsOfBytecode<DeegenGenerated_BytecodeBuilder_ ## e>();                   \
            for (size_t i = variantOrdBase; i < variantOrdBase + numVariants; i++) {                                            \
                res[i] = DeegenGenerated_BytecodeBuilder_ ##e <BytecodeBuilderImpl>::x_isVariantEmittable[i - variantOrdBase];  \
            }                                                                                                                   \
        }
        PP_FOR_EACH(macro, GENERATED_ALL_BYTECODE_BUILDER_BYTECODE_NAMES)
#undef macro
        return res;
    }();

    static constexpr std::array<uint8_t, x_numTotalVariants> x_bcLengthArray = []() {
        std::array<uint8_t, x_numTotalVariants> res;
#define macro(e)                                                                                                                \
        {                                                                                                                       \
            constexpr size_t variantOrdBase = GetBytecodeOpcodeBase<DeegenGenerated_BytecodeBuilder_ ## e>();                   \
            constexpr size_t numVariants = GetNumVariantsOfBytecode<DeegenGenerated_BytecodeBuilder_ ## e>();                   \
            for (size_t i = variantOrdBase; i < variantOrdBase + numVariants; i++) {                                            \
                res[i] = DeegenGenerated_BytecodeBuilder_ ##e <BytecodeBuilderImpl>::x_bytecodeLength[i - variantOrdBase];      \
            }                                                                                                                   \
        }
        PP_FOR_EACH(macro, GENERATED_ALL_BYTECODE_BUILDER_BYTECODE_NAMES)
#undef macro
        return res;
    }();

    static constexpr std::array<uint8_t, x_numTotalVariants> x_bcOutputOperandOffsetArray = []() {
        std::array<uint8_t, x_numTotalVariants> res;
#define macro(e)                                                                                                                        \
        {                                                                                                                               \
            constexpr size_t variantOrdBase = GetBytecodeOpcodeBase<DeegenGenerated_BytecodeBuilder_ ## e>();                           \
            constexpr size_t numVariants = GetNumVariantsOfBytecode<DeegenGenerated_BytecodeBuilder_ ## e>();                           \
            for (size_t i = variantOrdBase; i < variantOrdBase + numVariants; i++) {                                                    \
                res[i] = DeegenGenerated_BytecodeBuilder_ ##e <BytecodeBuilderImpl>::x_bytecodeOutputOperandOffset[i - variantOrdBase]; \
            }                                                                                                                           \
        }
        PP_FOR_EACH(macro, GENERATED_ALL_BYTECODE_BUILDER_BYTECODE_NAMES)
#undef macro
        return res;
    }();

    static constexpr std::array<uint8_t, x_numTotalVariants> x_bcBranchOperandOffsetArray = []() {
        std::array<uint8_t, x_numTotalVariants> res;
#define macro(e)                                                                                                                        \
        {                                                                                                                               \
            constexpr size_t variantOrdBase = GetBytecodeOpcodeBase<DeegenGenerated_BytecodeBuilder_ ## e>();                           \
            constexpr size_t numVariants = GetNumVariantsOfBytecode<DeegenGenerated_BytecodeBuilder_ ## e>();                           \
            for (size_t i = variantOrdBase; i < variantOrdBase + numVariants; i++) {                                                    \
                res[i] = DeegenGenerated_BytecodeBuilder_ ##e <BytecodeBuilderImpl>::x_bytecodeBranchOperandOffset[i - variantOrdBase]; \
            }                                                                                                                           \
        }
        PP_FOR_EACH(macro, GENERATED_ALL_BYTECODE_BUILDER_BYTECODE_NAMES)
#undef macro
        return res;
    }();

    static constexpr std::array<BCKind, x_numTotalVariants> x_bcKindArray = []() {
        std::array<BCKind, x_numTotalVariants> res;
#define macro(e)                                                                                                \
        {                                                                                                       \
            constexpr size_t variantOrdBase = GetBytecodeOpcodeBase<DeegenGenerated_BytecodeBuilder_ ## e>();   \
            constexpr size_t numVariants = GetNumVariantsOfBytecode<DeegenGenerated_BytecodeBuilder_ ## e>();   \
            for (size_t i = variantOrdBase; i < variantOrdBase + numVariants; i++) { res[i] = BCKind::e; }      \
        }
        PP_FOR_EACH(macro, GENERATED_ALL_BYTECODE_BUILDER_BYTECODE_NAMES)
#undef macro
        return res;
    }();
};

}   // namespace DeegenBytecodeBuilder

using BytecodeMetadataStructTypeList = DeegenBytecodeBuilder::BytecodeMetadataTypeListInfo::DeduplicatedTy;

constexpr size_t x_num_bytecode_metadata_struct_kinds = std::tuple_size_v<BytecodeMetadataStructTypeList>;

constexpr auto x_bytecode_metadata_struct_size_list = ComputeConstexprTraitArrayForTuple<uint32_t /*TraitType*/, BytecodeMetadataStructTypeList>(
    []<typename T>() -> uint32_t {
        return static_cast<uint32_t>(T::GetSize());
    });

constexpr auto x_bytecode_metadata_struct_log_2_alignment_list = ComputeConstexprTraitArrayForTuple<uint32_t /*TraitType*/, BytecodeMetadataStructTypeList>(
    []<typename T>() -> uint32_t {
        size_t alignment = T::GetAlignment();
        assert(is_power_of_2(alignment));
        uint32_t log2res = 0;
        while (alignment != 1)
        {
            log2res++;
            alignment /= 2;
        }
        return log2res;
    });

namespace detail {

template<typename Tuple>
struct ForEachBytecodeMetadataHelper
{
    template<size_t ord, typename Lambda>
    static ALWAYS_INLINE void Run(uintptr_t ptr, const uint16_t* cntArray, const Lambda& lambda)
    {
        if constexpr(ord < std::tuple_size_v<Tuple>)
        {
            using MetadataTy = std::tuple_element_t<ord, Tuple>;
            constexpr size_t alignment = MetadataTy::GetAlignment();
            uintptr_t mdAddr = (ptr + alignment - 1) / alignment * alignment;
            MetadataTy* md = MetadataTy::CastFromAddr(reinterpret_cast<void*>(mdAddr));
            MetadataTy* mdEnd = md + cntArray[ord];
            while (md < mdEnd)
            {
                lambda(md);
                md++;
            }
            Run<ord + 1>(reinterpret_cast<uintptr_t>(mdEnd), cntArray, lambda);
        }
    }
};

}   // namespace detail

// 'lambda' should be a generic lambda taking a T* parameter, where T will be an instantiation of DeegenMetadata
//
template<typename Lambda>
void ALWAYS_INLINE ForEachBytecodeMetadata(CodeBlock* cb, const Lambda& lambda)
{
    UnlinkedCodeBlock* ucb = cb->m_owner;
    uintptr_t mdStart = cb->GetBytecodeMetadataStart();
    detail::ForEachBytecodeMetadataHelper<BytecodeMetadataStructTypeList>::Run<0>(mdStart, ucb->m_bytecodeMetadataUseCounts, lambda);
}
