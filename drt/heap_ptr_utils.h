#pragma once

#include "common_utils.h"

template<typename T>
using RestrictPtr = T* __restrict__;

template<typename T>
using ConstRestrictPtr = RestrictPtr<const T>;

#define CLANG_GS_ADDRESS_SPACE_IDENTIFIER 256
#define CLANG_FS_ADDRESS_SPACE_IDENTIFIER 257

#define CLANG_ADDRESS_SPACE_IDENTIFIER_FOR_HEAP_PTR CLANG_GS_ADDRESS_SPACE_IDENTIFIER

template<typename T>
using HeapPtr = T __attribute__((address_space(CLANG_ADDRESS_SPACE_IDENTIFIER_FOR_HEAP_PTR))) *;

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

// Utility for accessing unaligned data
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

template<typename T>
using Packed = __attribute__((__packed__, __aligned__(1))) T;

// A severe limitation with HeapPtr classes is that it cannot call member method (since C++ always assumes that the hidden 'this' pointer is
// in the normal address space, but our HeapPtr is in the GS address space, which cannot be converted to normal address space automatically)
// However, in many cases we are just using class as a wrapper for some word-sized data. In this case it's trivial to make a copy of the class,
// so we can workaround this limitation.
//
// This is what the 'TCGet' and 'TCSet' (TC stands for TriviallyCopyable) is for.
//

template<typename T>
void ALWAYS_INLINE TCSet(T& target, T obj)
{
    static_assert(std::is_trivially_copyable_v<T>);
    target = obj;
}
