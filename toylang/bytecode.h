#pragma once

#include "common_utils.h"

namespace ToyLang {

using namespace CommonUtils;

template<typename T>
using RestrictPtr = T* __restrict__;

template<typename T>
using ConstRestrictPtr = RestrictPtr<const T>;

#define CLANG_GS_ADDRESS_SPACE_IDENTIFIER 256
#define CLANG_FS_ADDRESS_SPACE_IDENTIFIER 257

template<typename T>
using HeapPtr = T __attribute__((address_space(CLANG_GS_ADDRESS_SPACE_IDENTIFIER))) *;

template<typename T>
using HeapRef = T __attribute__((address_space(CLANG_GS_ADDRESS_SPACE_IDENTIFIER))) &;

template<typename P, typename T>
inline constexpr bool IsPtrOrHeapPtr = std::is_same_v<P, T*> || std::is_same_v<P, HeapPtr<T>>;

template<typename T> struct IsHeapPtrType : std::false_type { };
template<typename T> struct IsHeapPtrType<HeapPtr<T>> : std::true_type { };

template<typename DstPtr, typename SrcPtr>
struct ReinterpretCastPreservingAddressSpaceTypeImpl
{
    static_assert(std::is_pointer_v<DstPtr>, "DstPtr should be a pointer type");
    static_assert(std::is_pointer_v<SrcPtr>, "SrcPtr should be a pointer type");
    static_assert(!IsHeapPtrType<DstPtr>::value, "DstPtr should not be a HeapPtr pointer");

    using type = DstPtr;
};

template<typename DstPtr, typename Src>
struct ReinterpretCastPreservingAddressSpaceTypeImpl<DstPtr, HeapPtr<Src>>
{
    static_assert(std::is_pointer_v<DstPtr>, "Dst should be a pointer type");
    static_assert(!IsHeapPtrType<DstPtr>::value, "DstPtr should not be a HeapPtr pointer");

    using Dst = std::remove_pointer_t<DstPtr>;
    using type = HeapPtr<Dst>;
};

template<typename DstPtr, typename SrcPtr>
using ReinterpretCastPreservingAddressSpaceType = typename ReinterpretCastPreservingAddressSpaceTypeImpl<DstPtr, SrcPtr>::type;

template<typename DstPtr, typename SrcPtr>
ReinterpretCastPreservingAddressSpaceType<DstPtr, SrcPtr> ReinterpretCastPreservingAddressSpace(SrcPtr p)
{
    return reinterpret_cast<ReinterpretCastPreservingAddressSpaceType<DstPtr, SrcPtr>>(p);
}

template<typename T>
struct remove_heap_ptr
{
    static_assert(std::is_pointer_v<T>);
    using type = T;
};

template<typename T>
struct remove_heap_ptr<HeapPtr<T>>
{
    using type = T*;
};

template<typename T>
using remove_heap_ptr_t = typename remove_heap_ptr<T>::type;

// Equivalent to memcpy, but asserts that the address range does not overlap
//
inline void ALWAYS_INLINE SafeMemcpy(void* dst, const void* src, size_t len)
{
    assert(reinterpret_cast<uintptr_t>(dst) + len <= reinterpret_cast<uintptr_t>(src) || reinterpret_cast<uintptr_t>(src) + len <= reinterpret_cast<uintptr_t>(dst));
    memcpy(dst, src, len);
}

#define HOI_SYS_HEAP 1
#define HOI_USR_HEAP 2
#define HEAP_OBJECT_INFO_LIST                                                           \
  /* Enum Name                      C++ name                        Lives in       */   \
    (STRING,                        HeapString,                     HOI_USR_HEAP)       \
  , (FUNCTION,                      FunctionObject,                 HOI_USR_HEAP)       \
  , (USERDATA,                      HeapCDataObject,                HOI_USR_HEAP)       \
  , (THREAD,                        CoroutineRuntimeContext,        HOI_USR_HEAP)       \
  , (TABLE,                         HeapTableObject,                HOI_USR_HEAP)       \
  , (StructuredHiddenClass,         StructuredHiddenClass,          HOI_SYS_HEAP)       \
  , (DictionaryHiddenClass,         DictionaryHiddenClass,          HOI_SYS_HEAP)       \
  , (HiddenClassAnchorHashTable,    HiddenClassAnchorHashTable,     HOI_SYS_HEAP)

#define HOI_ENUM_NAME(hoi) PP_TUPLE_GET_1(hoi)
#define HOI_CLASS_NAME(hoi) PP_TUPLE_GET_2(hoi)
#define HOI_HEAP_KIND(hoi) (PP_TUPLE_GET_3(hoi))

// Forward declare all the classes
//
#define macro(hoi) class HOI_CLASS_NAME(hoi);
PP_FOR_EACH(macro, HEAP_OBJECT_INFO_LIST)
#undef macro

enum class Type : uint8_t
{
    NIL,
    BOOLEAN,
    DOUBLE,

    // Declare enum for heap object types
    //
#define macro(hoi) HOI_ENUM_NAME(hoi),
PP_FOR_EACH(macro, HEAP_OBJECT_INFO_LIST)
#undef macro

    X_END_OF_ENUM
};

template<typename T>
struct TypeEnumForHeapObjectImpl
{
    static constexpr Type value = Type::X_END_OF_ENUM;
};

#define macro(hoi)                                                     \
    template<> struct TypeEnumForHeapObjectImpl<HOI_CLASS_NAME(hoi)> { \
        static constexpr Type value = Type::HOI_ENUM_NAME(hoi);        \
    };
PP_FOR_EACH(macro, HEAP_OBJECT_INFO_LIST)
#undef macro

// Type mapping from class name to enum name
//
template<typename T>
constexpr Type TypeEnumForHeapObject = TypeEnumForHeapObjectImpl<T>::value;

template<typename T>
constexpr bool IsHeapObjectType = (TypeEnumForHeapObject<T> != Type::X_END_OF_ENUM);

template<typename T>
struct TypeMayLiveInSystemHeapImpl : std::true_type { };

#define macro(hoi)                                                                      \
    template<> struct TypeMayLiveInSystemHeapImpl<HOI_CLASS_NAME(hoi)>                  \
        : std::integral_constant<bool, ((HOI_HEAP_KIND(hoi) & HOI_SYS_HEAP) > 0)> { };
PP_FOR_EACH(macro, HEAP_OBJECT_INFO_LIST)
#undef macro

template<typename T>
constexpr bool TypeMayLiveInSystemHeap = TypeMayLiveInSystemHeapImpl<T>::value;

template<typename T>
struct TypeMayLiveInUserHeapImpl : std::true_type { };

#define macro(hoi)                                                                      \
    template<> struct TypeMayLiveInUserHeapImpl<HOI_CLASS_NAME(hoi)>                    \
        : std::integral_constant<bool, ((HOI_HEAP_KIND(hoi) & HOI_USR_HEAP) > 0)> { };
PP_FOR_EACH(macro, HEAP_OBJECT_INFO_LIST)
#undef macro

template<typename T>
constexpr bool TypeMayLiveInUserHeap = TypeMayLiveInUserHeapImpl<T>::value;

class HeapEntityCommonHeader
{
public:
    Type m_type;
    uint8_t m_padding1;     // reserved for GC

    template<typename T>
    static void Populate(T self)
    {
        using RawTypePtr = remove_heap_ptr_t<T>;
        static_assert(std::is_pointer_v<RawTypePtr>);
        using RawType = std::remove_pointer_t<RawTypePtr>;
        static_assert(IsHeapObjectType<RawType>);
        static_assert(std::is_base_of_v<HeapEntityCommonHeader, RawType>);
        self->m_type = TypeEnumForHeapObject<RawType>;
        self->m_padding1 = 0;
    }
};
static_assert(sizeof(HeapEntityCommonHeader) == 2);

template<typename T = void>
struct UserHeapPointer
{
    UserHeapPointer() : m_value(0) { }
    UserHeapPointer(int64_t value)
        : m_value(value)
    {
        static_assert(TypeMayLiveInUserHeap<T>);
        assert(x_minValue <= m_value && m_value <= x_maxValue);
        if constexpr(!std::is_same_v<T, void>)
        {
            assert(m_value % static_cast<int64_t>(std::alignment_of_v<T>) == 0);
        }
    }

    template<typename U, typename = std::enable_if_t<std::is_same_v<T, void> || std::is_same_v<T, uint8_t> || std::is_same_v<U, void> || std::is_same_v<U, uint8_t> || std::is_same_v<T, U>>>
    UserHeapPointer(HeapPtr<U> value)
        : UserHeapPointer(reinterpret_cast<intptr_t>(value))
    {
        assert(As<U>() == value);
    }

    template<typename U, typename = std::enable_if_t<std::is_same_v<T, void> || std::is_same_v<T, uint8_t> || std::is_same_v<U, void> || std::is_same_v<U, uint8_t> || std::is_same_v<T, U>>>
    UserHeapPointer(UserHeapPointer<U>& other)
        : UserHeapPointer(other.m_value)
    { }

    template<typename U, typename = std::enable_if_t<std::is_same_v<T, void> || std::is_same_v<T, uint8_t> || std::is_same_v<U, void> || std::is_same_v<U, uint8_t> || std::is_same_v<T, U>>>
    UserHeapPointer(UserHeapPointer<U>&& other)
        : UserHeapPointer(other.m_value)
    { }

    template<typename U, typename = std::enable_if_t<std::is_same_v<T, void> || std::is_same_v<T, uint8_t> || std::is_same_v<U, void> || std::is_same_v<U, uint8_t> || std::is_same_v<T, U>>>
    UserHeapPointer(HeapRef<UserHeapPointer<U>> other)
        : UserHeapPointer(other.m_value)
    { }

    template<typename U, typename = std::enable_if_t<std::is_same_v<T, void> || std::is_same_v<T, uint8_t> || std::is_same_v<U, void> || std::is_same_v<U, uint8_t> || std::is_same_v<T, U>>>
    UserHeapPointer& operator=(UserHeapPointer<U> other)
    {
        m_value = other.m_value;
        return *this;
    }

    template<typename U = T>
    HeapPtr<U> WARN_UNUSED AsNoAssert() const
    {
        static_assert(TypeMayLiveInUserHeap<U>);
        return reinterpret_cast<HeapPtr<U>>(m_value);
    }

    template<typename U = T>
    HeapPtr<U> WARN_UNUSED As() const
    {
        static_assert(std::is_same_v<T, void> || std::is_same_v<T, uint8_t> ||
                      std::is_same_v<U, void> || std::is_same_v<U, uint8_t> ||
                      std::is_same_v<T, U>, "should either keep the type, or reinterpret from/to void*/uint8_t*");
        if constexpr(IsHeapObjectType<U>)
        {
            static_assert(std::is_base_of_v<HeapEntityCommonHeader, U>);
            static_assert(offsetof_base_v<HeapEntityCommonHeader, U> == 0);
            // UserHeapPointer should only come from boxed values, so they should never be nullptr
            //
            assert(AsNoAssert<HeapEntityCommonHeader>()->m_type == TypeEnumForHeapObject<U>);
        }
        return AsNoAssert<U>();
    }

    bool WARN_UNUSED operator==(const UserHeapPointer& rhs) const
    {
        return m_value == rhs.m_value;
    }

    static constexpr int64_t x_minValue = static_cast<int64_t>(0xFFFFFFFE00000000ULL);
    static constexpr int64_t x_maxValue = static_cast<int64_t>(0xFFFFFFFEFFFFFFFFULL);

    intptr_t m_value;
};

// A conservative estimation of valid heap address value
// We assume all address in [0, x_minimum_valid_heap_address) must be invalid pointer
//
constexpr uint32_t x_minimum_valid_heap_address = 64;

template<typename T = void>
struct SystemHeapPointer
{
    SystemHeapPointer() : m_value(0) { }
    SystemHeapPointer(uint32_t value)
        : m_value(value)
    {
        static_assert(TypeMayLiveInSystemHeap<T>);
        if constexpr(!std::is_same_v<T, void>)
        {
            assert(m_value % std::alignment_of_v<T> == 0);
        }
    }

    template<typename U, typename = std::enable_if_t<std::is_same_v<T, void> || std::is_same_v<T, uint8_t> || std::is_same_v<U, void> || std::is_same_v<U, uint8_t> || std::is_same_v<T, U>>>
    SystemHeapPointer(HeapPtr<U> value)
        : SystemHeapPointer(SafeIntegerCast<uint32_t>(reinterpret_cast<intptr_t>(value)))
    {
        assert(As<U>() == value);
    }

    template<typename U, typename = std::enable_if_t<std::is_same_v<T, void> || std::is_same_v<T, uint8_t> || std::is_same_v<U, void> || std::is_same_v<U, uint8_t> || std::is_same_v<T, U>>>
    SystemHeapPointer(SystemHeapPointer<U>& other)
        : SystemHeapPointer(other.m_value)
    { }

    template<typename U, typename = std::enable_if_t<std::is_same_v<T, void> || std::is_same_v<T, uint8_t> || std::is_same_v<U, void> || std::is_same_v<U, uint8_t> || std::is_same_v<T, U>>>
    SystemHeapPointer(SystemHeapPointer<U>&& other)
        : SystemHeapPointer(other.m_value)
    { }

    template<typename U, typename = std::enable_if_t<std::is_same_v<T, void> || std::is_same_v<T, uint8_t> || std::is_same_v<U, void> || std::is_same_v<U, uint8_t> || std::is_same_v<T, U>>>
    SystemHeapPointer(HeapRef<SystemHeapPointer<U>> other)
        : SystemHeapPointer(other.m_value)
    { }

    template<typename U, typename = std::enable_if_t<std::is_same_v<T, void> || std::is_same_v<T, uint8_t> || std::is_same_v<U, void> || std::is_same_v<U, uint8_t> || std::is_same_v<T, U>>>
    SystemHeapPointer& operator=(SystemHeapPointer<U> other)
    {
        m_value = other.m_value;
        return *this;
    }

    template<typename U = T>
    HeapPtr<U> WARN_UNUSED AsNoAssert() const
    {
        static_assert(TypeMayLiveInSystemHeap<U>);
        return reinterpret_cast<HeapPtr<U>>(static_cast<uint64_t>(m_value));
    }

    template<typename U = T>
    HeapPtr<U> WARN_UNUSED As() const
    {
        static_assert(std::is_same_v<T, void> || std::is_same_v<T, uint8_t> ||
                      std::is_same_v<U, void> || std::is_same_v<U, uint8_t> ||
                      std::is_same_v<T, U>, "should either keep the type, or reinterpret from/to void*/uint8_t*");
        if constexpr(IsHeapObjectType<U>)
        {
            static_assert(std::is_base_of_v<HeapEntityCommonHeader, U>);
            static_assert(offsetof_base_v<HeapEntityCommonHeader, U> == 0);
            AssertImp(m_value >= x_minimum_valid_heap_address, AsNoAssert<HeapEntityCommonHeader>()->m_type == TypeEnumForHeapObject<U>);
        }
        return AsNoAssert<U>();
    }

    bool WARN_UNUSED operator==(const SystemHeapPointer& rhs) const
    {
        return m_value == rhs.m_value;
    }

    uint32_t m_value;
};

template<typename T = void>
struct GeneralHeapPointer
{
    GeneralHeapPointer() : m_value(0) { }
    GeneralHeapPointer(int32_t value)
        : m_value(value)
    {
        if constexpr(!std::is_same_v<T, void>)
        {
            static_assert(std::alignment_of_v<T> % (1 << x_shiftFromRawOffset) == 0);
            assert(value % static_cast<int64_t>(std::alignment_of_v<T> / (1 << x_shiftFromRawOffset)) == 0);
        }
        AssertImp(m_value < 0, x_negMinValue <= m_value && m_value <= x_negMaxValue);
        AssertImp(m_value >= 0, m_value <= x_posMaxValue);
    }

    template<typename U, typename = std::enable_if_t<std::is_same_v<T, void> || std::is_same_v<T, uint8_t> || std::is_same_v<U, void> || std::is_same_v<U, uint8_t> || std::is_same_v<T, U>>>
    GeneralHeapPointer(HeapPtr<U> value)
        : GeneralHeapPointer(SafeIntegerCast<int32_t>(ArithmeticShiftRight(reinterpret_cast<intptr_t>(value), x_shiftFromRawOffset)))
    {
        assert(As<U>() == value);
    }

    template<typename U, typename = std::enable_if_t<std::is_same_v<T, void> || std::is_same_v<T, uint8_t> || std::is_same_v<U, void> || std::is_same_v<U, uint8_t> || std::is_same_v<T, U>>>
    GeneralHeapPointer(GeneralHeapPointer<U>& other)
        : GeneralHeapPointer(other.m_value)
    { }

    template<typename U, typename = std::enable_if_t<std::is_same_v<T, void> || std::is_same_v<T, uint8_t> || std::is_same_v<U, void> || std::is_same_v<U, uint8_t> || std::is_same_v<T, U>>>
    GeneralHeapPointer(GeneralHeapPointer<U>&& other)
        : GeneralHeapPointer(other.m_value)
    { }

    template<typename U, typename = std::enable_if_t<std::is_same_v<T, void> || std::is_same_v<T, uint8_t> || std::is_same_v<U, void> || std::is_same_v<U, uint8_t> || std::is_same_v<T, U>>>
    GeneralHeapPointer(HeapRef<GeneralHeapPointer<U>> other)
        : GeneralHeapPointer(other.m_value)
    { }

    template<typename U, typename = std::enable_if_t<std::is_same_v<T, void> || std::is_same_v<T, uint8_t> || std::is_same_v<U, void> || std::is_same_v<U, uint8_t> || std::is_same_v<T, U>>>
    GeneralHeapPointer& operator=(GeneralHeapPointer<U> other)
    {
        m_value = other.m_value;
        return *this;
    }

    template<typename U = T>
    HeapPtr<U> WARN_UNUSED AsNoAssert() const
    {
        static_assert(TypeMayLiveInSystemHeap<U> || TypeMayLiveInUserHeap<U>);
        return reinterpret_cast<HeapPtr<U>>(ArithmeticShiftLeft(static_cast<int64_t>(m_value), x_shiftFromRawOffset));
    }

    template<typename U = T>
    HeapPtr<U> WARN_UNUSED As() const
    {
        static_assert(std::is_same_v<T, void> || std::is_same_v<T, uint8_t> ||
                      std::is_same_v<U, void> || std::is_same_v<U, uint8_t> ||
                      std::is_same_v<T, U>, "should either keep the type, or reinterpret from/to void*/uint8_t*");
        if constexpr(IsHeapObjectType<U>)
        {
            static_assert(std::is_base_of_v<HeapEntityCommonHeader, U>);
            static_assert(offsetof_base_v<HeapEntityCommonHeader, U> == 0);
            AssertImp(m_value >= static_cast<int32_t>(x_minimum_valid_heap_address >> x_shiftFromRawOffset),
                      AsNoAssert<HeapEntityCommonHeader>()->m_type == TypeEnumForHeapObject<U>);
        }
        return AsNoAssert<U>();
    }

    bool WARN_UNUSED operator==(const GeneralHeapPointer& rhs) const
    {
        return m_value == rhs.m_value;
    }

    static constexpr uint32_t x_shiftFromRawOffset = 2;
    static constexpr int32_t x_negMinValue = static_cast<int32_t>(0x80000000);
    static constexpr int32_t x_negMaxValue = static_cast<int32_t>(0xBFFFFFFF);
    static constexpr int32_t x_posMaxValue = static_cast<int32_t>(0x3FFFFFFF);

    int32_t m_value;
};

template<typename T>
struct SpdsPtr
{
    SpdsPtr() : m_value(0) { }
    SpdsPtr(int32_t value)
        : m_value(value)
    {
        assert(m_value < 0);
    }

    SpdsPtr(SpdsPtr& other)
        : SpdsPtr(other.m_value)
    { }

    SpdsPtr(HeapRef<SpdsPtr> other)
        : SpdsPtr(other.m_value)
    { }

    SpdsPtr& operator=(const SpdsPtr& other)
    {
        m_value = other.m_value;
        return *this;
    }

    HeapPtr<T> WARN_UNUSED ALWAYS_INLINE Get() const
    {
        return reinterpret_cast<HeapPtr<T>>(static_cast<int64_t>(m_value));
    }

    int32_t m_value;
};

class HeapPtrTranslator
{
public:
    HeapPtrTranslator(uint64_t segRegBase) : m_segRegBase(segRegBase) { }

    template<typename T>
    T* WARN_UNUSED TranslateToRawPtr(HeapPtr<T> ptr) const
    {
        static_assert(!IsHeapPtrType<T*>::value);
        return reinterpret_cast<T*>(reinterpret_cast<uintptr_t>(ptr) + m_segRegBase);
    }

    template<typename T>
    UserHeapPointer<T> WARN_UNUSED TranslateToUserHeapPtr(T* ptr) const
    {
        return UserHeapPointer<T> { TranslateToHeapPtr(ptr) };
    }

    template<typename T>
    SystemHeapPointer<T> WARN_UNUSED TranslateToSystemHeapPtr(T* ptr) const
    {
        return SystemHeapPointer<T> { TranslateToHeapPtr(ptr) };
    }

    template<typename T>
    SpdsPtr<T> WARN_UNUSED TranslateToSpdsPtr(T* ptr) const
    {
        return SpdsPtr<T> { TranslateToHeapPtr(ptr) };
    }

    template<typename T>
    GeneralHeapPointer<T> WARN_UNUSED TranslateToGeneralHeapPtr(T* ptr) const
    {
        return GeneralHeapPointer<T> { TranslateToHeapPtr(ptr) };
    }

private:
    template<typename T>
    HeapPtr<T> TranslateToHeapPtr(T* ptr) const
    {
        static_assert(!IsHeapPtrType<T*>::value);
        return reinterpret_cast<HeapPtr<T>>(reinterpret_cast<uint64_t>(ptr) - m_segRegBase);
    }

    uint64_t m_segRegBase;
};

constexpr size_t x_spdsAllocationPageSize = 4096;
static_assert(is_power_of_2(x_spdsAllocationPageSize));

// If 'isTempAlloc' is true, we give the memory pages back to VM when the struct is destructed
// Singlethread use only.
// Currently chunk size is always 4KB, so allocate small objects only.
//
template<typename Host, bool isTempAlloc>
class SpdsAllocImpl : NonCopyable, NonMovable
{
public:
    SpdsAllocImpl(Host* host)
        : m_host(host)
        , m_curChunk(0)
        , m_lastChunkInTheChain(0)
    { }

    SpdsAllocImpl()
        : SpdsAllocImpl(nullptr)
    { }

    ~SpdsAllocImpl()
    {
        if constexpr(isTempAlloc)
        {
            ReturnMemory();
        }
    }

    void SetHost(Host* host)
    {
        m_host = host;
    }

    template<typename T>
    SpdsPtr<T> ALWAYS_INLINE WARN_UNUSED Alloc()
    {
        static_assert(sizeof(T) <= x_spdsAllocationPageSize - RoundUpToMultipleOf<alignof(T)>(isTempAlloc ? 4ULL : 0ULL));
        return SpdsPtr<T> { AllocMemory<alignof(T)>(static_cast<uint32_t>(sizeof(T))) };
    }

private:
    template<size_t alignment>
    int32_t ALWAYS_INLINE WARN_UNUSED AllocMemory(uint32_t length)
    {
        static_assert(is_power_of_2(alignment) && alignment <= 32);
        assert(m_curChunk <= 0 && length > 0 && length % alignment == 0);

        m_curChunk &= ~static_cast<int>(alignment - 1);
        assert(m_curChunk % alignment == 0);
        if (likely((static_cast<uint32_t>(m_curChunk) & (x_spdsAllocationPageSize - 1)) >= length))
        {
            m_curChunk -= length;
            return m_curChunk;
        }
        else
        {
            int32_t oldChunk = (m_curChunk & (~static_cast<int>(x_spdsAllocationPageSize - 1))) + static_cast<int>(x_spdsAllocationPageSize);
            m_curChunk = m_host->SpdsAllocatePage();
            assert(m_curChunk <= 0);
            if (oldChunk > 0)
            {
                m_lastChunkInTheChain = m_curChunk;
            }
            assert(m_curChunk % x_spdsAllocationPageSize == 0);
            if constexpr(isTempAlloc)
            {
                m_curChunk -= 4;
                *reinterpret_cast<int32_t*>(reinterpret_cast<intptr_t>(m_host) + m_curChunk) = oldChunk;
                m_curChunk &= ~static_cast<int>(alignment - 1);
            }
            m_curChunk -= length;
            return m_curChunk;
        }
    }

    void ReturnMemory()
    {
        int32_t chunk = (m_curChunk & (~static_cast<int>(x_spdsAllocationPageSize - 1))) + static_cast<int>(x_spdsAllocationPageSize);
        if (chunk != static_cast<int>(x_spdsAllocationPageSize))
        {
            m_host->SpdsPutAllocatedPagesToFreeList(chunk, m_lastChunkInTheChain);
        }
    }

    Host* m_host;
    int32_t m_curChunk;
    int32_t m_lastChunkInTheChain;
};

struct MiscImmediateValue
{
    // All misc immediate values must be between [0, 127]
    // 0 is more efficient to use than the others in that a XOR instruction is not needed to create the value
    //
    static constexpr uint64_t x_nil = 0;
    static constexpr uint64_t x_false = 2;
    static constexpr uint64_t x_true = 3;

    MiscImmediateValue() : m_value(0) { }
    MiscImmediateValue(uint64_t value)
        : m_value(value)
    {
        assert(m_value == x_nil || m_value == x_true || m_value == x_false);
    }

    bool ALWAYS_INLINE IsNil() const
    {
        return m_value == 0;
    }

    bool ALWAYS_INLINE IsBoolean() const
    {
        return m_value != 0;
    }

    bool ALWAYS_INLINE GetBooleanValue() const
    {
        assert(IsBoolean());
        return m_value & 1;
    }

    MiscImmediateValue WARN_UNUSED ALWAYS_INLINE FlipBooleanValue() const
    {
        assert(IsBoolean());
        return MiscImmediateValue { m_value ^ 1 };
    }

    static MiscImmediateValue WARN_UNUSED ALWAYS_INLINE CreateNil()
    {
        return MiscImmediateValue { x_nil };
    }

    static MiscImmediateValue WARN_UNUSED ALWAYS_INLINE CreateFalse()
    {
        return MiscImmediateValue { x_false };
    }

    static MiscImmediateValue WARN_UNUSED ALWAYS_INLINE CreateTrue()
    {
        return MiscImmediateValue { x_true };
    }

    bool WARN_UNUSED ALWAYS_INLINE operator==(const MiscImmediateValue& rhs) const
    {
        return m_value == rhs.m_value;
    }

    uint64_t m_value;
};

struct TValue
{
    // We use the following NaN boxing scheme:
    //         / 0000 **** **** ****
    // double {          ...
    //         \ FFFA **** **** ****
    // int       FFFB FFFF **** ****
    // other     FFFC FFFF 0000 00** (00 - 7F only)
    // pointer   FFFF FFFE **** ****
    //
    // The FFFE word in pointer may carry tagging info (fine as long as it's not 0xFFFF), but currently we don't need it.
    //

    TValue() : m_value(0) { }
    TValue(uint64_t value) : m_value(value) { }

    static constexpr uint64_t x_int32Tag = 0xFFFBFFFF00000000ULL;
    static constexpr uint64_t x_mivTag = 0xFFFCFFFF0000007FULL;

    // Translates to a single ANDN instruction with BMI1 support
    //
    bool ALWAYS_INLINE IsInt32(uint64_t int32Tag) const
    {
        assert(int32Tag == x_int32Tag);
        bool result = (m_value & int32Tag) == int32Tag;
        AssertIff(result, static_cast<uint32_t>(m_value >> 32) == 0xFFFBFFFFU);
        return result;
    }

    // Translates to imm8 LEA instruction + ANDN instruction with BMI1 support
    //
    bool ALWAYS_INLINE IsMIV(uint64_t mivTag) const
    {
        assert(mivTag == x_mivTag);
        bool result = (m_value & (mivTag - 0x7F)) == (mivTag - 0x7F);
        AssertIff(result, x_mivTag - 0x7F <= m_value && m_value <= x_mivTag);
        return result;
    }

    bool ALWAYS_INLINE IsPointer(uint64_t mivTag) const
    {
        assert(mivTag == x_mivTag);
        bool result = (m_value > mivTag);
        AssertIff(result, static_cast<uint32_t>(m_value >> 32) == 0xFFFFFFFEU);
        return result;
    }

    bool ALWAYS_INLINE IsDouble(uint64_t int32Tag) const
    {
        assert(int32Tag == x_int32Tag);
        bool result = m_value < int32Tag;
        AssertIff(result, m_value <= 0xFFFAFFFFFFFFFFFFULL);
        return result;
    }

    double ALWAYS_INLINE AsDouble() const
    {
        assert(IsDouble(x_int32Tag) && !IsMIV(x_mivTag) && !IsPointer(x_mivTag) && !IsInt32(x_int32Tag));
        return cxx2a_bit_cast<double>(m_value);
    }

    int32_t ALWAYS_INLINE AsInt32() const
    {
        assert(IsInt32(x_int32Tag) && !IsMIV(x_mivTag) && !IsPointer(x_mivTag) && !IsDouble(x_int32Tag));
        return BitwiseTruncateTo<int32_t>(m_value);
    }

    template<typename T = void>
    UserHeapPointer<T> ALWAYS_INLINE AsPointer() const
    {
        assert(IsPointer(x_mivTag) && !IsMIV(x_mivTag) && !IsDouble(x_int32Tag) && !IsInt32(x_int32Tag));
        return UserHeapPointer<T> { static_cast<int64_t>(m_value) };
    }

    MiscImmediateValue ALWAYS_INLINE AsMIV(uint64_t mivTag) const
    {
        assert(mivTag == x_mivTag && IsMIV(x_mivTag) && !IsDouble(x_int32Tag) && !IsInt32(x_int32Tag) && !IsPointer(x_mivTag));
        return MiscImmediateValue { m_value ^ mivTag };
    }

    static TValue WARN_UNUSED ALWAYS_INLINE CreateInt32(int32_t value, uint64_t int32Tag)
    {
        assert(int32Tag == x_int32Tag);
        TValue result { int32Tag | ZeroExtendTo<uint64_t>(value) };
        assert(result.AsInt32() == value);
        return result;
    }

    static TValue WARN_UNUSED ALWAYS_INLINE CreateDouble(double value)
    {
        TValue result { cxx2a_bit_cast<uint64_t>(value) };
        SUPRESS_FLOAT_EQUAL_WARNING(
            AssertImp(!std::isnan(value), result.AsDouble() == value);
            AssertIff(std::isnan(value), std::isnan(result.AsDouble()));
        )
        return result;
    }

    template<typename T>
    static TValue WARN_UNUSED ALWAYS_INLINE CreatePointer(UserHeapPointer<T> ptr)
    {
        TValue result { static_cast<uint64_t>(ptr.m_value) };
        assert(result.AsPointer<T>() == ptr);
        return result;
    }

    static TValue WARN_UNUSED ALWAYS_INLINE CreateMIV(MiscImmediateValue miv, uint64_t mivTag)
    {
        assert(mivTag == x_mivTag);
        TValue result { miv.m_value ^ mivTag };
        assert(result.AsMIV(mivTag).m_value == miv.m_value);
        return result;
    }

    uint64_t m_value;
};

// [ 4GB user heap ] [ 2GB padding ] [ 2GB short-pointer data structures ] [ up to 4GB system heap ]
//                                                                         ^
//    userheap                                   SPDS region      4GB aligned baseptr     systemheap
//
template<typename CRTP>
class VMMemoryManager
{
public:
    static constexpr size_t x_pageSize = 4096;
    static constexpr size_t x_vmLayoutLength = 12ULL << 30;
    static constexpr size_t x_vmLayoutAlignment = 4ULL << 30;
    static constexpr size_t x_vmBaseOffset = 8ULL << 30;
    static constexpr size_t x_vmUserHeapSize = 4ULL << 30;

    template<typename... Args>
    static CRTP* WARN_UNUSED Create(Args&&... args)
    {
        void* ptrVoid = mmap(nullptr, x_vmLayoutLength + x_vmLayoutAlignment, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
        CHECK_LOG_ERROR_WITH_ERRNO(ptrVoid != MAP_FAILED, "Failed to reserve VM address range");

        // cut out the 4GB-aligned 12GB space, and unmap the remaining
        //
        {
            uintptr_t ptr = reinterpret_cast<uintptr_t>(ptrVoid);
            uintptr_t alignedPtr = RoundUpToMultipleOf<x_vmLayoutAlignment>(ptr);
            assert(alignedPtr >= ptr && alignedPtr % x_vmLayoutAlignment == 0 && alignedPtr - ptr < x_vmLayoutAlignment);

            // If any unmap failed, log a warning, but continue execution.
            //
            if (alignedPtr > ptr)
            {
                int r = munmap(reinterpret_cast<void*>(ptr), alignedPtr - ptr);
                LOG_WARNING_WITH_ERRNO_IF(r != 0, "Failed to unmap unnecessary VM address range");
            }

            {
                uintptr_t addr = alignedPtr + x_vmLayoutLength;
                int r = munmap(reinterpret_cast<void*>(addr), x_vmLayoutAlignment - (alignedPtr - ptr));
                LOG_WARNING_WITH_ERRNO_IF(r != 0, "Failed to unmap unnecessary VM address range");
            }

            ptrVoid = reinterpret_cast<void*>(alignedPtr);
        }

        bool success = false;
        void* unmapPtrOnFailure = ptrVoid;
        size_t unmapLengthOnFailure = x_vmLayoutLength;

        Auto(
            if (!success)
            {
                int r = munmap(unmapPtrOnFailure, unmapLengthOnFailure);
                LOG_WARNING_WITH_ERRNO_IF(r != 0, "Cannot unmap VM on failure cleanup");
            }
        );

        // Map memory and initialize the VM struct
        //
        void* vmVoid = reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(ptrVoid) + x_vmBaseOffset);
        constexpr size_t sizeToMap = RoundUpToMultipleOf<x_pageSize>(sizeof(CRTP));
        {
            void* r = mmap(vmVoid, sizeToMap, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_POPULATE | MAP_FIXED, -1, 0);
            CHECK_LOG_ERROR_WITH_ERRNO(r != MAP_FAILED, "Failed to allocate VM struct");
            TestAssert(vmVoid == r);
        }

        CRTP* vm = new (vmVoid) CRTP();
        assert(vm == vmVoid);
        Auto(
            if (!success)
            {
                vm->~CRTP();
            }
        );

        CHECK_LOG_ERROR(vm->Initialize(std::forward<Args>(args)...));
        Auto(
            if (!success)
            {
                vm->Cleanup();
            }
        );

        success = true;
        return vm;
    }

    void Destroy()
    {
        CRTP* ptr = static_cast<CRTP*>(this);
        ptr->Cleanup();
        ptr->~CRTP();

        void* unmapAddr = reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(ptr) - x_vmBaseOffset);
        int r = munmap(unmapAddr, x_vmLayoutLength);
        LOG_WARNING_WITH_ERRNO_IF(r != 0, "Cannot unmap VM");
    }

    static CRTP* GetActiveVMForCurrentThread()
    {
        return reinterpret_cast<CRTP*>(reinterpret_cast<HeapPtr<CRTP>>(0)->m_self);
    }

    HeapPtrTranslator GetHeapPtrTranslator() const
    {
        return HeapPtrTranslator { VMBaseAddress() };
    }

    void SetUpSegmentationRegister()
    {
        X64_SetSegmentationRegister<X64SegmentationRegisterKind::GS>(VMBaseAddress());
    }

    SpdsAllocImpl<CRTP, true /*isTempAlloc*/> WARN_UNUSED CreateSpdsArenaAlloc()
    {
        return SpdsAllocImpl<CRTP, true /*isTempAlloc*/>(static_cast<CRTP*>(this));
    }

    SpdsAllocImpl<CRTP, false /*isTempAlloc*/>& WARN_UNUSED GetCompilerThreadSpdsAlloc()
    {
        return m_compilerThreadSpdsAlloc;
    }

    SpdsAllocImpl<CRTP, false /*isTempAlloc*/>& WARN_UNUSED GetExecutionThreadSpdsAlloc()
    {
        return m_executionThreadSpdsAlloc;
    }

    // Grab one 4K page from the SPDS region.
    // The page can be given back via SpdsPutAllocatedPagesToFreeList()
    // NOTE: if the page is [begin, end), this returns 'end', not 'begin'! SpdsReturnMemoryFreeList also expects 'end', not 'begin'
    //
    int32_t WARN_UNUSED ALWAYS_INLINE SpdsAllocatePage()
    {
        {
            int32_t out;
            if (SpdsAllocateTryGetFreeListPage(&out))
            {
                return out;
            }
        }
        return SpdsAllocatePageSlowPath();
    }

    void SpdsPutAllocatedPagesToFreeList(int32_t firstPage, int32_t lastPage)
    {
        std::atomic<int32_t>* addr = reinterpret_cast<std::atomic<int32_t>*>(VMBaseAddress() + SignExtendTo<uint64_t>(lastPage) - 4);
        uint64_t taggedValue = m_spdsPageFreeList.load(std::memory_order_relaxed);
        while (true)
        {
            uint32_t tag = BitwiseTruncateTo<uint32_t>(taggedValue >> 32);
            int32_t head = BitwiseTruncateTo<int32_t>(taggedValue);
            addr->store(head, std::memory_order_relaxed);

            tag++;
            uint64_t newTaggedValue = (static_cast<uint64_t>(tag) << 32) | ZeroExtendTo<uint64_t>(firstPage);
            if (m_spdsPageFreeList.compare_exchange_weak(taggedValue /*expected, inout*/, newTaggedValue /*desired*/, std::memory_order_release, std::memory_order_relaxed))
            {
                break;
            }
        }
    }

    // Allocate a chunk of memory from the user heap
    // Only execution thread may do this
    //
    UserHeapPointer<void> WARN_UNUSED AllocFromUserHeap(uint32_t length)
    {
        assert(length > 0 && length % 8 == 0);
        // TODO: we currently do not have GC, so it's only a bump allocator..
        //
        m_userHeapCurPtr -= static_cast<int64_t>(length);
        if (unlikely(m_userHeapCurPtr < m_userHeapPtrLimit))
        {
            BumpUserHeap();
        }
        return UserHeapPointer<void> { m_userHeapCurPtr };
    }

    // Allocate a chunk of memory from the system heap
    // Only execution thread may do this
    //
    SystemHeapPointer<void> WARN_UNUSED AllocFromSystemHeap(uint32_t length)
    {
        assert(length > 0 && length % 8 == 0);
        // TODO: we currently do not have GC, so it's only a bump allocator..
        //
        uint32_t result = m_systemHeapCurPtr;
        VM_FAIL_IF(AddWithOverflowCheck(m_systemHeapCurPtr, length, &m_systemHeapCurPtr),
            "Resource limit exceeded: system heap overflowed 4GB memory limit.");

        if (unlikely(m_systemHeapCurPtr > m_systemHeapPtrLimit))
        {
            BumpSystemHeap();
        }
        return SystemHeapPointer<void> { result };
    }

    bool WARN_UNUSED Initialize()
    {
        static_assert(std::is_base_of_v<VMMemoryManager, CRTP>, "wrong use of CRTP pattern");
        // These restrictions might not be necessary, but just to make things safe and simple
        //
        static_assert(!std::is_polymorphic_v<CRTP>, "must be not polymorphic");
        static_assert(offsetof_base_v<VMMemoryManager, CRTP> == 0, "VM must inherit VMMemoryManager as the first inherited class");

        m_self = reinterpret_cast<uintptr_t>(static_cast<CRTP*>(this));

        m_userHeapPtrLimit = -static_cast<int64_t>(x_vmBaseOffset - x_vmUserHeapSize);
        m_userHeapCurPtr = -static_cast<int64_t>(x_vmBaseOffset - x_vmUserHeapSize);

        static_assert(sizeof(CRTP) >= x_minimum_valid_heap_address);
        m_systemHeapPtrLimit = static_cast<uint32_t>(RoundUpToMultipleOf<x_pageSize>(sizeof(CRTP)));
        m_systemHeapCurPtr = sizeof(CRTP);

        m_spdsPageFreeList.store(static_cast<uint64_t>(x_spdsAllocationPageSize));
        m_spdsPageAllocLimit = 0;

        m_executionThreadSpdsAlloc.SetHost(static_cast<CRTP*>(this));
        m_compilerThreadSpdsAlloc.SetHost(static_cast<CRTP*>(this));

        return true;
    }

    void Cleanup() { }

private:
    uintptr_t VMBaseAddress() const
    {
        uintptr_t result = reinterpret_cast<uintptr_t>(static_cast<const CRTP*>(this));
        assert(result == m_self);
        return result;
    }

    void NO_INLINE BumpUserHeap()
    {
        assert(m_userHeapCurPtr < m_userHeapPtrLimit);
        VM_FAIL_IF(m_userHeapCurPtr < -static_cast<intptr_t>(x_vmBaseOffset),
            "Resource limit exceeded: user heap overflowed 4GB memory limit.");

        constexpr size_t x_allocationSize = 65536;
        // TODO: consider allocating smaller sizes on the first few allocations
        //
        intptr_t newHeapLimit = m_userHeapCurPtr & (~static_cast<intptr_t>(x_allocationSize - 1));
        assert(newHeapLimit <= m_userHeapCurPtr && newHeapLimit % static_cast<int64_t>(x_pageSize) == 0 && newHeapLimit < m_userHeapPtrLimit);
        size_t lengthToAllocate = static_cast<size_t>(m_userHeapPtrLimit - newHeapLimit);
        assert(lengthToAllocate % x_pageSize == 0);

        uintptr_t allocAddr = VMBaseAddress() + static_cast<uint64_t>(newHeapLimit);
        void* r = mmap(reinterpret_cast<void*>(allocAddr), lengthToAllocate, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_POPULATE | MAP_FIXED, -1, 0);
        VM_FAIL_WITH_ERRNO_IF(r == MAP_FAILED,
            "Out of Memory: Allocation of length %llu failed", static_cast<unsigned long long>(lengthToAllocate));
        assert(r == reinterpret_cast<void*>(allocAddr));

        m_userHeapPtrLimit = newHeapLimit;
        assert(m_userHeapPtrLimit <= m_userHeapCurPtr);
        assert(m_userHeapPtrLimit >= -static_cast<intptr_t>(x_vmBaseOffset));
    }

    void NO_INLINE BumpSystemHeap()
    {
        assert(m_systemHeapCurPtr > m_systemHeapPtrLimit);
        constexpr uint32_t x_allocationSize = 65536;

        VM_FAIL_IF(m_systemHeapCurPtr > std::numeric_limits<uint32_t>::max() - x_allocationSize,
            "Resource limit exceeded: system heap overflowed 4GB memory limit.");

        // TODO: consider allocating smaller sizes on the first few allocations
        //
        uint32_t newHeapLimit = RoundUpToMultipleOf<x_allocationSize>(m_systemHeapCurPtr);
        assert(newHeapLimit >= m_systemHeapCurPtr && newHeapLimit % static_cast<int64_t>(x_pageSize) == 0 && newHeapLimit > m_systemHeapPtrLimit);

        size_t lengthToAllocate = static_cast<size_t>(newHeapLimit - m_systemHeapPtrLimit);
        assert(lengthToAllocate % x_pageSize == 0);

        uintptr_t allocAddr = VMBaseAddress() + static_cast<uint64_t>(m_systemHeapPtrLimit);
        void* r = mmap(reinterpret_cast<void*>(allocAddr), lengthToAllocate, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_POPULATE | MAP_FIXED, -1, 0);
        VM_FAIL_WITH_ERRNO_IF(r == MAP_FAILED,
            "Out of Memory: Allocation of length %llu failed", static_cast<unsigned long long>(lengthToAllocate));
        assert(r == reinterpret_cast<void*>(allocAddr));

        m_systemHeapPtrLimit = newHeapLimit;
        assert(m_systemHeapPtrLimit >= m_systemHeapCurPtr);
    }

    bool WARN_UNUSED SpdsAllocateTryGetFreeListPage(int32_t* out)
    {
        uint64_t taggedValue = m_spdsPageFreeList.load(std::memory_order_acquire);
        while (true)
        {
            int32_t head = BitwiseTruncateTo<int32_t>(taggedValue);
            assert(head % x_spdsAllocationPageSize == 0);
            if (head == x_spdsAllocationPageSize)
            {
                return false;
            }
            uint32_t tag = BitwiseTruncateTo<uint32_t>(taggedValue >> 32);

            std::atomic<int32_t>* addr = reinterpret_cast<std::atomic<int32_t>*>(VMBaseAddress() + SignExtendTo<uint64_t>(head) - 4);
            int32_t newHead = addr->load(std::memory_order_relaxed);
            assert(newHead % x_spdsAllocationPageSize == 0);
            tag++;
            uint64_t newTaggedValue = (static_cast<uint64_t>(tag) << 32) | ZeroExtendTo<uint64_t>(newHead);

            if (m_spdsPageFreeList.compare_exchange_weak(taggedValue /*expected, inout*/, newTaggedValue /*desired*/, std::memory_order_release, std::memory_order_acquire))
            {
                *out = head;
                return true;
            }
        }
    }

    int32_t NO_INLINE WARN_UNUSED SpdsAllocatePageSlowPath()
    {
        while (true)
        {
            if (m_spdsAllocationMutex.try_lock())
            {
                int32_t result = SpdsAllocatePageSlowPathImpl();
                m_spdsAllocationMutex.unlock();
                return result;
            }
            else
            {
                // Someone else has took the lock, we wait until they finish, and retry
                //
                {
                    std::lock_guard<std::mutex> blinkLock(m_spdsAllocationMutex);
                }
                int32_t out;
                if (SpdsAllocateTryGetFreeListPage(&out))
                {
                    return out;
                }
            }
        }
    }

    // Allocate a chunk of memory, return one of the pages, and put the rest into free list
    //
    int32_t WARN_UNUSED SpdsAllocatePageSlowPathImpl()
    {
        constexpr int32_t x_allocationSize = 65536;

        // Compute how much memory we should allocate
        // We allocate 4K, 4K, 8K, 16K, 32K first
        // After that we allocate 64K each time
        //
        assert(m_spdsPageAllocLimit % x_pageSize == 0 && m_spdsPageAllocLimit % x_spdsAllocationPageSize == 0);
        size_t lengthToAllocate = x_allocationSize;
        if (unlikely(m_spdsPageAllocLimit > -x_allocationSize))
        {
            if (m_spdsPageAllocLimit == 0)
            {
                lengthToAllocate = 4096;
            }
            else
            {
                assert(m_spdsPageAllocLimit < 0);
                lengthToAllocate = static_cast<size_t>(-m_spdsPageAllocLimit);
                assert(lengthToAllocate <= x_allocationSize);
            }
        }
        assert(lengthToAllocate > 0 && lengthToAllocate % x_pageSize == 0 && lengthToAllocate % x_spdsAllocationPageSize == 0);

        VM_FAIL_IF(SubWithOverflowCheck(m_spdsPageAllocLimit, static_cast<int32_t>(lengthToAllocate), &m_spdsPageAllocLimit),
            "Resource limit exceeded: SPDS region overflowed 2GB memory limit.");

        // Allocate memory
        //
        uintptr_t allocAddr = VMBaseAddress() + SignExtendTo<uint64_t>(m_spdsPageAllocLimit);
        assert(allocAddr % x_pageSize == 0 && allocAddr % x_spdsAllocationPageSize == 0);
        void* r = mmap(reinterpret_cast<void*>(allocAddr), static_cast<size_t>(lengthToAllocate), PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_POPULATE | MAP_FIXED, -1, 0);
        VM_FAIL_WITH_ERRNO_IF(r == MAP_FAILED,
            "Out of Memory: Allocation of length %llu failed", static_cast<unsigned long long>(lengthToAllocate));

        assert(r == reinterpret_cast<void*>(allocAddr));

        // The first page is returned to caller
        //
        int32_t result = m_spdsPageAllocLimit + x_spdsAllocationPageSize;

        // Insert the other pages, if any, into free list
        //
        size_t numPages = lengthToAllocate / x_spdsAllocationPageSize;
        if (numPages > 1)
        {
            int32_t cur = result + static_cast<int32_t>(x_spdsAllocationPageSize);
            int32_t firstPage = cur;
            for (size_t i = 1; i < numPages - 1; i++)
            {
                int32_t next = cur + static_cast<int32_t>(x_spdsAllocationPageSize);
                std::atomic<int32_t>* addr = reinterpret_cast<std::atomic<int32_t>*>(VMBaseAddress() + SignExtendTo<uint64_t>(cur) - 4);
                addr->store(next, std::memory_order_relaxed);
                cur = next;
            }

            int32_t lastPage = cur;
            assert(lastPage == m_spdsPageAllocLimit + lengthToAllocate);

            SpdsPutAllocatedPagesToFreeList(firstPage, lastPage);
        }
        return result;
    }

protected:
    VMMemoryManager() { }

    // must be first member, stores the value of static_cast<CRTP*>(this)
    //
    uintptr_t m_self;

    alignas(64) SpdsAllocImpl<CRTP, false /*isTempAlloc*/> m_executionThreadSpdsAlloc;

    // user heap region grows from high address to low address
    // highest physically mapped address of the user heap region (offsets from m_self)
    //
    int64_t m_userHeapPtrLimit;

    // lowest logically used address of the user heap region (offsets from m_self)
    //
    int64_t m_userHeapCurPtr;

    // system heap region grows from low address to high address
    // lowest physically unmapped address of the system heap region (offsets from m_self)
    //
    uint32_t m_systemHeapPtrLimit;

    // lowest logically available address of the system heap region (offsets from m_self)
    //
    uint32_t m_systemHeapCurPtr;

    alignas(64) std::mutex m_spdsAllocationMutex;

    // SPDS region grows from high address to low address
    //
    std::atomic<uint64_t> m_spdsPageFreeList;
    int32_t m_spdsPageAllocLimit;

    alignas(64) SpdsAllocImpl<CRTP, false /*isTempAlloc*/> m_compilerThreadSpdsAlloc;
};

class alignas(8) HeapString final : public HeapEntityCommonHeader
{
public:
    // This is the high 16 bits of the XXHash64 value, for quick comparison
    //
    uint16_t m_hashHigh;
    // This is the low 32 bits of the XXHash64 value, for hash table indexing and quick comparison
    //
    uint32_t m_hashLow;
    // The length of the string
    //
    uint32_t m_length;
    // The string itself
    //
    uint8_t m_string[0];

    void PopulateHeader(StringLengthAndHash slah)
    {
        HeapEntityCommonHeader::Populate(this);
        m_hashHigh = static_cast<uint16_t>(slah.m_hashValue >> 48);
        m_hashLow = BitwiseTruncateTo<uint32_t>(slah.m_hashValue);
        m_length = SafeIntegerCast<uint32_t>(slah.m_length);
    }

    // Returns the allocation length to store a string of length 'length'
    //
    static size_t ComputeAllocationLengthForString(size_t length)
    {
        constexpr size_t x_inStructAvailableLength = 4;
        static_assert(sizeof(HeapString) - offsetof(HeapString, m_string) == x_inStructAvailableLength);
        size_t tailLength = RoundUpToMultipleOf<8>(length + 8 - x_inStructAvailableLength) - 8;
        return sizeof(HeapString) + tailLength;
    }
};
static_assert(sizeof(HeapString) == 16);

// In Lua all strings are hash-consed
//
template<typename CRTP>
class GlobalStringHashConser
{
private:
    // The hash table stores GeneralHeapPointer
    // We know that they must be UserHeapPointer, so the below values should never appear as valid values
    //
    static constexpr int32_t x_nonexistentValue = 0;
    static constexpr int32_t x_deletedValue = 4;

    static bool WARN_UNUSED IsNonExistentOrDeleted(GeneralHeapPointer<HeapString> ptr)
    {
        AssertIff(ptr.m_value >= 0, ptr.m_value == x_nonexistentValue || ptr.m_value == x_deletedValue);
        return ptr.m_value >= 0;
    }

    static bool WARN_UNUSED IsNonExistent(GeneralHeapPointer<HeapString> ptr)
    {
        return ptr.m_value == x_nonexistentValue;
    }

    // max load factor is x_loadfactor_numerator / (2^x_loadfactor_denominator_shift)
    //
    static constexpr uint32_t x_loadfactor_denominator_shift = 1;
    static constexpr uint32_t x_loadfactor_numerator = 1;

    // Compare if 's' is equal to the abstract multi-piece string represented by 'iterator'
    //
    // The iterator should provide two methods:
    // (1) bool HasMore() returns true if it has not yet reached the end
    // (2) std::pair<const void*, uint32_t> GetAndAdvance() returns the current string piece and advance the iterator
    //
    template<typename Iterator>
    static bool WARN_UNUSED CompareMultiPieceStringEqual(Iterator iterator, const HeapString* s)
    {
        uint32_t length = s->m_length;
        const uint8_t* ptr = s->m_string;
        while (iterator.HasMore())
        {
            const void* curStr;
            uint32_t curLen;
            std::tie(curStr, curLen) = iterator.GetAndAdvance();

            if (curLen > length)
            {
                return false;
            }
            if (memcmp(ptr, curStr, curLen) != 0)
            {
                return false;
            }
            ptr += curLen;
            length -= curLen;
        }
        return length == 0;
    }

    template<typename Iterator>
    HeapString* WARN_UNUSED MaterializeMultiPieceString(Iterator iterator, StringLengthAndHash slah)
    {
        size_t allocationLength = HeapString::ComputeAllocationLengthForString(slah.m_length);
        VM_FAIL_IF(!IntegerCanBeRepresentedIn<uint32_t>(allocationLength),
            "Cannot create a string longer than 4GB (attempted length: %llu bytes).", static_cast<unsigned long long>(allocationLength));

        HeapPtrTranslator translator = static_cast<CRTP*>(this)->GetHeapPtrTranslator();
        UserHeapPointer<void> uhp = static_cast<CRTP*>(this)->AllocFromUserHeap(static_cast<uint32_t>(allocationLength));

        HeapString* ptr = translator.TranslateToRawPtr(uhp.AsNoAssert<HeapString>());
        ptr->PopulateHeader(slah);

        uint8_t* curDst = ptr->m_string;
        while (iterator.HasMore())
        {
            const void* curStr;
            uint32_t curLen;
            std::tie(curStr, curLen) = iterator.GetAndAdvance();

            SafeMemcpy(curDst, curStr, curLen);
            curDst += curLen;
        }

        // Assert that the provided length and hash value matches reality
        //
        assert(curDst - ptr->m_string == static_cast<intptr_t>(slah.m_length));
        assert(HashString(ptr->m_string, ptr->m_length) == slah.m_hashValue);
        return ptr;
    }

    static void ReinsertDueToResize(GeneralHeapPointer<HeapString>* hashTable, uint32_t hashTableSizeMask, GeneralHeapPointer<HeapString> e)
    {
        uint32_t slot = e.As<HeapString>()->m_hashLow & hashTableSizeMask;
        while (hashTable[slot].m_value != x_nonexistentValue)
        {
            slot = (slot + 1) & hashTableSizeMask;
        }
        hashTable[slot] = e;
    }

    // TODO: when we have GC thread we need to figure out how this interacts with GC
    //
    void ExpandHashTableIfNeeded()
    {
        if (likely(m_elementCount <= (m_hashTableSizeMask >> x_loadfactor_denominator_shift) * x_loadfactor_numerator))
        {
            return;
        }

        assert(m_hashTable != nullptr && is_power_of_2(m_hashTableSizeMask + 1));
        VM_FAIL_IF(m_hashTableSizeMask >= (1U << 29),
            "Global string hash table has grown beyond 2^30 slots");
        uint32_t newSize = (m_hashTableSizeMask + 1) * 2;
        uint32_t newMask = newSize - 1;
        GeneralHeapPointer<HeapString>* newHt = new (std::nothrow) GeneralHeapPointer<HeapString>[newSize];
        VM_FAIL_IF(newHt == nullptr,
            "Out of memory, failed to resize global string hash table to size %u", static_cast<unsigned>(newSize));

        static_assert(x_nonexistentValue == 0, "we are relying on this to do memset");
        memset(newHt, 0, sizeof(GeneralHeapPointer<HeapString>) * newSize);

        GeneralHeapPointer<HeapString>* cur = m_hashTable;
        GeneralHeapPointer<HeapString>* end = m_hashTable + m_hashTableSizeMask + 1;
        while (cur < end)
        {
            if (!IsNonExistentOrDeleted(cur->m_value))
            {
                ReinsertDueToResize(newHt, newMask, *cur);
            }
            cur++;
        }
        delete [] m_hashTable;
        m_hashTable = newHt;
        m_hashTableSizeMask = newMask;
    }

    // Insert an abstract multi-piece string into the hash table if it does not exist
    // Return the HeapString
    //
    template<typename Iterator>
    UserHeapPointer<HeapString> WARN_UNUSED InsertMultiPieceString(Iterator iterator)
    {
        HeapPtrTranslator translator = static_cast<CRTP*>(this)->GetHeapPtrTranslator();

        StringLengthAndHash lenAndHash = HashMultiPieceString(iterator);
        uint64_t hash = lenAndHash.m_hashValue;
        size_t length = lenAndHash.m_length;
        uint16_t expectedHashHigh = static_cast<uint16_t>(hash >> 48);
        uint32_t expectedHashLow = BitwiseTruncateTo<uint32_t>(hash);

        uint32_t slotForInsertion = static_cast<uint32_t>(-1);
        uint32_t slot = static_cast<uint32_t>(hash) & m_hashTableSizeMask;
        while (true)
        {
            {
                GeneralHeapPointer<HeapString> ptr = m_hashTable[slot];
                if (IsNonExistentOrDeleted(ptr))
                {
                    // If this string turns out to be non-existent, this can be a slot to insert the string
                    //
                    if (slotForInsertion == static_cast<uint32_t>(-1))
                    {
                        slotForInsertion = slot;
                    }
                    if (IsNonExistent(ptr))
                    {
                        break;
                    }
                    else
                    {
                        goto next_slot;
                    }
                }

                HeapPtr<HeapString> s = ptr.As<HeapString>();
                if (s->m_hashHigh != expectedHashHigh || s->m_hashLow != expectedHashLow || s->m_length != length)
                {
                    goto next_slot;
                }

                HeapString* rawPtr = translator.TranslateToRawPtr(s);
                if (!CompareMultiPieceStringEqual(iterator, rawPtr))
                {
                    goto next_slot;
                }

                // We found the string
                //
                return translator.TranslateToUserHeapPtr(rawPtr);
            }
next_slot:
            slot = (slot + 1) & m_hashTableSizeMask;
        }

        // The string is not found, insert it into the hash table
        //
        assert(slotForInsertion != static_cast<uint32_t>(-1));
        assert(IsNonExistentOrDeleted(m_hashTable[slotForInsertion]));

        m_elementCount++;
        HeapString* element = MaterializeMultiPieceString(iterator, lenAndHash);
        m_hashTable[slotForInsertion] = translator.TranslateToGeneralHeapPtr(element);

        ExpandHashTableIfNeeded();

        return translator.TranslateToUserHeapPtr(element);
    }

public:
    // Create a string by concatenating start[0] ~ start[len-1]
    // Each TValue must be a string
    //
    UserHeapPointer<HeapString> WARN_UNUSED CreateStringObjectFromConcatenation(TValue* start, size_t len)
    {
#ifndef NDEBUG
        for (size_t i = 0; i < len; i++)
        {
            assert(start[i].IsPointer(TValue::x_mivTag));
            assert(start[i].AsPointer().As<HeapEntityCommonHeader>()->m_type == Type::STRING);
        }
#endif
        struct Iterator
        {
            bool HasMore()
            {
                return m_cur < m_end;
            }

            std::pair<const uint8_t*, uint32_t> GetAndAdvance()
            {
                assert(m_cur < m_end);
                HeapString* e = m_translator.TranslateToRawPtr(m_cur->AsPointer().As<HeapString>());
                m_cur++;
                return std::make_pair(static_cast<const uint8_t*>(e->m_string), e->m_length);
            }

            TValue* m_cur;
            TValue* m_end;
            HeapPtrTranslator m_translator;
        };

        return InsertMultiPieceString(Iterator {
            .m_cur = start,
            .m_end = start + len,
            .m_translator = static_cast<CRTP*>(this)->GetHeapPtrTranslator()
        });
    }

    // Create a string by concatenating str1 .. start[0] ~ start[len-1]
    // str1 and each TValue must be a string
    //
    UserHeapPointer<HeapString> WARN_UNUSED CreateStringObjectFromConcatenation(UserHeapPointer<HeapString> str1, TValue* start, size_t len)
    {
#ifndef NDEBUG
        assert(str1.As()->m_type == Type::STRING);
        for (size_t i = 0; i < len; i++)
        {
            assert(start[i].IsPointer(TValue::x_mivTag));
            assert(start[i].AsPointer().As<HeapEntityCommonHeader>()->m_type == Type::STRING);
        }
#endif

        struct Iterator
        {
            Iterator(UserHeapPointer<HeapString> str1, TValue* start, size_t len, HeapPtrTranslator translator)
                : m_isFirst(true)
                , m_firstString(str1)
                , m_cur(start)
                , m_end(start + len)
                , m_translator(translator)
            { }

            bool HasMore()
            {
                return m_isFirst || m_cur < m_end;
            }

            std::pair<const uint8_t*, uint32_t> GetAndAdvance()
            {
                HeapString* e;
                if (m_isFirst)
                {
                    m_isFirst = false;
                    e = m_translator.TranslateToRawPtr(m_firstString.As<HeapString>());
                }
                else
                {
                    assert(m_cur < m_end);
                    e = m_translator.TranslateToRawPtr(m_cur->AsPointer().As<HeapString>());
                    m_cur++;
                }
                return std::make_pair(static_cast<const uint8_t*>(e->m_string), e->m_length);
            }

            bool m_isFirst;
            UserHeapPointer<HeapString> m_firstString;
            TValue* m_cur;
            TValue* m_end;
            HeapPtrTranslator m_translator;
        };

        return InsertMultiPieceString(Iterator(str1, start, len, static_cast<CRTP*>(this)->GetHeapPtrTranslator()));
    }

    UserHeapPointer<HeapString> WARN_UNUSED CreateStringObjectFromRawString(const void* str, uint32_t len)
    {
        struct Iterator
        {
            Iterator(const void* str, uint32_t len)
                : m_str(str)
                , m_len(len)
                , m_isFirst(true)
            { }

            bool HasMore()
            {
                return m_isFirst;
            }

            std::pair<const void*, uint32_t> GetAndAdvance()
            {
                assert(m_isFirst);
                m_isFirst = false;
                return std::make_pair(m_str, m_len);
            }

            const void* m_str;
            uint32_t m_len;
            bool m_isFirst;
        };

        return InsertMultiPieceString(Iterator(str, len));
    }

    uint32_t GetGlobalStringHashConserCurrentHashTableSize() const
    {
        return m_hashTableSizeMask + 1;
    }

    uint32_t GetGlobalStringHashConserCurrentElementCount() const
    {
        return m_elementCount;
    }

    bool WARN_UNUSED Initialize()
    {
        static constexpr uint32_t x_initialSize = 1024;
        m_hashTable = new (std::nothrow) GeneralHeapPointer<HeapString>[x_initialSize];
        CHECK_LOG_ERROR(m_hashTable != nullptr, "Failed to allocate space for initial hash table");

        static_assert(x_nonexistentValue == 0, "required for memset");
        memset(m_hashTable, 0, sizeof(GeneralHeapPointer<HeapString>) * x_initialSize);

        m_hashTableSizeMask = x_initialSize - 1;
        m_elementCount = 0;
        return true;
    }

    void Cleanup()
    {
        if (m_hashTable != nullptr)
        {
            delete [] m_hashTable;
        }
    }

private:
    uint32_t m_hashTableSizeMask;
    uint32_t m_elementCount;
    // use GeneralHeapPointer because it's 4 bytes
    // All pointers are actually always HeapPtr<HeapString>
    //
    GeneralHeapPointer<HeapString>* m_hashTable;
};

class VM;

// We want to solve the following problem. Given a tree of size n and max depth D with a value on each node, we want to support:
// (1) Insert a new leaf.
// (2) Given a node, query if a value appeared on the path from node to root. If yes, at which depth.
//
// Trivially this problem can be solved via persistent data structure (a persistent trie or treap)
// with O(log D) insert, O(log D) query and O(n log D) space, but the constant hidden in big O is too large
// to be useful in our use case, and also we want faster query.
//
// We instead use a O(sqrt D) insert (amortized), O(1) query and O(n sqrt D) space algorithm.
//
// Let S = sqrt D, for each node x in the tree satisfying both of the following:
// (1) The depth of x is a multiple of S.
// (2) The subtree rooted by x has a maximum depth of >= S (thanks Yinzhan!).
// For each such x, we build a hash table containing all elements from root to x. We call such x 'anchors'.
//
// Lemma 1. There are at most n/S anchors in the tree.
// Proof. For each anchor x, there exists a chain of length S starting at x and going downwards.
//        Let chain(x) denote one such chain for x. All chain(x) must be disjoint from each other. QED
//
// So the total size of the anchor hash tables is bounded by O(D * n / S) = O(n sqrt D).
//
// For each node, we also build a hash table containing nodes from itself to the nearest anchor upwards.
// Then each query can be answered in O(1) by checking its own hash table and the anchor's hash table.
//
// Each node's individual hash table clearly has size O(S), so the total size is O(nS) = O(n sqrt D).
//
// By the way, this scheme can be extended to yield O(D^(1/c)) insert, O(c) query, O(n * D^(1/c)) space for any c,
// but likely the constant is too large for them to be useful, so we just use the sqrt n version.
//
// Below is the constant of S (in our use case, we are targeting a tree of maximum depth 254).
//
constexpr uint32_t x_log2_hiddenClassBlockSize = 4;
constexpr uint32_t x_hiddenClassBlockSize = (1 << x_log2_hiddenClassBlockSize);

enum class StructuredHiddenClassTransitionKind : uint8_t
{
    BadTransitionKind,
    AddProperty,
    AddPropertyAndGrowPropertyStorageCapacity,
    AddMetaTable,
    TransitToPolyMetaTable,
    RemoveMetaTable,
    GrowArrayStorageCapacity,
    TransitArrayToSparseMap
};

struct StructuredHiddenClassWritePropertyResult
{
    // Denote if the transition resulted in a different HiddenClass pointer
    // If true, the caller should update the object's HiddenClass pointer
    //
    bool m_transitionedToNewHiddenClass;

    // If true, the hidden class just transitioned into dictionary mode
    //
    bool m_transitionedToDictionaryMode;

    // If true, the caller should grow the object's butterfly property part
    //
    bool m_shouldGrowButterfly;

    // The slot ordinal to write into
    //
    uint32_t m_slotOrdinal;

    // Filled if m_transitionedToNewHiddenClass is true
    // This is a StructuredHiddenClass if m_transitionedToDictionaryMode is false,
    // or a DictionaryHiddenClass if m_transitionedToDictionaryMode is true
    //
    SystemHeapPointer<void> m_newHiddenClass;
};

struct StructuredHiddenClassAddOrRemoveMetatableResult
{
    // Denote if the transition resulted in a different HiddenClass pointer
    // If true, the caller should update the object's HiddenClass pointer
    //
    bool m_transitionedToNewHiddenClass;

    // If true, the HiddenClass is in PolyMetatable mode, m_slotOrdinal is filled to contain the slot ordinal for the metatable,
    // and the user should fill the metatable (or 'nil' for remove metatable) into that slot
    //
    bool m_shouldInsertMetatable;

    // If true, the caller should grow the object's butterfly property part
    //
    bool m_shouldGrowButterfly;

    // If 'm_shouldInsertMetatable' is true, the slot ordinal to write into
    //
    uint32_t m_slotOrdinal;

    // Filled if m_transitionedToNewHiddenClass is true
    //
    SystemHeapPointer<StructuredHiddenClass> m_newHiddenClass;
};

// Only used for array write when the array part is not in sparse map mode
//
struct StructuredHiddenClassWriteArrayResult
{
    // Denote if the transition resulted in a different HiddenClass pointer
    // If true, the caller should update the object's HiddenClass pointer
    //
    bool m_transitionedToNewHiddenClass;

    // If true, the caller should convert the array part to a sparse map
    //
    bool m_shouldConvertToSparseMapIndexing;

    // If true, the caller should grow the object's butterfly array part
    // Only relavent if m_shouldConvertToSparseMapIndexing is false
    //
    bool m_shouldGrowButterfly;

    // Filled if m_transitionedToNewHiddenClass is true
    //
    SystemHeapPointer<StructuredHiddenClass> m_newHiddenClass;
};

class alignas(8) HiddenClassOutlinedTransitionTable
{
public:
    struct HashTableEntry
    {
        int32_t m_key;
        SystemHeapPointer<void> m_value;
    };

    // Special keys for operations other than AddProperty
    //
    static constexpr int32_t x_key_invalid = 0;
    // First time we add a metatable, this key points to the Structure with new metatable
    // Next time we add a metatable, if we found the metatable is different from the existing one,
    // we create a new Structure with PolyMetatable, and replace the value corresponding to
    // x_key_add_or_to_poly_metatable to the PolyMetatable structure
    //
    static constexpr int32_t x_key_add_or_to_poly_metatable = 2;
    static constexpr int32_t x_key_remove_metatable = 3;
    static constexpr int32_t x_key_grow_array_capacity = 4;

    static constexpr uint32_t x_initialHashTableSize = 4;

    bool ShouldResizeForThisInsertion()
    {
        return m_numElementsInHashTable >= m_hashTableMask / 2 + 2;
    }


    uint32_t m_hashTableMask;
    uint32_t m_numElementsInHashTable;
    HashTableEntry m_hashTable[0];
};

class StructuredHiddenClass;

struct HiddenClassKeyHashHelper
{
    static uint32_t GetHashValueForStringKey(UserHeapPointer<HeapString> stringKey)
    {
        HeapPtr<HeapString> s = stringKey.As<HeapString>();
        assert(s->m_type == Type::STRING);
        return s->m_hashLow;
    }

    static uint32_t GetHashValueForMaybeNonStringKey(UserHeapPointer<void> key)
    {
        HeapPtr<HeapEntityCommonHeader> hdr = key.As<HeapEntityCommonHeader>();
        if (hdr->m_type == Type::STRING)
        {
            return GetHashValueForStringKey(UserHeapPointer<HeapString>(key));
        }
        else
        {
            return static_cast<uint32_t>(HashPrimitiveTypes(key.m_value));
        }
    }
};

// The structure for the anchor hash tables as described in the algorithm.
//
// [ hash table ] [ header ] [ block pointers ]
//                ^
//                pointer to object
//
class alignas(8) HiddenClassAnchorHashTable final : public HeapEntityCommonHeader
{
public:
    struct HashTableEntry
    {
        uint8_t m_ordinal;
        uint8_t m_checkHash;
    };
    static_assert(sizeof(HashTableEntry) == 2);

    static constexpr uint8_t x_hashTableEmptyValue = 0xff;

    static constexpr size_t OffsetOfTrailingVarLengthArray()
    {
        return offsetof_member(HiddenClassAnchorHashTable, m_blockPointers);
    }

    template<typename T, typename = std::enable_if_t<IsPtrOrHeapPtr<T, HiddenClassAnchorHashTable>>>
    static GeneralHeapPointer<void> GetPropertyNameAtSlot(T self, uint8_t ordinal)
    {
        assert(ordinal < self->m_numBlocks * x_hiddenClassBlockSize);
        uint8_t blockOrd = ordinal >> x_log2_hiddenClassBlockSize;
        uint8_t offset = ordinal & static_cast<uint8_t>(x_hiddenClassBlockSize - 1);
        SystemHeapPointer<GeneralHeapPointer<void>> p = self->m_blockPointers[blockOrd];
        return p.As()[offset];
    }

    static HiddenClassAnchorHashTable* WARN_UNUSED Create(VM* vm, StructuredHiddenClass* shc);

    template<typename T, typename = std::enable_if_t<IsPtrOrHeapPtr<T, HiddenClassAnchorHashTable>>>
    static ReinterpretCastPreservingAddressSpaceType<HashTableEntry*, T> GetHashTableBegin(T self)
    {
        uint32_t hashTableSize = GetHashTableSizeFromHashTableMask(self->m_hashTableMask);
        return GetHashTableEnd(self) - hashTableSize;
    }

    template<typename T, typename = std::enable_if_t<IsPtrOrHeapPtr<T, HiddenClassAnchorHashTable>>>
    static ReinterpretCastPreservingAddressSpaceType<HashTableEntry*, T> GetHashTableEnd(T self)
    {
        return ReinterpretCastPreservingAddressSpace<HashTableEntry*>(self);
    }

    void CloneHashTableTo(HashTableEntry* htStart, uint32_t htSize);

    template<typename T, typename = std::enable_if_t<IsPtrOrHeapPtr<T, HiddenClassAnchorHashTable>>>
    static bool WARN_UNUSED GetSlotOrdinalFromPropertyNameAndHash(T self, GeneralHeapPointer<void> key, uint32_t hashValue, uint32_t& result /*out*/)
    {
        int64_t hashTableMask = static_cast<int64_t>(self->m_hashTableMask);
        uint8_t checkHash = static_cast<uint8_t>(hashValue);
        int64_t hashSlot = static_cast<int64_t>(hashValue >> 8) | hashTableMask;
        auto hashTableEnd = GetHashTableEnd(self);
        DEBUG_ONLY(uint32_t hashTableSize = GetHashTableSizeFromHashTableMask(self->m_hashTableMask);)
        assert(-static_cast<int64_t>(hashTableSize) <= hashSlot && hashSlot < 0);

        while (true)
        {
            uint8_t htSlotOrdinal = hashTableEnd[hashSlot].m_ordinal;
            uint8_t htSlotCheckHash = hashTableEnd[hashSlot].m_checkHash;
            if (htSlotOrdinal == x_hashTableEmptyValue)
            {
                return false;
            }
            if (htSlotCheckHash == checkHash)
            {
                GeneralHeapPointer<void> p = GetPropertyNameAtSlot(self, htSlotOrdinal);
                if (p == key)
                {
                    result = htSlotOrdinal;
                    return true;
                }
            }

            hashSlot = (hashSlot - 1) | hashTableMask;
            assert(-static_cast<int64_t>(hashTableSize) <= hashSlot && hashSlot < 0);
        }
    }

    static uint32_t GetHashTableSizeFromHashTableMask(int32_t mask)
    {
        assert(mask < 0);
        mask = ~mask;
        assert(mask > 0 && mask < std::numeric_limits<int32_t>::max());
        mask++;
        assert(is_power_of_2(mask));
        return static_cast<uint32_t>(mask);
    }

    uint8_t m_numBlocks;
    uint8_t m_numTotalSlots;    // just m_numBlocks * x_hiddenClassBlockSize

    // This is a negative mask! use GetHashTableSizeFromHashTableMask() to recover hash table size
    //
    int32_t m_hashTableMask;

    SystemHeapPointer<GeneralHeapPointer<void>> m_blockPointers[0];
};
static_assert(sizeof(HiddenClassAnchorHashTable) == 8);

// A structured hidden class
//
// Future work: we can use one structure to store a chain of PropertyAdd transitions (and user
// use pointer tag to distinguish which node in the chain it is referring to). This should further reduce memory consumption.
//
// [ hash table ] [ header ] [ non-full block elements ] [ optional last full block pointer ]
//                ^
//                pointer to object
//
class alignas(8) StructuredHiddenClass final : public HeapEntityCommonHeader
{
public:
    static constexpr size_t OffsetOfTrailingVarLengthArray()
    {
        return offsetof_member(StructuredHiddenClass, m_values);
    }

    // The total number of in-use slots in the represented object (this is different from the capacity!)
    //
    uint8_t m_numSlots;

    // The length for the non-full block
    // Can be computed from m_numSlots but we store it for simplicity since we have a byte to spare here
    //
    uint8_t m_nonFullBlockLen;

    static uint8_t ComputeNonFullBlockLength(uint8_t numSlots)
    {
        if (numSlots == 0) { return 0; }
        uint8_t nonFullBlockLen = static_cast<uint8_t>(((static_cast<uint32_t>(numSlots) - 1) & (x_hiddenClassBlockSize - 1)) + 1);
        return nonFullBlockLen;
    }

    // The anchor hash table, 0 if not exist
    // Anchor hash table only exists if there are at least 2 * x_hiddenClassBlockSize entries,
    // so hidden class smaller than that doesn't query anchor hash table
    //
    SystemHeapPointer<HiddenClassAnchorHashTable> m_anchorHashTable;

    // Lua doesn't need to consider delete
    // If we need to support delete, we need to use a special record to denote an entry is deleted
    // by the inline hash table, we probably will also need a free list to recycle the deleted slots
    //

    // The array capacity in the butterfly object
    //
    uint32_t m_arrayStorageCapacity;

    // The hash mask of the inline hash table
    // The inline hash table contains entries for the last full block and all entries in the non-full block
    //
    uint8_t m_inlineHashTableMask;

    // The object's inline named property storage capacity
    // slot [0, m_inlineNamedStorageCapacity) goes in the inline storage
    //
    uint8_t m_inlineNamedStorageCapacity;

    // The object's butterfly named property storage capacity
    // slot >= m_inlineNamedStorageCapacity goes here
    //
    uint8_t m_butterflyNamedStorageCapacity;

    // The kind of the transition that transitioned from the parent to this node
    //
    StructuredHiddenClassTransitionKind m_parentEdgeTransitionKind;

    // The parent of this node
    //
    SystemHeapPointer<StructuredHiddenClass> m_parent;

    static constexpr int32_t x_noMetaTable = 0;
    static constexpr int32_t x_polyMetaTable = 2;

    // The metatable of the object (it is always a pointer to userheap)
    // x_noMetaTable if not exist, x_polyMetaTable if there are two objects with otherwise the same
    // structure but different metatables (i.e. must inspect the object to retrieve the metatable)
    //
    GeneralHeapPointer<void> m_metatable;

    template<typename T, typename = std::enable_if_t<IsPtrOrHeapPtr<T, StructuredHiddenClass>>>
    static bool IsTransitionTableEmpty(T self)
    {
        return self->m_transitionTable == 0;
    }

    template<typename T, typename = std::enable_if_t<IsPtrOrHeapPtr<T, StructuredHiddenClass>>>
    static bool IsSingletonTransitionTarget(T self)
    {
        return self->m_transitionTable & 1U;
    }

    template<typename T, typename = std::enable_if_t<IsPtrOrHeapPtr<T, StructuredHiddenClass>>>
    static SystemHeapPointer<StructuredHiddenClass> GetSingletonTransitionTarget(T self)
    {
        assert(IsSingletonTransitionTarget(self));
        return SystemHeapPointer<StructuredHiddenClass> { self->m_transitionTable ^ 1U };
    }

    template<typename T, typename = std::enable_if_t<IsPtrOrHeapPtr<T, StructuredHiddenClass>>>
    static HeapPtr<HiddenClassOutlinedTransitionTable> GetTransitionTable(T self)
    {
        assert(!IsTransitionTableEmpty(self) && !IsSingletonTransitionTarget(self));
        SystemHeapPointer<HiddenClassOutlinedTransitionTable> p { self->m_transitionTable };
        return p.As();
    }

    // This is a tagged SystemHeapPointer
    // When the tag is 1, it is a StructuredHiddenClass/DictionaryHiddenClass pointer, denoting that currently existing
    //     transition is uniquely to that pointer. The type of the pointer can be distinguished by checking m_numSlots == 254.
    // When the tag is 0, it is a HiddenClassTransitionTable pointer
    //
    uint32_t m_transitionTable;

    static constexpr int8_t x_inlineHashTableEmptyValue = 0x7f;

    struct InlineHashTableEntry
    {
        uint8_t m_checkHash;
        int8_t m_ordinal;
    };
    static_assert(sizeof(InlineHashTableEntry) == 2);

    template<typename T, typename = std::enable_if_t<IsPtrOrHeapPtr<T, StructuredHiddenClass>>>
    static ReinterpretCastPreservingAddressSpaceType<InlineHashTableEntry*, T> GetInlineHashTableBegin(T self)
    {
        uint32_t hashTableSize = ComputeHashTableSizeFromHashTableMask(self->m_inlineHashTableMask);
        return GetInlineHashTableEnd(self) - hashTableSize;
    }

    template<typename T, typename = std::enable_if_t<IsPtrOrHeapPtr<T, StructuredHiddenClass>>>
    static ReinterpretCastPreservingAddressSpaceType<InlineHashTableEntry*, T> GetInlineHashTableEnd(T self)
    {
        return ReinterpretCastPreservingAddressSpace<InlineHashTableEntry*>(self);
    }

    static uint32_t ComputeHashTableSizeFromHashTableMask(uint8_t mask)
    {
        uint32_t v = static_cast<uint32_t>(mask) + 1;
        assert(is_power_of_2(v));
        return v;
    }

    static constexpr uint8_t x_maxNumSlots = 254;

    // Return false if the structure is transitioned to a dictionary, and no field in 'result' is filled.
    // Otherwise, return true and 'result' is filled accordingly:
    // (1) m_slotOrdinal represent the slot ordinal to write.
    // (2) m_transitionedToNewStructure represent whether the structure transitioned to a new one.
    //     If yes, m_shouldGrowButterfly and m_newStructure are filled accordingly.
    //
    template<typename T, typename = std::enable_if_t<IsPtrOrHeapPtr<T, StructuredHiddenClass>>>
    static bool WARN_UNUSED AddStringProperty(T self, UserHeapPointer<HeapString> stringKey, AddPropertyResult& result /*out*/ );

    template<typename T, typename = std::enable_if_t<IsPtrOrHeapPtr<T, StructuredHiddenClass>>>
    static bool WARN_UNUSED IsAnchorTableContainsFinalBlock(T self)
    {
        SystemHeapPointer<HiddenClassAnchorHashTable> p = self->m_anchorHashTable;
        if (p.m_value == 0)
        {
            return false;
        }
        uint8_t lim = self->m_numSlots & (~static_cast<uint8_t>(x_hiddenClassBlockSize - 1));
        assert(p.As()->m_numTotalSlots == lim || p.As()->m_numTotalSlots == lim - x_hiddenClassBlockSize);
        return p.As()->m_numTotalSlots == lim;
    }

    // Perform an AddProperty transition, fill in 'result' accordingly.
    //
    void PerformAddPropertyTransition(VM* vm, UserHeapPointer<void> key, AddPropertyResult& result /*out*/);

    template<typename T, typename = std::enable_if_t<IsPtrOrHeapPtr<T, StructuredHiddenClass>>>
    static SystemHeapPointer<HiddenClassAnchorHashTable> WARN_UNUSED BuildNewAnchorTableIfNecessary(T self);

    template<typename T, typename = std::enable_if_t<IsPtrOrHeapPtr<T, StructuredHiddenClass>>>
    static bool WARN_UNUSED GetSlotOrdinalFromStringProperty(T self, UserHeapPointer<HeapString> stringKey, uint32_t& result /*out*/)
    {
        return GetSlotOrdinalFromPropertyNameAndHash(self, stringKey, HiddenClassKeyHashHelper::GetHashValueForStringKey(stringKey), result /*out*/);
    }

    template<typename T, typename = std::enable_if_t<IsPtrOrHeapPtr<T, StructuredHiddenClass>>>
    static bool WARN_UNUSED GetSlotOrdinalFromMaybeNonStringProperty(T self, UserHeapPointer<void> key, uint32_t& result /*out*/)
    {
        return GetSlotOrdinalFromPropertyNameAndHash(self, key, HiddenClassKeyHashHelper::GetHashValueForMaybeNonStringKey(key), result /*out*/);
    }

    template<typename T, typename = std::enable_if_t<IsPtrOrHeapPtr<T, StructuredHiddenClass>>>
    static bool WARN_UNUSED GetSlotOrdinalFromPropertyNameAndHash(T self, UserHeapPointer<void> key, uint32_t hashvalue, uint32_t& result /*out*/)
    {
        GeneralHeapPointer<void> keyG = GeneralHeapPointer<void> { key.As<void>() };
        if (QueryInlineHashTable(self, keyG, static_cast<uint16_t>(hashvalue), result /*out*/))
        {
            return true;
        }
        SystemHeapPointer<HiddenClassAnchorHashTable> anchorHt = self->m_anchorHashTable;
        if (likely(anchorHt.m_value == 0))
        {
            return false;
        }
        return HiddenClassAnchorHashTable::GetSlotOrdinalFromPropertyNameAndHash(anchorHt.As(), keyG, hashvalue, result /*out*/);
    }

    template<typename T, typename = std::enable_if_t<IsPtrOrHeapPtr<T, StructuredHiddenClass>>>
    static bool WARN_UNUSED QueryInlineHashTable(T self, GeneralHeapPointer<void> key, uint16_t hashvalue, uint32_t& result /*out*/)
    {
        int64_t hashMask = ~ZeroExtendTo<int64_t>(self->m_inlineHashTableMask);
        assert(hashMask < 0);
        int64_t hashSlot = ZeroExtendTo<int64_t>(hashvalue >> 8) | hashMask;

        DEBUG_ONLY(uint32_t hashTableSize = ComputeHashTableSizeFromHashTableMask(self->m_inlineHashTableMask);)
        assert(-static_cast<int64_t>(hashTableSize) <= hashSlot && hashSlot < 0);

        uint8_t checkHash = static_cast<uint8_t>(hashvalue);
        auto hashTableEnd = GetInlineHashTableEnd(self);

        while (true)
        {
            int8_t slotOrdinal = hashTableEnd[hashSlot].m_ordinal;
            uint8_t slotCheckHash = hashTableEnd[hashSlot].m_checkHash;
            if (slotOrdinal == x_inlineHashTableEmptyValue)
            {
                return false;
            }
            if (slotCheckHash == checkHash)
            {
                GeneralHeapPointer<void> prop = GetPropertyNameFromInlineHashTableOrdinal(self, slotOrdinal);
                if (prop == key)
                {
                    result = GetPropertySlotFromInlineHashTableOrdinal(self, slotOrdinal);
                    return true;
                }
            }
            hashSlot = (hashSlot - 1) | hashMask;
            assert(-static_cast<int64_t>(hashTableSize) <= hashSlot && hashSlot < 0);
        }
    }

    static bool HasFinalFullBlockPointer(uint8_t numSlots)
    {
        return numSlots >= x_hiddenClassBlockSize && numSlots % x_hiddenClassBlockSize != 0;
    }

    template<typename T, typename = std::enable_if_t<IsPtrOrHeapPtr<T, StructuredHiddenClass>>>
    static ReinterpretCastPreservingAddressSpaceType<SystemHeapPointer<GeneralHeapPointer<void>>*, T> GetFinalFullBlockPointerAddress(T self)
    {
        assert(HasFinalFullBlockPointer(self->m_numSlots));
        return ReinterpretCastPreservingAddressSpace<SystemHeapPointer<GeneralHeapPointer<void>>*>(self->m_values + self->m_nonFullBlockLen);
    }

    template<typename T, typename = std::enable_if_t<IsPtrOrHeapPtr<T, StructuredHiddenClass>>>
    static SystemHeapPointer<GeneralHeapPointer<void>> GetFinalFullBlockPointer(T self)
    {
        return *GetFinalFullBlockPointerAddress(self);
    }

    template<typename T, typename = std::enable_if_t<IsPtrOrHeapPtr<T, StructuredHiddenClass>>>
    static SystemHeapPointer<StructuredHiddenClass> GetHiddenClassOfFullBlockPointer(T self)
    {
        // The full block pointer points at one past the end of the block
        //
        uint32_t addr = GetFinalFullBlockPointer(self).m_value;

        // So subtracting the length of the block gives us the m_values pointer
        //
        addr -= static_cast<uint32_t>(sizeof(GeneralHeapPointer<void>)) * static_cast<uint32_t>(x_hiddenClassBlockSize);

        // Finally, subtract the offset of m_values to get the class pointer
        //
        addr -= static_cast<uint32_t>(OffsetOfTrailingVarLengthArray());

        return SystemHeapPointer<StructuredHiddenClass> { addr };
    }

    template<typename T, typename = std::enable_if_t<IsPtrOrHeapPtr<T, StructuredHiddenClass>>>
    static GeneralHeapPointer<void> GetPropertyNameFromInlineHashTableOrdinal(T self, int8_t ordinal)
    {
        if (ordinal >= 0)
        {
            // A non-negative offset denote the offset into the non-full block
            //
            assert(ordinal < self->m_nonFullBlockLen);
            return GeneralHeapPointer<void> { self->m_values[ordinal] };
        }
        else
        {
            // Ordinal [-x_hiddenClassBlockSize, 0) denote the offset into the final full block pointer
            // Note that the final full block pointer points at one past the end of the block, so we can simply index using the ordinal
            //
            assert(HasFinalFullBlockPointer(self->m_numSlots));
            assert(-static_cast<int8_t>(x_hiddenClassBlockSize) <= ordinal);
            SystemHeapPointer<GeneralHeapPointer<void>> u = GetFinalFullBlockPointer(self);
            return u.As()[ordinal];
        }
    }

    template<typename T, typename = std::enable_if_t<IsPtrOrHeapPtr<T, StructuredHiddenClass>>>
    static uint32_t GetPropertySlotFromInlineHashTableOrdinal(T self, int8_t ordinal)
    {
        assert(-static_cast<int8_t>(x_hiddenClassBlockSize) <= ordinal && ordinal < self->m_nonFullBlockLen);
        AssertImp(ordinal < 0, HasFinalFullBlockPointer(self->m_numSlots));
        int result = static_cast<int>(self->m_numSlots - self->m_nonFullBlockLen) + static_cast<int>(ordinal);
        assert(result >= 0);
        return static_cast<uint32_t>(result);
    }

    static uint32_t ComputeTrailingVarLengthArrayLengthBytes(uint8_t numSlots)
    {
        uint32_t result = ComputeNonFullBlockLength(numSlots) * static_cast<uint32_t>(sizeof(GeneralHeapPointer<void>));
        if (HasFinalFullBlockPointer(numSlots))
        {
            result += static_cast<uint32_t>(sizeof(SystemHeapPointer<GeneralHeapPointer<void>>));
        }
        return result;
    }

    // The value var-length array contains the non-full block, and,
    // if m_numSlots >= x_hiddenClassBlockSize && m_numSlot % x_hiddenClassBlockSize != 0, the pointer to the last full block
    //
    GeneralHeapPointer<void> m_values[0];
};
static_assert(sizeof(StructuredHiddenClass) == 32);

class StructuredHiddenClassIterator
{
public:
    StructuredHiddenClassIterator(StructuredHiddenClass* hiddenClass);
    StructuredHiddenClassIterator(SystemHeapPointer<StructuredHiddenClass> hc)
    {
        HeapPtr<StructuredHiddenClass> hiddenClass = hc.As();
        assert(hiddenClass->m_type == Type::StructuredHiddenClass);
        SystemHeapPointer<HiddenClassAnchorHashTable> aht = hiddenClass->m_anchorHashTable;

        m_hiddenClass = hc;
        m_anchorTable = aht;
        m_ord = 0;
        m_maxOrd = hiddenClass->m_numSlots;

        if (aht.m_value != 0)
        {
            HeapPtr<HiddenClassAnchorHashTable> anchorHashTable = aht.As();
            assert(anchorHashTable->m_numBlocks > 0);
            m_curPtr = anchorHashTable->m_blockPointers[0];
            m_anchorTableMaxOrd = anchorHashTable->m_numTotalSlots;
        }
        else
        {
            m_anchorTableMaxOrd = 0;
            if (StructuredHiddenClass::HasFinalFullBlockPointer(hiddenClass->m_numSlots))
            {
                m_curPtr = StructuredHiddenClass::GetFinalFullBlockPointer(hiddenClass);
            }
            else
            {
                m_curPtr = m_hiddenClass.As<uint8_t>() + StructuredHiddenClass::OffsetOfTrailingVarLengthArray();
            }
        }
    }

    bool HasMore()
    {
        return m_ord < m_maxOrd;
    }

    GeneralHeapPointer<void> GetCurrentKey()
    {
        assert(m_ord < m_maxOrd);
        return m_curPtr.As();
    }

    uint8_t GetCurrentSlotOrdinal()
    {
        assert(m_ord < m_maxOrd);
        return m_ord;
    }

    void Advance()
    {
        m_ord++;
        if (unlikely((m_ord & static_cast<uint8_t>(x_hiddenClassBlockSize - 1)) == 0))
        {
            if (m_ord < m_maxOrd)
            {
                HeapPtr<StructuredHiddenClass> hiddenClass = m_hiddenClass.As<StructuredHiddenClass>();
                if (m_ord == m_anchorTableMaxOrd)
                {
                    if (StructuredHiddenClass::HasFinalFullBlockPointer(m_maxOrd) &&
                        !StructuredHiddenClass::IsAnchorTableContainsFinalBlock(hiddenClass))
                    {
                        m_curPtr = StructuredHiddenClass::GetFinalFullBlockPointer(hiddenClass);
                    }
                    else
                    {
                        m_curPtr = m_hiddenClass.m_value + static_cast<uint32_t>(StructuredHiddenClass::OffsetOfTrailingVarLengthArray());
                    }
                }
                else if (m_ord > m_anchorTableMaxOrd)
                {
                    assert(m_ord == m_anchorTableMaxOrd + x_hiddenClassBlockSize);
                    assert(StructuredHiddenClass::HasFinalFullBlockPointer(hiddenClass->m_numSlots) &&
                           !StructuredHiddenClass::IsAnchorTableContainsFinalBlock(hiddenClass));
                    assert(m_curPtr == StructuredHiddenClass::GetFinalFullBlockPointer(hiddenClass).As() + x_hiddenClassBlockSize);
                    m_curPtr = m_hiddenClass.As<uint8_t>() + StructuredHiddenClass::OffsetOfTrailingVarLengthArray();
                }
                else
                {
                    HeapPtr<HiddenClassAnchorHashTable> anchorTable = m_anchorTable.As<HiddenClassAnchorHashTable>();
                    m_curPtr = anchorTable->m_blockPointers[m_ord >> x_log2_hiddenClassBlockSize];
                }
            }
        }
        else
        {
            m_curPtr = m_curPtr.As() + 1;
        }
    }

private:
    SystemHeapPointer<StructuredHiddenClass> m_hiddenClass;
    // It's important to store m_anchorTable here since it may change
    //
    SystemHeapPointer<HiddenClassAnchorHashTable> m_anchorTable;
    SystemHeapPointer<GeneralHeapPointer<void>> m_curPtr;
    uint8_t m_ord;
    uint8_t m_maxOrd;
    uint8_t m_anchorTableMaxOrd;
};
static_assert(sizeof(StructuredHiddenClassIterator) == 16);

class VM : public VMMemoryManager<VM>, public GlobalStringHashConser<VM>
{
public:

    bool WARN_UNUSED Initialize()
    {
        bool success = false;

        CHECK_LOG_ERROR(static_cast<VMMemoryManager<VM>*>(this)->Initialize());
        Auto(if (!success) static_cast<VMMemoryManager<VM>*>(this)->Cleanup());

        CHECK_LOG_ERROR(static_cast<GlobalStringHashConser<VM>*>(this)->Initialize());
        Auto(if (!success) static_cast<GlobalStringHashConser<VM>*>(this)->Cleanup());

        success = true;
        return true;
    }

    void Cleanup()
    {
        static_cast<GlobalStringHashConser<VM>*>(this)->Cleanup();
        static_cast<VMMemoryManager<VM>*>(this)->Cleanup();
    }


};

template<typename T>
remove_heap_ptr_t<T> TranslateToRawPointer(VM* vm, T ptr)
{
    if constexpr(IsHeapPtrType<T>::value)
    {
        return vm->GetHeapPtrTranslator().TranslateToRawPtr(ptr);
    }
    else
    {
        static_assert(std::is_pointer_v<T>);
        return ptr;
    }
}

inline StructuredHiddenClassIterator::StructuredHiddenClassIterator(StructuredHiddenClass* hiddenClass)
    : StructuredHiddenClassIterator(VM::GetActiveVMForCurrentThread()->GetHeapPtrTranslator().TranslateToSystemHeapPtr(hiddenClass))
{ }

inline HiddenClassAnchorHashTable* WARN_UNUSED HiddenClassAnchorHashTable::Create(VM* vm, StructuredHiddenClass* shc)
{
    uint8_t numElements = shc->m_numSlots;
    assert(numElements % x_hiddenClassBlockSize == 0);
    uint8_t numBlocks = numElements >> x_log2_hiddenClassBlockSize;

    // Do the space calculations and allocate the struct
    //
    uint32_t hashTableSize = RoundUpToPowerOfTwo(static_cast<uint32_t>(numElements)) * 2;
    assert(hashTableSize % 8 == 0);
    int64_t hashTableMask = ~static_cast<int64_t>(hashTableSize - 1);

    uint32_t hashTableLengthBytes = hashTableSize * static_cast<uint32_t>(sizeof(HashTableEntry));
    uint32_t trailingArrayLengthBytes = numBlocks * static_cast<uint32_t>(sizeof(SystemHeapPointer<GeneralHeapPointer<void>>));
    uint32_t allocationSize = hashTableLengthBytes + static_cast<uint32_t>(OffsetOfTrailingVarLengthArray()) + trailingArrayLengthBytes;
    allocationSize = RoundUpToMultipleOf<8>(allocationSize);
    SystemHeapPointer<void> objectAddressStart = vm->AllocFromSystemHeap(allocationSize);

    // First, fill in the header
    //
    HashTableEntry* hashTableStart = TranslateToRawPointer(vm, objectAddressStart.As<HashTableEntry>());
    HashTableEntry* hashTableEnd = hashTableStart + hashTableSize;
    HiddenClassAnchorHashTable* r = reinterpret_cast<HiddenClassAnchorHashTable*>(hashTableEnd);
    HeapEntityCommonHeader::Populate(r);
    r->m_numBlocks = numBlocks;
    r->m_numTotalSlots = numElements;
    r->m_hashTableMask = static_cast<int32_t>(hashTableMask);
    assert(GetHashTableSizeFromHashTableMask(r->m_hashTableMask) == hashTableSize);

    SystemHeapPointer<HiddenClassAnchorHashTable> oldAnchorTableV = shc->m_anchorHashTable;
    AssertImp(oldAnchorTableV.m_value == 0, numElements == x_hiddenClassBlockSize);
    AssertImp(oldAnchorTableV.m_value != 0, oldAnchorTableV.As()->m_numBlocks == numBlocks - 1);

    // Now, copy in the content of the existing anchor table
    //
    if (oldAnchorTableV.m_value != 0)
    {
        // Copy in the hash table
        //
        HiddenClassAnchorHashTable* oldAnchorTable = TranslateToRawPointer(vm, oldAnchorTableV.As<HiddenClassAnchorHashTable>());
        oldAnchorTable->CloneHashTableTo(hashTableStart, hashTableSize);

        // Copy in the block pointer list
        //
        SafeMemcpy(r->m_blockPointers, oldAnchorTable->m_blockPointers, sizeof(SystemHeapPointer<GeneralHeapPointer<void>>) * oldAnchorTable->m_numBlocks);
    }

    // Now, insert the new full block into the hash table
    //
    for (uint32_t i = 0; i < x_hiddenClassBlockSize; i++)
    {
        GeneralHeapPointer<void> e  = shc->m_values[i];
        UserHeapPointer<void> eu { e.As<void>() };
        uint32_t hashValue = HiddenClassKeyHashHelper::GetHashValueForMaybeNonStringKey(eu);

        uint8_t checkHash = static_cast<uint8_t>(hashValue);
        int64_t hashSlot = static_cast<int64_t>(hashValue >> 8) | hashTableMask;
        assert(-static_cast<int64_t>(hashTableSize) <= hashSlot && hashSlot < 0);

        while (hashTableEnd[hashSlot].m_ordinal != x_hashTableEmptyValue)
        {
            hashSlot = (hashSlot - 1) | hashTableMask;
            assert(-static_cast<int64_t>(hashTableSize) <= hashSlot && hashSlot < 0);
        }

        hashTableEnd[hashSlot].m_ordinal = static_cast<uint8_t>(numElements - x_hiddenClassBlockSize + i);
        hashTableEnd[hashSlot].m_checkHash = checkHash;
    }

    // And finally fill in the pointer for the new block
    //
    {
        SystemHeapPointer<uint8_t> base = vm->GetHeapPtrTranslator().TranslateToSystemHeapPtr(shc);
        base = base.As() + static_cast<uint32_t>(StructuredHiddenClass::OffsetOfTrailingVarLengthArray());
        base = base.As() + static_cast<uint32_t>(sizeof(GeneralHeapPointer<void>)) * x_hiddenClassBlockSize;
        r->m_blockPointers[numBlocks - 1] = base;
    }

    // In debug mode, check that the anchor hash table contains all the expected elements, no more and no less
    //
#ifndef NDEBUG
    {
        // Make sure all element in the list are distinct and can be found in the hash table
        //
        ReleaseAssert(r->m_numTotalSlots == numElements);
        uint32_t elementCount = 0;
        std::set<int64_t> elementSet;
        for (uint8_t i = 0; i < r->m_numTotalSlots; i++)
        {
            GeneralHeapPointer<void> p = HiddenClassAnchorHashTable::GetPropertyNameAtSlot(r, i);
            UserHeapPointer<void> key { p.As() };
            ReleaseAssert(!elementSet.count(key.m_value));
            elementSet.insert(key.m_value);
            elementCount++;

            uint32_t querySlot = static_cast<uint32_t>(-1);
            bool found = HiddenClassAnchorHashTable::GetSlotOrdinalFromPropertyNameAndHash(
                        r, p, HiddenClassKeyHashHelper::GetHashValueForMaybeNonStringKey(key), querySlot /*out*/);
            ReleaseAssert(found);
            ReleaseAssert(querySlot == i);
        }
        ReleaseAssert(elementCount == r->m_numTotalSlots && elementSet.size() == elementCount);

        // Make sure the hash table doesn't contain anything other than the elements in the list
        //
        uint32_t elementCountHt = 0;
        std::set<int64_t> elementSetHt;
        for (uint32_t i = 0; i < GetHashTableSizeFromHashTableMask(r->m_hashTableMask); i++)
        {
            HashTableEntry e = GetHashTableBegin(r)[i];
            if (e.m_ordinal != x_hashTableEmptyValue)
            {
                GeneralHeapPointer<void> p = HiddenClassAnchorHashTable::GetPropertyNameAtSlot(r, e.m_ordinal);
                UserHeapPointer<void> key { p.As() };
                ReleaseAssert(elementSet.count(key.m_value));
                ReleaseAssert(!elementSetHt.count(key.m_value));
                elementSetHt.insert(key.m_value);
                elementCountHt++;
            }
        }
        ReleaseAssert(elementCountHt == elementCount);
        ReleaseAssert(elementSetHt.size() == elementCount);
    }
#endif

    return r;
}

inline void HiddenClassAnchorHashTable::CloneHashTableTo(HiddenClassAnchorHashTable::HashTableEntry* htStart, uint32_t htSize)
{
    uint32_t selfHtSize = GetHashTableSizeFromHashTableMask(m_hashTableMask);
    assert(htSize >= selfHtSize);

    if (htSize == selfHtSize)
    {
        // If the target hash table has equal size, a memcpy is sufficient
        //
        SafeMemcpy(htStart, GetHashTableBegin(this), sizeof(HashTableEntry) * htSize);
    }
    else
    {
        // Otherwise, we must insert every element into the new hash table
        //
        memset(htStart, x_hashTableEmptyValue, sizeof(HashTableEntry) * htSize);

        assert(is_power_of_2(htSize));
        HashTableEntry* htEnd = htStart + htSize;
        int64_t htMask = ~ZeroExtendTo<int64_t>(htSize - 1);
        assert(htMask < 0);
        for (uint8_t blockOrd = 0; blockOrd < m_numBlocks; blockOrd++)
        {
            HeapPtr<GeneralHeapPointer<void>> p = m_blockPointers[blockOrd].As();
            for (uint8_t offset = 0; offset < x_hiddenClassBlockSize; offset++)
            {
                GeneralHeapPointer<void> e = p[offset];
                UserHeapPointer<void> eu { e.As() };
                uint32_t hashValue = HiddenClassKeyHashHelper::GetHashValueForMaybeNonStringKey(eu);

                uint8_t checkHash = static_cast<uint8_t>(hashValue);
                int64_t hashSlot = static_cast<int64_t>(hashValue >> 8) | htMask;
                assert(-static_cast<int64_t>(htSize) <= hashSlot && hashSlot < 0);

                while (htEnd[hashSlot].m_ordinal != x_hashTableEmptyValue)
                {
                    hashSlot = (hashSlot - 1) | htMask;
                    assert(-static_cast<int64_t>(htSize) <= hashSlot && hashSlot < 0);
                }

                htEnd[hashSlot].m_ordinal = static_cast<uint8_t>((blockOrd << x_log2_hiddenClassBlockSize) | offset);
                htEnd[hashSlot].m_checkHash = checkHash;
            }
        }
    }
}

template<typename T, typename>
bool WARN_UNUSED StructuredHiddenClass::AddStringProperty(T self, UserHeapPointer<HeapString> stringKey, AddPropertyResult& result /*out*/ )
{
    // TODO: check for existing transition

    if (GetSlotOrdinalFromStringProperty(self, stringKey, result.m_slotOrdinal /*out*/))
    {
        result.m_transitionedToNewHiddenClass = false;
        return true;
    }

    assert(self->m_numSlots <= x_maxNumSlots);
    if (self->m_numSlots == x_maxNumSlots)
    {
        return false;   // transition to dictionary
    }



    VM* vm = VM::GetActiveVMForCurrentThread();
    StructuredHiddenClass* selfRaw = TranslateToRawPointer(vm, self);
    selfRaw->PerformAddPropertyTransition(vm, stringKey, result /*out*/);
    return true;
}

template<typename T, typename>
SystemHeapPointer<HiddenClassAnchorHashTable> WARN_UNUSED StructuredHiddenClass::BuildNewAnchorTableIfNecessary(T self)
{
    assert(self->m_nonFullBlockLen == x_hiddenClassBlockSize);
    assert(self->m_numSlots > 0 && self->m_numSlots % x_hiddenClassBlockSize == 0);
    SystemHeapPointer<HiddenClassAnchorHashTable> anchorHt = self->m_anchorHashTable;
    AssertIff(!IsAnchorTableContainsFinalBlock(self), anchorHt.m_value == 0 || anchorHt.As()->m_numTotalSlots != self->m_numSlots);
    if (!IsAnchorTableContainsFinalBlock(self))
    {
        AssertImp(anchorHt.m_value != 0, anchorHt.As()->m_numTotalSlots == self->m_numSlots - static_cast<uint8_t>(x_hiddenClassBlockSize));
        // The updated anchor hash table has not been built, we need to build it now
        //
        VM* vm = VM::GetActiveVMForCurrentThread();
        StructuredHiddenClass* selfRaw = TranslateToRawPointer(vm, self);
        HiddenClassAnchorHashTable* newAnchorTable = HiddenClassAnchorHashTable::Create(vm, selfRaw);
        selfRaw->m_anchorHashTable = vm->GetHeapPtrTranslator().TranslateToSystemHeapPtr(newAnchorTable);
        anchorHt = selfRaw->m_anchorHashTable;

        // Once the updated anchor hash table is built, we don't need the inline hash table any more, empty it out
        //
        InlineHashTableEntry* ht = GetInlineHashTableBegin(selfRaw);
        size_t htLengthBytes = ComputeHashTableSizeFromHashTableMask(self->m_inlineHashTableMask) * sizeof(InlineHashTableEntry);
        memset(ht, x_inlineHashTableEmptyValue, htLengthBytes);
    }

    assert(anchorHt.As()->m_numTotalSlots == self->m_numSlots);
    return anchorHt;
}

inline void StructuredHiddenClass::PerformAddPropertyTransition(VM* vm, UserHeapPointer<void> key, AddPropertyResult& result /*out*/)
{
    assert(m_numSlots < x_maxNumSlots);
    assert(m_numSlots <= static_cast<uint32_t>(m_inlineNamedStorageCapacity) + m_butterflyNamedStorageCapacity);
    assert(m_nonFullBlockLen <= x_hiddenClassBlockSize);

    // Work out various properties of the new hidden class
    //
    SystemHeapPointer<HiddenClassAnchorHashTable> anchorTableForNewNode;
    uint8_t nonFullBlockCopyLengthForNewNode;
    bool hasFinalBlockPointer;
    bool mayMemcpyOldInlineHashTable;
    bool inlineHashTableMustContainFinalBlock;
    SystemHeapPointer<GeneralHeapPointer<void>> finalBlockPointerValue;    // only filled if hasFinalBlockPointer

    if (m_nonFullBlockLen == x_hiddenClassBlockSize - 1)
    {
        // We are about to fill our current non-full block to full capacity, so the previous full block's node now qualifies to become an anchor
        // If it has not become an anchor yet, build it.
        //
        AssertIff(m_numSlots >= x_hiddenClassBlockSize, HasFinalFullBlockPointer(m_numSlots));
        AssertIff(m_numSlots >= x_hiddenClassBlockSize, m_anchorHashTable.m_value != 0);
        if (m_numSlots >= x_hiddenClassBlockSize)
        {
            SystemHeapPointer<StructuredHiddenClass> anchorTargetHiddenClass = GetHiddenClassOfFullBlockPointer(this);
            anchorTableForNewNode = BuildNewAnchorTableIfNecessary(anchorTargetHiddenClass.As());
        }
        else
        {
            anchorTableForNewNode.m_value = 0;
        }
        nonFullBlockCopyLengthForNewNode = x_hiddenClassBlockSize - 1;
        hasFinalBlockPointer = false;
        inlineHashTableMustContainFinalBlock = false;
        // The new node's inline hash table should only contain the last x_hiddenClassBlockSize elements
        // If our hash table doesn't contain the final block, then our hash table only contains the last x_hiddenClassBlockSize - 1 elements, only in this case we can copy
        //
        mayMemcpyOldInlineHashTable = !IsAnchorTableContainsFinalBlock(this);
    }
    else if (m_nonFullBlockLen == x_hiddenClassBlockSize)
    {
        // The current node's non-full block is full. The new node will have a new non-full block, and its final block pointer will point to us.
        //
        anchorTableForNewNode = m_anchorHashTable;
        nonFullBlockCopyLengthForNewNode = 0;
        hasFinalBlockPointer = true;
        SystemHeapPointer<uint8_t> val = vm->GetHeapPtrTranslator().TranslateToSystemHeapPtr(this);
        val = val.As() + static_cast<uint32_t>(OffsetOfTrailingVarLengthArray());
        val = val.As() + x_hiddenClassBlockSize * static_cast<uint32_t>(sizeof(GeneralHeapPointer<void>));
        finalBlockPointerValue = val;
        inlineHashTableMustContainFinalBlock = !IsAnchorTableContainsFinalBlock(this);
        mayMemcpyOldInlineHashTable = false;
    }
    else
    {
        // The current node's non-full block will not reach full capcity after the transition.
        //
        nonFullBlockCopyLengthForNewNode = m_nonFullBlockLen;
        // The new node has a final block pointer iff we do
        //
        hasFinalBlockPointer = HasFinalFullBlockPointer(m_numSlots);
        if (hasFinalBlockPointer)
        {
            finalBlockPointerValue = GetFinalFullBlockPointer(this);
        }
        if (IsAnchorTableContainsFinalBlock(this))
        {
            // If our anchor table already contains the final block, this is the good case.
            // The new node's inline hash table doesn't need to contain the final block, and it may directly copy from our inline hash table
            //
            inlineHashTableMustContainFinalBlock = false;
            mayMemcpyOldInlineHashTable = true;
            anchorTableForNewNode = m_anchorHashTable;
        }
        else
        {
            // Otherwise, if we have a final block pointer, we want to check if that node has been promoted to an anchor.
            // If yes, then the new node can set its anchor to that node instead of our anchor so it doesn't have to contain the final block
            //
            bool useUpdatedAnchor = false;
            SystemHeapPointer<HiddenClassAnchorHashTable> updatedAnchor;
            if (hasFinalBlockPointer)
            {
                HeapPtr<StructuredHiddenClass> anchorClass = GetHiddenClassOfFullBlockPointer(this).As<StructuredHiddenClass>();
                assert(anchorClass->m_numSlots > 0 && anchorClass->m_numSlots % x_hiddenClassBlockSize == 0);
                assert(anchorClass->m_numSlots == m_numSlots - m_nonFullBlockLen);
                if (IsAnchorTableContainsFinalBlock(anchorClass))
                {
                    useUpdatedAnchor = true;
                    updatedAnchor = anchorClass->m_anchorHashTable;
                }
            }
            if (!useUpdatedAnchor)
            {
                anchorTableForNewNode = m_anchorHashTable;
                inlineHashTableMustContainFinalBlock = hasFinalBlockPointer;
                mayMemcpyOldInlineHashTable = true;
            }
            else
            {
                anchorTableForNewNode = updatedAnchor;
                inlineHashTableMustContainFinalBlock = false;
                mayMemcpyOldInlineHashTable = false;
            }
        }
    }
    assert(0 <= nonFullBlockCopyLengthForNewNode && nonFullBlockCopyLengthForNewNode < x_log2_hiddenClassBlockSize);
    AssertImp(inlineHashTableMustContainFinalBlock, hasFinalBlockPointer);

    // Check if a butterfly space expansion is needed
    // TODO: refine the growth strategy
    //
    assert(m_inlineNamedStorageCapacity < x_maxNumSlots);
    bool needButterflyExpansion = false;
    uint8_t newButterflyCapacity = 0;
    if (m_butterflyNamedStorageCapacity == 0)
    {
        assert(m_numSlots <= m_inlineNamedStorageCapacity);
        if (m_numSlots == m_inlineNamedStorageCapacity)
        {
            needButterflyExpansion = true;
            constexpr uint8_t x_initialMinimumButterflyCapacity = 4;
            constexpr uint8_t x_butterflyCapacityFromInlineCapacityFactor = 2;
            uint8_t capacity = m_inlineNamedStorageCapacity / x_butterflyCapacityFromInlineCapacityFactor;
            capacity = std::max(capacity, x_initialMinimumButterflyCapacity);
            capacity = std::min(capacity, static_cast<uint8_t>(x_maxNumSlots - m_inlineNamedStorageCapacity));
            newButterflyCapacity = capacity;
        }
    }
    else
    {
        assert(m_numSlots > m_inlineNamedStorageCapacity);
        constexpr uint8_t x_butterflyCapacityGrowthFactor = 2;
        uint8_t usedLen = m_numSlots - m_inlineNamedStorageCapacity;
        assert(usedLen <= m_butterflyNamedStorageCapacity);
        if (usedLen == m_butterflyNamedStorageCapacity)
        {
            needButterflyExpansion = true;
            uint32_t capacity = static_cast<uint32_t>(m_butterflyNamedStorageCapacity) * x_butterflyCapacityGrowthFactor;
            capacity = std::min(capacity, static_cast<uint32_t>(x_maxNumSlots - m_inlineNamedStorageCapacity));
            newButterflyCapacity = static_cast<uint8_t>(capacity);
        }
    }
    AssertImp(needButterflyExpansion, static_cast<uint32_t>(newButterflyCapacity) + m_inlineNamedStorageCapacity > m_numSlots);
    AssertIff(!needButterflyExpansion, static_cast<uint32_t>(m_inlineNamedStorageCapacity) + m_butterflyNamedStorageCapacity > m_numSlots);

    // Work out the space needed for the new hidden class and perform allocation
    //
    uint8_t numElementsInInlineHashTable = nonFullBlockCopyLengthForNewNode + 1;
    if (hasFinalBlockPointer && inlineHashTableMustContainFinalBlock)
    {
        numElementsInInlineHashTable += static_cast<uint8_t>(x_hiddenClassBlockSize);
    }

    uint32_t htSize = RoundUpToPowerOfTwo(numElementsInInlineHashTable) * 2;
    if (htSize < 8) { htSize = 8; }
    assert(htSize <= 256);

    uint8_t htMaskToStore = static_cast<uint8_t>(htSize - 1);
    int64_t htMask = ~ZeroExtendTo<int64_t>(htMaskToStore);
    assert(htSize == ComputeHashTableSizeFromHashTableMask(htMaskToStore));

    AssertIff(hasFinalBlockPointer, HasFinalFullBlockPointer(m_numSlots + 1));

    uint32_t hashTableLengthBytes = static_cast<uint32_t>(sizeof(InlineHashTableEntry)) * htSize;
    uint32_t trailingVarLenArrayLengthBytes = ComputeTrailingVarLengthArrayLengthBytes(m_numSlots + 1);
    uint32_t totalObjectLengthBytes = hashTableLengthBytes + static_cast<uint32_t>(OffsetOfTrailingVarLengthArray()) + trailingVarLenArrayLengthBytes;
    totalObjectLengthBytes = RoundUpToMultipleOf<8>(totalObjectLengthBytes);

    SystemHeapPointer<void> objectAddressStart = vm->AllocFromSystemHeap(totalObjectLengthBytes);

    // Populate the header
    //
    InlineHashTableEntry* htBegin = vm->GetHeapPtrTranslator().TranslateToRawPtr(objectAddressStart.As<InlineHashTableEntry>());
    InlineHashTableEntry* htEnd = htBegin + htSize;
    StructuredHiddenClass* r = reinterpret_cast<StructuredHiddenClass*>(htEnd);

    HeapEntityCommonHeader::Populate(r);
    r->m_numSlots = m_numSlots + 1;
    r->m_nonFullBlockLen = nonFullBlockCopyLengthForNewNode + 1;
    assert(r->m_nonFullBlockLen == ComputeNonFullBlockLength(r->m_numSlots));
    r->m_anchorHashTable = anchorTableForNewNode;
    r->m_arrayStorageCapacity = m_arrayStorageCapacity;
    r->m_inlineHashTableMask = htMaskToStore;
    r->m_inlineNamedStorageCapacity = m_inlineNamedStorageCapacity;
    if (needButterflyExpansion)
    {
        r->m_butterflyNamedStorageCapacity = newButterflyCapacity;
        r->m_parentEdgeTransitionKind = StructuredHiddenClassTransitionKind::AddPropertyAndGrowButterflyStorage;
    }
    else
    {
        r->m_butterflyNamedStorageCapacity = m_butterflyNamedStorageCapacity;
        r->m_parentEdgeTransitionKind = StructuredHiddenClassTransitionKind::AddProperty;
    }
    r->m_parent = vm->GetHeapPtrTranslator().TranslateToSystemHeapPtr(this);
    r->m_metatable = m_metatable;
    r->m_transitionTable = 0;

    // Populate the element list
    //
    {
        // Copy everything
        //
        SafeMemcpy(r->m_values, m_values, sizeof(GeneralHeapPointer<void>) * nonFullBlockCopyLengthForNewNode);

        // Insert the new element
        //
        r->m_values[nonFullBlockCopyLengthForNewNode] = key.As<void>();

        // Write the final block pointer if needed
        //
        AssertIff(hasFinalBlockPointer, HasFinalFullBlockPointer(r->m_numSlots));
        if (hasFinalBlockPointer)
        {
            *GetFinalFullBlockPointerAddress(r) = finalBlockPointerValue;
        }
    }

    auto insertNonExistentElementIntoInlineHashTable = [&htEnd, &htMask, &htSize]
            (UserHeapPointer<void> element, int8_t ordinalOfElement) ALWAYS_INLINE
    {
        uint16_t hashOfElement = static_cast<uint16_t>(HiddenClassKeyHashHelper::GetHashValueForMaybeNonStringKey(element));
        uint8_t checkHash = static_cast<uint8_t>(hashOfElement);
        int64_t hashSlot = ZeroExtendTo<int64_t>(hashOfElement >> 8) | htMask;

        assert(-static_cast<int64_t>(htSize) <= hashSlot && hashSlot < 0);
        while (htEnd[hashSlot].m_ordinal != x_inlineHashTableEmptyValue)
        {
            hashSlot = (hashSlot - 1) | htMask;
            assert(-static_cast<int64_t>(htSize) <= hashSlot && hashSlot < 0);
        }

        htEnd[hashSlot].m_ordinal = ordinalOfElement;
        htEnd[hashSlot].m_checkHash = checkHash;

        std::ignore = htSize;
    };

    // Populate the inline hash table
    //
    // If we are allowed to memcpy the hash table (i.e. the new table is always old table + 1 element), do it if possible.
    //
    if (mayMemcpyOldInlineHashTable && htMaskToStore == m_inlineHashTableMask)
    {
        assert(ComputeHashTableSizeFromHashTableMask(m_inlineHashTableMask) == htSize);
        SafeMemcpy(htBegin, GetInlineHashTableBegin(this), hashTableLengthBytes);

        // Insert the newly-added element
        //
        insertNonExistentElementIntoInlineHashTable(key, static_cast<int8_t>(nonFullBlockCopyLengthForNewNode));
    }
    else
    {
        // We must manually insert every element into the hash table
        //
        memset(htBegin, x_inlineHashTableEmptyValue, hashTableLengthBytes);

        // First insert the final block if needed
        //
        if (inlineHashTableMustContainFinalBlock)
        {
            HeapPtr<GeneralHeapPointer<void>> p = GetFinalFullBlockPointer(r).As();
            for (int8_t i = -static_cast<int8_t>(x_hiddenClassBlockSize); i < 0; i++)
            {
                GeneralHeapPointer<void> e = p[i];
                insertNonExistentElementIntoInlineHashTable(e.As(), i /*ordinalOfElement*/);
            }
        }

        // Then insert the non-full block
        //
        for (int8_t i = 0; i <= nonFullBlockCopyLengthForNewNode; i++)
        {
            GeneralHeapPointer<void> e = r->m_values[i];
            insertNonExistentElementIntoInlineHashTable(e.As(), i /*ordinalOfElement*/);
        }
    }

    // In debug mode, check that the new node contains all the expected elements, no more and no less
    //
#ifndef NDEBUG
    {
        uint32_t elementCount = 0;
        std::set<int64_t> elementSet;
        StructuredHiddenClassIterator iterator(r);
        while (iterator.HasMore())
        {
            GeneralHeapPointer<void> keyG = iterator.GetCurrentKey();
            UserHeapPointer<void> keyToLookup { keyG.As() };
            uint8_t slot = iterator.GetCurrentSlotOrdinal();
            iterator.Advance();

            // Check that the iterator returns each key once and exactly once
            //
            ReleaseAssert(!elementSet.count(keyToLookup.m_value));
            elementSet.insert(keyToLookup.m_value);
            elementCount++;
            ReleaseAssert(elementCount <= r->m_numSlots);

            // Check that the key exists in the new node and is at the expected place
            //
            {
                uint32_t querySlot = static_cast<uint32_t>(-1);
                bool found = GetSlotOrdinalFromMaybeNonStringProperty(r, keyToLookup, querySlot /*out*/);
                ReleaseAssert(found);
                ReleaseAssert(querySlot == slot);
            }

            // Check that the key exists in the old node iff it is not equal to the newly inserted key
            //
            {
                uint32_t querySlot = static_cast<uint32_t>(-1);
                bool found = GetSlotOrdinalFromMaybeNonStringProperty(this, keyToLookup, querySlot /*out*/);
                if (keyToLookup != key)
                {
                    ReleaseAssert(found);
                    ReleaseAssert(querySlot == slot);
                }
                else
                {
                    ReleaseAssert(!found);
                }
            }
        }

        // Check that the iterator returned exactly the expected number of keys, and the newly inserted key is inside it
        //
        ReleaseAssert(elementCount == r->m_numSlots && elementSet.size() == elementCount);
        ReleaseAssert(elementSet.count(key.m_value));

        // Check the inline hash table, make sure it contains nothing unexpected
        // We don't have to check the anchor hash table because it has its own self-check
        //
        uint32_t inlineHtElementCount = 0;
        std::set<int64_t> inlineHtElementSet;
        for (uint32_t i = 0; i < ComputeHashTableSizeFromHashTableMask(r->m_inlineHashTableMask); i++)
        {
            InlineHashTableEntry e = GetInlineHashTableBegin(r)[i];
            if (e.m_ordinal != x_inlineHashTableEmptyValue)
            {
                GeneralHeapPointer<void> keyG = GetPropertyNameFromInlineHashTableOrdinal(r, e.m_ordinal);
                UserHeapPointer<void> keyToLookup { keyG.As() };

                ReleaseAssert(!inlineHtElementSet.count(keyToLookup.m_value));
                inlineHtElementSet.insert(keyToLookup.m_value);
                inlineHtElementCount++;

                ReleaseAssert(elementSet.count(keyToLookup.m_value));

                if (r->m_anchorHashTable.m_value != 0)
                {
                    uint32_t querySlot = static_cast<uint32_t>(-1);
                    bool found = HiddenClassAnchorHashTable::GetSlotOrdinalFromPropertyNameAndHash(
                                r->m_anchorHashTable.As<HiddenClassAnchorHashTable>(),
                                keyG,
                                HiddenClassKeyHashHelper::GetHashValueForMaybeNonStringKey(keyToLookup),
                                querySlot /*out*/);
                    ReleaseAssert(!found);
                }
            }
        }

        uint32_t expectedElementsInInlineHt = r->m_nonFullBlockLen;
        if (IsAnchorTableContainsFinalBlock(r))
        {
            expectedElementsInInlineHt += x_hiddenClassBlockSize;
        }
        ReleaseAssert(inlineHtElementCount == expectedElementsInInlineHt);
        ReleaseAssert(inlineHtElementSet.size() == inlineHtElementCount);
    }
#endif

    // Update the transition table
    //

    // TODO

    // Populate 'result'
    //
    result.m_transitionedToNewHiddenClass = true;
    result.m_shouldGrowButterfly = needButterflyExpansion;
    result.m_slotOrdinal = m_numSlots;
    result.m_newHiddenClass = vm->GetHeapPtrTranslator().TranslateToSystemHeapPtr(r);
}

class IRNode
{
public:
    virtual ~IRNode() { }

};

class IRLogicalVariable
{
public:

};

class IRBasicBlock
{
public:
    std::vector<IRNode*> m_nodes;
    std::vector<IRNode*> m_varAtHead;
    std::vector<IRNode*> m_varAvailableAtTail;
};

class IRConstant : public IRNode
{
public:

};

class IRGetLocal : public IRNode
{
public:
    int m_slot;
    IRLogicalVariable* m_vinfo;
};

class IRSetLocal : public IRNode
{
public:
    IRNode* m_value;
    int m_slot;
    IRLogicalVariable* m_vinfo;
};

class IRAdd : public IRNode
{
public:
    IRNode* m_lhs;
    IRNode* m_rhs;
};

class IRReturn : public IRNode
{
public:
    IRNode* m_value;
};

class IRCheckIsConstant : public IRNode
{
public:
    IRNode* m_value;
    TValue m_constant;
};

class BytecodeSlot
{
public:
    constexpr BytecodeSlot() : m_value(x_invalidValue) { }

    static constexpr BytecodeSlot WARN_UNUSED Local(int ord)
    {
        assert(ord >= 0);
        return BytecodeSlot(ord);
    }
    static constexpr BytecodeSlot WARN_UNUSED Constant(int ord)
    {
        assert(ord < 0);
        return BytecodeSlot(ord);
    }

    bool IsInvalid() const { return m_value == x_invalidValue; }
    bool IsLocal() const { assert(!IsInvalid()); return m_value >= 0; }
    bool IsConstant() const { assert(!IsInvalid()); return m_value < 0; }

    int WARN_UNUSED LocalOrd() const { assert(IsLocal()); return m_value; }
    int WARN_UNUSED ConstantOrd() const { assert(IsConstant()); return m_value; }

    explicit operator int() const { return m_value; }

private:
    constexpr BytecodeSlot(int value) : m_value(value) { }

    static constexpr int x_invalidValue = 0x7fffffff;
    int m_value;
};

enum class CodeBlockType
{
    INTERPRETER,
    BASELINE,
    FIRST_LEVEL_OPT
};

class CodeBlock
{
public:
    virtual ~CodeBlock() = default;
    virtual CodeBlockType GetCodeBlockType() const = 0;

};

class GlobalObject;
class alignas(64) CoroutineRuntimeContext
{
public:
    // The constant table of the current function, if interpreter
    //
    uint64_t* m_constants;

    // The global object, if interpreter
    //
    GlobalObject* m_globalObject;

    // slot [m_variadicRetSlotBase + ord] holds variadic return value 'ord'
    //
    uint32_t m_numVariadicRets;
    uint32_t m_variadicRetSlotBegin;

    // The stack object
    //
    uint64_t* m_stackObject;


};

using InterpreterFn = void(*)(CoroutineRuntimeContext* /*rc*/, RestrictPtr<void> /*stackframe*/, ConstRestrictPtr<uint8_t> /*instr*/, uint64_t /*unused*/);

class BaselineCodeBlock;
class FLOCodeBlock;

class InterpreterCodeBlock final : public CodeBlock
{
public:
    virtual CodeBlockType GetCodeBlockType() const override final
    {
        return CodeBlockType::INTERPRETER;
    }

    uint32_t m_stackFrameNumSlots;
    uint32_t m_numFixedArguments;
    uint32_t m_numUpValues;
    uint32_t m_bytecodeLength;

    bool m_hasVariadicArguments;

    // The entry point of the function, can be interpreter or jit
    //
    InterpreterFn m_functionEntryPoint;

    BaselineCodeBlock* m_baselineCodeBlock;
    FLOCodeBlock* m_floCodeBlock;

    uint8_t m_bytecode[0];
};

class FunctionObject : public HeapEntityCommonHeader
{
public:
    template<typename T, typename = std::enable_if_t<IsPtrOrHeapPtr<T, FunctionObject>>>
    static HeapPtr<InterpreterCodeBlock> ALWAYS_INLINE GetInterpreterCodeBlock(T self)
    {
        SystemHeapPointer<void> bytecode = self->m_bytecode;
        return bytecode.As<InterpreterCodeBlock>() - 1;
    }

    uint16_t m_padding;
    SystemHeapPointer<void> m_bytecode;
    TValue m_upValues[0];
};
static_assert(sizeof(FunctionObject) == 8);

// stack frame format:
//     [... VarArgs ...] [Header] [... Locals ...]
//                                ^
//                                stack frame pointer (sfp)
//
class alignas(8) StackFrameHeader
{
public:
    // The address of the caller stack frame
    //
    StackFrameHeader* m_caller;
    // The return address
    //
    void* m_retAddr;
    // The function corresponding to this stack frame
    //
    HeapPtr<FunctionObject> m_func;
    // If the function is calling (i.e. not topmost frame), denotes the offset of the bytecode that performed the call
    //
    uint32_t m_callerBytecodeOffset;
    // Total number of variadic arguments passed to the function
    //
    uint32_t m_numVariadicArguments;

    static StackFrameHeader* GetStackFrameHeader(void* sfp)
    {
        return reinterpret_cast<StackFrameHeader*>(sfp) - 1;
    }

    static TValue* GetLocalAddr(void* sfp, BytecodeSlot slot)
    {
        assert(slot.IsLocal());
        int ord = slot.LocalOrd();
        return reinterpret_cast<TValue*>(sfp) + ord;
    }

    static TValue GetLocal(void* sfp, BytecodeSlot slot)
    {
        return *GetLocalAddr(sfp, slot);
    }
};

static_assert(sizeof(StackFrameHeader) % sizeof(TValue) == 0);
static constexpr size_t x_sizeOfStackFrameHeaderInTermsOfTValue = sizeof(StackFrameHeader) / sizeof(TValue);

// The varg part of each inlined function can always
// be represented as a list of locals plus a suffix of the original function's varg
//
class InlinedFunctionVarArgRepresentation
{
public:
    // The prefix ordinals
    //
    std::vector<int> m_prefix;
    // The suffix of the original function's varg beginning at that ordinal (inclusive)
    //
    int m_suffix;
};

class InliningStackEntry
{
public:
    // The base ordinal of stack frame header
    //
    int m_baseOrd;
    // Number of fixed arguments for this function
    //
    int m_numArguments;
    // Number of locals for this function
    //
    int m_numLocals;
    // Varargs of this function
    //
    InlinedFunctionVarArgRepresentation m_varargs;

};

class BytecodeToIRTransformer
{
public:
    // Remap a slot in bytecode to the physical slot for the interpreter/baseline JIT
    //
    void RemapSlot(BytecodeSlot /*slot*/)
    {

    }

    void TransformFunctionImpl(IRBasicBlock* /*bb*/)
    {

    }

    std::vector<InliningStackEntry> m_inlineStack;
};

enum class Opcode
{
    BcReturn,
    BcCall,
    BcAddVV,
    BcSubVV,
    BcIsLTVV,
    BcConstant,
    X_END_OF_ENUM
};

extern const InterpreterFn x_interpreter_dispatches[static_cast<size_t>(Opcode::X_END_OF_ENUM)];

#define Dispatch(rc, stackframe, instr)                                                                                          \
    do {                                                                                                                         \
        uint8_t dispatch_nextopcode = *reinterpret_cast<const uint8_t*>(instr);                                                  \
        assert(dispatch_nextopcode < static_cast<size_t>(Opcode::X_END_OF_ENUM));                                                \
_Pragma("clang diagnostic push")                                                                                                 \
_Pragma("clang diagnostic ignored \"-Wuninitialized\"")                                                                          \
        uint64_t dispatch_unused;                                                                                                \
        [[clang::musttail]] return x_interpreter_dispatches[dispatch_nextopcode]((rc), (stackframe), (instr), dispatch_unused);  \
_Pragma("clang diagnostic pop")                                                                                                  \
    } while (false)

inline void EnterInterpreter(CoroutineRuntimeContext* rc, RestrictPtr<void> sfp, ConstRestrictPtr<uint8_t> bcu, uint64_t /*unused*/)
{
    Dispatch(rc, sfp, bcu);
}

// The return statement is required to fill nil up to x_minNilFillReturnValues values even if it returns less than that many values
//
constexpr uint32_t x_minNilFillReturnValues = 3;

class BcReturn
{
public:
    uint8_t m_opcode;
    bool m_isVariadicRet;
    uint16_t m_numReturnValues;
    BytecodeSlot m_slotBegin;

    static void Execute(CoroutineRuntimeContext* rc, RestrictPtr<void> sfp, ConstRestrictPtr<uint8_t> bcu, uint64_t /*unused*/)
    {
        const BcReturn* bc = reinterpret_cast<const BcReturn*>(bcu);
        assert(bc->m_opcode == static_cast<uint8_t>(Opcode::BcReturn));
        assert(bc->m_slotBegin.IsLocal());
        TValue* pbegin = StackFrameHeader::GetLocalAddr(sfp, bc->m_slotBegin);
        uint32_t numRetValues = bc->m_numReturnValues;
        if (bc->m_isVariadicRet)
        {
            assert(rc->m_numVariadicRets != static_cast<uint32_t>(-1));
            TValue* pdst = pbegin + bc->m_numReturnValues;
            TValue* psrc = reinterpret_cast<TValue*>(sfp) + rc->m_variadicRetSlotBegin;
            numRetValues += rc->m_numVariadicRets;
            SafeMemcpy(pdst, psrc, sizeof(TValue) * rc->m_numVariadicRets);
        }
        // No matter we consumed variadic ret or not, it is no longer valid after the return
        //
        DEBUG_ONLY(rc->m_numVariadicRets = static_cast<uint32_t>(-1);)

        // Fill nil up to x_minNilFillReturnValues values
        // TODO: we can also just do a vectorized write
        //
        {
            uint32_t idx = numRetValues;
            while (idx < x_minNilFillReturnValues)
            {
                pbegin[idx] = TValue::CreateMIV(MiscImmediateValue::CreateNil(), TValue::x_mivTag);
                idx++;
            }
        }

        StackFrameHeader* hdr = StackFrameHeader::GetStackFrameHeader(sfp);
        using RetFn = void(*)(CoroutineRuntimeContext* /*rc*/, void* /*sfp*/, uint8_t* /*retValuesStart*/, uint64_t /*numRetValues*/);
        RetFn retAddr = reinterpret_cast<RetFn>(hdr->m_retAddr);
        StackFrameHeader* callerSf = hdr->m_caller;
        [[clang::musttail]] return retAddr(rc, static_cast<void*>(callerSf), reinterpret_cast<uint8_t*>(pbegin), numRetValues);
    }
} __attribute__((__packed__));

class BcCall
{
public:
    uint8_t m_opcode;
    bool m_keepVariadicRet;
    bool m_passVariadicRetAsParam;
    uint32_t m_numFixedParams;
    uint32_t m_numFixedRets;    // only used when m_keepVariadicRet == false
    BytecodeSlot m_funcSlot;   // params are [m_funcSlot + 1, ... m_funcSlot + m_numFixedParams]

    static void Execute(CoroutineRuntimeContext* rc, RestrictPtr<void> sfp, ConstRestrictPtr<uint8_t> bcu, uint64_t /*unused*/)
    {
        const BcCall* bc = reinterpret_cast<const BcCall*>(bcu);
        assert(bc->m_opcode == static_cast<uint8_t>(Opcode::BcCall));
        StackFrameHeader* hdr = StackFrameHeader::GetStackFrameHeader(sfp);
        HeapPtrTranslator translator = VM::GetActiveVMForCurrentThread()->GetHeapPtrTranslator();
        SystemHeapPointer<uint8_t> callerBytecodeStart = hdr->m_func->m_bytecode;
        hdr->m_callerBytecodeOffset = SafeIntegerCast<uint32_t>(reinterpret_cast<const uint8_t*>(bc) - translator.TranslateToRawPtr(callerBytecodeStart.As()));

        assert(bc->m_funcSlot.IsLocal());
        TValue* begin = StackFrameHeader::GetLocalAddr(sfp, bc->m_funcSlot);
        TValue func = *begin;
        begin++;

        if (func.IsPointer(TValue::x_mivTag))
        {
            if (func.AsPointer().As<HeapEntityCommonHeader>()->m_type == Type::FUNCTION)
            {
                HeapPtr<FunctionObject> target = func.AsPointer().As<FunctionObject>();

                TValue* sfEnd = reinterpret_cast<TValue*>(sfp) + FunctionObject::GetInterpreterCodeBlock(hdr->m_func)->m_stackFrameNumSlots;
                TValue* baseForNextFrame = sfEnd + x_sizeOfStackFrameHeaderInTermsOfTValue;

                uint32_t numFixedArgsToPass = bc->m_numFixedParams;
                uint32_t totalArgs = numFixedArgsToPass;
                if (bc->m_passVariadicRetAsParam)
                {
                    totalArgs += rc->m_numVariadicRets;
                }

                uint32_t numCalleeExpectingArgs = FunctionObject::GetInterpreterCodeBlock(target)->m_numFixedArguments;
                bool calleeTakesVarArgs = FunctionObject::GetInterpreterCodeBlock(target)->m_hasVariadicArguments;

                // If the callee takes varargs and it is not empty, set up the varargs
                //
                if (unlikely(calleeTakesVarArgs))
                {
                    uint32_t actualNumVarArgs = 0;
                    if (totalArgs > numCalleeExpectingArgs)
                    {
                        actualNumVarArgs = totalArgs - numCalleeExpectingArgs;
                        baseForNextFrame += actualNumVarArgs;
                    }

                    // First, if we need to pass varret, move the whole varret to the correct position
                    //
                    if (bc->m_passVariadicRetAsParam)
                    {
                        TValue* varRetbegin = reinterpret_cast<TValue*>(sfp) + rc->m_variadicRetSlotBegin;
                        // TODO: over-moving is fine
                        memmove(baseForNextFrame + numFixedArgsToPass, varRetbegin, sizeof(TValue) * rc->m_numVariadicRets);
                    }

                    // Now, copy the fixed args to the correct position
                    //
                    SafeMemcpy(baseForNextFrame, begin, sizeof(TValue) * numFixedArgsToPass);

                    // Now, set up the vararg part
                    //
                    if (totalArgs > numCalleeExpectingArgs)
                    {
                        SafeMemcpy(sfEnd, baseForNextFrame + numCalleeExpectingArgs, sizeof(TValue) * (totalArgs - numCalleeExpectingArgs));
                    }

                    // Finally, set up the numVarArgs field in the frame header
                    //
                    StackFrameHeader* sfh = reinterpret_cast<StackFrameHeader*>(baseForNextFrame) - 1;
                    sfh->m_numVariadicArguments = actualNumVarArgs;
                }
                else
                {
                    // First, if we need to pass varret, move the whole varret to the correct position, up to the number of args the callee accepts
                    //
                    if (bc->m_passVariadicRetAsParam)
                    {
                        if (numCalleeExpectingArgs > numFixedArgsToPass)
                        {
                            TValue* varRetbegin = reinterpret_cast<TValue*>(sfp) + rc->m_variadicRetSlotBegin;
                            // TODO: over-moving is fine
                            memmove(baseForNextFrame + numFixedArgsToPass, varRetbegin, sizeof(TValue) * std::min(rc->m_numVariadicRets, numCalleeExpectingArgs - numFixedArgsToPass));
                        }
                    }

                    // Now, copy the fixed args to the correct position, up to the number of args the callee accepts
                    //
                    SafeMemcpy(baseForNextFrame, begin, sizeof(TValue) * std::min(numFixedArgsToPass, numCalleeExpectingArgs));
                }

                // Finally, pad in nils if necessary
                //
                if (totalArgs < numCalleeExpectingArgs)
                {
                    TValue* p = baseForNextFrame + totalArgs;
                    TValue* end = baseForNextFrame + numCalleeExpectingArgs;
                    while (p < end)
                    {
                        *p = TValue::CreateMIV(MiscImmediateValue::CreateNil(), TValue::x_mivTag);
                        p++;
                    }
                }

                // Set up the stack frame header
                //
                StackFrameHeader* sfh = reinterpret_cast<StackFrameHeader*>(baseForNextFrame) - 1;
                sfh->m_caller = reinterpret_cast<StackFrameHeader*>(sfp);
                sfh->m_retAddr = reinterpret_cast<void*>(OnReturn);
                sfh->m_func = target;

                _Pragma("clang diagnostic push")
                _Pragma("clang diagnostic ignored \"-Wuninitialized\"")
                uint64_t unused;
                SystemHeapPointer<void> targetBytecodeStart = target->m_bytecode;
                [[clang::musttail]] return FunctionObject::GetInterpreterCodeBlock(target)->m_functionEntryPoint(rc, reinterpret_cast<RestrictPtr<void>>(baseForNextFrame), translator.TranslateToRawPtr(targetBytecodeStart.As<uint8_t>()), unused);
                _Pragma("clang diagnostic pop")
            }
            else
            {
                assert(false && "unimplemented");
            }
        }
        else
        {
            assert(false && "unimplemented");
        }
    }

    static void OnReturn(CoroutineRuntimeContext* rc, RestrictPtr<void> stackframe, ConstRestrictPtr<uint8_t> retValuesU, uint64_t numRetValues)
    {
        const TValue* retValues = reinterpret_cast<const TValue*>(retValuesU);
        StackFrameHeader* hdr = StackFrameHeader::GetStackFrameHeader(stackframe);
        HeapPtrTranslator translator = VM::GetActiveVMForCurrentThread()->GetHeapPtrTranslator();
        SystemHeapPointer<uint8_t> callerBytecodeStart = hdr->m_func->m_bytecode;
        ConstRestrictPtr<uint8_t> bcu = translator.TranslateToRawPtr(callerBytecodeStart.As()) + hdr->m_callerBytecodeOffset;
        const BcCall* bc = reinterpret_cast<const BcCall*>(bcu);
        assert(static_cast<Opcode>(bc->m_opcode) == Opcode::BcCall);
        if (bc->m_keepVariadicRet)
        {
            rc->m_numVariadicRets = SafeIntegerCast<uint32_t>(numRetValues);
            rc->m_variadicRetSlotBegin = SafeIntegerCast<uint32_t>(retValues - reinterpret_cast<TValue*>(stackframe));
        }
        else
        {
            if (bc->m_numFixedRets <= x_minNilFillReturnValues)
            {
                SafeMemcpy(StackFrameHeader::GetLocalAddr(stackframe, bc->m_funcSlot), retValues, sizeof(TValue) * bc->m_numFixedRets);
            }
            else
            {
                TValue* dst = StackFrameHeader::GetLocalAddr(stackframe, bc->m_funcSlot);
                if (numRetValues < bc->m_numFixedRets)
                {
                    SafeMemcpy(dst, retValues, sizeof(TValue) * numRetValues);
                    while (numRetValues < bc->m_numFixedRets)
                    {
                        dst[numRetValues] = TValue::CreateMIV(MiscImmediateValue::CreateNil(), TValue::x_mivTag);
                        numRetValues++;
                    }
                }
                else
                {
                    SafeMemcpy(dst, retValues, sizeof(TValue) * bc->m_numFixedRets);
                }
            }
        }
        Dispatch(rc, stackframe, bcu + sizeof(BcCall));
    }
} __attribute__((__packed__));

class BcAddVV
{
public:
    uint8_t m_opcode;
    BytecodeSlot m_lhs;
    BytecodeSlot m_rhs;
    BytecodeSlot m_result;

    static void Execute(CoroutineRuntimeContext* rc, RestrictPtr<void> stackframe, ConstRestrictPtr<uint8_t> bcu, uint64_t /*unused*/)
    {
        const BcAddVV* bc = reinterpret_cast<const BcAddVV*>(bcu);
        assert(bc->m_opcode == static_cast<uint8_t>(Opcode::BcAddVV));
        TValue lhs = StackFrameHeader::GetLocal(stackframe, bc->m_lhs);
        TValue rhs = StackFrameHeader::GetLocal(stackframe, bc->m_rhs);
        if (likely(lhs.IsDouble(TValue::x_int32Tag) && rhs.IsDouble(TValue::x_int32Tag)))
        {
            *StackFrameHeader::GetLocalAddr(stackframe, bc->m_result) = TValue::CreateDouble(lhs.AsDouble() + rhs.AsDouble());
            Dispatch(rc, stackframe, bcu + sizeof(BcAddVV));
        }
        else
        {
            assert(false && "unimplemented");
        }
    }
} __attribute__((__packed__));

class BcSubVV
{
public:
    uint8_t m_opcode;
    BytecodeSlot m_lhs;
    BytecodeSlot m_rhs;
    BytecodeSlot m_result;

    static void Execute(CoroutineRuntimeContext* rc, RestrictPtr<void> stackframe, ConstRestrictPtr<uint8_t> bcu, uint64_t /*unused*/)
    {
        const BcSubVV* bc = reinterpret_cast<const BcSubVV*>(bcu);
        assert(bc->m_opcode == static_cast<uint8_t>(Opcode::BcSubVV));
        TValue lhs = StackFrameHeader::GetLocal(stackframe, bc->m_lhs);
        TValue rhs = StackFrameHeader::GetLocal(stackframe, bc->m_rhs);
        if (likely(lhs.IsDouble(TValue::x_int32Tag) && rhs.IsDouble(TValue::x_int32Tag)))
        {
            *StackFrameHeader::GetLocalAddr(stackframe, bc->m_result) = TValue::CreateDouble(lhs.AsDouble() - rhs.AsDouble());
            Dispatch(rc, stackframe, bcu + sizeof(BcSubVV));
        }
        else
        {
            assert(false && "unimplemented");
        }
    }
} __attribute__((__packed__));

class BcIsLTVV
{
public:
    uint8_t m_opcode;
    BytecodeSlot m_lhs;
    BytecodeSlot m_rhs;
    int32_t m_offset;

    static void Execute(CoroutineRuntimeContext* rc, RestrictPtr<void> stackframe, ConstRestrictPtr<uint8_t> bcu, uint64_t /*unused*/)
    {
        const BcIsLTVV* bc = reinterpret_cast<const BcIsLTVV*>(bcu);
        assert(bc->m_opcode == static_cast<uint8_t>(Opcode::BcIsLTVV));
        TValue lhs = StackFrameHeader::GetLocal(stackframe, bc->m_lhs);
        TValue rhs = StackFrameHeader::GetLocal(stackframe, bc->m_rhs);
        if (likely(lhs.IsDouble(TValue::x_int32Tag) && rhs.IsDouble(TValue::x_int32Tag)))
        {
            if (lhs.AsDouble() < rhs.AsDouble())
            {
                Dispatch(rc, stackframe, reinterpret_cast<ConstRestrictPtr<uint8_t>>(reinterpret_cast<intptr_t>(bcu) + bc->m_offset));
            }
            else
            {
                Dispatch(rc, stackframe, bcu + sizeof(BcIsLTVV));
            }
        }
        else
        {
            assert(false && "unimplemented");
        }
    }
} __attribute__((__packed__));

class BcConstant
{
public:
    uint8_t m_opcode;
    BytecodeSlot m_dst;
    TValue m_value;

    static void Execute(CoroutineRuntimeContext* rc, RestrictPtr<void> stackframe, ConstRestrictPtr<uint8_t> bcu, uint64_t /*unused*/)
    {
        const BcConstant* bc = reinterpret_cast<const BcConstant*>(bcu);
        assert(bc->m_opcode == static_cast<uint8_t>(Opcode::BcConstant));
        *StackFrameHeader::GetLocalAddr(stackframe, bc->m_dst) = bc->m_value;
        Dispatch(rc, stackframe, bcu + sizeof(BcConstant));
    }
} __attribute__((__packed__));

}   // namespace ToyLang
