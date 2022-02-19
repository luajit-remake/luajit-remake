#pragma once

#include "common.h"

namespace CommonUtils {

// From Itanium C++ ABI Section 2.3: [https://itanium-cxx-abi.github.io/cxx-abi/abi.html#member-pointers]
//     A pointer to member function is a pair as follows:
//     ptr:
//         For a non-virtual function, this field is a simple function pointer. (Under current base Itanium psABI conventions,
//         that is a pointer to a GP/function address pair.) For a virtual function, it is 1 plus the virtual table offset (in bytes)
//         of the function, represented as a ptrdiff_t. The value zero represents a NULL pointer, independent of the adjustment
//         field value below.
//
//     adj:
//          The required adjustment to this, represented as a ptrdiff_t.
//
//     It has the size, data size, and alignment of a class containing those two members, in that order.
//     (For 64-bit Itanium, that will be 16, 16, and 8 bytes respectively.)
//
class ItaniumMemFnPointer
{
public:
    template<typename MethPtr>
    ItaniumMemFnPointer(MethPtr p)
    {
        static_assert(std::is_member_function_pointer<MethPtr>::value, "Not a member function pointer");
        static_assert(sizeof(MethPtr) == sizeof(ptrdiff_t) * 2, "Itanium ABI member function pointer has unexpected size!");
        ptrdiff_t* raw = reinterpret_cast<ptrdiff_t*>(&p);
        m_ptr = raw[0];
        m_adj = raw[1];
    }

    bool WARN_UNUSED IsNullptr() const
    {
        return m_ptr == 0;
    }

    bool WARN_UNUSED IsVirtual() const
    {
        // The Itanium ABI specification allows a tricky way to check virtual-ness:
        // If the function is non-virtual, the ptr is function address (which is apparently at least aligned to 2 bytes).
        // Otherwise, the ptr is vtable offset (also apparently at least aligned to 2 bytes) + 1.
        // So we can distinguish between the two cases using the value of 'ptr' modulo 2.
        //
        return m_ptr % 2 != 0;
    }

    bool WARN_UNUSED HasNonzeroThisPointerAdjustment() const
    {
        TestAssert(!IsNullptr());
        return m_adj != 0;
    }

    ptrdiff_t GetThisPointerAdjustment() const
    {
        TestAssert(!IsNullptr());
        return m_adj;
    }

    // Returns true if the member function pointer may be casted to a plain pointer
    // and simply called by additionally passing 'this' as first parameter
    //
    bool WARN_UNUSED MayConvertToPlainPointer() const
    {
        return !IsNullptr() && !IsVirtual() && !HasNonzeroThisPointerAdjustment();
    }

    void* GetRawFunctionPointer() const
    {
        TestAssert(!IsVirtual());
        return reinterpret_cast<void*>(m_ptr);
    }

private:
    ptrdiff_t m_ptr;
    ptrdiff_t m_adj;
};

// A convenience function to get the function address for a class method.
// The returned function address may be used to call the function as if it were a free function.
//
// It only works for non-virtual method and when the adjustment to 'this' pointer is zero.
// If not, an assertion is fired.
//
template<typename MethPtr>
void* GetClassMethodPtr(MethPtr p)
{
    ItaniumMemFnPointer fp(p);
    TestAssert(fp.MayConvertToPlainPointer());
    return fp.GetRawFunctionPointer();
}

// Same as above, except also works for plain pointers
//
template<typename MethPtr>
void* GetClassMethodOrPlainFunctionPtr(MethPtr p)
{
    if constexpr(std::is_member_function_pointer_v<MethPtr>)
    {
        return GetClassMethodPtr(p);
    }
    else
    {
        static_assert(std::is_pointer_v<MethPtr>);
        return reinterpret_cast<void*>(p);
    }
}

}   // namespace CommonUtils
