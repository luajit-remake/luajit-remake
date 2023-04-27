#pragma once

#include "common_utils.h"
#include "heap_object_common.h"

class VM;

// The whole point here is to not include "vm.h", so we have to hardcode the value
// However, the correctness of this value is static_asserted in vm.h
//
static constexpr uintptr_t x_segmentRegisterSelfReferencingOffset = 0;

// Same as VM::GetActiveVMForCurrentThread(), but no need to include vm.h
//
inline VM* VM_GetActiveVMForCurrentThread()
{
    return *reinterpret_cast<HeapPtr<VM*>>(x_segmentRegisterSelfReferencingOffset);
}

inline void AssertIsSpdsPointer(void* DEBUG_ONLY(ptr))
{
#ifndef NDEBUG
    VM* vm = VM_GetActiveVMForCurrentThread();
    uintptr_t vmVoid = reinterpret_cast<uintptr_t>(vm);
    uintptr_t ptrVoid = reinterpret_cast<uintptr_t>(ptr);
    assert(vmVoid - (1ULL << 31) <= ptrVoid && ptrVoid < vmVoid);
#endif
}

inline void AssertIsSystemHeapPointer(void* DEBUG_ONLY(ptr))
{
#ifndef NDEBUG
    VM* vm = VM_GetActiveVMForCurrentThread();
    uintptr_t vmVoid = reinterpret_cast<uintptr_t>(vm);
    uintptr_t ptrVoid = reinterpret_cast<uintptr_t>(ptr);
    assert(vmVoid <= ptrVoid && ptrVoid < vmVoid + (1ULL << 31));
#endif
}

inline void AssertIsSpdsOrSystemHeapPointer(void* DEBUG_ONLY(ptr))
{
#ifndef NDEBUG
    VM* vm = VM_GetActiveVMForCurrentThread();
    uintptr_t vmVoid = reinterpret_cast<uintptr_t>(vm);
    uintptr_t ptrVoid = reinterpret_cast<uintptr_t>(ptr);
    assert(vmVoid - (1ULL << 31) <= ptrVoid && ptrVoid < vmVoid + (1ULL << 31));
#endif
}

// This excludes null
//
inline void AssertIsUserHeapPointer(void* DEBUG_ONLY(ptr))
{
#ifndef NDEBUG
    VM* vm = VM_GetActiveVMForCurrentThread();
    uintptr_t vmVoid = reinterpret_cast<uintptr_t>(vm);
    uintptr_t ptrVoid = reinterpret_cast<uintptr_t>(ptr);
    assert(vmVoid - (16ULL << 30) <= ptrVoid && ptrVoid < vmVoid - (4ULL << 30));
#endif
}

inline void AssertIsValidHeapPointer(void* DEBUG_ONLY(ptr))
{
#ifndef NDEBUG
    VM* vm = VM_GetActiveVMForCurrentThread();
    uintptr_t vmVoid = reinterpret_cast<uintptr_t>(vm);
    uintptr_t ptrVoid = reinterpret_cast<uintptr_t>(ptr);
    assert((vmVoid - (16ULL << 30) <= ptrVoid && ptrVoid < vmVoid - (4ULL << 30)) ||
           (vmVoid - (1ULL << 31) <= ptrVoid && ptrVoid < vmVoid + (1ULL << 31)));
#endif
}

inline void AssertIsValidHeapPointer(HeapPtr<void> DEBUG_ONLY(ptr))
{
#ifndef NDEBUG
    intptr_t ptrVoid = reinterpret_cast<intptr_t>(ptr);
    assert((-(16LL << 30) <= ptrVoid && ptrVoid < -(4LL << 30)) ||
           (-(1LL << 31) <= ptrVoid && ptrVoid < (1LL << 31)));
#endif
}

template<typename T>
remove_heap_ptr_t<T> TranslateToRawPointer(T ptr);

constexpr uint32_t x_vmBasePtrLog2Alignment = 35;

template<typename T>
add_heap_ptr_t<T> TranslateToHeapPtr(T ptr)
{
    if constexpr(IsHeapPtrType<T>::value)
    {
        AssertIsValidHeapPointer(ptr);
        return ptr;
    }
    else
    {
        AssertIsValidHeapPointer(ptr);
        uintptr_t val = reinterpret_cast<uintptr_t>(ptr);
        constexpr uint32_t shift = 64 - x_vmBasePtrLog2Alignment;
        val = SignExtendedShiftRight(val << shift, shift);
        add_heap_ptr_t<T> r = reinterpret_cast<add_heap_ptr_t<T>>(val);
        assert(TranslateToRawPointer(r) == ptr);
        return r;
    }
}

template<typename T = void>
struct UserHeapPointer
{
    UserHeapPointer() : m_value(0) { }

    template<typename U, typename = std::enable_if_t<std::is_same_v<T, void> || std::is_same_v<T, uint8_t> || std::is_same_v<U, void> || std::is_same_v<U, uint8_t> || std::is_same_v<T, U>>>
    UserHeapPointer(HeapPtr<U> value)
        : m_value(reinterpret_cast<intptr_t>(value))
    {
        static_assert(TypeMayLiveInUserHeap<T>);
        assert(m_value == 0 || (x_minValue <= m_value && m_value <= x_maxValue));
        assert(As<U>() == value);
    }

    template<typename U, typename = std::enable_if_t<std::is_same_v<T, void> || std::is_same_v<T, uint8_t> || std::is_same_v<U, void> || std::is_same_v<U, uint8_t> || std::is_same_v<T, U>>>
    UserHeapPointer(U* value)
        : UserHeapPointer(TranslateToHeapPtr(value))
    {
        assert(TranslateToRawPointer(As<U>()) == value);
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
            // UserHeapPointer should only come from boxed values, so they should never be nullptr
            //
            assert(AsNoAssert<UserHeapGcObjectHeader>()->m_type == TypeEnumForHeapObject<U>);
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
    SystemHeapPointer(U* value)
        : SystemHeapPointer(BitwiseTruncateTo<uint32_t>(reinterpret_cast<uintptr_t>(value)))
    {
        AssertIsSystemHeapPointer(value);
    }

    template<typename U, typename = std::enable_if_t<std::is_same_v<T, void> || std::is_same_v<T, uint8_t> || std::is_same_v<U, void> || std::is_same_v<U, uint8_t> || std::is_same_v<T, U>>>
    SystemHeapPointer(HeapPtr<U> value)
        : SystemHeapPointer(SafeIntegerCast<uint32_t>(reinterpret_cast<intptr_t>(value)))
    {
        assert(As<U>() == value);
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
            static_assert(std::is_base_of_v<SystemHeapGcObjectHeader, U>);
            static_assert(offsetof_base_v<SystemHeapGcObjectHeader, U> == 0);
            AssertImp(m_value >= x_minimum_valid_heap_address, AsNoAssert<SystemHeapGcObjectHeader>()->m_type == TypeEnumForHeapObject<U>);
        }
        return AsNoAssert<U>();
    }

    bool WARN_UNUSED operator==(const SystemHeapPointer& rhs) const
    {
        return m_value == rhs.m_value;
    }

    uint32_t m_value;
};

template<typename... Types>
struct SystemHeapPointerTaggedUnion
{
private:
    static constexpr size_t x_numTypes = sizeof...(Types);
    static_assert(is_typelist_pairwise_distinct<Types...>);

    static constexpr uint32_t x_tagMask = RoundUpToPowerOfTwo(static_cast<uint32_t>(x_numTypes)) - 1;
    static_assert(is_power_of_2(x_tagMask + 1));

    template<typename T, size_t i>
    static constexpr uint32_t GetTagImpl()
    {
        constexpr size_t x_minAlignment = std::min({ std::alignment_of_v<Types>... });
        static_assert(x_minAlignment >= x_numTypes, "not enough spare bits to tag the pointer!");
        static_assert(i < x_numTypes, "Type T is not inside the union!");
        if constexpr(std::is_same_v<T, parameter_pack_nth_t<i, Types...>>)
        {
            return static_cast<uint32_t>(i);
        }
        else
        {
            return GetTagImpl<T, i + 1>();
        }
    }

    template<typename T>
    static constexpr uint32_t x_tagFor = GetTagImpl<T, 0>();

public:
    SystemHeapPointerTaggedUnion() : m_value(0) { }
    SystemHeapPointerTaggedUnion(uint32_t value) : m_value(value) { }

    template<typename T>
    SystemHeapPointerTaggedUnion(SystemHeapPointer<T> val)
        : m_value(val.m_value | x_tagFor<T>)
    {
        assert(val.m_value % std::alignment_of_v<T> == 0);
    }

    bool IsNullPtr() { return m_value == 0; }

    // DEVNOTE: before calling this function you must have checked for nullptr!
    //
    template<typename T>
    bool IsType()
    {
        assert(!IsNullPtr());
        return (m_value & x_tagMask) == x_tagFor<T>;
    }

    template<typename T>
    void Store(SystemHeapPointer<T> value)
    {
        assert(value.m_value % std::alignment_of_v<T> == 0);
        m_value = value.m_value | x_tagFor<T>;
    }

    template<typename T>
    HeapPtr<T> As()
    {
        assert(IsType<T>());
        return SystemHeapPointer<T> { m_value ^ x_tagFor<T> }.As();
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
        : GeneralHeapPointer(SafeIntegerCast<int32_t>(SignExtendedShiftRight(reinterpret_cast<intptr_t>(value), x_shiftFromRawOffset)))
    {
        assert(As<U>() == value);
    }

    template<typename U, typename = std::enable_if_t<std::is_same_v<T, void> || std::is_same_v<T, uint8_t> || std::is_same_v<U, void> || std::is_same_v<U, uint8_t> || std::is_same_v<T, U>>>
    GeneralHeapPointer(U* value)
        : GeneralHeapPointer(BitwiseTruncateTo<int32_t>(SignExtendedShiftRight(reinterpret_cast<intptr_t>(value), x_shiftFromRawOffset)))
    {
        assert(TranslateToRawPointer(As<U>()) == value);
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

        HeapPtr<U> r = AsNoAssert<U>();
        if constexpr(!std::is_same_v<U, void>)
        {
            assert(reinterpret_cast<uintptr_t>(r) % std::alignment_of_v<U> == 0);
        }
        if constexpr(IsHeapObjectType<U>)
        {
            if constexpr(TypeMayLiveInSystemHeap<U>)
            {
                assert(static_cast<int32_t>(x_minimum_valid_heap_address >> x_shiftFromRawOffset) <= m_value && m_value <= x_posMaxValue);
                assert(reinterpret_cast<HeapPtr<SystemHeapGcObjectHeader>>(r)->m_type == TypeEnumForHeapObject<U>);
            }
            else
            {
                static_assert(TypeMayLiveInUserHeap<U>);
                assert(x_negMinValue <= m_value && m_value <= x_negMaxValue);
                assert(reinterpret_cast<HeapPtr<UserHeapGcObjectHeader>>(r)->m_type == TypeEnumForHeapObject<U>);
            }
        }
        return r;
    }

    bool WARN_UNUSED operator==(const GeneralHeapPointer& rhs) const
    {
        return m_value == rhs.m_value;
    }

    bool WARN_UNUSED IsUserHeapPointer() const { return m_value < 0; }

    static constexpr uint32_t x_shiftFromRawOffset = 3;
    static constexpr int32_t x_negMinValue = static_cast<int32_t>(0x80000000);
    static constexpr int32_t x_negMaxValue = static_cast<int32_t>(0xDFFFFFFF);
    static constexpr int32_t x_posMaxValue = static_cast<int32_t>(0x1FFFFFFF);

    int32_t m_value;
};

template<typename T>
struct SpdsPtr
{
    SpdsPtr() : m_value(0) { }
    SpdsPtr(int32_t value)
        : m_value(value)
    {
        if constexpr(!std::is_same_v<T, void>)
        {
            assert(m_value % static_cast<int32_t>(alignof(T)) == 0);
        }
        assert(m_value < static_cast<int32_t>(x_minimum_valid_heap_address));
    }

    // In our VM layout, the base pointer is at least 4GB aligned
    // so the lower 32 bits of a raw pointer in Spds region can be directly used as a SpdsPtr
    //
    SpdsPtr(T* rawPtr)
        : SpdsPtr(BitwiseTruncateTo<int32_t>(reinterpret_cast<intptr_t>(rawPtr)))
    {
        AssertIsSpdsPointer(rawPtr);
    }

    SpdsPtr(HeapPtr<T> heapPtr)
        : SpdsPtr(SafeIntegerCast<int32_t>(reinterpret_cast<intptr_t>(heapPtr)))
    { }

    bool IsInvalidPtr() const
    {
        return m_value >= 0;
    }

    HeapPtr<T> WARN_UNUSED ALWAYS_INLINE AsPtr() const
    {
        assert(!IsInvalidPtr());
        return reinterpret_cast<HeapPtr<T>>(static_cast<int64_t>(m_value));
    }

    template<typename Enable = T, typename = std::enable_if_t<std::is_same_v<Enable, T> && !std::is_same_v<T, void>>>
    HeapRef<Enable> WARN_UNUSED operator*() const
    {
        static_assert(std::is_same_v<Enable, T>);
        return *AsPtr();
    }

    HeapPtr<T> WARN_UNUSED operator->() const
    {
        return AsPtr();
    }

    bool WARN_UNUSED operator==(const SpdsPtr& rhs) const
    {
        return m_value == rhs.m_value;
    }

    int32_t m_value;
};

template<typename T>
struct SpdsOrSystemHeapPtr
{
    SpdsOrSystemHeapPtr() : m_value(0) { }
    SpdsOrSystemHeapPtr(int32_t value)
        : m_value(value)
    { }

    SpdsOrSystemHeapPtr(T* rawPtr)
        : SpdsOrSystemHeapPtr(BitwiseTruncateTo<int32_t>(reinterpret_cast<intptr_t>(rawPtr)))
    {
        AssertIsSpdsOrSystemHeapPointer(rawPtr);
    }

    SpdsOrSystemHeapPtr(HeapPtr<T> heapPtr)
        : SpdsOrSystemHeapPtr(SafeIntegerCast<int32_t>(reinterpret_cast<intptr_t>(heapPtr)))
    { }

    bool IsInvalidPtr() const
    {
        return 0 <= m_value && m_value < static_cast<int32_t>(x_minimum_valid_heap_address);
    }

    HeapPtr<T> WARN_UNUSED ALWAYS_INLINE AsPtr() const
    {
        assert(!IsInvalidPtr());
        return reinterpret_cast<HeapPtr<T>>(static_cast<int64_t>(m_value));
    }

    template<typename Enable = T, typename = std::enable_if_t<std::is_same_v<Enable, T> && !std::is_same_v<T, void>>>
    HeapRef<Enable> WARN_UNUSED operator*() const
    {
        static_assert(std::is_same_v<Enable, T>);
        return *AsPtr();
    }

    HeapPtr<T> WARN_UNUSED operator->() const
    {
        return AsPtr();
    }

    bool WARN_UNUSED operator==(const SpdsOrSystemHeapPtr& rhs) const
    {
        return m_value == rhs.m_value;
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

// Same as VM::GetActiveVMForCurrentThread()->GetHeapPtrTranslator(), but no need to include vm.h
//
inline HeapPtrTranslator VM_GetHeapPtrTranslatorBasedOnVMForCurrentThread()
{
    return HeapPtrTranslator { reinterpret_cast<uintptr_t>(VM_GetActiveVMForCurrentThread()) };
}

template<typename T>
remove_heap_ptr_t<T> TranslateToRawPointer(VM* vm, T ptr)
{
    if constexpr(IsHeapPtrType<T>::value)
    {
        HeapPtrTranslator translator { reinterpret_cast<uintptr_t>(vm) };
        return translator.TranslateToRawPtr(ptr);
    }
    else
    {
        static_assert(std::is_pointer_v<T>);
        return ptr;
    }
}

template<typename T>
remove_heap_ptr_t<T> TranslateToRawPointer(T ptr)
{
    if constexpr(IsHeapPtrType<T>::value)
    {
        return VM_GetHeapPtrTranslatorBasedOnVMForCurrentThread().TranslateToRawPtr(ptr);
    }
    else
    {
        static_assert(std::is_pointer_v<T>);
        return ptr;
    }
}
