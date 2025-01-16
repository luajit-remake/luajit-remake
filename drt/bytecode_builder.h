#pragma once

#include "bytecode_builder_utils.h"
#include "runtime_utils.h"

#include "generated/all_bytecode_builder_apis.h"

class CodeBlock;

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

inline const char* GetBytecodeHumanReadableNameFromBCKind(BCKind k)
{
    switch (k)
    {
#define macro(e) case BCKind::e: { return PP_STRINGIFY(e); }
        PP_FOR_EACH(macro, GENERATED_ALL_BYTECODE_BUILDER_BYTECODE_NAMES)
#undef macro
    default:
    {
        return "(invalid bytecode)";
    }
    }   /* switch */
}

namespace detail {

template<BCKind e, typename CRTP>
struct BytecodeBuilderImplClassNameForBcKindImpl;

#define macro(e) template<typename CRTP> struct BytecodeBuilderImplClassNameForBcKindImpl<BCKind::e, CRTP> { using type = DeegenGenerated_BytecodeBuilder_ ## e<CRTP>; };
PP_FOR_EACH(macro, GENERATED_ALL_BYTECODE_BUILDER_BYTECODE_NAMES)
#undef macro

template<BCKind e, typename CRTP>
using BytecodeBuilderImplClassNameForBcKind = typename BytecodeBuilderImplClassNameForBcKindImpl<e, CRTP>::type;

}   // namespace detail

#define macro(e) , public DeegenGenerated_BytecodeBuilder_ ## e<BytecodeAccessor<isDecodingMode>>
template<bool isDecodingMode>
class BytecodeAccessor : public BytecodeBuilderBase<isDecodingMode, BytecodeMetadataTypeListInfo> PP_FOR_EACH(macro, GENERATED_ALL_BYTECODE_BUILDER_BYTECODE_NAMES) {
#undef macro

public:
    BytecodeAccessor() = default;

    BytecodeAccessor(uint8_t* bytecodeStream, size_t bytecodeStreamLength, TValue* constantTableEnd)
        : BytecodeBuilderBase<isDecodingMode, BytecodeMetadataTypeListInfo>(bytecodeStream, bytecodeStreamLength, constantTableEnd)
    { }

private:

#define macro(e) friend class DeegenGenerated_BytecodeBuilder_ ## e<BytecodeAccessor<isDecodingMode>>;
PP_FOR_EACH(macro, GENERATED_ALL_BYTECODE_BUILDER_BYTECODE_NAMES)
#undef macro

    template<template<typename> class T>
    static constexpr size_t GetBytecodeOpcodeBase()
    {
        size_t res = 0;
#define macro(e)                                                                                                                                       \
        if constexpr(std::is_same_v<T<BytecodeAccessor<isDecodingMode>>, DeegenGenerated_BytecodeBuilder_ ## e<BytecodeAccessor<isDecodingMode>>>) {   \
            return res;                                                                                                                                \
        } else {                                                                                                                                       \
            res += GetNumVariantsOfBytecode<DeegenGenerated_BytecodeBuilder_ ## e>();

        PP_FOR_EACH(macro, GENERATED_ALL_BYTECODE_BUILDER_BYTECODE_NAMES)
#undef macro

        std::ignore = res;
        static_assert(type_dependent_false<T<BytecodeAccessor<isDecodingMode>>>::value, "bad type T!");
        return static_cast<size_t>(-1);

#define macro(e) }
        PP_FOR_EACH(macro, GENERATED_ALL_BYTECODE_BUILDER_BYTECODE_NAMES)
#undef macro
    }

    template<template<typename> class T>
    static constexpr size_t GetNumVariantsOfBytecode()
    {
        // static_assert(std::is_base_of_v<T<BytecodeAccessor<isDecodingMode>>, BytecodeAccessor<isDecodingMode>>);
        return T<BytecodeAccessor<isDecodingMode>>::GetNumVariants();
    }

    friend class DeegenInterpreterDispatchTableBuilder;
    friend class DeegenInterpreterOpcodeNameTableBuilder;

    template<BCKind bytecodeKind>
    using OperandsTypeForBytecodeKind = typename detail::BytecodeBuilderImplClassNameForBcKind<bytecodeKind, BytecodeAccessor<isDecodingMode>>::Operands;

    using base = BytecodeBuilderBase<isDecodingMode, BytecodeMetadataTypeListInfo>;

public:
    using base::GetBytecodeStart;
    using base::GetCurLength;
    using base::m_bufferBegin;
    using base::m_bufferCur;
    using base::GetBuiltBytecodeSequenceImpl;
    using base::GetConstantFromConstantTable;

    // We should refactor this definition to a unified place..
    //
    using BytecodeOpcodeTy = uint16_t;

    BytecodeOpcodeTy GetCanonicalizedOpcodeAtPosition(size_t bcPos)
    {
        BytecodeOpcodeTy opcode = GetRawOpcodeAtPosition(bcPos);
        Assert(opcode < x_numTotalVariants);
        if constexpr(!isDecodingMode)
        {
            // If we are building the bytecode stream, we know BytecodeBuilder can only emit non-quickening bytecodes,
            // so the bytecode opcode must have been canonicalized. Assert this.
            //
            Assert(x_isPrimitiveBcArray[opcode]);
            Assert(x_canonicalizedBcArray[opcode] == opcode);
            return opcode;
        }
        else
        {
            // However, if we are decoding a bytecode stream, the runtime execution may have quickened the bytecodes,
            // so we must canonicalize the bytecode before returning.
            //
            return x_canonicalizedBcArray[opcode];
        }
    }

    BytecodeOpcodeTy GetCanonicalizedOpcodeFromOpcode(BytecodeOpcodeTy opcode)
    {
        Assert(opcode < x_numTotalVariants);
        return x_canonicalizedBcArray[opcode];
    }

    // Returns whether the bytecode at offset 'bcPos' is a non-quickening bytecode.
    // 'bcPos' must be a valid offset that points at the beginning of a bytecode.
    //
    // This function is only used for assertion purpose as BytecodeBuilder can only emit non-quickening bytecode.
    //
    bool WARN_UNUSED IsPrimitiveBytecode(size_t bcPos)
    {
        BytecodeOpcodeTy opcode = GetRawOpcodeAtPosition(bcPos);
        Assert(opcode < x_numTotalVariants);
        return x_isPrimitiveBcArray[opcode];
    }

    // Get the kind of the bytecode at offset 'bcPos' of the bytecode stream.
    // 'bcPos' must be a valid offset that points at the beginning of a bytecode.
    //
    BCKind WARN_UNUSED GetBytecodeKind(size_t bcPos)
    {
        BytecodeOpcodeTy opcode = GetCanonicalizedOpcodeAtPosition(bcPos);
        return x_bcKindArray[opcode];
    }

    const char* WARN_UNUSED GetBytecodeKindName(size_t bcPos)
    {
        return GetBytecodeHumanReadableNameFromBCKind(GetBytecodeKind(bcPos));
    }

    size_t GetOutputOperand(size_t bcPos)
    {
        BytecodeOpcodeTy opcode = GetCanonicalizedOpcodeAtPosition(bcPos);
        uint8_t offset = x_bcOutputOperandOffsetArray[opcode];
        Assert(offset != 255);
        // The output operand slot is always a BcSlot and is currently hardcoded to 2 bytes
        //
        return UnalignedLoad<uint16_t>(GetBytecodeStart() + bcPos + offset);
    }

    // Set the output operand of the bytecode at 'bcPos', overwriting the old value.
    //
    void SetOutputOperand(size_t bcPos, size_t value)
    {
        Assert(!isDecodingMode);
        Assert(IsPrimitiveBytecode(bcPos));
        BytecodeOpcodeTy opcode = GetCanonicalizedOpcodeAtPosition(bcPos);
        uint8_t offset = x_bcOutputOperandOffsetArray[opcode];
        Assert(offset != 255);
        // Currently the output operand slot is hardcoded to 2 bytes
        //
        Assert(value <= 32767);
        // Output operand is always a BcSlot, so we should just store the slot value
        //
        UnalignedStore<uint16_t>(GetBytecodeStart() + bcPos + offset, static_cast<uint16_t>(value));
    }

    // TODO: we likely need to support larger offset size in the future, but for now just stick with int16_t
    //
    void SetBranchTargetOffset(size_t bcPos, int16_t destBcOffset)
    {
        Assert(!isDecodingMode);
        Assert(IsPrimitiveBytecode(bcPos));
        BytecodeOpcodeTy opcode = GetCanonicalizedOpcodeAtPosition(bcPos);
        uint8_t branchOperandOffsetInBc = x_bcBranchOperandOffsetArray[opcode];
        Assert(branchOperandOffsetInBc != 255);
        UnalignedStore<int16_t>(GetBytecodeStart() + bcPos + branchOperandOffsetInBc, destBcOffset);
    }

    // Update the branch target of the bytecode at 'bcPos', overwriting the old value.
    // Return false if a overflow happened (currently our bytecode branch operand is only int16_t)
    //
    bool WARN_UNUSED SetBranchTarget(size_t bcPos, size_t destBcPos)
    {
        Assert(!isDecodingMode);
        int64_t diff = static_cast<int64_t>(destBcPos - bcPos);
        // TODO: we likely need to support larger offset size in the future, but for now just stick with int16_t
        //
        using ValueType = int16_t;
        if (unlikely(diff < std::numeric_limits<ValueType>::min() || diff > std::numeric_limits<ValueType>::max()))
        {
            return false;
        }
        SetBranchTargetOffset(bcPos, static_cast<ValueType>(diff));
        return true;
    }

    bool WARN_UNUSED BytecodeHasOutputOperand(size_t bcPos)
    {
        BytecodeOpcodeTy opcode = GetCanonicalizedOpcodeAtPosition(bcPos);
        return x_bcOutputOperandOffsetArray[opcode] != 255;
    }

    bool WARN_UNUSED BytecodeHasBranchOperand(size_t bcPos)
    {
        BytecodeOpcodeTy opcode = GetCanonicalizedOpcodeAtPosition(bcPos);
        return x_bcBranchOperandOffsetArray[opcode] != 255;
    }

    size_t WARN_UNUSED GetNextBytecodePosition(size_t bcPos)
    {
        return bcPos + GetLengthOfSpecifiedBytecode(bcPos);
    }

    // The bytecode target to jump to is bcPos + offset
    //
    ssize_t WARN_UNUSED GetBranchTargetOffset(size_t bcPos)
    {
        BytecodeOpcodeTy opcode = GetCanonicalizedOpcodeAtPosition(bcPos);
        uint8_t branchOperandOffsetInBc = x_bcBranchOperandOffsetArray[opcode];
        Assert(branchOperandOffsetInBc != 255);

        using ValueType = int16_t;
        ValueType offset = UnalignedLoad<ValueType>(GetBytecodeStart() + bcPos + branchOperandOffsetInBc);
        return offset;
    }

    size_t WARN_UNUSED GetBranchTarget(size_t bcPos)
    {
        ssize_t offset = GetBranchTargetOffset(bcPos);
        ssize_t dest = static_cast<ssize_t>(bcPos) + offset;
        Assert(dest >= 0);
        return static_cast<size_t>(dest);
    }

    // Replace a bytecode by another bytecode of equal length
    // The equal-length requirement must be ensured using the DEEGEN_ADD_BYTECODE_SAME_LENGTH_CONSTRAINT API
    //
    template<BCKind bytecodeKind>
    void ALWAYS_INLINE ReplaceBytecode(size_t bcPos, const OperandsTypeForBytecodeKind<bytecodeKind>& operands)
    {
        Assert(!isDecodingMode);
        static_assert(x_isBcKindPotentiallyReplaceable[static_cast<size_t>(bytecodeKind)],
                      "To replace a bytecode by another, you must use SameLengthConstraint in bytecode definition to ensure that they have equal length");

#ifndef NDEBUG
        size_t oldLen = GetLengthOfSpecifiedBytecode(bcPos);
        uint8_t* oldBytecodeStreamBegin = m_bufferBegin;
#endif
        Assert(bcPos < GetCurLength());

        // Hackily change the current bytecode pointer to 'bcPos', so the bytecode is created there
        //
        uint8_t* oldBufferCur = m_bufferCur;
        m_bufferCur = m_bufferBegin + bcPos;

        // Call the corresponding 'create' function to create the bytecode
        //
#define macro(e) if constexpr(bytecodeKind == BCKind::e) { this->Create ## e(operands); } else
        PP_FOR_EACH(macro, GENERATED_ALL_BYTECODE_BUILDER_BYTECODE_NAMES) { static_assert(type_dependent_false<decltype(bytecodeKind)>::value, "unexpected BCKind"); }
#undef macro

        // Since this is an equal-length replacement, it should never cause buffer growth
        //
        Assert(m_bufferBegin == oldBytecodeStreamBegin);
        // Assert that the generated bytecode indeed has the same length as the replaced bytecode
        //
        Assert(m_bufferCur == m_bufferBegin + bcPos + oldLen);
        Assert(m_bufferCur <= oldBufferCur);

        // Now, change back the current buffer pointer
        //
        m_bufferCur = oldBufferCur;
    }

    // A best-effort check to check that the bytecode stream is well-formed.
    // Currently it only checks that all the branches are branching to a valid location (i.e., not into the middle of another bytecode).
    //
    bool WARN_UNUSED CheckWellFormedness()
    {
        Assert(!isDecodingMode);
        std::unordered_set<size_t> bytecodeBoundarySet;
        uint8_t* cur = m_bufferBegin;
        while (cur < m_bufferCur)
        {
            size_t bcPos = static_cast<size_t>(cur - m_bufferBegin);
            if (!IsPrimitiveBytecode(bcPos))
            {
                fprintf(stderr, "[INTERNAL ERROR] The bytecode generated by the frontend is not well-formed! "
                                "Error: Bytecode at %llu is not a primitive bytecode.\n", static_cast<unsigned long long>(bcPos));
                return false;
            }
            bytecodeBoundarySet.insert(bcPos);
            cur += GetLengthOfSpecifiedBytecode(bcPos);
        }
        if (cur != m_bufferCur)
        {
            fprintf(stderr, "[INTERNAL ERROR] The bytecode generated by the frontend is not well-formed! "
                            "Error: Bytecode stream ended in the middle of a bytecode.\n");
            return false;
        }
        cur = m_bufferBegin;
        while (cur < m_bufferCur)
        {
            size_t bcPos = static_cast<size_t>(cur - m_bufferBegin);
            Assert(IsPrimitiveBytecode(bcPos));
            if (BytecodeHasBranchOperand(bcPos))
            {
                size_t branchTarget = GetBranchTarget(bcPos);
                if (!bytecodeBoundarySet.count(branchTarget))
                {
                    fprintf(stderr, "[INTERNAL ERROR] The bytecode generated by the frontend is not well-formed! "
                                    "Error: Bytecode at %llu is branching to %llu, which is in the middle of a bytecode.\n",
                                    static_cast<unsigned long long>(bcPos), static_cast<unsigned long long>(branchTarget));
                    return false;
                }
            }
            cur += GetLengthOfSpecifiedBytecode(bcPos);
        }
        Assert(cur == m_bufferCur);
        return true;
    }

    std::pair<uint8_t*, size_t> GetBuiltBytecodeSequence()
    {
        Assert(!isDecodingMode);
        static_assert(sizeof(BytecodeOpcodeTy) <= x_numExtraPaddingAtBytecodeStreamEnd);
        std::pair<uint8_t*, size_t> res = GetBuiltBytecodeSequenceImpl();
        // Append the "stopper" bytecode opcode right after the end of the bytecode sequence
        //
        Assert(res.second >= x_numExtraPaddingAtBytecodeStreamEnd);
        uint8_t* loc = res.first + res.second - x_numExtraPaddingAtBytecodeStreamEnd;
        UnalignedStore<BytecodeOpcodeTy>(loc, SafeIntegerCast<BytecodeOpcodeTy>(x_numTotalVariants));
        return res;
    }

    static constexpr size_t GetTotalBytecodeKinds()
    {
        return x_numTotalVariants;
    }

    BytecodeRWCInfo WARN_UNUSED GetDataFlowReadInfo(size_t bcPos)
    {
        Assert(isDecodingMode);
        BytecodeOpcodeTy opcode = GetCanonicalizedOpcodeAtPosition(bcPos);
        TestAssert(x_rwcReadInfoFns[opcode] != nullptr);
        return (this->*(x_rwcReadInfoFns[opcode]))(bcPos);
    }

    BytecodeRWCInfo WARN_UNUSED GetDataFlowWriteInfo(size_t bcPos)
    {
        Assert(isDecodingMode);
        BytecodeOpcodeTy opcode = GetCanonicalizedOpcodeAtPosition(bcPos);
        TestAssert(x_rwcWriteInfoFns[opcode] != nullptr);
        return (this->*(x_rwcWriteInfoFns[opcode]))(bcPos);
    }

    BytecodeRWCInfo WARN_UNUSED GetDataFlowClobberInfo(size_t bcPos)
    {
        Assert(isDecodingMode);
        BytecodeOpcodeTy opcode = GetCanonicalizedOpcodeAtPosition(bcPos);
        TestAssert(x_rwcClobberInfoFns[opcode] != nullptr);
        return (this->*(x_rwcClobberInfoFns[opcode]))(bcPos);
    }

    template<typename T>
    bool WARN_UNUSED IsBytecodeIntrinsic(size_t bcPos)
    {
        Assert(isDecodingMode);
        BytecodeOpcodeTy opcode = GetRawOpcodeAtPosition(bcPos);
        return (x_bcIntrinsicOrdinalArray[opcode] == ::detail::bytecode_intrinsic_ordinal_from_ty<T>);
    }

    template<typename T>
    T WARN_UNUSED GetBytecodeIntrinsicInfo(size_t bcPos)
    {
        Assert(isDecodingMode);
        static_assert(!std::is_same_v<T, void>);
        TestAssert(IsBytecodeIntrinsic<T>(bcPos));
        BytecodeOpcodeTy opcode = GetCanonicalizedOpcodeAtPosition(bcPos);

#define macro(e)                                                                                                                                            \
        if constexpr(std::is_same_v<typename DeegenGenerated_BytecodeBuilder_ ##e <BytecodeAccessor<isDecodingMode>>::BytecodeIntrinsicDefTy, T>) {         \
            constexpr size_t variantOrdBase = GetBytecodeOpcodeBase<DeegenGenerated_BytecodeBuilder_ ## e>();                                               \
            constexpr size_t numVariants = GetNumVariantsOfBytecode<DeegenGenerated_BytecodeBuilder_ ## e>();                                               \
            if (variantOrdBase <= opcode && opcode < variantOrdBase + numVariants) {                                                                        \
                auto fn = DeegenGenerated_BytecodeBuilder_ ##e <BytecodeAccessor<isDecodingMode>>::x_bytecodeIntrinsicInfoGetters[opcode - variantOrdBase]; \
                TestAssert(fn != nullptr);                                                                                                                  \
                return (this->*fn)(bcPos);                                                                                                                  \
            }                                                                                                                                               \
        }
        PP_FOR_EACH(macro, GENERATED_ALL_BYTECODE_BUILDER_BYTECODE_NAMES)
#undef macro
        TestAssert(false);
        __builtin_unreachable();
    }

    // Return true if this bytecode is a barrier instruction, i.e. control flow will never fallthrough to the next bytecode
    //
    bool IsBytecodeBarrier(size_t bcPos)
    {
        BytecodeOpcodeTy opcode = GetCanonicalizedOpcodeAtPosition(bcPos);
        TestAssert(x_bcIsBarrierArray[opcode] == 0 || x_bcIsBarrierArray[opcode] == 1);
        return x_bcIsBarrierArray[opcode];
    }

    static bool WARN_UNUSED BCKindMayBranch(BCKind bcKind)
    {
        TestAssert(bcKind != BCKind::X_END_OF_ENUM);
        size_t idx = static_cast<size_t>(bcKind);
        TestAssert(idx < static_cast<size_t>(BCKind::X_END_OF_ENUM));
        TestAssert(x_bcKindHasBranchOperandArray[idx] == 0 || x_bcKindHasBranchOperandArray[idx] == 1);
        return x_bcKindHasBranchOperandArray[idx];
    }

    static bool WARN_UNUSED BCKindIsBarrier(BCKind bcKind)
    {
        TestAssert(bcKind != BCKind::X_END_OF_ENUM);
        size_t idx = static_cast<size_t>(bcKind);
        TestAssert(idx < static_cast<size_t>(BCKind::X_END_OF_ENUM));
        TestAssert(x_bcKindIsBarrierArray[idx] == 0 || x_bcKindIsBarrierArray[idx] == 1);
        return x_bcKindIsBarrierArray[idx];
    }

    static bool WARN_UNUSED BCKindMayMakeTailCall(BCKind bcKind)
    {
        TestAssert(bcKind != BCKind::X_END_OF_ENUM);
        size_t idx = static_cast<size_t>(bcKind);
        TestAssert(idx < static_cast<size_t>(BCKind::X_END_OF_ENUM));
        return x_bcKindMayMakeTailCallArray[idx];
    }

    size_t WARN_UNUSED GetDfgNodeSpecificDataLength(size_t bcPos)
    {
        Assert(isDecodingMode);
        BCKind bcKind = GetBytecodeKind(bcPos);
        TestAssert(bcKind < BCKind::X_END_OF_ENUM);
        return x_bcKindDfgNsdLengthArray[static_cast<size_t>(bcKind)];
    }

    void PopulateDfgNodeSpecificData(void* nsdPtr, size_t bcPos)
    {
        Assert(isDecodingMode);
        BytecodeOpcodeTy opcode = GetCanonicalizedOpcodeAtPosition(bcPos);
        TestAssert(x_dfgNsdInfoWriterFnArray[opcode] != nullptr);
        return (this->*(x_dfgNsdInfoWriterFnArray[opcode]))(reinterpret_cast<uint8_t*>(nsdPtr), bcPos);
    }

    static void DumpDfgNodeSpecificData(BCKind bcKind, FILE* file, void* nsdPtr, bool& shouldPrefixCommaOnFirstItem)
    {
        Assert(isDecodingMode);
        TestAssert(bcKind < BCKind::X_END_OF_ENUM);
        (x_bcKindDumpDfgNsdInfoFnArray[static_cast<size_t>(bcKind)])(file, reinterpret_cast<uint8_t*>(nsdPtr), shouldPrefixCommaOnFirstItem);
    }

private:

    BytecodeOpcodeTy GetRawOpcodeAtPosition(size_t bcPos)
    {
        Assert(bcPos < GetCurLength());
        Assert(bcPos + sizeof(BytecodeOpcodeTy) <= GetCurLength());
        BytecodeOpcodeTy opcode = UnalignedLoad<BytecodeOpcodeTy>(GetBytecodeStart() + bcPos);
        Assert(opcode < x_bcKindArray.size());
        return opcode;
    }

    size_t WARN_UNUSED GetLengthOfSpecifiedBytecode(size_t bcPos)
    {
        BytecodeOpcodeTy opcode = GetCanonicalizedOpcodeAtPosition(bcPos);
        uint8_t res = x_bcLengthArray[opcode];
        Assert(res != 255 && res > 0);
        Assert(bcPos + res <= GetCurLength());
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
#define macro(e)                                                                                                                             \
        {                                                                                                                                    \
            constexpr size_t variantOrdBase = GetBytecodeOpcodeBase<DeegenGenerated_BytecodeBuilder_ ## e>();                                \
            constexpr size_t numVariants = GetNumVariantsOfBytecode<DeegenGenerated_BytecodeBuilder_ ## e>();                                \
            for (size_t i = variantOrdBase; i < variantOrdBase + numVariants; i++) {                                                         \
                res[i] = DeegenGenerated_BytecodeBuilder_ ##e <BytecodeAccessor<isDecodingMode>>::x_isVariantEmittable[i - variantOrdBase];  \
            }                                                                                                                                \
        }
        PP_FOR_EACH(macro, GENERATED_ALL_BYTECODE_BUILDER_BYTECODE_NAMES)
#undef macro
        return res;
    }();

    static constexpr std::array<uint16_t, x_numTotalVariants> x_canonicalizedBcArray = []() {
        std::array<uint16_t, x_numTotalVariants> res;
        for (size_t i = 0; i < x_numTotalVariants; i++)
        {
            if (x_isPrimitiveBcArray[i])
            {
                res[i] = SafeIntegerCast<uint16_t>(i);
            }
            else
            {
                ReleaseAssert(i > 0);
                res[i] = res[i - 1];
            }
        }
        for (size_t i = 0; i < x_numTotalVariants; i++)
        {
            ReleaseAssert(res[res[i]] == res[i]);
        }
        return res;
    }();

    static constexpr std::array<uint8_t, x_numTotalVariants> x_bcLengthArray = []() {
        std::array<uint8_t, x_numTotalVariants> res;
#define macro(e)                                                                                                                             \
        {                                                                                                                                    \
            constexpr size_t variantOrdBase = GetBytecodeOpcodeBase<DeegenGenerated_BytecodeBuilder_ ## e>();                                \
            constexpr size_t numVariants = GetNumVariantsOfBytecode<DeegenGenerated_BytecodeBuilder_ ## e>();                                \
            for (size_t i = variantOrdBase; i < variantOrdBase + numVariants; i++) {                                                         \
                res[i] = DeegenGenerated_BytecodeBuilder_ ##e <BytecodeAccessor<isDecodingMode>>::x_bytecodeLength[i - variantOrdBase];      \
            }                                                                                                                                \
        }
        PP_FOR_EACH(macro, GENERATED_ALL_BYTECODE_BUILDER_BYTECODE_NAMES)
#undef macro
        return res;
    }();

    static constexpr std::array<uint8_t, x_numTotalVariants> x_bcOutputOperandOffsetArray = []() {
        std::array<uint8_t, x_numTotalVariants> res;
#define macro(e)                                                                                                                                     \
        {                                                                                                                                            \
            constexpr size_t variantOrdBase = GetBytecodeOpcodeBase<DeegenGenerated_BytecodeBuilder_ ## e>();                                        \
            constexpr size_t numVariants = GetNumVariantsOfBytecode<DeegenGenerated_BytecodeBuilder_ ## e>();                                        \
            for (size_t i = variantOrdBase; i < variantOrdBase + numVariants; i++) {                                                                 \
                res[i] = DeegenGenerated_BytecodeBuilder_ ##e <BytecodeAccessor<isDecodingMode>>::x_bytecodeOutputOperandOffset[i - variantOrdBase]; \
            }                                                                                                                                        \
        }
        PP_FOR_EACH(macro, GENERATED_ALL_BYTECODE_BUILDER_BYTECODE_NAMES)
#undef macro
        return res;
    }();

    static constexpr std::array<uint8_t, x_numTotalVariants> x_bcBranchOperandOffsetArray = []() {
        std::array<uint8_t, x_numTotalVariants> res;
#define macro(e)                                                                                                                                     \
        {                                                                                                                                            \
            constexpr size_t variantOrdBase = GetBytecodeOpcodeBase<DeegenGenerated_BytecodeBuilder_ ## e>();                                        \
            constexpr size_t numVariants = GetNumVariantsOfBytecode<DeegenGenerated_BytecodeBuilder_ ## e>();                                        \
            for (size_t i = variantOrdBase; i < variantOrdBase + numVariants; i++) {                                                                 \
                res[i] = DeegenGenerated_BytecodeBuilder_ ##e <BytecodeAccessor<isDecodingMode>>::x_bytecodeBranchOperandOffset[i - variantOrdBase]; \
            }                                                                                                                                        \
        }
        PP_FOR_EACH(macro, GENERATED_ALL_BYTECODE_BUILDER_BYTECODE_NAMES)
#undef macro
        return res;
    }();

    static constexpr std::array<bool, static_cast<size_t>(BCKind::X_END_OF_ENUM)> x_bcKindHasBranchOperandArray = []() {
        constexpr size_t totalKinds = static_cast<size_t>(BCKind::X_END_OF_ENUM);
        std::array<bool, totalKinds> res;
#define macro(e)                                                                                                                                     \
        {                                                                                                                                            \
            constexpr size_t variantOrdBase = GetBytecodeOpcodeBase<DeegenGenerated_BytecodeBuilder_ ## e>();                                        \
            constexpr size_t numVariants = GetNumVariantsOfBytecode<DeegenGenerated_BytecodeBuilder_ ## e>();                                        \
            for (size_t i = variantOrdBase; i < variantOrdBase + numVariants; i++) {                                                                 \
                ReleaseAssert((x_bcBranchOperandOffsetArray[i] == 255) == (x_bcBranchOperandOffsetArray[variantOrdBase] == 255));                    \
            }                                                                                                                                        \
            res[static_cast<size_t>(BCKind::e)] = (x_bcBranchOperandOffsetArray[variantOrdBase] != 255);                                             \
        }
        PP_FOR_EACH(macro, GENERATED_ALL_BYTECODE_BUILDER_BYTECODE_NAMES)
#undef macro
        return res;
    }();

    static constexpr std::array<uint8_t, x_numTotalVariants> x_bcIsBarrierArray = []() {
        std::array<uint8_t, x_numTotalVariants> res;
#define macro(e)                                                                                                                                     \
        {                                                                                                                                            \
            constexpr size_t variantOrdBase = GetBytecodeOpcodeBase<DeegenGenerated_BytecodeBuilder_ ## e>();                                        \
            constexpr size_t numVariants = GetNumVariantsOfBytecode<DeegenGenerated_BytecodeBuilder_ ## e>();                                        \
            for (size_t i = variantOrdBase; i < variantOrdBase + numVariants; i++) {                                                                 \
                res[i] = DeegenGenerated_BytecodeBuilder_ ##e <BytecodeAccessor<isDecodingMode>>::x_isBytecodeBarrier[i - variantOrdBase];           \
            }                                                                                                                                        \
        }
        PP_FOR_EACH(macro, GENERATED_ALL_BYTECODE_BUILDER_BYTECODE_NAMES)
#undef macro
        return res;
    }();

    // For now, for simplicity, we require that all the variants of a certain bytecode kind must have the same IsBarrier property
    //
    static constexpr std::array<uint8_t, static_cast<size_t>(BCKind::X_END_OF_ENUM)> x_bcKindIsBarrierArray = []() {
        constexpr size_t totalKinds = static_cast<size_t>(BCKind::X_END_OF_ENUM);
        std::array<uint8_t, totalKinds> res;
#define macro(e)                                                                                                                                     \
        {                                                                                                                                            \
            constexpr size_t variantOrdBase = GetBytecodeOpcodeBase<DeegenGenerated_BytecodeBuilder_ ## e>();                                        \
            constexpr size_t numVariants = GetNumVariantsOfBytecode<DeegenGenerated_BytecodeBuilder_ ## e>();                                        \
            for (size_t i = variantOrdBase; i < variantOrdBase + numVariants; i++) {                                                                 \
                if (x_isPrimitiveBcArray[i])                                                                                                         \
                    ReleaseAssert(x_bcIsBarrierArray[variantOrdBase] == x_bcIsBarrierArray[i]);                                                      \
            }                                                                                                                                        \
            res[static_cast<size_t>(BCKind::e)] = x_bcIsBarrierArray[variantOrdBase];                                                                \
        }
        PP_FOR_EACH(macro, GENERATED_ALL_BYTECODE_BUILDER_BYTECODE_NAMES)
#undef macro
        return res;
    }();

    static constexpr std::array<bool, static_cast<size_t>(BCKind::X_END_OF_ENUM)> x_bcKindMayMakeTailCallArray = []() {
        constexpr size_t totalKinds = static_cast<size_t>(BCKind::X_END_OF_ENUM);
        std::array<bool, totalKinds> resArr;
#define macro(e)                                                                                                                                     \
        {                                                                                                                                            \
            uint8_t res = 255;                                                                                                                       \
            constexpr size_t variantOrdBase = GetBytecodeOpcodeBase<DeegenGenerated_BytecodeBuilder_ ## e>();                                        \
            constexpr size_t numVariants = GetNumVariantsOfBytecode<DeegenGenerated_BytecodeBuilder_ ## e>();                                        \
            for (size_t i = variantOrdBase; i < variantOrdBase + numVariants; i++) {                                                                 \
                uint8_t val = DeegenGenerated_BytecodeBuilder_ ##e <BytecodeAccessor<isDecodingMode>>::x_bytecodeMayMakeTailCall[i - variantOrdBase];\
                if (x_isPrimitiveBcArray[i]) {                                                                                                       \
                    ReleaseAssert(val != 255 && (res == 255 || res == val));                                                                         \
                    res = val;                                                                                                                       \
                } else {                                                                                                                             \
                    ReleaseAssert(val == 255);                                                                                                       \
                }                                                                                                                                    \
            }                                                                                                                                        \
            ReleaseAssert(res == 0 || res == 1);                                                                                                     \
            resArr[static_cast<size_t>(BCKind::e)] = res;                                                                                            \
        }
        PP_FOR_EACH(macro, GENERATED_ALL_BYTECODE_BUILDER_BYTECODE_NAMES)
#undef macro
        return resArr;
    }();

    using RWCInfoGetterFn = BytecodeRWCInfo(BytecodeAccessor<isDecodingMode>::*)(size_t);

    static constexpr std::array<RWCInfoGetterFn, x_numTotalVariants> x_rwcReadInfoFns = []() {
        std::array<RWCInfoGetterFn, x_numTotalVariants> res;
#define macro(e)                                                                                                                                     \
        {                                                                                                                                            \
            constexpr size_t variantOrdBase = GetBytecodeOpcodeBase<DeegenGenerated_BytecodeBuilder_ ## e>();                                        \
            constexpr size_t numVariants = GetNumVariantsOfBytecode<DeegenGenerated_BytecodeBuilder_ ## e>();                                        \
            for (size_t i = variantOrdBase; i < variantOrdBase + numVariants; i++) {                                                                 \
                res[i] = DeegenGenerated_BytecodeBuilder_ ##e <BytecodeAccessor<isDecodingMode>>::x_bytecodeReadDeclGetters[i - variantOrdBase];     \
            }                                                                                                                                        \
        }
        PP_FOR_EACH(macro, GENERATED_ALL_BYTECODE_BUILDER_BYTECODE_NAMES)
#undef macro
        return res;
    }();

    static constexpr std::array<RWCInfoGetterFn, x_numTotalVariants> x_rwcWriteInfoFns = []() {
        std::array<RWCInfoGetterFn, x_numTotalVariants> res;
#define macro(e)                                                                                                                                     \
        {                                                                                                                                            \
            constexpr size_t variantOrdBase = GetBytecodeOpcodeBase<DeegenGenerated_BytecodeBuilder_ ## e>();                                        \
            constexpr size_t numVariants = GetNumVariantsOfBytecode<DeegenGenerated_BytecodeBuilder_ ## e>();                                        \
            for (size_t i = variantOrdBase; i < variantOrdBase + numVariants; i++) {                                                                 \
                res[i] = DeegenGenerated_BytecodeBuilder_ ##e <BytecodeAccessor<isDecodingMode>>::x_bytecodeWriteDeclGetters[i - variantOrdBase];    \
            }                                                                                                                                        \
        }
        PP_FOR_EACH(macro, GENERATED_ALL_BYTECODE_BUILDER_BYTECODE_NAMES)
#undef macro
        return res;
    }();

    static constexpr std::array<RWCInfoGetterFn, x_numTotalVariants> x_rwcClobberInfoFns = []() {
        std::array<RWCInfoGetterFn, x_numTotalVariants> res;
#define macro(e)                                                                                                                                     \
        {                                                                                                                                            \
            constexpr size_t variantOrdBase = GetBytecodeOpcodeBase<DeegenGenerated_BytecodeBuilder_ ## e>();                                        \
            constexpr size_t numVariants = GetNumVariantsOfBytecode<DeegenGenerated_BytecodeBuilder_ ## e>();                                        \
            for (size_t i = variantOrdBase; i < variantOrdBase + numVariants; i++) {                                                                 \
                res[i] = DeegenGenerated_BytecodeBuilder_ ##e <BytecodeAccessor<isDecodingMode>>::x_bytecodeClobberDeclGetters[i - variantOrdBase];  \
            }                                                                                                                                        \
        }
        PP_FOR_EACH(macro, GENERATED_ALL_BYTECODE_BUILDER_BYTECODE_NAMES)
#undef macro
        return res;
    }();

    static constexpr std::array<uint8_t, x_numTotalVariants> x_bcIntrinsicOrdinalArray = []() {
        std::array<uint8_t, x_numTotalVariants> res;
#define macro(e)                                                                                                                    \
        {                                                                                                                           \
            constexpr size_t variantOrdBase = GetBytecodeOpcodeBase<DeegenGenerated_BytecodeBuilder_ ## e>();                       \
            constexpr size_t numVariants = GetNumVariantsOfBytecode<DeegenGenerated_BytecodeBuilder_ ## e>();                       \
            for (size_t i = variantOrdBase; i < variantOrdBase + numVariants; i++) {                                                \
                res[i] = DeegenGenerated_BytecodeBuilder_ ##e <BytecodeAccessor<isDecodingMode>>::x_bytecodeIntrinsicOrdinal;       \
            }                                                                                                                       \
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

    static constexpr std::array<bool, static_cast<size_t>(BCKind::X_END_OF_ENUM)> x_isBcKindPotentiallyReplaceable = []() {
        std::array<bool, static_cast<size_t>(BCKind::X_END_OF_ENUM)> res;
#define macro(e) res[static_cast<size_t>(BCKind::e)] = DeegenGenerated_BytecodeBuilder_ ##e <BytecodeAccessor<isDecodingMode>>::x_isPotentiallyReplaceable;
        PP_FOR_EACH(macro, GENERATED_ALL_BYTECODE_BUILDER_BYTECODE_NAMES)
#undef macro
        return res;
    }();

    static constexpr std::array<uint8_t, static_cast<size_t>(BCKind::X_END_OF_ENUM)> x_bcKindDfgNsdLengthArray = []() {
        std::array<uint8_t, static_cast<size_t>(BCKind::X_END_OF_ENUM)> res;
#define macro(e)                                                                                                                        \
        {                                                                                                                               \
            size_t nsdLength = DeegenGenerated_BytecodeBuilder_ ##e <BytecodeAccessor<isDecodingMode>>::x_dfgNodeSpecificDataLength;    \
            ReleaseAssert(nsdLength <= 255);                                                                                            \
            res[static_cast<size_t>(BCKind::e)] = static_cast<uint8_t>(nsdLength);                                                      \
        }
        PP_FOR_EACH(macro, GENERATED_ALL_BYTECODE_BUILDER_BYTECODE_NAMES)
#undef macro
        return res;
    }();

    using DfgNsdInfoWriterFn = void(BytecodeAccessor<isDecodingMode>::*)(uint8_t*, size_t);

    static constexpr std::array<DfgNsdInfoWriterFn, x_numTotalVariants> x_dfgNsdInfoWriterFnArray = []() {
        std::array<DfgNsdInfoWriterFn, x_numTotalVariants> res;
#define macro(e)                                                                                                                                \
        {                                                                                                                                       \
            constexpr size_t variantOrdBase = GetBytecodeOpcodeBase<DeegenGenerated_BytecodeBuilder_ ## e>();                                   \
            constexpr size_t numVariants = GetNumVariantsOfBytecode<DeegenGenerated_BytecodeBuilder_ ## e>();                                   \
            for (size_t i = variantOrdBase; i < variantOrdBase + numVariants; i++) {                                                            \
                res[i] = DeegenGenerated_BytecodeBuilder_ ##e <BytecodeAccessor<isDecodingMode>>::x_dfgNsdInfoWriterFns[i - variantOrdBase];    \
            }                                                                                                                                   \
        }
        PP_FOR_EACH(macro, GENERATED_ALL_BYTECODE_BUILDER_BYTECODE_NAMES)
#undef macro
        return res;
    }();

    using DfgDumpNsdInfoFn = void(*)(FILE*, uint8_t*, bool&);

    static constexpr std::array<DfgDumpNsdInfoFn, static_cast<size_t>(BCKind::X_END_OF_ENUM)> x_bcKindDumpDfgNsdInfoFnArray = []() {
        std::array<DfgDumpNsdInfoFn, static_cast<size_t>(BCKind::X_END_OF_ENUM)> res;
#define macro(e)   \
        res[static_cast<size_t>(BCKind::e)] = &DeegenGenerated_BytecodeBuilder_ ##e <BytecodeAccessor<isDecodingMode>>::DumpDfgNodeSpecificDataImpl;

        PP_FOR_EACH(macro, GENERATED_ALL_BYTECODE_BUILDER_BYTECODE_NAMES)
#undef macro
        return res;
    }();

private:
    using DfgRangeOpRWInfoGetterFn = void(*)(uint8_t*, uint32_t*, uint32_t*, size_t&, size_t&, size_t&);

    template<typename T>
    static constexpr DfgRangeOpRWInfoGetterFn GetDfgRangeOpRWInfoGetterFnArrayForBytecode()
    {
        return T::x_dfgRangeOpRWInfoGetterFns;
    }

    static constexpr std::array<DfgRangeOpRWInfoGetterFn, static_cast<size_t>(BCKind::X_END_OF_ENUM)> x_dfgRangeOpRWInfoGetterArrays = []() {
        std::array<DfgRangeOpRWInfoGetterFn, static_cast<size_t>(BCKind::X_END_OF_ENUM)> res;
#define macro(e)   \
        res[static_cast<size_t>(BCKind::e)] = GetDfgRangeOpRWInfoGetterFnArrayForBytecode<DeegenGenerated_BytecodeBuilder_ ##e <BytecodeAccessor<isDecodingMode>>>();

        PP_FOR_EACH(macro, GENERATED_ALL_BYTECODE_BUILDER_BYTECODE_NAMES)
#undef macro
        return res;
    }();

    using DfgPredictionPropagationSetupFn = void(*)(DfgPredictionPropagationSetupInfo&, uint8_t*);

    template<typename T>
    static constexpr DfgPredictionPropagationSetupFn GetDfgPredictionPropagationSetupFuncForBytecode()
    {
        return T::x_dfgPredictionPropagationSetupFn;
    }

    static constexpr std::array<DfgPredictionPropagationSetupFn, static_cast<size_t>(BCKind::X_END_OF_ENUM)> x_dfgPredictionPropagationSetupFnArrays = []() {
        std::array<DfgPredictionPropagationSetupFn, static_cast<size_t>(BCKind::X_END_OF_ENUM)> res;
#define macro(e)   \
        res[static_cast<size_t>(BCKind::e)] = GetDfgPredictionPropagationSetupFuncForBytecode<DeegenGenerated_BytecodeBuilder_ ##e <BytecodeAccessor<isDecodingMode>>>();

        PP_FOR_EACH(macro, GENERATED_ALL_BYTECODE_BUILDER_BYTECODE_NAMES)
#undef macro
        return res;
    }();

public:

    // Get the mapping of the DFG Node SSA inputs/outputs to the locations into the ranged operand
    //
    // nsd:        the NodeSpecificData of the DFG Node
    //
    // inputOrds:  The relative offset from the range start for all the reads of the range will be stored here.
    //             This corresponds to the last X SSA inputs of the DFG node, in the same order
    //
    // outputOrds: Similar, but for outputs
    //
    // numInputs:  Caller should set this to be the size of the inputOrds buffer, for assertion check
    //             After the call, this will be the actual # of items populated into inputOrds
    //
    // numOutputs: Similar, but for outputs
    //
    // rangeLen:   After the call, this will be the required length of the range operand when doing codegen
    //
    static void ALWAYS_INLINE GetDfgRangeOperandRWInfo(BCKind bcKind,
                                                       uint8_t* nsd,
                                                       uint32_t* inputOrds /*out*/,
                                                       uint32_t* outputOrds /*out*/,
                                                       size_t& numInputs /*inout*/,
                                                       size_t& numOutputs /*inout*/,
                                                       size_t& rangeLen /*out*/)
    {
        Assert(isDecodingMode);
        TestAssert(bcKind < BCKind::X_END_OF_ENUM);
        DfgRangeOpRWInfoGetterFn func = x_dfgRangeOpRWInfoGetterArrays[static_cast<size_t>(bcKind)];
        if (func == nullptr)
        {
            numInputs = 0;
            numOutputs = 0;
            rangeLen = 0;
            return;
        }

        return func(nsd, inputOrds, outputOrds, numInputs, numOutputs, rangeLen);
    }

    static void ALWAYS_INLINE SetupPredictionPropagationData(BCKind bcKind,
                                                             DfgPredictionPropagationSetupInfo& state /*inout*/,
                                                             uint8_t* nsd)
    {
        Assert(isDecodingMode);
        TestAssert(bcKind < BCKind::X_END_OF_ENUM);
        DfgPredictionPropagationSetupFn func = x_dfgPredictionPropagationSetupFnArrays[static_cast<size_t>(bcKind)];
        TestAssert(func != nullptr);
        func(state, nsd);
    }

    static bool ALWAYS_INLINE BytecodeHasRangeOperand(BCKind bcKind)
    {
        Assert(isDecodingMode);
        TestAssert(bcKind < BCKind::X_END_OF_ENUM);
        return x_dfgRangeOpRWInfoGetterArrays[static_cast<size_t>(bcKind)] != nullptr;
    }
};

class BytecodeBuilder final : public BytecodeAccessor<false /*isDecodingMode*/>
{
public:
    BytecodeBuilder() = default;
};

class BytecodeDecoder final : public BytecodeAccessor<true /*isDecodingMode*/>
{
public:
    BytecodeDecoder(CodeBlock* cb);
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
        Assert(is_power_of_2(alignment));
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

inline constexpr std::array<DfgPredictionPropagationImplFuncTy, static_cast<size_t>(DeegenBytecodeBuilder::BCKind::X_END_OF_ENUM)> x_dfgPredictionPropagationFuncs = []()
{
    using namespace DeegenBytecodeBuilder;
    std::array<DfgPredictionPropagationImplFuncTy, static_cast<size_t>(BCKind::X_END_OF_ENUM)> res;
    std::array<bool, static_cast<size_t>(BCKind::X_END_OF_ENUM)> resSet;
    for (size_t i = 0; i < static_cast<size_t>(BCKind::X_END_OF_ENUM); i++) { resSet[i] = false; }

#define macro(e)                                                                                                                \
    res[static_cast<size_t>(BCKind::e)] = DfgPredictionPropagationImplFuncPtr<DeegenGenerated_BytecodeBuilder_ ## e>::value;    \
    resSet[static_cast<size_t>(BCKind::e)] = true;

    PP_FOR_EACH(macro, GENERATED_ALL_BYTECODE_BUILDER_BYTECODE_NAMES)
#undef macro

    for (size_t i = 0; i < static_cast<size_t>(BCKind::X_END_OF_ENUM); i++) { ReleaseAssert(resSet[i]); }
    return res;
}();
