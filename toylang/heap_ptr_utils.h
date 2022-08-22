#pragma once

#include "common_utils.h"

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

template<typename T> struct IsHeapRefType : std::false_type { };
template<typename T> struct IsHeapRefType<HeapRef<T>> : std::true_type { };

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

template<typename T>
struct add_heap_ptr
{
    static_assert(std::is_pointer_v<T>);
    using type = HeapPtr<std::remove_pointer_t<T>>;
};

template<typename T>
struct add_heap_ptr<HeapPtr<T>>
{
    using type = T;
};

template<typename T>
using add_heap_ptr_t = typename add_heap_ptr<T>::type;

template<typename T>
struct remove_heap_ref
{
    static_assert(std::is_lvalue_reference_v<T>);
    using type = T;
};

template<typename T>
struct remove_heap_ref<HeapRef<T>>
{
    using type = T&;
};

template<typename T>
using remove_heap_ref_t = typename remove_heap_ref<T>::type;

// Utility for accessing unaligned data, both normal pointer and HeapPtr
//
template<typename T>
T UnalignedLoad(const void* ptr)
{
    static_assert(std::is_trivially_copyable_v<T>);
    T result { };
    memcpy(&result, ptr, sizeof(T));
    return result;
}

template<typename T>
void UnalignedStore(void* ptr, T value)
{
    static_assert(std::is_trivially_copyable_v<T>);
    memcpy(ptr, &value, sizeof(T));
}

// Unfortunately memcpy cannot work on HeapPtr
// But it seems like Clang still generates optimal code if we use a union
//
// DEVNOTE: This might be problematic with strict aliasing (since it seems like only 'memcpy' is
// exempted from strict aliasing, not anything else).. hopefully this doesn't break...
//
template<typename T>
T WARN_UNUSED ALWAYS_INLINE UnalignedLoad(HeapPtr<const void> ptr)
{
    static_assert(sizeof(unsigned char) == 1);
    static_assert(std::is_trivially_copyable_v<T>);
    union U {
        U() {}
        T obj;
        unsigned char asBytes[sizeof(T)];
    } u;

    HeapPtr<const unsigned char> pu = reinterpret_cast<HeapPtr<const unsigned char>>(ptr);
    for (size_t i = 0; i < sizeof(T); i++) u.asBytes[i] = pu[i];
    return u.obj;
}

template<typename T>
void ALWAYS_INLINE UnalignedStore(HeapPtr<void> ptr, T value)
{
    static_assert(sizeof(unsigned char) == 1);
    static_assert(std::is_trivially_copyable_v<T>);
    union U {
        U() {}
        T obj;
        unsigned char asBytes[sizeof(T)];
    } u;

    u.obj = value;
    HeapPtr<unsigned char> pu = reinterpret_cast<HeapPtr<unsigned char>>(ptr);
    for (size_t i = 0; i < sizeof(T); i++) pu[i] = u.asBytes[i];
}

template<typename T>
class Packed
{
public:
    static_assert(std::is_trivially_copyable_v<T>);

    Packed() : Packed(T { }) { }
    Packed(const T& value) { UnalignedStore<T>(m_storage, value); }

    uint8_t m_storage[sizeof(T)];
};

template<typename T>
struct is_packed_class_impl : std::false_type { };

template<typename T>
struct is_packed_class_impl<Packed<T>> : std::true_type { };

template<typename T>
struct is_packed_class_impl<const Packed<T>> : std::true_type { };

template<typename T>
constexpr bool is_packed_class_v = is_packed_class_impl<T>::value;

template<typename T>
struct remove_packed_impl { using type = std::remove_const_t<T>; };

template<typename T>
struct remove_packed_impl<Packed<T>> { using type = T; };

template<typename T>
struct remove_packed_impl<const Packed<T>> { using type = T; };

template<typename T>
using remove_packed_t = typename remove_packed_impl<T>::type;

// A severe limitation with HeapPtr classes is that it cannot call member method (since C++ always assumes that the hidden 'this' pointer is
// in the normal address space, but our HeapPtr is in the GS address space, which cannot be converted to normal address space automatically)
// However, in many cases we are just using class as a wrapper for some word-sized data. In this case it's trivial to make a copy of the class,
// so we can workaround this limitation.
//
// This is what the 'TCGet' and 'TCSet' (TC stands for TriviallyCopyable) is for.
//
template<typename T>
remove_packed_t<T> WARN_UNUSED ALWAYS_INLINE TCGet(T& obj)
{
    static_assert(std::is_trivially_copyable_v<T>);
    if constexpr(is_packed_class_v<T>)
    {
        return UnalignedLoad<remove_packed_t<T>>(obj.m_storage);
    }
    else
    {
        return obj;
    }
}

template<typename T>
remove_packed_t<T> WARN_UNUSED ALWAYS_INLINE TCGet(HeapRef<T> obj)
{
    static_assert(std::is_trivially_copyable_v<T>);
    if constexpr(is_packed_class_v<T>)
    {
        return UnalignedLoad<remove_packed_t<T>>(obj.m_storage);
    }
    else
    {
        return UnalignedLoad<T>(&obj);
    }
}

template<typename T>
void ALWAYS_INLINE TCSet(T& target, T obj)
{
    static_assert(std::is_trivially_copyable_v<T>);
    target = obj;
}

template<typename T>
void ALWAYS_INLINE TCSet(HeapRef<T> target, T obj)
{
    static_assert(std::is_trivially_copyable_v<T>);
    UnalignedStore<T>(&target, obj);
}

// Overloads for Packed classes
//
template<typename T>
void ALWAYS_INLINE TCSet(Packed<T>& target, T obj)
{
    static_assert(std::is_trivially_copyable_v<T>);
    UnalignedStore<T>(target.m_storage, obj);

}
template<typename T>
void ALWAYS_INLINE TCSet(HeapRef<Packed<T>> target, T obj)
{
    static_assert(std::is_trivially_copyable_v<T>);
    UnalignedStore<T>(target.m_storage, obj);
}
