#pragma once

#include "common_utils.h"
#include "ptr_utils.h"
#include "tvalue.h"
#include "deegen_metadata_accessor.h"
#include "bytecode_rwc_info.h"
#include "bytecode_intrinsic_info.h"

// We need two extra bytes to store the "stopper" bytecode opcode at the end for baseline JIT codegen,
// but for legacy reason this value is 4 and I don't want to change it anymore
//
static constexpr size_t x_numExtraPaddingAtBytecodeStreamEnd = 4;

namespace DeegenBytecodeBuilder
{

// Refers to a local (a bytecode slot in current function frame)
//
struct Local
{
    explicit Local(uint64_t ord) : m_localOrd(ord) { }
    bool operator==(const Local& other) const { return m_localOrd == other.m_localOrd; }
    int64_t AsRawValue() { return static_cast<int64_t>(m_localOrd); }

    uint64_t m_localOrd;
};

// Enscapulates a TValue with a known type
// This is used for bytecodes that accepts constant operands.
// LLVM is not smart enough to understand the boxing and typecheck operation (e.g., it cannot know that TValue::Create<tDouble>(..).Is<tDouble>() must be true).
// So whenever a bytecode accepts a constant operand, we also allow this class to be passed in instead of a TValue,
// so that LLVM can better optimize away the branches in the bytecode builder.
//
struct TValueWithKnownType
{
    TypeSpeculationMask m_knownTypeMask;
    TValue m_value;
};

// The user-exposed API to construct a TValueWithKnownType.
//
template<typename T, typename = std::enable_if_t<fn_num_args<decltype(&T::encode)> == 1>>
TValueWithKnownType ALWAYS_INLINE WARN_UNUSED Cst(arg_nth_t<decltype(&T::encode), 0 /*argOrd*/> val)
{
    static_assert(IsValidTypeSpecialization<T>);
    return TValueWithKnownType {
        .m_knownTypeMask = x_typeSpeculationMaskFor<T>,
        .m_value = TValue::Create<T>(val)
    };
}

template<typename T, typename = std::enable_if_t<fn_num_args<decltype(&T::encode)> == 0>>
TValueWithKnownType ALWAYS_INLINE WARN_UNUSED Cst()
{
    static_assert(IsValidTypeSpecialization<T>);
    return TValueWithKnownType {
        .m_knownTypeMask = x_typeSpeculationMaskFor<T>,
        .m_value = TValue::Create<T>()
    };
}

// Refers to a constant, it can be either constructed by a TValue, or constructed by the 'Cst' API
//
struct CstWrapper
{
    CstWrapper(TValueWithKnownType v) : m_hasKnownTypeMask(true), m_knownTypeMask(v.m_knownTypeMask), m_value(v.m_value) { }
    CstWrapper(TValue v) : m_hasKnownTypeMask(false), m_value(v) { }

    template<typename T>
    bool HasType() const
    {
        if (m_hasKnownTypeMask)
        {
            constexpr TypeSpeculationMask queryMask = x_typeSpeculationMaskFor<T>;
            if ((m_knownTypeMask & queryMask) == m_knownTypeMask)
            {
                assert(m_value.Is<T>());
                return true;
            }
            if ((m_knownTypeMask & queryMask) == 0)
            {
                assert(!m_value.Is<T>());
                return false;
            }
            return m_value.Is<T>();
        }
        else
        {
            return m_value.Is<T>();
        }
    }

    bool operator==(const TValue& other) const { return m_value.m_value == other.m_value; }
    bool operator==(const TValueWithKnownType& other) const { return m_value.m_value == other.m_value.m_value; }

    bool m_hasKnownTypeMask;
    TypeSpeculationMask m_knownTypeMask;
    TValue m_value;
};

struct LocalOrCstWrapper
{
    LocalOrCstWrapper(Local v) : m_isLocal(true), m_localOrd(v.m_localOrd) { }
    LocalOrCstWrapper(TValue v) : m_isLocal(false), m_hasKnownTypeMask(false), m_value(v) { }
    LocalOrCstWrapper(TValueWithKnownType v) : m_isLocal(false), m_hasKnownTypeMask(true), m_knownTypeMask(v.m_knownTypeMask), m_value(v.m_value) { }

    Local AsLocal() const { assert(m_isLocal); return Local(m_localOrd); }
    TValue AsConstant() const { assert(!m_isLocal); return m_value; }

    bool operator==(const Local& other) const { return (m_isLocal && AsLocal() == other); }
    bool operator==(const TValue& other) const { return (!m_isLocal && AsConstant().m_value == other.m_value); }
    bool operator==(const TValueWithKnownType& other) const { return m_value.m_value == other.m_value.m_value; }

    template<typename T>
    bool IsConstantAndHasType() const
    {
        if (m_isLocal)
        {
            return false;
        }
        if (m_hasKnownTypeMask)
        {
            constexpr TypeSpeculationMask queryMask = x_typeSpeculationMaskFor<T>;
            if ((m_knownTypeMask & queryMask) == m_knownTypeMask)
            {
                assert(m_value.Is<T>());
                return true;
            }
            if ((m_knownTypeMask & queryMask) == 0)
            {
                assert(!m_value.Is<T>());
                return false;
            }
            return m_value.Is<T>();
        }
        else
        {
            return m_value.Is<T>();
        }
    }

    bool m_isLocal;
    uint64_t m_localOrd;
    bool m_hasKnownTypeMask;
    TypeSpeculationMask m_knownTypeMask;
    TValue m_value;
};

template<typename T>
struct ForbidUninitialized
{
    ForbidUninitialized(T v) : m_value(v) { }
    bool operator==(const T& other) const { return m_value == other; }

    int64_t AsRawValue() { return SafeIntegerCast<int64_t>(m_value); }

    static_assert(std::is_integral_v<T>);

    T m_value;
};

struct MetadataFieldPatchRecord
{
    uint32_t bytecodeOffset;
    uint16_t typeOrd;
    uint16_t index;
};

// Returns the length of the metadata part, which should also be the trailing array size of the code block
//
uint32_t WARN_UNUSED PatchBytecodeMetadataFields(RestrictPtr<uint8_t> bytecodeStart, size_t bytecodeLen, const uint16_t* numOfEachMetadataKind, const std::vector<MetadataFieldPatchRecord>& patchList);

template<bool isDecodingMode, typename MetadataTypeListInfo>
class BytecodeBuilderBase
{
    MAKE_NONCOPYABLE(BytecodeBuilderBase);
    MAKE_NONMOVABLE(BytecodeBuilderBase);

    static constexpr size_t x_numBytecodeMetadataKinds = MetadataTypeListInfo::numElements;

public:
    template<bool>
    friend class BytecodeAccessor;

    std::pair<uint64_t*, size_t> GetBuiltConstantTable()
    {
        assert(!isDecodingMode);
        // Internally, to better fit our implementation, we store the constant table reversed, and index it with negative indices.
        // So now it's time to reverse the constant table before returning to user.
        //
        size_t len = m_constantTable.size();
        uint64_t* tab = new uint64_t[len];
        CopyAndReverseConstantTable(tab /*out*/, m_constantTable.data(), len);
        return std::make_pair(tab, len);
    }

    uint32_t GetBytecodeMetadataTotalLength()
    {
        assert(!isDecodingMode);
        if (!m_metadataFieldPatched)
        {
            m_metadataFieldPatched = true;
            PatchMetadataFields();
        }
        assert(m_bytecodeMetadataLength % 8 == 0);
        return m_bytecodeMetadataLength;
    }

    const std::array<uint16_t, x_numBytecodeMetadataKinds>& GetBytecodeMetadataUseCountArray()
    {
        assert(!isDecodingMode);
        return m_bytecodeMetadataCnt;
    }

    size_t GetCurLength()
    {
        return static_cast<size_t>(m_bufferCur - m_bufferBegin);
    }

    constexpr bool IsDecodingMode()
    {
        return isDecodingMode;
    }

protected:
    BytecodeBuilderBase()
    {
        TestAssert(!isDecodingMode);
        constexpr size_t initialSize = 128;
        m_bufferBegin = new uint8_t[initialSize];
        m_bufferCur = m_bufferBegin;
        m_bufferEnd = m_bufferBegin + initialSize;
        for (size_t i = 0; i < m_bytecodeMetadataCnt.size(); i++) { m_bytecodeMetadataCnt[i] = 0; }
        m_metadataFieldPatched = false;
    }

    BytecodeBuilderBase(uint8_t* bytecodeStream, size_t bytecodeStreamLength, TValue* constantTableEndInDecodingMode)
    {
        TestAssert(isDecodingMode);
        m_bufferBegin = bytecodeStream;
        m_bufferCur = bytecodeStream + bytecodeStreamLength;
        m_bufferEnd = m_bufferCur;
        m_constantTableEndInDecodingMode = constantTableEndInDecodingMode;
    }

    ~BytecodeBuilderBase()
    {
        if (!isDecodingMode && m_bufferBegin != nullptr)
        {
            delete [] m_bufferBegin;
            m_bufferBegin = nullptr;
        }
    }

    std::pair<uint8_t*, size_t> GetBuiltBytecodeSequenceImpl()
    {
        assert(!isDecodingMode);
        if (!m_metadataFieldPatched)
        {
            m_metadataFieldPatched = true;
            PatchMetadataFields();
        }

        size_t len = GetCurLength();
        uint8_t* res = new uint8_t[len + x_numExtraPaddingAtBytecodeStreamEnd];
        memcpy(res, GetBytecodeStart(), len);
        memset(res + len, 0, x_numExtraPaddingAtBytecodeStreamEnd);
        return std::make_pair(res, len + x_numExtraPaddingAtBytecodeStreamEnd);
    }

    uint8_t* Reserve(size_t size)
    {
        assert(!isDecodingMode);
        if (likely(m_bufferEnd - m_bufferCur >= static_cast<ssize_t>(size)))
        {
            return m_bufferCur;
        }
        size_t cap = static_cast<size_t>(m_bufferEnd - m_bufferBegin);
        size_t current = static_cast<size_t>(m_bufferCur - m_bufferBegin);
        size_t needed = current + size;
        while (cap < needed) { cap *= 2; }
        uint8_t* newArray = new uint8_t[cap];
        memcpy(newArray, m_bufferBegin, current);
        delete [] m_bufferBegin;
        m_bufferBegin = newArray;
        m_bufferCur = newArray + current;
        m_bufferEnd = newArray + cap;
        return m_bufferCur;
    }

    void MarkWritten(size_t size)
    {
        assert(!isDecodingMode);
        m_bufferCur += size;
        assert(m_bufferCur <= m_bufferEnd);
    }

    uint8_t* GetBytecodeStart()
    {
        return m_bufferBegin;
    }

    int64_t InsertConstantIntoTable(TValue cst)
    {
        assert(!isDecodingMode);
        uint64_t v = cst.m_value;
        auto res = m_constantTableLocationMap.emplace(std::make_pair(v, m_constantTable.size()));
        auto it = res.first;
        if (res.second)
        {
            // The constant 'v' didn't exist, and was just inserted into the map
            //
            assert(it->second == m_constantTable.size());
            m_constantTable.push_back(v);
        }
        else
        {
            // The constant 'v' already exists in the table, no need to do anything
            //
        }

        uint64_t constantTableOrd = it->second;
        // Internally, to better fit our implementation, we store the constant table reversed, and index it with negative indices.
        //
        return -static_cast<int64_t>(constantTableOrd) - 1;
    }

public:
    TValue ALWAYS_INLINE GetConstantFromConstantTable(int64_t ord)
    {
        assert(ord < 0);
        if constexpr(isDecodingMode)
        {
            return m_constantTableEndInDecodingMode[ord];
        }
        else
        {
            ord = -(ord + 1);
            assert(static_cast<size_t>(ord) < m_constantTable.size());
            TValue res; res.m_value = m_constantTable[static_cast<size_t>(ord)];
            return res;
        }
    }

protected:
    template<typename MetadataType>
    void RegisterMetadataField(uint8_t* ptr)
    {
        assert(!isDecodingMode);
        constexpr size_t typeOrd = MetadataTypeListInfo::template typeOrdinal<MetadataType>;
        static_assert(typeOrd < x_numBytecodeMetadataKinds);
        uint16_t index = m_bytecodeMetadataCnt[typeOrd];
        ReleaseAssert(index < std::numeric_limits<uint16_t>::max());
        m_bytecodeMetadataCnt[typeOrd]++;
        m_metadataFieldPatchRecords.push_back({
            .bytecodeOffset = SafeIntegerCast<uint32_t>(ptr - m_bufferBegin),
            .typeOrd = SafeIntegerCast<uint16_t>(typeOrd),
            .index = index
        });
    }

private:
    static void CopyAndReverseConstantTable(RestrictPtr<uint64_t> dst /*out*/, RestrictPtr<uint64_t> src, size_t len)
    {
        for (size_t i = 0; i < len; i++)
        {
            dst[i] = src[len - i - 1];
        }
    }

    void PatchMetadataFields()
    {
        assert(!isDecodingMode);
        // This is lame: due to header file dependency issues, this implementation has to sit in a CPP file..
        // So we are assuming that our template parameter 'MetadataTypeListInfo' is just the BytecodeMetadataTypeListInfo
        // define in bytecode_builder.h, but we cannot assert it. Honestly this is fragile, but let's go with it for now.
        //
        // Note that the bytecodeLength below must agree with the length returned by GetBuiltBytecodeSequence()
        //
        m_bytecodeMetadataLength = PatchBytecodeMetadataFields(
            m_bufferBegin /*bytecodeStart*/,
            GetCurLength() + x_numExtraPaddingAtBytecodeStreamEnd /*bytecodeLen*/,
            m_bytecodeMetadataCnt.data(),
            m_metadataFieldPatchRecords);
    }

    uint8_t* m_bufferBegin;
    uint8_t* m_bufferCur;
    uint8_t* m_bufferEnd;

    TValue* m_constantTableEndInDecodingMode;

    std::vector<uint64_t> m_constantTable;
    // TODO: We probably don't want to use std::unordered_map since it's so poorly implemented.. but let's stay simple for now
    //
    std::unordered_map<uint64_t, uint64_t> m_constantTableLocationMap;

    std::array<uint16_t, x_numBytecodeMetadataKinds> m_bytecodeMetadataCnt;

    bool m_metadataFieldPatched;
    uint32_t m_bytecodeMetadataLength;
    std::vector<MetadataFieldPatchRecord> m_metadataFieldPatchRecords;
};

}   // namespace DeegenBytecodeBuilder
