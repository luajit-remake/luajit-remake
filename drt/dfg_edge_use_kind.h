#pragma once

#include "common_utils.h"
#include "tvalue.h"

namespace dfg {

enum UseKind : uint16_t
{
    // Must be first member
    // This is a boxed value, but no type assumption on the type of the value
    // Equivalent to: precond = tTop, check = tTop
    //
    UseKind_Untyped,
    // This is an unboxed pointer pointing to a closed Upvalue object
    //
    UseKind_KnownCapturedVar,
    // This is a value that is statically known to be an unboxed 64-bit integer
    //
    UseKind_KnownUnboxedInt64,
    // This edge is never reachable
    // Equivalent to: precond = tBottom
    //
    UseKind_Unreachable,
    // This edge always causes an OSR exit
    // Equivalent to: precond = tTop, check = tBottom
    //
    UseKind_AlwaysOsrExit,
    // Guest language use kinds start here, all built-in use kind must come before this
    //
    // The first proven (i.e., no runtime check needed) non-trivial use kind
    // Equivalent to precond = tXXX, check = tXXX where tXXX != tTop && tXXX != tBottom,
    // defined in the same order as x_list_of_type_speculation_mask_and_name
    //
    UseKind_FirstProvenUseKind,
    // The first use kind that requires non-trivial runtime check
    // Each corresponds to a type checker implementation
    //
    UseKind_FirstUnprovenUseKind = static_cast<uint16_t>(UseKind_FirstProvenUseKind + x_list_of_type_speculation_masks.size() - 2)
};

inline constexpr std::array<const char*, UseKind_FirstProvenUseKind> x_dfgEdgeUseKindBuiltinKindNames = {
    "UntypedUse",
    "KnownCapturedVarUse",
    "KnownUnboxedInt64Use",
    "UnreachableUse",
    "AlwaysOsrExitUse"
};

// For debug and testing use only
//
struct TypeCheckerMethodCostInfo
{
    TypeMaskTy m_precondMask;
    TypeMaskTy m_checkMask;
    size_t m_cost;
};

// Whether the UseKind requires non-trivial runtime check logic
// Note that this function returns false for UseKind_Unreachable and UseKind_AlwaysOsrExit
//
constexpr bool UseKindRequiresNonTrivialRuntimeCheck(UseKind useKind)
{
    return useKind >= UseKind_FirstUnprovenUseKind;
}

// x_list_of_type_speculation_mask_and_name[ord] is the real type mask
//
enum class TypeMaskOrd : uint16_t;

inline TypeMask GetTypeMaskFromOrd(TypeMaskOrd typeMaskOrd)
{
    TestAssert(static_cast<size_t>(typeMaskOrd) < x_list_of_type_speculation_masks.size());
    return x_list_of_type_speculation_masks[static_cast<size_t>(typeMaskOrd)];
}

}   // namespace dfg
