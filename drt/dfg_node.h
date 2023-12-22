#pragma once

#include "common.h"
#include "tvalue.h"
#include "dfg_arena.h"
#include "dfg_logical_variable_info.h"
#include "dfg_virtual_register.h"
#include "dfg_code_origin.h"

namespace DeegenBytecodeBuilder {

// Forward declare to avoid including the whole bytecode_builder.h
//
enum class BCKind : uint8_t;

}   // namespace DeegenBytecodeBuilder

namespace dfg
{

using BCKind = DeegenBytecodeBuilder::BCKind;
extern const BCKind x_bcKindEndOfEnum;          // for assertion purpose only

// The list of built-in DFG node types. Below are the explanation for each of the node type.
//
// Constant:
//     Input: none
//     Output: 1, the constant value
//     Param: the TValue constant
//
//     Represents a constant boxed value, never put into a basic block
//
// UnboxedConstant:
//     Input: none
//     Output: 1, the unboxed constant value
//     Param: a uint64_t integer
//
//     Represents a raw unboxed 64-bit constant value, never put into a basic block
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
//     However, a Nop node may take any number of input edges, and those edges may require checks.
//     (Input edges that does not require a check can be freely removed)
//     Therefore, a Nop is only truly a no-op if it doesn't have any input edge that requires a check.
//
// GetLocal:
//     Input: none
//     Output: 1, the value stored in the local
//     Param: the local ordinal
//
//     Get the value of the specified local variable.
//
//     Standard load-store forwarding rules apply:
//     1. A GetLocal preceded by another GetLocal in the same BB can be replaced by the earlier GetLocal.
//     2. A GetLocal preceded by a SetLocal in the same BB can be replaced by the value stored by the SetLocal.
//
//     However, if a GetLocal is not preceded by another GetLocal/SetLocal in the same BB and has no user,
//     it *cannot* be removed, though one can turned it into a PhantomLocal. See PhantomLocal.
//
// SetLocal:
//     Input: 1, the value to store
//     Output: none
//     Param: the local ordinal
//
//     Store the value into the specified local variable.
//
//     A SetLocal(Val, virtualReg) must be preceded by a ShadowStore(Val, interpSlot) (with no other SetLocal to
//     virtualReg or ShadowStore to interpSlot in between) where interpSlot is the interpreter slot corresponding
//     to virtualReg in this InlinedCallFrame.
//
//     Dead stores (a SetLocal that cannot flow to any GetLocal or PhantomLocal) can always be removed.
//
// PhantomLocal:
//     Input: none
//     Output: none
//     Param: the local ordinal
//
//     It states that the current value of the local is needed for the purpose of OSR exit (so it must be
//     available in the machine state) until the end of this basic block, or until the next SetLocal to
//     this local, whichever is earlier.
//
//     Therefore, a PhantomLocal generates no code nor OSR event, but it must be treated as a load of the local,
//     so a SetLocal cannot be removed if it can flow to a PhantomLocal.
//
//     There are two creation rules of PhantomLocals:
//     1. A GetLocal with no user can be turned into to a PhantomLocal.
//     2. If a basic block is speculatively removed, for each live local at the head of the removed basic
//        block, a PhantomLocal must be added to the end of all the predecessor basic blocks. (More precisely,
//        if a local becomes dead at the tail of a predecessor block after the removal, a PhantomLocal must be added).
//
//     The semantics of PhantomLocal imply that it can be removed in many cases:
//     1. If a PhantomLocal is followed or preceded by a GetLocal or another PhantomLocal in the same BB (with
//        no SetLocal in between), the PhantomLocal can be safely removed. This is because the GetLocal is
//        already keeping the value of the local available inside the basic block, and GetLocal cannot be
//        deleted but only turn to PhantomLocal, and a basic block cannot be split.
//     2. If a PhantomLocal is preceded by a SetLocal in the same BB, the PhantomLocal can be removed. This is
//        because the value stored into the local is already available inside the basic block.
//     3. If a PhantomLocal can flow to another PhantomLocal or a GetLocal in data flow, it can be removed.
//        This is because any SetLocal that can flow to the PhantomLocal must also be able to flow to the other
//        PhantomLocal/GetLocal thus is kept alive.
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
//     If V is not a constant-like node, one of the following things must happen within the same basic
//     block afterwards, or the IR is illegal:
//     1. A SetLocal stores V to the VirtualRegister corresponding to interpreter slot X.
//     2. A Phantom(V) node show up. The last Phantom(V) in the basic block marks a bytecode death of slot X,
//        at which point the value of slot X in the interpreter stack becomes UndefValue.
//
//     Condition (2) can be relaxed a bit to avoid unnecessary Phantom nodes:
//     1. If there is another use of V after the last Phantom(V), all the Phantom(V) can be removed since IR
//        is already keeping V available until the last OSR exit point that may need it.
//     2. If there is no OSR exit point between ShadowStore and the last Phantom(V), all the Phantom(V) can
//        be removed for the same reason.
//
// Phantom:
//     Input: 1
//     Output: none
//     Param: none
//
//     States that the input SSA value is potentially required for the purpose of OSR exit until this point,
//     so the SSA value must be kept available in the machine state until this point. See ShadowStore.
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
//     Param: none
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
//     Input: 1, a constant C
//     Output: 1, the kth value in the variadic results, or the constant C if k is too large.
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
//     [k + 1, k + #0] are GetKthVariadicRes nodes, so [k + #0 + 1, end) must all be nil values.
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
// U64SaturateSub:
//     Input: an unboxed uint64_t value "input"
//     Output: an unboxed uint64_t value
//     Param: a int64_t value "valToSub"   <-- note that it's a int64_t, not uint64_t!
//
//     Returns 0 if valToSub >= 0 && input < valToSub, otherwise returns input - valToSub.
//
// GetKthVariadicArg:
//     Input: none
//     Output: 1, the value of the variadic argument, or nil if k is too large
//     Param: k.
//
//     Returns the k-th variadic argument of the root function.
//
// GetUpvalue:
//     Input: 1, the function object (an unboxed HeapPtr) to retrieve the upvalue from
//     Output: 1, the value of the requested upvalue
//     Param: the upvalue ordinal, and whether the upvalue is immutable
//
//     Get the value of an upvalue from the given function object.
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
#define DFG_BUILTIN_NODE_KIND_LIST                  \
    (Constant)                                      \
  , (UnboxedConstant)                               \
  , (UndefValue)                                    \
  , (Argument)                                      \
  , (GetNumVariadicArgs)                            \
  , (GetKthVariadicArg)                             \
  , (GetFunctionObject)                             \
  , (Nop)                                           \
  , (GetLocal)                                      \
  , (PhantomLocal)                                  \
  , (SetLocal)                                      \
  , (ShadowStore)                                   \
  , (Phantom)                                       \
  , (CreateCapturedVar)                             \
  , (GetCapturedVar)                                \
  , (SetCapturedVar)                                \
  , (GetKthVariadicRes)                             \
  , (GetNumVariadicRes)                             \
  , (CreateVariadicRes)                             \
  , (PrependVariadicRes)                            \
  , (CheckU64InBound)                               \
  , (U64SaturateSub)                                \
  , (CreateFunctionObject)                          \
  , (GetUpvalue)                                    \
  , (SetUpvalue)                                    \
  , (Return)                                        \
  , (Phi)                                           \

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

enum UseKind : uint16_t
{
    // Must be first member
    // This is a boxed value, but no type assumption on the type of the value
    //
    UseKind_Untyped,
    // This is an unboxed pointer pointing to a closed Upvalue object
    //
    UseKind_KnownCapturedVar,
    // This is a value that is statically known to be an unboxed 64-bit integer
    //
    UseKind_KnownUnboxedInt64,
    // Must be last member
    //
    UseKind_FirstAvailableGuestLanguageUseKind
};

struct Graph;

struct Node;
struct Phi;

struct Value
{
    Value() = default;
    Value(std::nullptr_t) : m_node(nullptr) { }
    Value(ArenaPtr<Node> node, uint16_t outputOrd) : m_node(node), m_outputOrd(outputOrd) { }
    Value(Node* node, uint16_t outputOrd) : Value(DfgAlloc()->GetArenaPtr(node), outputOrd) { }

    ArenaPtr<Node> m_node;
    uint16_t m_outputOrd;
};

struct PhiOrNode
{
    PhiOrNode() = default;
    PhiOrNode(Phi* ptr) : m_ptr(ptr) { TestAssert(IsNull() || IsPhi()); }
    PhiOrNode(Node* ptr) : m_ptr(ptr) { TestAssert(IsNull() || !IsPhi()); }
    PhiOrNode(std::nullptr_t) : m_ptr(nullptr) {}

    bool IsNull() { return m_ptr == nullptr; }
    bool IsPhi() { TestAssert(!IsNull()); return GetNodeKind() == NodeKind_Phi; }
    bool IsNode() { return !IsPhi(); }

    Phi* AsPhi()
    {
        TestAssert(IsPhi());
        return reinterpret_cast<Phi*>(m_ptr);
    }

    Node* AsNode()
    {
        TestAssert(!IsPhi());
        return reinterpret_cast<Node*>(m_ptr);
    }

    NodeKind GetNodeKind()
    {
        // Use UnalignedLoad to avoid breaking strict aliasing
        //
        return static_cast<NodeKind>(UnalignedLoad<std::underlying_type_t<NodeKind>>(m_ptr));
    }

    friend bool operator==(const PhiOrNode& a, const PhiOrNode& b)
    {
        return a.m_ptr == b.m_ptr;
    }

private:
    void* m_ptr;
};

struct Node
{
    MAKE_NONCOPYABLE(Node);
    MAKE_NONMOVABLE(Node);

private:
    friend class Arena;                 // this class should only be alloc'ed in DFG arena

    Node(NodeKind kind)
        : m_nodeKind(kind)
        , m_flags(0)
        , m_outputInfoArray(nullptr)
        , m_variadicResultInput(nullptr)
        , m_replacement(nullptr)
#ifndef NDEBUG
        , m_initializedNumInputs(false)
        , m_initializedNumOutputs(false)
        , m_initializedNodeOrigin(false)
#endif
    { }

public:
    friend struct Graph;

    // The bit field that carries the varies flags
    //
    using FlagsTy = uint32_t;

    // When the value == x_maxInlineOperands + 1, this node is using outlined operands
    // Otherwise the value is the number of inlined operands this node has
    //
    using Flags_NumInlinedOperands = BitFieldMember<FlagsTy, uint32_t, 0 /*start*/, 3 /*width*/>;

    // If this is true, the last element of m_inlineOperands is repurposed to represent the outlined array
    //
    bool HasOutlinedInput()
    {
        assert(m_initializedNumInputs);
        return Flags_NumInlinedOperands::Get(m_flags) == (x_maxInlineOperands + 1);
    }

    // Whether this node has an direct output
    //
    using Flags_HasDirectOutput = BitFieldMember<FlagsTy, bool, 3 /*start*/, 1 /*width*/>;

    bool HasDirectOutput() { assert(m_initializedNumOutputs); return Flags_HasDirectOutput::Get(m_flags); }

    // Whether this node has outlined node-specific data
    //
    using Flags_HasOutlinedNodeSpecificData = BitFieldMember<FlagsTy, bool, 4 /*start*/, 1 /*width*/>;

    bool HasOutlinedNodeSpecificData() { return Flags_HasOutlinedNodeSpecificData::Get(m_flags); }

    // Whether this node may OSR exit
    //
    using Flags_MayOsrExit = BitFieldMember<FlagsTy, bool, 5 /*start*/, 1 /*width*/>;

    bool MayOsrExit() { return Flags_MayOsrExit::Get(m_flags); }
    void SetMayOsrExit(bool val) { Flags_MayOsrExit::Set(m_flags, val); }

    // Whether this node is OK to OSR exit
    //
    using Flags_ExitOK = BitFieldMember<FlagsTy, bool, 6 /*start*/, 1 /*width*/>;

    bool IsExitOK() { return Flags_ExitOK::Get(m_flags); }
    void SetExitOK(bool val) { Flags_ExitOK::Set(m_flags, val); }

    // Whether this node generates VariadicResult
    //
    using Flags_GeneratesVariadicResults = BitFieldMember<FlagsTy, bool, 7 /*start*/, 1 /*width*/>;

    bool IsNodeGeneratesVR() { return Flags_GeneratesVariadicResults::Get(m_flags); }
    void SetNodeGeneratesVR(bool val) { Flags_GeneratesVariadicResults::Set(m_flags, val); }

    // Whether this node accesses VariadicResult as input
    //
    using Flags_AccessesVariadicResults = BitFieldMember<FlagsTy, bool, 8 /*start*/, 1 /*width*/>;

    bool IsNodeAccessesVR() { return Flags_AccessesVariadicResults::Get(m_flags); }
    void SetNodeAccessesVR(bool val) { Flags_AccessesVariadicResults::Set(m_flags, val); }

    // Whether this node could clobber VariadicResult during its execution
    //
    using Flags_ClobbersVariadicResults = BitFieldMember<FlagsTy, bool, 9 /*start*/, 1 /*width*/>;

    bool IsNodeClobbersVR() { return Flags_ClobbersVariadicResults::Get(m_flags); }
    void SetNodeClobbersVR(bool val) { Flags_ClobbersVariadicResults::Set(m_flags, val); }

    using Flags_AccessesVariadicArguments = BitFieldMember<FlagsTy, bool, 10 /*start*/, 1 /*width*/>;

    bool IsNodeAccessesVA() { return Flags_AccessesVariadicArguments::Get(m_flags); }
    void SetNodeAccessesVA(bool val) { Flags_AccessesVariadicArguments::Set(m_flags, val); }

    // Whether this node makes a tail call
    // Note that we require tail call to be never mixed with other types of terminal APIs (except Throw),
    // so any node that could make a tail call will always either make a tail call or throw an exception,
    // so it is always a terminal node
    //
    using Flags_NodeMakesTailCall = BitFieldMember<FlagsTy, bool, 11 /*start*/, 1 /*width*/>;

    bool IsNodeMakesTailCallNotConsideringTransform() { return Flags_NodeMakesTailCall::Get(m_flags); }
    void SetNodeMakesTailCallNotConsideringTransform(bool val) { Flags_NodeMakesTailCall::Set(m_flags, val); }

    // When we speculatively inline a function, tail calls inside the inlined function needs to be implemented specially.
    //
    // Specifically, if every inlined call from the root function to this function is tail call (i.e., the
    // InlinedCallFrame's IsTailCallAllTheWayDown() returns true), then the tail call in the inlined function must be
    // implemented by a tail call, and the tail call should overtake the root frame.
    //
    // However, if there exists a non-tail call in the inlined calls, the tail call in the inlined function must not
    // overtake the root frame. It needs to be implemented as a normal call that returns to the topmost frame that
    // made a non-tail call. That is, we need to implement this tail call as a normal call with the return continuation
    // being the return continuation of the most recent non-tail call. (Note that this does not break the no-unbounded-growth
    // requirement, since no infinite tail-call chain involving the root function can form as it already did a non-tail call).
    //
    // This flag records whether this is the case. Obviously, it only makes sense when IsNodeMakesTailCall() is true.
    //
    using Flags_TailCallTransformedToNormalCall = BitFieldMember<FlagsTy, bool, 12 /*start*/, 1 /*width*/>;

    bool IsNodeTailCallTransformedToNormalCall() { return Flags_TailCallTransformedToNormalCall::Get(m_flags); }
    void SetNodeTailCallTransformedToNormalCall(bool val) { Flags_TailCallTransformedToNormalCall::Set(m_flags, val); }

    // If true, it means the node does not have a "fallthrough" successor (i.e., the 'Return()' Deegen API is used)
    //
    using Flags_NodeIsBarrier = BitFieldMember<FlagsTy, bool, 13 /*start*/, 1 /*width*/>;

    bool IsNodeBarrier() { return Flags_NodeIsBarrier::Get(m_flags); }
    void SetNodeIsBarrier(bool val) { Flags_NodeIsBarrier::Set(m_flags, val); }

    // If true, it means that the node has a "branch" successor (i.e., the 'ReturnAndBranch()' Deegen API is used)
    //
    using Flags_NodeHasBranchTarget = BitFieldMember<FlagsTy, bool, 14 /*start*/, 1 /*width*/>;

    bool IsNodeHasBranchTarget() { return Flags_NodeHasBranchTarget::Get(m_flags); }
    void SetNodeHasBranchTarget(bool val) { Flags_NodeHasBranchTarget::Set(m_flags, val); }

    // Speculative Inlining Specialization kind
    //
    enum class SISKind
    {
        // Must be first, just a normal node
        //
        None,
        // The node is specialized for inlining a callee recorded in an call IC
        // Specifically, it will OSR exit at the earliest point it knows the call IC misses.
        // The function is required to be pure after the transformation
        //
        Prologue,
        // Process the return values after the inlined call finishes
        //
        Epilogue
    };

    using Flags_InlinerSpecializationInfo = BitFieldMember<FlagsTy, uint8_t, 15 /*start*/, 4 /*width*/>;

    void SetNodeAsNotSpecializedForInliner()
    {
        Flags_InlinerSpecializationInfo::Set(m_flags, 0);
    }

    void SetNodeInlinerSpecialization(bool isPrologue, uint8_t icSiteOrd)
    {
        TestAssert(icSiteOrd <= 6);     // we only have 4 bits here
        uint8_t compositeValue = icSiteOrd * 2 + (isPrologue ? 0 : 1) + 2;
        Flags_InlinerSpecializationInfo::Set(m_flags, compositeValue);
    }

    SISKind GetNodeSpecializedForInliningKind()
    {
        uint8_t compositeValue = Flags_InlinerSpecializationInfo::Get(m_flags);
        if (compositeValue == 0)
        {
            return SISKind::None;
        }
        if (compositeValue % 2 == 0)
        {
            return SISKind::Prologue;
        }
        else
        {
            return SISKind::Epilogue;
        }
    }

    bool IsNodeSpecializedForInlining()
    {
        return GetNodeSpecializedForInliningKind() != SISKind::None;
    }

    uint8_t GetNodeSpecializedForInliningCallSite()
    {
        TestAssert(IsNodeSpecializedForInlining());
        uint8_t compositeValue = Flags_InlinerSpecializationInfo::Get(m_flags);
        TestAssert(compositeValue >= 2);
        return static_cast<uint8_t>((compositeValue - 2) / 2);
    }

    // True if there is a node that uses one of the outputs of this node
    // This bit is usually stale, it is only accurate if you just computed it fresh.
    // Also, this bit has no meaning for constant-like nodes.
    //
    using Flags_IsReferenced = BitFieldMember<FlagsTy, bool, 19 /*start*/, 1 /*width*/>;

    bool IsNodeReferenced() { return Flags_IsReferenced::Get(m_flags); }
    void SetNodeReferenced(bool val) { Flags_IsReferenced::Set(m_flags, val); }

    // Get the total number of possible destinations where this node may transfer control to.
    //
    size_t GetNumNodeControlFlowSuccessors()
    {
        bool isBarrier = IsNodeBarrier();
        bool hasBranchTarget = IsNodeHasBranchTarget();
        bool mayDoTailCall = IsNodeMakesTailCallNotConsideringTransform();
        bool isTailCallTransformed = IsNodeTailCallTransformedToNormalCall();

        // Assert consistency
        //
        TestAssertImp(isTailCallTransformed, mayDoTailCall);
        TestAssertImp(mayDoTailCall, isBarrier && !hasBranchTarget);

        if (mayDoTailCall)
        {
            return (isTailCallTransformed ? 1 : 0);
        }
        else
        {
            return (isBarrier ? 0 : 1) + (hasBranchTarget ? 1 : 0);
        }
    }

    // A node is a terminal node if it does not have exactly 1 control flow successor (we don't care what kind of successor it is).
    //
    // A non-terminal node can show up at any place in a basic block, including terminal positions (which means an implicit
    // branch to the unique successor block).
    //
    // A terminal node with 0 successor must show up at the terminal position of a basic block. There are three possibilities:
    // (1) It is a function return (the built-in Return node)
    // (2) It is a node that makes a tail call that is not transformed to a normal call
    // (3) It is a node that always throws an exception or causes an OSR-exit.
    //
    // However, a terminal node with 2 successors does not necessarily show up at the terminal position, as it may have outputs
    // that needs to be stored to the stack. One should think of such a branchy node as outputting a "branch direction decision", and
    // the actual branch is taken at the end of the BB based on that decision.
    //
    bool IsTerminal()
    {
        return GetNumNodeControlFlowSuccessors() != 1;
    }

    // Represents a use edge
    //
    class Edge
    {
    public:
        constexpr Edge() = default;

        Edge(Value value, UseKind useKind = UseKind_Untyped, bool isStaticallyKnownNoCheckNeeded = false)
        {
            AssertImp(value.m_outputOrd == 0, DfgAlloc()->GetPtr(value.m_node)->HasDirectOutput());
            assert(value.m_outputOrd <= DfgAlloc()->GetPtr(value.m_node)->m_numExtraOutputs);
            m_operand = value.m_node;
            m_outputOrd = value.m_outputOrd;
            uint16_t encodedVal = 0;
            BFM_useKind::Set(encodedVal, useKind);
            BFM_isStaticallyProven::Set(encodedVal, isStaticallyKnownNoCheckNeeded);
            BFM_isProven::Set(encodedVal, false);
            BFM_isLastUse::Set(encodedVal, false);
            m_encodedVal = encodedVal;
        }

        Node* WARN_UNUSED GetOperand() { return m_operand; }

        void SetOperand(Node* node) { m_operand = node; }
        void SetOperand(ArenaPtr<Node> node) { m_operand = node; }

        // If the operand node has been marked for replacement, replace it.
        // Asserts that the replacement has no further replacement, and that the ordinal is valid for the replacement
        //
        void ReplaceOperandBasedOnReplacement()
        {
            Node* node = GetOperand();
            ArenaPtr<Node> replacementMaybeNull = node->GetReplacementNodeMaybeNullptr();
            if (!replacementMaybeNull.IsNull())
            {
                SetOperand(replacementMaybeNull);
                TestAssert(GetOperand()->GetReplacementNodeMaybeNullptr().IsNull());
                TestAssert(GetOperand()->IsOutputOrdValid(GetOutputOrdinal()));
            }
        }

        Value WARN_UNUSED GetValue()
        {
            return Value(m_operand, m_outputOrd);
        }

        uint16_t WARN_UNUSED GetOutputOrdinal() { return m_outputOrd; }

        bool IsStaticallyKnownNoCheckNeeded() { return BFM_isStaticallyProven::Get(m_encodedVal); }

        bool IsProven() { return BFM_isProven::Get(m_encodedVal); }
        void SetProven(bool val) { BFM_isProven::Set(m_encodedVal, val); }

        bool IsKill() { return BFM_isLastUse::Get(m_encodedVal); }
        void SetKill(bool val) { BFM_isLastUse::Set(m_encodedVal, val); }

        UseKind GetUseKind() { return BFM_useKind::Get(m_encodedVal); }
        void SetUseKind(UseKind useKind) { BFM_useKind::Set(m_encodedVal, useKind); }

        bool NeedsTypeCheck()
        {
            if (GetUseKind() == UseKind_Untyped) { return false; }
            if (IsStaticallyKnownNoCheckNeeded()) { return false; }
            return !IsProven();
        }

    private:
        // The Node target of this edge
        //
        ArenaPtr<Node> m_operand;

        // The output ordinal in the Node target
        // Note that 0 is reserved for explicit output, even if it does not exist
        // All the outlined outputs always start from ordinal 1, so 1 is always the first outlined output, etc.
        //
        uint16_t m_outputOrd;

        // bit 0: whether we have static information that the no type check is needed
        //
        using BFM_isStaticallyProven = BitFieldMember<uint16_t /*carrierType*/, bool /*type*/, 0 /*start*/, 1 /*width*/>;

        // bit 1: whether we proved that no type check is needed due to existing speculation
        //
        using BFM_isProven = BitFieldMember<uint16_t /*carrierType*/, bool /*type*/, 1 /*start*/, 1 /*width*/>;

        // bit 2: whether this edge is the last use of the operand
        //
        using BFM_isLastUse = BitFieldMember<uint16_t /*carrierType*/, bool /*type*/, 2 /*start*/, 1 /*width*/>;

        // bit 3-15: the UseKind
        //
        using BFM_useKind = BitFieldMember<uint16_t /*carrierType*/, UseKind /*type*/, 3 /*start*/, 13 /*width*/>;

        uint16_t m_encodedVal;
    };
    static_assert(sizeof(Edge) == 8);

    // If the node has >x_maxInlineOperands operands, the operand list will have to be stored outlined.
    //
    static constexpr size_t x_maxInlineOperands = 3;
    static_assert(x_maxInlineOperands >= 1);
    static_assert(x_maxInlineOperands + 1 < (static_cast<FlagsTy>(1) << Flags_NumInlinedOperands::BitWidth()));

    NodeKind GetNodeKind() { return m_nodeKind; }

    bool IsBuiltinNodeKind() { return m_nodeKind < NodeKind_FirstAvailableGuestLanguageNodeKind; }

    BCKind GetGuestLanguageBCKind()
    {
        TestAssert(!IsBuiltinNodeKind());
        BCKind result = static_cast<BCKind>(SafeIntegerCast<uint8_t>(m_nodeKind - NodeKind_FirstAvailableGuestLanguageNodeKind));
        TestAssert(result < x_bcKindEndOfEnum);
        return result;
    }

    // True if this node behaves like a constant, so it must not show up inside a basic block,
    // but can be referenced by any other nodes
    //
    bool IsConstantLikeNode()
    {
        return IsConstantNode() || IsUnboxedConstantNode() || IsUndefValueNode() || IsArgumentNode() || IsGetFunctionObjectNode() || IsGetNumVarArgsNode() || IsGetKthVarArgNode();
    }

    // The NodeKind, must be first member!
    //
    NodeKind m_nodeKind;
    // How many extra output values this node has
    //
    uint16_t m_numExtraOutputs;
    // Misc flags for this node
    //
    FlagsTy m_flags;

private:
    union EdgeOrOutlinedInputsInfo
    {
        Edge m_edge;
        // The last element of the inlined input array may be repurposed to store the outlined array info
        //
        struct {
            ArenaPtr<Edge> m_outlinedEdgeArray;
            uint32_t m_numOutlinedEdges;
        };
    };
    static_assert(sizeof(EdgeOrOutlinedInputsInfo) == 8);

    EdgeOrOutlinedInputsInfo m_inlineOperands[x_maxInlineOperands];

    void SetNumInputsImpl(size_t numInputs)
    {
        if (unlikely(numInputs > x_maxInlineOperands))
        {
            Flags_NumInlinedOperands::Set(m_flags, x_maxInlineOperands + 1);
            size_t numOutlinedInputs = numInputs - (x_maxInlineOperands - 1);
            Edge* outlinedInputs = DfgAlloc()->AllocateArray<Edge>(numOutlinedInputs);
            m_inlineOperands[x_maxInlineOperands - 1].m_outlinedEdgeArray = DfgAlloc()->GetArenaPtr(outlinedInputs);
            m_inlineOperands[x_maxInlineOperands - 1].m_numOutlinedEdges = SafeIntegerCast<uint32_t>(numOutlinedInputs);
        }
        else
        {
            Flags_NumInlinedOperands::Set(m_flags, SafeIntegerCast<uint32_t>(numInputs));
        }
    }

public:
    void SetNumInputs(size_t numInputs)
    {
        assert(!m_initializedNumInputs);
#ifndef NDEBUG
        m_initializedNumInputs = true;
#endif
        SetNumInputsImpl(numInputs);
    }

    // This also invalidates all the input edges!
    //
    void ResetNumInputs(size_t numInputs)
    {
#ifndef NDEBUG
        m_initializedNumInputs = true;
#endif
        SetNumInputsImpl(numInputs);
    }

    uint32_t GetNumInputs()
    {
        assert(m_initializedNumInputs);
        if (likely(!HasOutlinedInput()))
        {
            uint32_t numInlineOperands = Flags_NumInlinedOperands::Get(m_flags);
            assert(numInlineOperands <= x_maxInlineOperands);
            __builtin_assume(numInlineOperands <= x_maxInlineOperands);
            return numInlineOperands;
        }
        else
        {
            uint32_t numOutlinedEdges = m_inlineOperands[x_maxInlineOperands - 1].m_numOutlinedEdges;
            assert(numOutlinedEdges > 1);
            uint32_t numTotalEdges = numOutlinedEdges + static_cast<uint32_t>(x_maxInlineOperands - 1);
            __builtin_assume(numTotalEdges > x_maxInlineOperands);
            return numTotalEdges;
        }
    }

private:
    Edge* ALWAYS_INLINE GetOutlinedEdgeArray()
    {
        assert(HasOutlinedInput());
        return DfgAlloc()->GetPtr(m_inlineOperands[x_maxInlineOperands - 1].m_outlinedEdgeArray);
    }

    Edge& ALWAYS_INLINE GetInlinedInputImpl(uint32_t inputOrd)
    {
        assert(inputOrd < GetNumInputs());
        assert(inputOrd < x_maxInlineOperands - 1 || (inputOrd == x_maxInlineOperands - 1 && !HasOutlinedInput()));
        return m_inlineOperands[inputOrd].m_edge;
    }

    Edge& ALWAYS_INLINE GetOutlinedInputImpl(uint32_t inputOrd)
    {
        assert(inputOrd < GetNumInputs());
        assert(HasOutlinedInput());
        assert(inputOrd >= x_maxInlineOperands - 1);
        return GetOutlinedEdgeArray()[inputOrd - (x_maxInlineOperands - 1)];
    }

public:
    // Works only for nodes statically known to have a fixed number of inputs
    //
    template<size_t numFixedInputs>
    Edge& ALWAYS_INLINE GetInputEdgeForNodeWithFixedNumInputs(uint32_t inputOrd)
    {
        assert(m_initializedNumInputs);
        TestAssert(GetNumInputs() == numFixedInputs);
        if constexpr(numFixedInputs > x_maxInlineOperands)
        {
            assert(HasOutlinedInput());
            if (inputOrd < x_maxInlineOperands - 1)
            {
                return GetInlinedInputImpl(inputOrd);
            }
            else
            {
                return GetOutlinedInputImpl(inputOrd);
            }
        }
        else
        {
            assert(!HasOutlinedInput());
            assert(inputOrd < numFixedInputs);
            return GetInlinedInputImpl(inputOrd);
        }
    }

    // Works for any node, but slower than GetInputEdgeForNodeWithFixedNumInputs
    //
    Edge& ALWAYS_INLINE GetInputEdge(uint32_t inputOrd)
    {
        assert(m_initializedNumInputs);
        if (inputOrd < x_maxInlineOperands - 1 || !HasOutlinedInput())
        {
            return GetInlinedInputImpl(inputOrd);
        }
        else
        {
            return GetOutlinedInputImpl(inputOrd);
        }
    }

    template<typename Func>
    void ALWAYS_INLINE ForEachInputEdge(const Func& func)
    {
        if (likely(!HasOutlinedInput()))
        {
            uint32_t numInputs = GetNumInputs();
            TestAssert(numInputs <= x_maxInlineOperands);
            for (uint32_t i = 0; i < numInputs; i++)
            {
                Edge& e = m_inlineOperands[i].m_edge;
                func(e);
            }
        }
        else
        {
            TestAssert(GetNumInputs() > x_maxInlineOperands);
            for (uint32_t i = 0; i < x_maxInlineOperands - 1; i++)
            {
                Edge& e = m_inlineOperands[i].m_edge;
                func(e);
            }
            uint32_t numOutlinedInputs = m_inlineOperands[x_maxInlineOperands - 1].m_numOutlinedEdges;
            TestAssert(numOutlinedInputs > 1);
            Edge* outlinedInputs = m_inlineOperands[x_maxInlineOperands - 1].m_outlinedEdgeArray;
            for (size_t i = 0; i < numOutlinedInputs; i++)
            {
                Edge& e = outlinedInputs[i];
                func(e);
            }
        }
    }

private:
    void SetNumOutputsImpl(bool hasDirectOutput, size_t numExtraOutputs)
    {
        Flags_HasDirectOutput::Set(m_flags, hasDirectOutput);
        m_numExtraOutputs = SafeIntegerCast<uint16_t>(numExtraOutputs);

        size_t numTotalOutputs = numExtraOutputs + (hasDirectOutput ? 0 : 1);
        if (numTotalOutputs > 0)
        {
            OutputInfo* outputInfoArray = DfgAlloc()->AllocateArray<OutputInfo>(numTotalOutputs);
            m_outputInfoArray = DfgAlloc()->GetArenaPtr(hasDirectOutput ? outputInfoArray : outputInfoArray - 1);
        }
        else
        {
            m_outputInfoArray.m_value = 0;
        }

        assert(HasDirectOutput() == hasDirectOutput);
    }

public:
    void SetNumOutputs(bool hasDirectOutput, size_t numExtraOutputs)
    {
        assert(!m_initializedNumOutputs);
#ifndef NDEBUG
        m_initializedNumOutputs = true;
#endif
        SetNumOutputsImpl(hasDirectOutput, numExtraOutputs);
    }

    // This invalidates all existing outputs
    //
    void ResetNumOutputs(bool hasDirectOutput, size_t numExtraOutputs)
    {
#ifndef NDEBUG
        m_initializedNumOutputs = true;
#endif
        SetNumOutputsImpl(hasDirectOutput, numExtraOutputs);
    }

    struct OutputInfo
    {
        // The type speculation
        //
        TypeSpeculationMask m_speculation;
    };

    bool HasExtraOutput() { assert(m_initializedNumOutputs); return m_numExtraOutputs > 0; }
    size_t GetNumExtraOutputs() { assert(m_initializedNumOutputs); return m_numExtraOutputs; }

    size_t GetNumTotalOutputs()
    {
        return (HasDirectOutput() ? 1 : 0) + GetNumExtraOutputs();
    }

    bool IsOutputOrdValid(uint16_t ord)
    {
        uint16_t outputOrdLow = (HasDirectOutput() ? 0 : 1);
        uint16_t outputOrdHigh = SafeIntegerCast<uint16_t>(GetNumExtraOutputs());
        return outputOrdLow <= ord && ord <= outputOrdHigh;
    }

    OutputInfo& GetDirectOutputInfo()
    {
        assert(m_initializedNumOutputs);
        TestAssert(HasDirectOutput());
        return DfgAlloc()->GetPtr(m_outputInfoArray)[0];
    }

    OutputInfo& GetExtraOutputInfo(size_t outputOrd)
    {
        assert(m_initializedNumOutputs);
        TestAssert(1 <= outputOrd && outputOrd <= m_numExtraOutputs);
        return DfgAlloc()->GetPtr(m_outputInfoArray)[outputOrd];
    }

    OutputInfo& GetOutputInfo(size_t outputOrd)
    {
        assert(m_initializedNumOutputs);
        TestAssert((outputOrd == 0 && HasDirectOutput()) || (1 <= outputOrd && outputOrd <= m_numExtraOutputs));
        return DfgAlloc()->GetPtr(m_outputInfoArray)[outputOrd];
    }

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
    struct CapturedVarInfo
    {
        // When an OSR-exit happens, we must flush the values of the still-opening CapturedVar back to the stack frame.
        //
        uint32_t m_localOrdForOsrExit;
    };
    static_assert(sizeof(CapturedVarInfo) <= 8);

    struct UpvalueInfo
    {
        uint32_t m_ordinal;
        bool m_isImmutable;
    };
    static_assert(sizeof(UpvalueInfo) <= 8);

private:
    // Information about the outputs
    //
    ArenaPtr<OutputInfo> m_outputInfoArray;

    // If this node accesses variadic results, the node that produces variadic results
    //
    ArenaPtr<Node> m_variadicResultInput;

    // Stores node-specific data which interpretation depends on the node type
    // This mainly includes all sorts of immediate constants in the bytecode
    // Make sure to not exceed 8 bytes!
    //
    union {
        uint8_t* m_outlinedNodeSpecificData;
        uint8_t m_inlinedNodeSpecifcData[8];
        TValue m_constantNodeConstantValue;         // used only for constant node
        CapturedVarInfo m_capturedVarInfo;          // used only for CreateCapturedVar node
        UpvalueInfo m_upvalueInfo;                  // used only for GetUpvalue node
        Phi* m_getLocalDataFlowInfo;                // used only for GetLocal and PhantomLocal node
        uint64_t m_nsdAsU64;
    };

    // Where this node comes from in the bytecode
    //
    CodeOrigin m_nodeOrigin;

    // If this node caused an OSR exit, where the control flow should continue at in the bytecode
    //
    OsrExitDestination m_osrExitDest;

public:
    void SetVariadicResultInputNode(Node* node)
    {
        TestAssert(IsNodeAccessesVR());
        m_variadicResultInput = node;
    }

    Node* GetVariadicResultInputNode()
    {
        TestAssert(IsNodeAccessesVR());
        if (m_variadicResultInput.IsNull())
        {
            return nullptr;
        }
        else
        {
            return m_variadicResultInput;
        }
    }

    uint8_t* GetNodeSpecificData()
    {
        if (HasOutlinedNodeSpecificData())
        {
            return m_outlinedNodeSpecificData;
        }
        else
        {
            return m_inlinedNodeSpecifcData;
        }
    }

private:
    void SetOutlinedNodeSpecificDataRegion(uint8_t* region)
    {
        assert(!HasOutlinedNodeSpecificData());
        m_outlinedNodeSpecificData = region;
        Flags_HasOutlinedNodeSpecificData::Set(m_flags, true);
        assert(HasOutlinedNodeSpecificData() && GetNodeSpecificData() == region);
    }

public:
    void SetNodeSpecificDataLength(size_t length, size_t alignment = 1)
    {
        TestAssert(!IsBuiltinNodeKind());
        TestAssert(is_power_of_2(alignment));
        TestAssert(length % alignment == 0);
        if (length <= 8 && alignment <= 8)
        {
            return;
        }
        SetOutlinedNodeSpecificDataRegion(DfgAlloc()->AllocateUninitializedMemoryWithAlignment(length, alignment));
    }

    // Does not initialize anything other than NodeKind
    //
    static Node* WARN_UNUSED CreateGuestLanguageNode(BCKind bcKind)
    {
        TestAssert(bcKind < x_bcKindEndOfEnum);
        return DfgAlloc()->AllocateObject<Node>(static_cast<NodeKind>(NodeKind_FirstAvailableGuestLanguageNodeKind + static_cast<int>(bcKind)));
    }

    bool IsConstantNode() { return m_nodeKind == NodeKind_Constant; }
    bool IsUnboxedConstantNode() { return m_nodeKind == NodeKind_UnboxedConstant; }
    bool IsUndefValueNode() { return m_nodeKind == NodeKind_UndefValue; }
    bool IsArgumentNode() { return m_nodeKind == NodeKind_Argument; }
    bool IsGetFunctionObjectNode() { return m_nodeKind == NodeKind_GetFunctionObject; }
    bool IsGetNumVarArgsNode() { return m_nodeKind == NodeKind_GetNumVariadicArgs; }
    bool IsGetKthVarArgNode() { return m_nodeKind == NodeKind_GetKthVariadicArg; }

private:
    static Node* WARN_UNUSED CreateConstantNode(TValue constantValue)
    {
        Node* r = DfgAlloc()->AllocateObject<Node>(NodeKind_Constant);
        r->SetNumInputs(0);
        r->SetNumOutputs(true /*hasDirectOutput*/, 0 /*numExtraOutputs*/);
        r->m_constantNodeConstantValue = constantValue;
        return r;
    }

    static Node* WARN_UNUSED CreateUnboxedConstantNode(uint64_t value)
    {
        Node* r = DfgAlloc()->AllocateObject<Node>(NodeKind_UnboxedConstant);
        r->SetNumInputs(0);
        r->SetNumOutputs(true /*hasDirectOutput*/, 0 /*numExtraOutputs*/);
        r->m_nsdAsU64 = value;
        return r;
    }

    static Node* WARN_UNUSED CreateUndefValueNode()
    {
        Node* r = DfgAlloc()->AllocateObject<Node>(NodeKind_UndefValue);
        r->SetNumInputs(0);
        r->SetNumOutputs(true /*hasDirectOutput*/, 0 /*numExtraOutputs*/);
        return r;
    }

    static Node* WARN_UNUSED CreateArgumentNode(size_t argOrd)
    {
        Node* r = DfgAlloc()->AllocateObject<Node>(NodeKind_Argument);
        r->SetNumInputs(0);
        r->SetNumOutputs(true /*hasDirectOutput*/, 0 /*numExtraOutputs*/);
        r->m_nsdAsU64 = argOrd;
        return r;
    }

    static Node* WARN_UNUSED CreateGetFunctionObjectNode()
    {
        Node* r = DfgAlloc()->AllocateObject<Node>(NodeKind_GetFunctionObject);
        r->SetNumInputs(0);
        r->SetNumOutputs(true /*hasDirectOutput*/, 0 /*numExtraOutputs*/);
        return r;
    }

    static Node* WARN_UNUSED CreateGetNumVarArgsNode()
    {
        Node* r = DfgAlloc()->AllocateObject<Node>(NodeKind_GetNumVariadicArgs);
        r->SetNumInputs(0);
        r->SetNumOutputs(true /*hasDirectOutput*/, 0 /*numExtraOutputs*/);
        return r;
    }

    static Node* WARN_UNUSED CreateGetKthVariadicArgNode(size_t k)
    {
        Node* r = DfgAlloc()->AllocateObject<Node>(NodeKind_GetKthVariadicArg);
        r->SetNumInputs(0);
        r->SetNumOutputs(true /*hasDirectOutput*/, 0 /*numExtraOutputs*/);
        r->m_nsdAsU64 = k;
        return r;
    }

public:
    bool IsGetUpvalueNode() { return m_nodeKind == NodeKind_GetUpvalue; }

    UpvalueInfo& GetInfoForGetUpvalue()
    {
        TestAssert(IsGetUpvalueNode());
        return m_upvalueInfo;
    }

    static Node* WARN_UNUSED CreateGetUpvalueNode(Value functionObject, uint32_t upvalueOrd, bool isImmutable)
    {
        Node* r = DfgAlloc()->AllocateObject<Node>(NodeKind_GetUpvalue);
        r->SetNumInputs(1);
        r->GetInputEdgeForNodeWithFixedNumInputs<1>(0) = functionObject;
        r->SetNumOutputs(true /*hasDirectOutput*/, 0 /*numExtraOutputs*/);
        r->m_upvalueInfo = UpvalueInfo { .m_ordinal = upvalueOrd, .m_isImmutable = isImmutable };
        return r;
    }

    bool IsSetUpvalueNode() { return m_nodeKind == NodeKind_SetUpvalue; }

    static Node* WARN_UNUSED CreateSetUpvalueNode(Value functionObject, uint32_t upvalueOrd, Value valueToSet)
    {
        Node* r = DfgAlloc()->AllocateObject<Node>(NodeKind_SetUpvalue);
        r->SetNumInputs(2);
        r->GetInputEdgeForNodeWithFixedNumInputs<2>(0) = functionObject;
        r->GetInputEdgeForNodeWithFixedNumInputs<2>(1) = valueToSet;
        r->SetNumOutputs(false /*hasDirectOutput*/, 0 /*numExtraOutputs*/);
        r->m_nsdAsU64 = upvalueOrd;
        return r;
    }

    TValue GetConstantNodeValue()
    {
        TestAssert(IsConstantNode());
        return m_constantNodeConstantValue;
    }

    uint32_t GetArgumentOrdinal()
    {
        TestAssert(IsArgumentNode());
        return SafeIntegerCast<uint32_t>(m_nsdAsU64);
    }

    void SetNodeParamAsUInt64(uint64_t val)
    {
        m_nsdAsU64 = val;
    }

    uint64_t GetNodeParamAsUInt64()
    {
        return m_nsdAsU64;
    }

    bool IsGetLocalNode() { return m_nodeKind == NodeKind_GetLocal; }
    bool IsSetLocalNode() { return m_nodeKind == NodeKind_SetLocal; }
    bool IsPhantomLocalNode() { return m_nodeKind == NodeKind_PhantomLocal; }

    bool IsGetOrPhantomLocalNode() { return IsGetLocalNode() || IsPhantomLocalNode(); }

    struct LocalVarAccessInfo
    {
        LocalVarAccessInfo(Node* owner, InterpreterSlot interpreterSlot, VirtualRegister virtualRegister)
        {
            m_dsuParent = owner;
            m_logicalVariableInfo = nullptr;
            m_interpreterSlotOrd = SafeIntegerCast<uint32_t>(interpreterSlot.Value());
            m_virtualRegisterOrd = SafeIntegerCast<uint32_t>(virtualRegister.Value());
        }

        bool IsConsistentWith(const LocalVarAccessInfo& other)
        {
            return m_interpreterSlotOrd == other.m_interpreterSlotOrd && m_virtualRegisterOrd == other.m_virtualRegisterOrd;
        }

        bool IsLogicalVariableInfoPointerSetUp()
        {
            return !m_logicalVariableInfo.IsNull();
        }

        LogicalVariableInfo* GetLogicalVariableInfo()
        {
            TestAssert(IsLogicalVariableInfoPointerSetUp());
            return m_logicalVariableInfo;
        }

        void SetLogicalVariableInfo(LogicalVariableInfo* info)
        {
            TestAssert(!IsLogicalVariableInfoPointerSetUp());
            m_logicalVariableInfo = info;
        }

        VirtualRegister GetLocalOrd()
        {
            return VirtualRegister(m_virtualRegisterOrd);
        }

        InterpreterSlot GetInterpreterSlot()
        {
            return InterpreterSlot(m_interpreterSlotOrd);
        }

        ArenaPtr<Node> m_dsuParent;
        ArenaPtr<LogicalVariableInfo> m_logicalVariableInfo;
        uint32_t m_interpreterSlotOrd;
        uint32_t m_virtualRegisterOrd;
    };
    // Must be 16 bytes since it is stored in input slot 1-2
    //
    static_assert(sizeof(LocalVarAccessInfo) == 16);

    bool HasLogicalVariableInfo()
    {
        return IsGetLocalNode() || IsSetLocalNode() || IsPhantomLocalNode();
    }

    VirtualRegister GetLocalOperationVirtualRegister()
    {
        TestAssert(HasLogicalVariableInfo());
        return GetLocalVarAccessInfo().GetLocalOrd();
    }

    InterpreterSlot GetLocalOperationInterpreterSlot()
    {
        TestAssert(HasLogicalVariableInfo());
        return GetLocalVarAccessInfo().GetInterpreterSlot();
    }

    // This information is only available if the graph is in block-local SSA form
    //
    Phi* GetDataFlowInfoForGetLocal()
    {
        TestAssert(IsGetLocalNode() || IsPhantomLocalNode());
        return m_getLocalDataFlowInfo;
    }

    void SetDataFlowInfoForGetLocal(Phi* info)
    {
        TestAssert(IsGetLocalNode() || IsPhantomLocalNode());
        m_getLocalDataFlowInfo = info;
    }

private:
    LocalVarAccessInfo& GetLocalVarAccessInfo()
    {
        // The LocalVarAccessInfo is a 16-byte struct that is stored in input slot 1-2
        //
        static_assert(x_maxInlineOperands >= 3);
        TestAssert(HasLogicalVariableInfo());
        return *reinterpret_cast<LocalVarAccessInfo*>(m_inlineOperands + 1);
    }

    Node* WARN_UNUSED LogicalVariableAccessDsuFindRoot()
    {
        TestAssert(HasLogicalVariableInfo());
        LocalVarAccessInfo& info = GetLocalVarAccessInfo();
        Node* p = info.m_dsuParent;
        if (p == this)
        {
            return this;
        }
        p = p->LogicalVariableAccessDsuFindRoot();
        TestAssert(p->GetLocalVarAccessInfo().IsConsistentWith(DfgAlloc()->GetPtr(info.m_dsuParent)->GetLocalVarAccessInfo()));
        info.m_dsuParent = p;
        return p;
    }

    void RegisterLogicalVariable(Graph* graph, LogicalVariableInfo* info);

public:
    // Initialize LogicalVariableInfo after all unification is done
    // For use by unification pass only
    //
    void SetupLogicalVariableInfoAfterDsuMerge(Graph* graph)
    {
        TestAssert(HasLogicalVariableInfo());
        LocalVarAccessInfo& info = GetLocalVarAccessInfo();
        if (info.IsLogicalVariableInfoPointerSetUp())
        {
            // The merge has been done (because we are an root and one of the children has done the setup), no work to do
            //
            TestAssert(LogicalVariableAccessDsuFindRoot() == this);
            TestAssert(info.GetLocalOrd().Value() == info.GetLogicalVariableInfo()->m_localOrd);
            TestAssert(info.GetInterpreterSlot().Value() == info.GetLogicalVariableInfo()->m_interpreterSlotOrd);
            return;
        }

        Node* root = LogicalVariableAccessDsuFindRoot();
        TestAssert(root->HasLogicalVariableInfo());
        LocalVarAccessInfo& rootInfo = root->GetLocalVarAccessInfo();

        if (!rootInfo.IsLogicalVariableInfoPointerSetUp())
        {
            TestAssert(info.IsConsistentWith(rootInfo));
            VirtualRegister vreg = rootInfo.GetLocalOrd();
            InterpreterSlot islot = rootInfo.GetInterpreterSlot();
            LogicalVariableInfo* lvInfo = LogicalVariableInfo::Create(vreg, islot);
            RegisterLogicalVariable(graph, lvInfo);
            rootInfo.SetLogicalVariableInfo(lvInfo);
        }

        TestAssert(rootInfo.IsLogicalVariableInfoPointerSetUp());
        TestAssert(info.GetLocalOrd().Value() == rootInfo.GetLogicalVariableInfo()->m_localOrd);
        TestAssert(info.GetInterpreterSlot().Value() == rootInfo.GetLogicalVariableInfo()->m_interpreterSlotOrd);

        // If we are root, we have done the setup above so no need to do anything
        // Otherwise, we need to set up our own LogicalVariableInfo to be the root's LogicalVariableInfo
        //
        if (this != root)
        {
            TestAssert(!info.IsLogicalVariableInfoPointerSetUp());
            info.SetLogicalVariableInfo(rootInfo.GetLogicalVariableInfo());
        }

        TestAssert(info.IsLogicalVariableInfoPointerSetUp());
        TestAssert(info.GetLocalOrd().Value() == info.GetLogicalVariableInfo()->m_localOrd);
        TestAssert(info.GetInterpreterSlot().Value() == info.GetLogicalVariableInfo()->m_interpreterSlotOrd);
    }

    // Mark that two load/store should be considered as operating on the same logical variable
    // For use by unification pass only
    //
    void MergeLogicalVariableInfo(Node* other)
    {
        TestAssert(HasLogicalVariableInfo() && other->HasLogicalVariableInfo());
        TestAssert(GetLocalVarAccessInfo().IsConsistentWith(other->GetLocalVarAccessInfo()));
        Node* p1 = LogicalVariableAccessDsuFindRoot();
        Node* p2 = other->LogicalVariableAccessDsuFindRoot();
        TestAssert(p1->HasLogicalVariableInfo() && p2->HasLogicalVariableInfo());
        TestAssert(GetLocalVarAccessInfo().IsConsistentWith(p1->GetLocalVarAccessInfo()));
        TestAssert(GetLocalVarAccessInfo().IsConsistentWith(p2->GetLocalVarAccessInfo()));
        TestAssert(p1->GetLocalVarAccessInfo().m_dsuParent == p1);
        TestAssert(p2->GetLocalVarAccessInfo().m_dsuParent == p2);
        p1->GetLocalVarAccessInfo().m_dsuParent = p2;
    }

    // For debug logic only
    //
    LocalVarAccessInfo Debug_GetLocalVarAccessInfo()
    {
        return GetLocalVarAccessInfo();
    }

    // For debug logic only, return nullptr if the LogicalVarInfo is not available yet
    //
    LogicalVariableInfo* Debug_TryGetLogicalVariableInfo()
    {
        if (GetLocalVarAccessInfo().IsLogicalVariableInfoPointerSetUp())
        {
            return GetLocalVarAccessInfo().GetLogicalVariableInfo();
        }
        else
        {
            return nullptr;
        }
    }

    LogicalVariableInfo* GetLogicalVariableInfo()
    {
        return GetLocalVarAccessInfo().GetLogicalVariableInfo();
    }

    bool IsNoopNode() { return m_nodeKind == NodeKind_Nop; }
    bool IsReturnNode() { return m_nodeKind == NodeKind_Return; }
    bool IsGetKthVariadicResNode() { return m_nodeKind == NodeKind_GetKthVariadicRes; }
    bool IsPrependVariadicResNode() { return m_nodeKind == NodeKind_PrependVariadicRes; }

    static Node* WARN_UNUSED CreateNoopNode()
    {
        Node* r = DfgAlloc()->AllocateObject<Node>(NodeKind_Nop);
        r->SetNumInputs(0);
        r->SetNumOutputs(false /*hasDirectOutput*/, 0 /*numExtraOutputs*/);
        return r;
    }

    static Node* WARN_UNUSED CreateGetLocalNode(VirtualRegister vreg, InterpreterSlot interpSlot)
    {
        Node* r = DfgAlloc()->AllocateObject<Node>(NodeKind_GetLocal);
        r->SetNumInputs(0);
        r->SetNumOutputs(true /*hasDirectOutput*/, 0 /*numExtraOutputs*/);
        r->GetLocalVarAccessInfo() = LocalVarAccessInfo(r, interpSlot, vreg);
        return r;
    }

    static Node* WARN_UNUSED CreateSetLocalNode(VirtualRegister vreg, InterpreterSlot interpSlot, Value valueToStore)
    {
        Node* r = DfgAlloc()->AllocateObject<Node>(NodeKind_SetLocal);
        r->SetNumInputs(1);
        r->GetInputEdgeForNodeWithFixedNumInputs<1>(0) = Edge(valueToStore);
        r->SetNumOutputs(false /*hasDirectOutput*/, 0 /*numExtraOutputs*/);
        r->GetLocalVarAccessInfo() = LocalVarAccessInfo(r, interpSlot, vreg);
        return r;
    }

    static Node* WARN_UNUSED CreateGetCapturedVarNode(Value capturedVar)
    {
        Node* r = DfgAlloc()->AllocateObject<Node>(NodeKind_GetCapturedVar);
        r->SetNumInputs(1);
        r->GetInputEdgeForNodeWithFixedNumInputs<1>(0) = Edge(capturedVar);
        r->SetNumOutputs(true /*hasDirectOutput*/, 0 /*numExtraOutputs*/);
        return r;
    }

    static Node* WARN_UNUSED CreateSetCapturedVarNode(Value capturedVar, Value valueToStore)
    {
        Node* r = DfgAlloc()->AllocateObject<Node>(NodeKind_SetCapturedVar);
        r->SetNumInputs(2);
        r->GetInputEdgeForNodeWithFixedNumInputs<2>(0) = Edge(capturedVar);
        r->GetInputEdgeForNodeWithFixedNumInputs<2>(1) = Edge(valueToStore);
        r->SetNumOutputs(false /*hasDirectOutput*/, 0 /*numExtraOutputs*/);
        return r;
    }

    static Node* WARN_UNUSED CreateCreateCapturedVarNode(Value value)
    {
        Node* r = DfgAlloc()->AllocateObject<Node>(NodeKind_CreateCapturedVar);
        r->SetNumInputs(1);
        r->GetInputEdgeForNodeWithFixedNumInputs<1>(0) = Edge(value);
        r->SetNumOutputs(true /*hasDirectOutput*/, 0 /*numExtraOutputs*/);
        return r;
    }

    static Node* WARN_UNUSED CreateShadowStoreNode(InterpreterSlot interpreterSlotOrd, Value value)
    {
        Node* r = DfgAlloc()->AllocateObject<Node>(NodeKind_ShadowStore);
        r->SetNumInputs(1);
        r->GetInputEdgeForNodeWithFixedNumInputs<1>(0) = Edge(value);
        r->SetNumOutputs(false /*hasDirectOutput*/, 0 /*numExtraOutputs*/);
        r->m_nsdAsU64 = interpreterSlotOrd.Value();
        return r;
    }

    static Node* WARN_UNUSED CreateGetKthVariadicResNode(size_t k, Node* vrProducer)
    {
        TestAssert(vrProducer != nullptr && reinterpret_cast<uint64_t>(vrProducer) != 1);
        TestAssert(vrProducer->IsNodeGeneratesVR());
        Node* r = DfgAlloc()->AllocateObject<Node>(NodeKind_GetKthVariadicRes);
        r->SetNumInputs(0);
        r->SetNumOutputs(true /*hasDirectOutput*/, 0 /*numExtraOutputs*/);
        r->SetNodeAccessesVR(true);
        r->m_variadicResultInput = vrProducer;
        r->m_nsdAsU64 = k;
        return r;
    }

    static Node* WARN_UNUSED CreateGetNumVariadicResNode(Node* vrProducer)
    {
        TestAssert(vrProducer != nullptr && reinterpret_cast<uint64_t>(vrProducer) != 1);
        TestAssert(vrProducer->IsNodeGeneratesVR());
        Node* r = DfgAlloc()->AllocateObject<Node>(NodeKind_GetNumVariadicRes);
        r->SetNumInputs(0);
        r->SetNumOutputs(true /*hasDirectOutput*/, 0 /*numExtraOutputs*/);
        r->SetNodeAccessesVR(true);
        r->m_variadicResultInput = vrProducer;
        return r;
    }

    // The inputs are not set up!
    //
    static Node* WARN_UNUSED CreateCreateVariadicResNode(size_t numFixedTerms)
    {
        Node* r = DfgAlloc()->AllocateObject<Node>(NodeKind_CreateVariadicRes);
        r->SetNumOutputs(false /*hasDirectOutput*/, 0 /*numExtraOutputs*/);
        r->SetNodeGeneratesVR(true);
        r->SetNodeClobbersVR(true);
        r->m_nsdAsU64 = numFixedTerms;
        return r;
    }

    // The inputs are not set up!
    //
    static Node* WARN_UNUSED CreatePrependVariadicResNode(Node* vrProducer)
    {
        TestAssert(reinterpret_cast<uint64_t>(vrProducer) != 1);
        TestAssertImp(vrProducer != nullptr, vrProducer->IsNodeGeneratesVR());
        Node* r = DfgAlloc()->AllocateObject<Node>(NodeKind_PrependVariadicRes);
        r->SetNumOutputs(false /*hasDirectOutput*/, 0 /*numExtraOutputs*/);
        r->SetNodeAccessesVR(true);
        r->SetNodeClobbersVR(true);
        r->SetNodeGeneratesVR(true);
        if (vrProducer == nullptr)
        {
            r->m_variadicResultInput = nullptr;
        }
        else
        {
            r->m_variadicResultInput = vrProducer;
        }
        return r;
    }

    static Node* WARN_UNUSED CreateCheckU64InBoundNode(Value value, uint64_t bound)
    {
        Node* r = DfgAlloc()->AllocateObject<Node>(NodeKind_CheckU64InBound);
        r->SetNumInputs(1);
        r->SetNumOutputs(false /*hasDirectOutput*/, 0 /*numExtraOutputs*/);
        r->GetInputEdgeForNodeWithFixedNumInputs<1>(0) = value;
        r->m_nsdAsU64 = bound;
        return r;
    }

    static Node* WARN_UNUSED CreateU64SaturateSubNode(Value value, int64_t valueToSub)
    {
        Node* r = DfgAlloc()->AllocateObject<Node>(NodeKind_U64SaturateSub);
        r->SetNumInputs(1);
        r->SetNumOutputs(true /*hasDirectOutput*/, 0 /*numExtraOutputs*/);
        r->GetInputEdgeForNodeWithFixedNumInputs<1>(0) = value;
        r->m_nsdAsU64 = static_cast<uint64_t>(valueToSub);
        return r;
    }

    bool IsShadowStoreNode() { return m_nodeKind == NodeKind_ShadowStore; }

    InterpreterSlot WARN_UNUSED GetShadowStoreInterpreterSlotOrd()
    {
        TestAssert(IsShadowStoreNode());
        return InterpreterSlot(m_nsdAsU64);
    }

    void SetNodeOrigin(CodeOrigin origin)
    {
        assert(!m_initializedNodeOrigin);
#ifndef NDEBUG
        m_initializedNodeOrigin = true;
#endif
        TestAssert(!origin.IsInvalid());
        m_nodeOrigin = origin;
    }

    CodeOrigin GetNodeOrigin() { assert(m_initializedNodeOrigin && !m_nodeOrigin.IsInvalid()); return m_nodeOrigin; }

    // Note that all nodes must have an OSR exit destination even if !IsExitOK() or !MayOSRExit(),
    // since it is also used to represent the current semantical program location in bytecode.
    // A node that is not exitOK means that we are in a semantical program location not representable by a bytecode boundary.
    // In that case the osrExitDest should be the bytecode that we have partially executed.
    //
    void SetOsrExitDest(OsrExitDestination dest) { m_osrExitDest = dest; }
    OsrExitDestination GetOsrExitDest() { return m_osrExitDest; }

    void ClearReplacement() { m_replacement = nullptr; }
    ArenaPtr<Node> GetReplacementNodeMaybeNullptr() { return m_replacement; }

    void SetReplacement(Node* replacement)
    {
        TestAssert(replacement != nullptr);
        // We forbid setting replacement for a constant-like node for now because it doesn't seem reasonable,
        // and also the Graph::ClearAllReplacements() currently does not clear replacement for constant-like nodes
        //
        TestAssert(!IsConstantLikeNode());
        m_replacement = replacement;
    }

    // For each input node X, if X has been marked to be replaced by node Y, change the input node to Y.
    //
    void DoReplacementForInputs()
    {
        ForEachInputEdge([&](Edge& e) ALWAYS_INLINE {
            e.ReplaceOperandBasedOnReplacement();
        });
    }

    // In addition to DoReplacementForInputs(), also set the IsNodeReferenced bit for each input node
    //
    void DoReplacementForInputsAndSetReferenceBit()
    {
        ForEachInputEdge([&](Edge& e) ALWAYS_INLINE {
            e.ReplaceOperandBasedOnReplacement();
            TestAssert(e.GetOperand()->IsOutputOrdValid(e.GetOutputOrdinal()));
            e.GetOperand()->SetNodeReferenced(true);
        });
    }

    // For NOP, the only input edges that matter are those that require a type check
    // Remove all edges that does not require a type check
    //
    void CleanUpInputEdgesForNop()
    {
        TestAssert(IsNoopNode());
        size_t numInputs = GetNumInputs();
        if (numInputs <= x_maxInlineOperands)
        {
            size_t newNumInputs = 0;
            for (size_t i = 0; i < numInputs; i++)
            {
                Edge& e = m_inlineOperands[i].m_edge;
                if (e.NeedsTypeCheck())
                {
                    m_inlineOperands[newNumInputs].m_edge = e;
                    newNumInputs++;
                }
            }
            TestAssert(newNumInputs <= x_maxInlineOperands);
            Flags_NumInlinedOperands::Set(m_flags, SafeIntegerCast<uint32_t>(newNumInputs));
            TestAssert(GetNumInputs() == newNumInputs);
        }
        else
        {
            size_t newNumInputs = 0;
            for (uint32_t i = 0; i < x_maxInlineOperands - 1; i++)
            {
                Edge& e = m_inlineOperands[i].m_edge;
                if (e.NeedsTypeCheck())
                {
                    TestAssert(newNumInputs < x_maxInlineOperands - 1);
                    m_inlineOperands[newNumInputs].m_edge = e;
                    newNumInputs++;
                }
            }
            size_t numOutlinedInputs = m_inlineOperands[x_maxInlineOperands - 1].m_numOutlinedEdges;
            TestAssert(numOutlinedInputs > 1);
            Edge* outlinedInputs = m_inlineOperands[x_maxInlineOperands - 1].m_outlinedEdgeArray;
            for (size_t i = 0; i < numOutlinedInputs; i++)
            {
                Edge& e = outlinedInputs[i];
                if (e.NeedsTypeCheck())
                {
                    if (newNumInputs < x_maxInlineOperands - 1)
                    {
                        m_inlineOperands[newNumInputs].m_edge = e;
                        newNumInputs++;
                    }
                    else
                    {
                        outlinedInputs[newNumInputs - (x_maxInlineOperands - 1)] = e;
                        newNumInputs++;
                    }
                }
            }

            if (newNumInputs < x_maxInlineOperands)
            {
                Flags_NumInlinedOperands::Set(m_flags, SafeIntegerCast<uint32_t>(newNumInputs));
            }
            else if (newNumInputs == x_maxInlineOperands)
            {
                m_inlineOperands[x_maxInlineOperands - 1].m_edge = outlinedInputs[0];
                Flags_NumInlinedOperands::Set(m_flags, SafeIntegerCast<uint32_t>(newNumInputs));
            }
            else
            {
                Flags_NumInlinedOperands::Set(m_flags, x_maxInlineOperands + 1);
                size_t newNumOutlinedInputs = newNumInputs - (x_maxInlineOperands - 1);
                m_inlineOperands[x_maxInlineOperands - 1].m_numOutlinedEdges = SafeIntegerCast<uint32_t>(newNumOutlinedInputs);
            }
            TestAssert(GetNumInputs() == newNumInputs);
        }

#ifdef TESTBUILD
        for (uint32_t i = 0; i < GetNumInputs(); i++)
        {
            TestAssert(GetInputEdge(i).NeedsTypeCheck());
        }
#endif
    }

private:
    void SetFlagsForNopNode()
    {
        TestAssert(IsNoopNode());
        SetMayOsrExit(false);
        SetNodeGeneratesVR(false);
        SetNodeAccessesVR(false);
        SetNodeClobbersVR(false);
        SetNodeAccessesVA(false);
        SetNodeMakesTailCallNotConsideringTransform(false);
        SetNodeTailCallTransformedToNormalCall(false);
        SetNodeIsBarrier(false);
        SetNodeHasBranchTarget(false);
        SetNodeAsNotSpecializedForInliner();
        m_variadicResultInput = nullptr;
    }

public:
    // Converts this node to a NOP.
    // All input edges that requires a check are kept, the remaining input edges are removed.
    // All outputs are removed.
    // TODO: we need to correctly set up all the attribute flags
    //
    void ConvertToNop()
    {
        ResetNumOutputs(false /*hasDirectOutput*/, 0 /*numExtraOutputs*/);
        m_nodeKind = NodeKind_Nop;
        SetFlagsForNopNode();
        CleanUpInputEdgesForNop();
    }

    // Converts this node to a NOP and remove all input edges, even if they require checks.
    //
    void ConvertToNopAndForceRemoveAllInputs()
    {
        ResetNumOutputs(false /*hasDirectOutput*/, 0 /*numExtraOutputs*/);
        ResetNumInputs(0);
        m_nodeKind = NodeKind_Nop;
        SetFlagsForNopNode();
    }

private:
    ArenaPtr<Node> m_replacement;

private:
#ifndef NDEBUG
    bool m_initializedNumInputs;
    bool m_initializedNumOutputs;
    bool m_initializedNodeOrigin;
#endif
};
static_assert(offsetof_member_v<&Node::m_nodeKind> == 0);

struct BasicBlock;

struct Phi
{
    // This Phi represents the value of local 'm_localOrd' at the start of 'm_basicBlock'
    //
    BasicBlock* GetBasicBlock() { return m_basicBlock; }
    size_t GetLocalOrd() { return m_localOrd; }

    // Only the unification pass should use this. After unification, Phi always carry LogicalVariableInfo*
    //
    Node* GetPhiOriginNodeForUnification()
    {
        TestAssert(m_compositeValue != 0);
        TestAssert((m_compositeValue & 1ULL) != 0);
        return reinterpret_cast<Node*>(m_compositeValue ^ 1ULL);
    }

    // Only the unification pass should use this
    //
    void SetLogicalVariable(LogicalVariableInfo* info)
    {
        TestAssert(info != nullptr);
        m_compositeValue = reinterpret_cast<uint64_t>(info);
        TestAssert(GetLogicalVariable() == info);
    }

    LogicalVariableInfo* GetLogicalVariable()
    {
        TestAssert(m_compositeValue != 0);
        TestAssert((m_compositeValue & 1ULL) == 0);
        return reinterpret_cast<LogicalVariableInfo*>(m_compositeValue);
    }

    PhiOrNode& IncomingValue(size_t ord)
    {
        TestAssert(ord < m_numChildren);
        return m_children[ord];
    }

    size_t GetNumIncomingValues() { return m_numChildren; }

    // IsTriviallyUndefValue() is true if no SetLocal can ever flow to this Phi
    // It's only used inside ConstructBlockLocalSSA pass to convert a GetLocal where
    // no SetLocal can ever flow into it to UndefValue
    //
    bool IsTriviallyUndefValue() { return !m_canSeeSetLocal; }
    void SetNotTriviallyUndefValue() { m_canSeeSetLocal = true; }

private:
    Phi()
        : m_nodeKindForPhi(NodeKind_Phi)
        , m_canSeeSetLocal(false)
        , m_numChildren(0)
        , m_compositeValue(0)
    { }

    friend class ::TempArenaAllocator;
    friend struct Graph;

    static Phi* Create(TempArenaAllocator& alloc, size_t numChildren, BasicBlock* bb, size_t localOrd, Node* node)
    {
        Phi* r = CreateImpl(alloc, numChildren, bb, localOrd);
        r->SetPhiOriginNode(node);
        return r;
    }

    static Phi* Create(TempArenaAllocator& alloc, size_t numChildren, BasicBlock* bb, size_t localOrd, LogicalVariableInfo* info)
    {
        Phi* r = CreateImpl(alloc, numChildren, bb, localOrd);
        r->SetPhiLogicalVariable(info);
        return r;
    }

    static Phi* CreateImpl(TempArenaAllocator& alloc, size_t numChildren, BasicBlock* bb, size_t localOrd)
    {
        static_assert(offsetof_member_v<&Phi::m_nodeKindForPhi> == 0);
        Phi* r = alloc.AllocateObjectWithTrailingBuffer<Phi>(numChildren * sizeof(PhiOrNode));
        r->m_numChildren = SafeIntegerCast<uint32_t>(numChildren);
        r->m_basicBlock = bb;
        r->m_localOrd = SafeIntegerCast<uint32_t>(localOrd);
        for (size_t i = 0; i < numChildren; i++)
        {
            r->m_children[i] = nullptr;
        }
        return r;
    }

    void SetPhiOriginNode(Node* node)
    {
        TestAssert(node != nullptr);
        TestAssert((reinterpret_cast<uint64_t>(node) & 1ULL) == 0);
        m_compositeValue = reinterpret_cast<uint64_t>(node) | 1ULL;
    }

    void SetPhiLogicalVariable(LogicalVariableInfo* info)
    {
        TestAssert(info != nullptr);
        TestAssert((reinterpret_cast<uint64_t>(info) & 1ULL) == 0);
        m_compositeValue = reinterpret_cast<uint64_t>(info);
    }

    // Must be first member!
    //
    NodeKind m_nodeKindForPhi;
    bool m_canSeeSetLocal;
    uint32_t m_numChildren;
    ArenaPtr<BasicBlock> m_basicBlock;
    uint32_t m_localOrd;

    // This is.. ugly. We use this field to store the Node* before unification, and we use it to store the LogicalVariable* after unification
    //
    uint64_t m_compositeValue;

    // Each value must be either a Phi node or a SetLocal node or an UndefValue node
    //
    PhiOrNode m_children[0];
};

using Edge = Node::Edge;

struct BasicBlock
{
    MAKE_NONCOPYABLE(BasicBlock);
    MAKE_NONMOVABLE(BasicBlock);

private:
    friend class Arena;                 // this class should only be alloc'ed in DFG arena

    BasicBlock()
        : m_localInfoAtHead(nullptr)
        , m_localInfoAtTail(nullptr)
        , m_terminator(nullptr)
        , m_numLocals(static_cast<uint32_t>(-1))
        , m_numSuccessors(static_cast<uint8_t>(-1))
        , m_isReachable(false)
    { }

public:
    Node* GetTerminator()
    {
        TestAssert(!m_terminator.IsNull());
        return m_terminator;
    }

    DVector<ArenaPtr<Node>> m_nodes;

    // An array of length m_numLocals, only available if graph is in BlockLocalSSA form
    //
    // Describes the first thing happened to a local in this basic block.
    // One can also use this info to deduce the value of the local at the start of the basic block.
    //
    // Each value must be one of the following:
    // (1) A Phi: nothing happened to this local inside this BB, the value of this local in this BB
    //     is described by this Phi, the tail variable must be the same Phi.
    // (2) A GetLocal/PhantomLocal: the value at head is the DataFlowInfo (a Phi node) of this GetLocal/PhantomLocal.
    // (3) A SetLocal: the value of the local at the beginning of this basic block does not matter,
    //     since the SetLocal is the first thing done to this local in this basic block.
    // (4) An UndefVal: this local is uninitialized and unused throughout this basic block.
    //     This implies that the tail variable is also UndefVal.
    // (5) nullptr: this local is unused and dead throughout this BB. Tail value must also be nullptr.
    //
    PhiOrNode* m_localInfoAtHead;

    // An array of length m_numLocals, only available if graph is in BlockLocalSSA form
    //
    // Describes the last thing happened to a local in this basic block
    // One can also use this info to deduce the value of this local at the end of the basic block.
    //
    // Each value must be one of the following:
    // (1) A Phi: nothing happened to this local inside this BB, the head variable must be the same Phi.
    // (2) A GetLocal/PhantomLocal: this local is read from, but not written to, in this BB.
    //     The head variable must be the same GetLocal/PhantomLocal, and the value at tail is the
    //     DataFlowInfo (a Phi node) of the GetLocal/PhantomLocal.
    // (3) A SetLocal: the value at tail is the value written by this SetLocal.
    // (4) An UndefVal: this local is uninitialized and unused throughout this basic block.
    // (5) nullptr: this local is unused and dead throughout this BB. Head value must also be nullptr.                                                                                  //
    //
    PhiOrNode* m_localInfoAtTail;

    // Currently our frontend does not have switch, so a basic block always have up to two successors.
    // 0 successor: this is either a function return or a node that certainly throws out an exception
    // 1 successor: this is an unconditional branch
    // 2 successors: this is a conditional branch, the fallthrough successor (i.e., the branch is not taken) always show up first
    //
    ArenaPtr<BasicBlock> m_successors[2];

    // Records the index of this basic block in the predecessor list of the successor
    //
    uint32_t m_predOrdForSuccessors[2];

    DVector<ArenaPtr<BasicBlock>> m_predecessors;

    // The terminator node of this basic block
    // If this block has 0/1 successors, the terminator must be pointing to the last node.
    // If this block has 2 successors, the terminator points to the branchy node, but it is not necessarily the last node,
    // since there can be straightline operations after the branchy node (you should think of the branchy node as outputting
    // a branch direction flag, that is only taken at the end of the basic block).
    //
    ArenaPtr<Node> m_terminator;

    uint32_t m_numLocals;
    uint8_t m_numSuccessors;
    bool m_isReachable;

    size_t GetNumSuccessors()
    {
        TestAssert(m_numSuccessors != static_cast<uint8_t>(-1));
        TestAssert(m_numSuccessors <= 2);
        __builtin_assume(m_numSuccessors <= 2);
        return m_numSuccessors;
    }
};

struct DfgControlFlowAndUpvalueAnalysisResult;
struct BytecodeLiveness;

// This class has a non-trivial destructor but is allocated in an arena allocator,
// so one should always use Graph::Create to allocate it, which returns a unique_ptr
//
struct Graph
{
    MAKE_NONCOPYABLE(Graph);
    MAKE_NONMOVABLE(Graph);

    enum class Form : uint8_t
    {
        // The initial IR graph form generated by the DFG frontend.
        // GetLocal / SetLocal do not have available LogicalVarInfo yet, and Phi information is not available.
        //
        PreUnification,
        // GetLocal / SetLocal have available LogicalVarInfo, but Phi information is not available
        // This form will never degrade to PreUnification form.
        //
        LoadStore,
        // Auxillary Phi information is available for basic block boundaries and GetLocals
        //
        BlockLocalSSA
    };

    static arena_unique_ptr<Graph> WARN_UNUSED Create(CodeBlock* rootCodeBlock)
    {
        Graph* graph = DfgAlloc()->AllocateObject<Graph>(rootCodeBlock);
        return arena_unique_ptr<Graph>(graph);
    }

private:
    friend class Arena;                 // this class should only be alloc'ed in DFG arena

    Graph(CodeBlock* rootCodeBlock)
    {
        m_rootCodeBlock = rootCodeBlock;
        m_undefValNode = Node::CreateUndefValueNode();
        m_functionObjectNode = Node::CreateGetFunctionObjectNode();
        if (rootCodeBlock->m_hasVariadicArguments)
        {
            m_numVarArgsNode = Node::CreateGetNumVarArgsNode();
        }
        else
        {
            m_numVarArgsNode = GetUnboxedConstant(0).m_node;
        }
        m_graphForm = Form::PreUnification;
        m_isCfgAvailable = false;
        // Avoid corner case where we don't have any locals
        //
        m_totalNumLocals = 1;
    }

public:
    CodeBlock* GetRootCodeBlock() { return m_rootCodeBlock; }

    bool IsPreUnificationForm() { return m_graphForm == Form::PreUnification; }
    bool IsLoadStoreForm() { return m_graphForm == Form::LoadStore; }
    bool IsBlockLocalSSAForm() { return m_graphForm == Form::BlockLocalSSA; }
    void DegradeToLoadStoreForm() { m_graphForm = Form::LoadStore; }
    void UpgradeToBlockLocalSSAForm() { m_graphForm = Form::BlockLocalSSA; }

    Value WARN_UNUSED GetConstant(TValue value)
    {
        Node*& r = m_constantCacheMap[value.m_value];
        if (r == nullptr)
        {
            r = Node::CreateConstantNode(value);
        }
        assert(r != nullptr);
        return Value(r, 0 /*outputOrd*/);
    }

    Value WARN_UNUSED GetUnboxedConstant(uint64_t value)
    {
        Node*& r = m_unboxedConstantCacheMap[value];
        if (r == nullptr)
        {
            r = Node::CreateUnboxedConstantNode(value);
        }
        assert(r != nullptr);
        return Value(r, 0 /*outputOrd*/);
    }

    Value WARN_UNUSED GetArgumentNode(size_t argOrd)
    {
        if (argOrd >= m_argumentCacheMap.size())
        {
            m_argumentCacheMap.resize(argOrd + 1, nullptr);
        }
        assert(argOrd < m_argumentCacheMap.size());
        if (m_argumentCacheMap[argOrd] == nullptr)
        {
            m_argumentCacheMap[argOrd] = Node::CreateArgumentNode(argOrd);
        }
        return Value(m_argumentCacheMap[argOrd], 0 /*outputOrd*/);
    }

    Value WARN_UNUSED GetUndefValue()
    {
        return Value(m_undefValNode, 0 /*outputOrd*/);
    }

    // Returns a Value (an unboxed HeapPtr) for the function object for the root function
    //
    Value WARN_UNUSED GetRootFunctionObject()
    {
        return Value(m_functionObjectNode, 0 /*outputOrd*/);
    }

    // Returns a Value (an unboxed uint64_t) for the number of variadic argments passed to the root function
    //
    Value WARN_UNUSED GetRootFunctionNumVarArgs()
    {
        return Value(m_numVarArgsNode, 0 /*outputOrd*/);
    }

    Value WARN_UNUSED GetRootFunctionVariadicArg(size_t varArgOrd)
    {
        if (varArgOrd >= m_variadicArgumentCacheMap.size())
        {
            m_variadicArgumentCacheMap.resize(varArgOrd + 1, nullptr);
        }
        assert(varArgOrd < m_variadicArgumentCacheMap.size());
        if (m_variadicArgumentCacheMap[varArgOrd] == nullptr)
        {
            m_variadicArgumentCacheMap[varArgOrd] = Node::CreateGetKthVariadicArgNode(varArgOrd);
        }
        return Value(m_variadicArgumentCacheMap[varArgOrd], 0 /*outputOrd*/);
    }

    void RegisterNewInlinedCallFrame(InlinedCallFrame* callFrame)
    {
        callFrame->SetInlineCallFrameOrdinal(static_cast<uint32_t>(m_allInlineCallFrames.size()));
        m_allInlineCallFrames.push_back(callFrame);
    }

    // It's possible that the same CodeBlock gets inlined multiple times in different places,
    // in which case we will get multiple InlinedCallFrame with the same CodeBlock.
    // It's wasteful to redundantly compute BytecodeLiveness in such cases, so we simply let the Graph
    // holds the cache map from CodeBlock to BytecodeLiveness here.
    //
    void RegisterBytecodeLivenessInfo(InlinedCallFrame* callFrame, const DfgControlFlowAndUpvalueAnalysisResult& cfUvInfo)
    {
        CodeBlock* cb = callFrame->GetCodeBlock();
        if (!m_bytecodeLivenessInfo.count(cb))
        {
            BytecodeLiveness* info = BytecodeLiveness::ComputeBytecodeLiveness(cb, cfUvInfo);
            m_bytecodeLivenessInfo[cb] = info;
        }

        TestAssert(m_bytecodeLivenessInfo.count(cb));
        BytecodeLiveness* bytecodeLiveness = m_bytecodeLivenessInfo[cb];
        callFrame->SetBytecodeLivenessInfo(bytecodeLiveness);
    }

    size_t GetNumInlinedCallFrames() { return m_allInlineCallFrames.size(); }
    InlinedCallFrame* GetInlinedCallFrameFromOrdinal(size_t ord)
    {
        TestAssert(ord < m_allInlineCallFrames.size());
        return m_allInlineCallFrames[ord];
    }

    Phi* AllocatePhi(size_t numChildren, BasicBlock* bb, size_t localOrd, Node* originNode)
    {
        return Phi::Create(m_phiNodeAllocator, numChildren, bb, localOrd, originNode);
    }

    Phi* AllocatePhi(size_t numChildren, BasicBlock* bb, size_t localOrd, LogicalVariableInfo* logicalVariable)
    {
        return Phi::Create(m_phiNodeAllocator, numChildren, bb, localOrd, logicalVariable);
    }

    void FreeMemoryForAllPhi()
    {
        m_phiNodeAllocator.Reset();
    }

    void UpdateTotalNumLocals(uint32_t numLocals)
    {
        m_totalNumLocals = std::max(m_totalNumLocals, numLocals);
    }

    uint32_t GetTotalNumLocals()
    {
        return m_totalNumLocals;
    }

    void RegisterLogicalVariable(LogicalVariableInfo* info)
    {
        size_t ord = m_allLogicalVariables.size();
        info->SetLogicalVariableOrdinal(SafeIntegerCast<uint32_t>(ord));
        m_allLogicalVariables.push_back(info);
    }

    const DVector<ArenaPtr<LogicalVariableInfo>>& GetAllLogicalVariables()
    {
        return m_allLogicalVariables;
    }

    void ClearAllReplacements()
    {
        for (BasicBlock* bb : m_blocks)
        {
            for (Node* node : bb->m_nodes)
            {
                node->ClearReplacement();
            }
        }
    }

    void ClearAllReplacementsAndIsReferencedBit()
    {
        for (BasicBlock* bb : m_blocks)
        {
            for (Node* node : bb->m_nodes)
            {
                node->ClearReplacement();
                node->SetNodeReferenced(false);
            }
        }
    }

    // Assert that no node references another node that is marked for replacement
    //
    void AssertReplacementIsComplete()
    {
#ifdef TESTBUILD
        for (BasicBlock* bb : m_blocks)
        {
            for (Node* node : bb->m_nodes)
            {
                uint32_t numInputs = node->GetNumInputs();
                for (uint32_t inputOrd = 0; inputOrd < numInputs; inputOrd++)
                {
                    Edge& e = node->GetInputEdge(inputOrd);
                    TestAssert(e.GetOperand()->GetReplacementNodeMaybeNullptr().IsNull());
                    TestAssert(e.GetOperand()->IsOutputOrdValid(e.GetOutputOrdinal()));
                }
            }
        }
#endif
    }

    // Recompute each BB's m_isReachable and predecessor list after the CFG changes
    // Unreachable block's predecessor list is set to empty
    //
    void ComputeReachabilityAndPredecessors()
    {
        TempArenaAllocator alloc;
        TestAssert(m_blocks.size() > 0);
        for (BasicBlock* bb : m_blocks)
        {
            bb->m_isReachable = false;
            bb->m_predecessors.clear();
        }
        TempVector<BasicBlock*> worklist(alloc);
        worklist.push_back(m_blocks[0]);
        m_blocks[0]->m_isReachable = true;
        while (!worklist.empty())
        {
            BasicBlock* bb = worklist.back();
            worklist.pop_back();
            for (size_t i = 0; i < bb->GetNumSuccessors(); i++)
            {
                BasicBlock* succ = bb->m_successors[i];
                if (!succ->m_isReachable)
                {
                    succ->m_isReachable = true;
                    worklist.push_back(succ);
                }
            }
        }
        for (BasicBlock* bb : m_blocks)
        {
            if (bb->m_isReachable)
            {
                TestAssert(bb->GetNumSuccessors() <= 2);
                for (size_t i = 0; i < bb->GetNumSuccessors(); i++)
                {
                    BasicBlock* succ = bb->m_successors[i];
                    TestAssert(succ->m_isReachable);
                    // Do not insert duplicate edges. This works since we know bb->m_numSuccessors <= 2 (as asserted above)
                    //
                    if (i == 1 && succ == bb->m_successors[0])
                    {
                        bb->m_predOrdForSuccessors[i] = bb->m_predOrdForSuccessors[0];
                        TestAssert(succ->m_predecessors[bb->m_predOrdForSuccessors[i]] == bb);
                        continue;
                    }
                    bb->m_predOrdForSuccessors[i] = SafeIntegerCast<uint32_t>(succ->m_predecessors.size());
                    succ->m_predecessors.push_back(bb);
                    TestAssert(succ->m_predecessors[bb->m_predOrdForSuccessors[i]] == bb);
                }
            }
        }
        m_isCfgAvailable = true;
    }

    // True if the predecessor info is up-to-date in the basic blocks
    // Code that invalidates the info is responsible for calling InvalidateCfg()
    //
    bool IsCfgAvailable() { return m_isCfgAvailable; }
    void InvalidateCfg() { m_isCfgAvailable = false; }

    // You cannot use this if you are *speculatively* removing a basic block!
    // This is only intended to be used by the DFG frontend to remove trivially dead basic block in the initial IR graph.
    //
    void RemoveTriviallyUnreachableBlocks()
    {
        size_t numSurvivedBlocks = 0;
        TestAssert(m_blocks.size() > 0 && m_blocks[0]->m_isReachable);
        for (size_t i = 0; i < m_blocks.size(); i++)
        {
            if (m_blocks[i]->m_isReachable)
            {
                m_blocks[numSurvivedBlocks] = m_blocks[i];
                numSurvivedBlocks++;
            }
        }
        m_blocks.resize(numSurvivedBlocks);
    }

    BasicBlock* GetEntryBB()
    {
        TestAssert(m_blocks.size() > 0);
        return m_blocks[0];
    }

    DVector<BasicBlock*> m_blocks;

private:
    CodeBlock* m_rootCodeBlock;
    DUnorderedMap<uint64_t /*tv*/, Node*> m_constantCacheMap;
    DUnorderedMap<uint64_t /*value*/, Node*> m_unboxedConstantCacheMap;
    DVector<Node*> m_argumentCacheMap;
    DVector<Node*> m_variadicArgumentCacheMap;
    DVector<ArenaPtr<InlinedCallFrame>> m_allInlineCallFrames;
    ArenaPtr<Node> m_undefValNode;
    ArenaPtr<Node> m_functionObjectNode;
    ArenaPtr<Node> m_numVarArgsNode;
    DUnorderedMap<CodeBlock*, BytecodeLiveness*> m_bytecodeLivenessInfo;
    TempArenaAllocator m_phiNodeAllocator;
    DVector<ArenaPtr<LogicalVariableInfo>> m_allLogicalVariables;
    uint32_t m_totalNumLocals;
    Form m_graphForm;
    bool m_isCfgAvailable;
};

inline void Node::RegisterLogicalVariable(Graph* graph, LogicalVariableInfo* info)
{
    graph->RegisterLogicalVariable(info);
}

}   // namespace dfg
