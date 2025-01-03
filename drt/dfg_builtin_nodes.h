#pragma once

#include "common_utils.h"
#include "tvalue.h"

namespace dfg {

// The list of built-in DFG node types. Below are the explanation for each of the node type.
//
// Constant:
//     Input: none
//     Output: 1, the constant value
//     Param: the TValue constant
//
//     Represents a constant boxed value, never put into a basic block.
//
//     Note that the NodeSpecificData of this node stores an int64_t, the ordinal in the constant table,
//     which is initially unassigned, and must be manually assigned later by a pass.
//     The value of the constant is stored inside the node (by repurposing an input slot), not inside the NodeSpecificData.
//
// UnboxedConstant:
//     Input: none
//     Output: 1, the unboxed constant value
//     Param: a uint64_t integer
//
//     Represents a raw unboxed 64-bit constant value, never put into a basic block.
//
//     Similar to Constant, the NodeSpecificData of this node stores an int64_t, the ordinal in the constant table,
//     which is initially unassigned, and must be manually assigned later by a pass.
//     The value of the constant is stored inside the node (by repurposing an input slot), not inside the NodeSpecificData.
//
// UndefValue:
//     Input: none
//     Output: 1, an uninitialized value
//     Param: none
//
//     Represents an uninitialized value, never put into a basic block
//
// Argument:
//     Input: none
//     Output: 1, the argument value
//     Param: the argument ordinal
//
//     Represents the value of an function argument, never put into a basic block
//
// GetNumVariadicArgs:
//     Input: none
//     Output: 1, an unboxed uint64_t
//     Param: none.
//
//     Returns the number of variadic arguments of the root function, never put into a basic block.
//
// GetKthVariadicArg:
//     Input: none
//     Output: 1, the value of the variadic argument, or nil if k is too large
//     Param: k.
//
//     Returns the k-th variadic argument of the root function.
//
// GetFunctionObject:
//     Input: none
//     Output: 1, an unboxed HeapPtr
//     Param: none
//
//     Returns the function object of the root function as an unboxed HeapPtr, never put into a basic block.
//
// Nop:
//     Input: >=0
//     Output: none
//     Param: none
//
//     The node itself does nothing.
//     However, a Nop node may take any number of input edges, and those edges may require checks (just like other nodes).
//     (Input edges that does not require a check can be freely removed)
//     Therefore, a Nop is only truly a no-op if it doesn't have any input edge that requires a check.
//
// GetLocal:
//     Input: none
//     Output: 1, the value stored in the local
//     Param: the DFG local ordinal
//
//     Get the value of the specified DFG local variable.
//
//     Standard load-store forwarding rules apply:
//     1. A GetLocal preceded by another GetLocal can be replaced by the earlier GetLocal.
//     2. A GetLocal preceded by a SetLocal can be replaced by the value stored by the SetLocal.
//     3. A GetLocal with no users can be removed.
//
//     The NodeSpecificData of GetLocal is the physical slot assigned to the local for the DFG frame,
//     which is only set up by a DFG pass at a very late point in the pipeline before code generation.
//
// SetLocal:
//     Input: 1, the value to store
//     Output: none
//     Param: the DFG local ordinal
//
//     Store the value into the specified DFG local variable.
//     The interpreter slot corresponding to this DFG local must contain the same value at this time point.
//
//     If a SetLocal is followed by another SetLocal in the same BB, the SetLocal can be removed (of course, you need to
//     replace all the GetLocals that sees this SetLocal to the value stored by this SetLocal).
//
//     A dead SetLocal that is the last SetLocal to a local in a BB can only be removed if the interpreter slot
//     corresponding to the DFG local is dead at the head of every successor block.
//     Precisely, "slot" is dead at the BeforeUse point of bytecode m_bcForInterpreterStateAtBBStart for every successor block.
//
//     See ShadowStore for why this must be the case.
//
//     The NodeSpecificData of SetLocal is the physical slot assigned to the local for the DFG frame,
//     which is only set up by a DFG pass at a very late point in the pipeline before code generation.
//
// ShadowStore:
//     Input: 1, the value to store
//     Output: none
//     Param: the interpreter stack slot ordinal
//
//     A ShadowStore represents an imaginary store to the imaginary interpreter stack frame.
//     That is, a ShadowStore of value V to slot X states that, if we were to OSR exit right now,
//     slot X in the interpreter stack frame needs to have value V.
//
//     Therefore, a ShadowStore generates no code, but only an OSR event in the OSR stackmap.
//
//     At the end of any basic block, it is required that for each *live* interpreter slot X (a slot is live
//     if it is live at the head of any successor block), the value in slot X of the imaginary interpreter stack
//     frame (as set by ShadowStore) is equal to the value in the DFG local corresponding to slot X (as set by SetLocal).
//
//     The DFG frontend will always generate a SetLocal after a ShadowStore, which satisfies this invariant.
//     To maintain this invariant, one may only remove a SetLocal if the corresponding interpreter slot is dead at the
//     head of every successor basic block.
//
// ShadowStoreUndefToRange:
//     Input: none
//     Output: none
//     Param: a range of interpreter stack slots
//
//     Equivalent to ShadowStore(UndefValue) to every interpreter slot in the given range.
//     This is used in favor of a bunch of ShadowStores to reduce the total number of IR nodes.
//
// Phantom:
//     Input: 1
//     Output: none
//     Param: none
//
//     States that the input SSA value is potentially required for the purpose of OSR exit until this point,
//     so the SSA value must be kept available in the machine state until this point.
//
//     A Phantom generates no code nor OSR event, it is merely needed so that register allocation knows when
//     it is truly safe to free a SSA value.
//
//     Phantoms are systematically and automatically inserted at the beginning of the backend pipeline, so
//     the frontend and optimizer do not need to think about this node.
//
// CreateCapturedVar:
//     Input: 1, the initial value of the captured variable
//     Output: 1, the created CapturedVar object, which is a closed Upvalue (which is required. It must
//                be interoperable with the lower tiers since the lower tier logic could access it)
//     Param: a CapturedVarInfo
//
//     Create a CapturedVar object. See CapturedVarInfo for more detail.
//
// GetCapturedVar:
//     Input: 1, the CapturedVar object
//     Output: 1, the value stored in the CapturedVar object
//     Param: none
//
//     Get the value stored in the CapturedVar object.
//
// SetCapturedVar:
//     Input: 2, the CapturedVar object and the value to store
//     Output: none
//     Param: none
//
//     Store the value into the CaptureVar object
//
// GetKthVariadicRes:
//     Input: VariadicResults
//     Output: 1, the kth value in the variadic results, or nil if k is too large.
//     Param: k, the ordinal to access
//
//     Get the kth value in the variadic results, or nil if k is too large.
//     This node will only show up due to speculatively inlining calls that passes variadic results as arguments.
//
// GetNumVariadicRes:
//     Input: VariadicResults
//     Output: 1, an unboxed uint64_t value, the # of variadic results
//     Param: none
//
//     Get the number of variadic results.
//     This node will only show up due to speculatively inlining calls that passes variadic results as arguments.
//
// CreateVariadicRes:
//     Input: >=1. Input #0 must be an unboxed integer.
//     Output: VariadicResults
//     Param: k, the number of fixed values
//
//     Get a varadic result consists of input [1, k + #0], #0 must be an unboxed uint64_t value that represents a valid index.
//     This node will only show up due to speculatively inlining calls that passes variadic results as arguments or
//     returns variadic results. Due to how this node is used, it is guaranteed that all the input nodes in range
//     [k + 1, end) are GetKthVariadicRes nodes and #0 is the actual number of return values, so [k + #0 + 1, end) must
//     all be nil values.
//
// PrependVariadicRes:
//     Input: >=0 values, VariadicResults
//     Output: VariadicResults
//     Param: none
//
//     Get a variadic result by prepending values to the existing variadic result.
//     A PrependVariadicRes with no input and a nullptr VariadicResults is also used to "initialize" the variadic
//     result if no preceding node produces a varaidic result in this basic block (which means the variadic result
//     is created by nodes in the control flow predecessor)
//
// CheckU64InBound:
//     Input: an unboxed uint64_t value
//     Output: none
//     Param: a uint64_t value "bound".
//
//     Check if input <= bound. If not, causes an OSR exit.
//
// I64SubSaturateToZero:
//     Input: an unboxed int64_t value "input"
//     Output: an unboxed int64_t value
//     Param: a int64_t value "valToSub"   <-- note that the operands are int64_t, not uint64_t!
//
//     Returns 0 if input < valToSub, otherwise returns input - valToSub.
//     Due to how we represent constants right now, we currently require that valToSub is in [-10^9, 10^9].
//     This is fine for our use case since this node is only used to compute vararg size during speculative inlining.
//
// CreateFunctionObject:
//     Input: >=2 (see below).
//     Output: 1, the newly created function object as a boxed value
//     Param: a uint64_t value k (see below)
//
//     Creates a new function object.
//     Input 0 is the parent function object.
//     Input 1 is the prototype (an unboxed value, the UnlinkedCodeBlock pointer)
//     The rest of the inputs are all the values captured in the parent stack frame, in the same order as what is described in
//     the UnlinkedCodeBlock's metadata. There is one exception though: if the capture is an immutable self-reference, the value
//     will not show up in the input (since its value should be the result of the CreateFunctionObject, which is a chicken-and-egg
//     problem). In that case, the param 'k' will indicate that the k-th upvalue in the UnlinkedCodeBlock's metadata is a
//     self-reference. Otherwise, 'k' is -1.
//
// GetUpvalueImmutable:
//     Input: 1, the function object (an unboxed HeapPtr) to retrieve the upvalue from
//     Output: 1, the value of the requested upvalue
//     Param: the upvalue ordinal.
//
//     Get the value of an upvalue from the given function object. The upvalue must be an immutable upvalue.
//
// GetUpvalueMutable:
//     Input: 1, the function object (an unboxed HeapPtr) to retrieve the upvalue from
//     Output: 1, the value of the requested upvalue
//     Param: the upvalue ordinal.
//
//     Get the value of an upvalue from the given function object. The upvalue must be a mutable upvalue.
//
// SetUpvalue:
//     Input: 2, the function object (an unboxed HeapPtr), and the value to put
//     Output: 0
//     Param: the upvalue ordinal
//
//     Stores the value into the given upvalue of the given function object.
//
// Return:
//     Input: any number of nodes, potentially VariadicResults as well
//     Output: 0
//     Param: none
//
//     Function return with the input nodes as return values, also appending variadic results if given.
//
// Phi:
//     This is not a Node! If nodeKind is Phi, it means that this object actually has type Phi
//     Phi nodes are auxillary. They are meant to supplement information but are safe to drop.
//
//
// The list of constant-like nodes. These nodes never show up in the basic block, and are uniqued.
// These nodes must come first in the NodeKind enum
//
//  NodeName                        NodeSpecificDataTy          IsNsdInlined      UsesCustomCodegen
#define DFG_CONSTANT_LIKE_NODE_KIND_LIST                                                            \
      (Constant                   , int64_t                   , true            , false)            \
    , (UnboxedConstant            , int64_t                   , true            , false)            \
    , (UndefValue                 , void                      , true            , true)             \
    , (Argument                   , uint64_t                  , true            , false)            \
    , (GetNumVariadicArgs         , void                      , true            , false)            \
    , (GetKthVariadicArg          , uint64_t                  , true            , false)            \
    , (GetFunctionObject          , void                      , true            , false)            \

// The list of all DFG builtin nodes (see above)
//
#define DFG_BUILTIN_NODE_KIND_LIST                                                                  \
    DFG_CONSTANT_LIKE_NODE_KIND_LIST                                                                \
    , (Nop                        , void                      , true            , true)             \
    , (GetLocal                   , uint64_t                  , true            , false)            \
    , (SetLocal                   , uint64_t                  , true            , false)            \
    , (ShadowStore                , uint64_t                  , true            , true)             \
    , (ShadowStoreUndefToRange    , Nsd_InterpSlotRange       , true            , true)             \
    , (Phantom                    , void                      , true            , true)             \
    , (CreateCapturedVar          , Nsd_CapturedVarInfo       , true            , false)            \
    , (GetCapturedVar             , void                      , true            , false)            \
    , (SetCapturedVar             , void                      , true            , false)            \
    , (GetKthVariadicRes          , uint64_t                  , true            , false)            \
    , (GetNumVariadicRes          , void                      , true            , false)            \
    , (CreateVariadicRes          , uint64_t                  , true            , true)             \
    , (PrependVariadicRes         , void                      , true            , true)             \
    , (CheckU64InBound            , uint64_t                  , true            , false)            \
    , (I64SubSaturateToZero       , int64_t                   , true            , false)            \
    , (CreateFunctionObject       , uint64_t                  , true            , true)             \
    , (GetUpvalueImmutable        , Nsd_UpvalueInfo           , true            , false)            \
    , (GetUpvalueMutable          , Nsd_UpvalueInfo           , true            , false)            \
    , (SetUpvalue                 , uint64_t                  , true            , false)            \
    , (Return                     , void                      , true            , true)             \
    , (Phi                        , void                      , true            , true)             \

// The defintions of the NodeSpecificData structs used by the builtin nodes
//
// A range of interpreter slots, used by ShadowStoreUndefToRange
//
struct Nsd_InterpSlotRange
{
    uint32_t m_slotStart;
    uint32_t m_numSlots;
};

// Some notes about Captured Variables:
//
// Lua's upvalue mechanism is handy for interpreter and baseline JIT, but very problematic for optimizing JIT.
// The bytecode does not distinguish normal local variables and local variables that are captured by upvalues.
// While this is simple and handy for the interpreter and the baseline JIT, this means that after any innocent
// operation (which can almost always make a call in a slow path), all the local variables that are captured by
// a upvalue may change value, which is very bad for us to reason about the code.
//
// For example, even for simple code 'a + b + a' where 'a' is captured by a upvalue:
//     %1 = GetLocal('a')
//     %2 = GetLocal('b')
//     %3 = Add(%1, %2)
//     %4 = GetLocal('a')
//     %5 = Add(%3, %4)
//
// '%1' doesn't necessarily alias with '%4' since 'a' might change value as 'a + b' may result in a call that can
// change the value of 'a'!
//
// As such, it's necessary to distinguish "normal local variables" and "local variables captured by a upvalue" in the
// optimizing JIT.
//
// So in the optimizing JIT, we use CapturedVar to implement upvalue.
//
// CaptureVar and upvalues are very similar, except with one difference: the source of truth of a CapturedVar is always
// stored inside the upvalue (never on the stack), even when the upvalue is still open. It is also not put into the
// open upvalue list.
//
// That is, even if accessed from the main function (i.e., where the upvalue is defined), we still must use LoadCapturedVar
// to access its value.
//
// So the code 'a + b + a + b' where 'a' is captured by a upvalue while 'b' is not would generate the following code:
//     %1 = GetClosureVar('a')
//     %2 = GetLocal('b')
//     %3 = Add(%1, %2)
//     %4 = GetClosureVar('a')
//     %5 = Add(%3, %4)
//     %6 = GetLocal('b')
//     %7 = Add(%5, %7)
//
// Adn we can reason (as one would normally expect) that '%6' must alias with '%2' (as they both GetLocal 'b' and there is
// no SetLocal 'b' in between) but '%1' doesn't necessarily alias with '%4' (as the 'Add' executed in between may result
// in a call, unless we speculate that the call doesn't happen).
//
// At implementation level, a CapturedVar is just a closed upvalue, so outside code (i.e. code outside the function being
// compiled) can still access the upvalue normally (as they don't care if the upvalue is closed or not). For logic inside the
// function being compiled, we need to properly generate GetLocal/GetClosureVar depending on whether the local is being
// captured or not, and UpvalueClose is now a no-op (since the CaptureVar does not live on the open upvalue list). When
// an OSR-exit happens, we also need to restore the stack frame and upvalue linked list to what is expected by the lower tiers.
//
struct Nsd_CapturedVarInfo
{
    // When an OSR-exit happens, we must flush the values of the still-opening CapturedVar back to the stack frame.
    //
    uint32_t m_localOrdForOsrExit;
};
static_assert(sizeof(Nsd_CapturedVarInfo) <= 8);

struct Nsd_UpvalueInfo
{
    uint32_t m_ordinal;
};
static_assert(sizeof(Nsd_UpvalueInfo) <= 8);

struct Phi;

// DFG NodeKind
// Built-in node kinds come first (and constant-like node kinds comes first within built-in node kinds)
// All guest language node kinds come after the built-in node kinds
//
enum NodeKind : uint16_t
{
#define macro(e) PP_CAT(NodeKind_, PP_TUPLE_GET_1(e)) ,
    PP_FOR_EACH(macro, DFG_BUILTIN_NODE_KIND_LIST)
#undef macro
    NodeKind_FirstAvailableGuestLanguageNodeKind
};

inline const char* GetDfgBuiltinNodeKindName(NodeKind kind)
{
    TestAssert(kind < NodeKind_FirstAvailableGuestLanguageNodeKind);
    switch (kind)
    {
#define macro(e) case PP_CAT(NodeKind_, PP_TUPLE_GET_1(e)): return PP_STRINGIFY(PP_TUPLE_GET_1(e));
        PP_FOR_EACH(macro, DFG_BUILTIN_NODE_KIND_LIST)
#undef macro
    default: __builtin_unreachable();
    }
}

constexpr size_t x_numTotalDfgBuiltinNodeKinds = NodeKind_FirstAvailableGuestLanguageNodeKind;

// We use uint64_t bitmask to represent per-builtin-node-kind boolean information
//
static_assert(x_numTotalDfgBuiltinNodeKinds <= 64);

namespace detail {

template<NodeKind nodeKind> struct BuiltinNodeNsdTyHelper;

#define macro(e) template<> struct BuiltinNodeNsdTyHelper<PP_CAT(NodeKind_, PP_TUPLE_GET_1(e))> { using type = PP_TUPLE_GET_2(e); };
PP_FOR_EACH(macro, DFG_BUILTIN_NODE_KIND_LIST)
#undef macro

}   // namespace detail

template<NodeKind nodeKind>
using dfg_builtin_node_nsd_t = typename detail::BuiltinNodeNsdTyHelper<nodeKind>::type;

namespace detail {

struct BuiltInNodeKindMaskWithInlinedNsd
{
    static constexpr uint64_t mask = []() {
        uint64_t res = 0;
#define macro(e)    \
        if (PP_TUPLE_GET_3(e)) { res |= static_cast<uint64_t>(1) << (PP_CAT(NodeKind_, PP_TUPLE_GET_1(e))); }
        PP_FOR_EACH(macro, DFG_BUILTIN_NODE_KIND_LIST)
#undef macro
        return res;
    }();
};

template<typename T>
struct BuiltinNodeKindsWithInlinedNsdOfType
{
    static constexpr uint64_t mask = []() {
        uint64_t res = 0;
#define macro(e)    \
        if ((PP_TUPLE_GET_3(e)) && std::is_same_v<PP_TUPLE_GET_2(e), T>) { res |= static_cast<uint64_t>(1) << (PP_CAT(NodeKind_, PP_TUPLE_GET_1(e))); }
        PP_FOR_EACH(macro, DFG_BUILTIN_NODE_KIND_LIST)
#undef macro
        return res;
    }();
};

template<typename T>
struct BuiltinNodeKindsWithNsdOfType
{
    static constexpr uint64_t mask = []() {
        uint64_t res = 0;
#define macro(e)    \
        if (std::is_same_v<PP_TUPLE_GET_2(e), T>) { res |= static_cast<uint64_t>(1) << (PP_CAT(NodeKind_, PP_TUPLE_GET_1(e))); }
        PP_FOR_EACH(macro, DFG_BUILTIN_NODE_KIND_LIST)
#undef macro
        return res;
    }();
};

struct BuiltinNodeKindsWithNsd
{
    static constexpr uint64_t mask = []() {
        uint64_t res = 0;
#define macro(e)    \
        if (!std::is_same_v<PP_TUPLE_GET_2(e), void>) { res |= static_cast<uint64_t>(1) << (PP_CAT(NodeKind_, PP_TUPLE_GET_1(e))); }
        PP_FOR_EACH(macro, DFG_BUILTIN_NODE_KIND_LIST)
#undef macro
        return res;
    }();
};

struct BuiltinNodeKindUsesCustomCodegen
{
    static constexpr uint64_t mask = []() {
        uint64_t res = 0;
#define macro(e)    \
        if (PP_TUPLE_GET_4(e)) { res |= static_cast<uint64_t>(1) << (PP_CAT(NodeKind_, PP_TUPLE_GET_1(e))); }
        PP_FOR_EACH(macro, DFG_BUILTIN_NODE_KIND_LIST)
#undef macro
        return res;
    }();
};

}   // namespace detail

// Returns if a builtin node kind has nsd
// The nodeKind must be a builtin node kind!
//
constexpr bool WARN_UNUSED DfgBuiltinNodeHasNsd(NodeKind nodeKind)
{
    TestAssert(nodeKind < NodeKind_FirstAvailableGuestLanguageNodeKind);
    constexpr uint64_t mask = detail::BuiltinNodeKindsWithNsd::mask;
    return mask & (static_cast<uint64_t>(1) << nodeKind);
}

// Returns if a builtin node kind has inlined nsd
// The nodeKind must be a builtin node kind!
//
constexpr bool WARN_UNUSED DfgBuiltinNodeHasInlinedNsd(NodeKind nodeKind)
{
    TestAssert(nodeKind < NodeKind_FirstAvailableGuestLanguageNodeKind);
    constexpr uint64_t mask = detail::BuiltInNodeKindMaskWithInlinedNsd::mask;
    return mask & (static_cast<uint64_t>(1) << nodeKind);
}

template<typename T>
constexpr bool WARN_UNUSED DfgNodeIsBuiltinNodeWithInlinedNsdType(NodeKind nodeKind)
{
    if (nodeKind >= NodeKind_FirstAvailableGuestLanguageNodeKind)
    {
        return false;
    }
    constexpr uint64_t mask = detail::BuiltinNodeKindsWithInlinedNsdOfType<T>::mask;
    return mask & (static_cast<uint64_t>(1) << nodeKind);
}

template<typename T>
constexpr bool WARN_UNUSED DfgNodeIsBuiltinNodeWithNsdType(NodeKind nodeKind)
{
    if (nodeKind >= NodeKind_FirstAvailableGuestLanguageNodeKind)
    {
        return false;
    }
    constexpr uint64_t mask = detail::BuiltinNodeKindsWithNsdOfType<T>::mask;
    return mask & (static_cast<uint64_t>(1) << nodeKind);
}

constexpr bool WARN_UNUSED DfgBuiltinNodeUseCustomCodegenImpl(NodeKind nodeKind)
{
    TestAssert(nodeKind < NodeKind_FirstAvailableGuestLanguageNodeKind);
    constexpr uint64_t mask = detail::BuiltinNodeKindUsesCustomCodegen::mask;
    return mask & (static_cast<uint64_t>(1) << nodeKind);
}

// The enum of all the custom builtin node codegen function identifiers
//
enum class DfgBuiltinNodeCustomCgFn
{
    CreateVariadicRes_StoreInfo,
    PrependVariadicRes_MoveAndStoreInfo,
    CreateFunctionObject_AllocAndSetup,
    CreateFunctionObject_BoxFunctionObject,
    Return_MoveVariadicRes,
    Return_RetWithVariadicRes,
    Return_WriteNil,
    Return_RetNoVariadicRes,
    Return_Ret1,
    Return_Ret0,
    X_END_OF_ENUM
};

}   // namespace dfg
