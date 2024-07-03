#pragma once

#include "common_utils.h"
#include "dfg_arena.h"
#include "dfg_virtual_register.h"
#include "runtime_utils.h"
#include "dfg_bytecode_liveness.h"

namespace dfg {

struct InlinedCallFrame;

// Describes a bytecode location inside a (possibly nested inlined) function
//
struct CodeOrigin
{
    CodeOrigin() : m_func(nullptr), m_bytecodeIndex(0) { }

    CodeOrigin(ArenaPtr<InlinedCallFrame> callFrame, size_t bytecodeIndex)
        : m_func(callFrame)
        , m_bytecodeIndex(SafeIntegerCast<uint32_t>(bytecodeIndex))
    { }

    CodeOrigin(InlinedCallFrame* callFrame, size_t bytecodeIndex)
        : m_func(callFrame)
        , m_bytecodeIndex(SafeIntegerCast<uint32_t>(bytecodeIndex))
    { }

    bool IsInvalid() const { return m_func.IsNull(); }

    InlinedCallFrame* GetInlinedCallFrame() const
    {
        TestAssert(!IsInvalid());
        return m_func;
    }

    uint32_t GetBytecodeIndex() const
    {
        TestAssert(!IsInvalid());
        return m_bytecodeIndex;
    }

    friend bool operator==(const CodeOrigin& a, const CodeOrigin& b)
    {
        return a.m_func.m_value == b.m_func.m_value && a.m_bytecodeIndex == b.m_bytecodeIndex;
    }

    friend bool operator!=(const CodeOrigin& a, const CodeOrigin& b)
    {
        return !(a.m_func.m_value == b.m_func.m_value && a.m_bytecodeIndex == b.m_bytecodeIndex);
    }

private:
    friend struct OsrExitDestination;

    ArenaPtr<InlinedCallFrame> m_func;
    uint32_t m_bytecodeIndex;
};
static_assert(sizeof(CodeOrigin) == 8);

// Describes the destination of an OSR exit
// This is mostly a CodeOrigin, except that it must handle a special case: an OSR exit happens after the semantic part
// of a conditional branch has executed, but before the branch was physically taken (for example, the conditional branch also
// has writes to the stack frame, so it's followed by SetLocals, and one of the SetLocals exited).
//
// Therefore, an OsrExitDestination is a <flag, codeOrigin>
// If flag is false, the exit destination is simply codeOrigin
// If flag is true, then the exit destination is one of the branch destinations of codeOrigin, depending on the direction
// of the branch taken (the branchy opcode needs to output this info anyway since even in normal execution, it cannot
// branch directly as there are SetLocals after it).
//
struct OsrExitDestination
{
    OsrExitDestination()
        : m_compositeValue(0)
        , m_bytecodeIndex(0)
    { }

    OsrExitDestination(bool isBranchDest, CodeOrigin dest)
    {
        ArenaPtr<InlinedCallFrame> p = dest.m_func;
        assert((p.m_value & 1U) == 0);
        m_compositeValue = p.m_value | (isBranchDest ? 1U : 0U);
        m_bytecodeIndex = dest.m_bytecodeIndex;
    }

    bool IsInvalid() { return m_compositeValue == 0; }

    bool IsBranchDest() { return (m_compositeValue & 1U); }

    CodeOrigin GetNormalDestination()
    {
        TestAssert(!IsBranchDest());
        ArenaPtr<InlinedCallFrame> p;
        p.m_value = m_compositeValue;
        assert((p.m_value & 1U) == 0);
        return CodeOrigin(p, m_bytecodeIndex);
    }

    CodeOrigin GetBranchBytecodeOrigin()
    {
        TestAssert(IsBranchDest());
        ArenaPtr<InlinedCallFrame> p;
        p.m_value = m_compositeValue ^ 1U;
        assert((p.m_value & 1U) == 0);
        return CodeOrigin(p, m_bytecodeIndex);
    }

    friend bool operator==(const OsrExitDestination& a, const OsrExitDestination& b)
    {
        return a.m_compositeValue == b.m_compositeValue && a.m_bytecodeIndex == b.m_bytecodeIndex;
    }

    friend bool operator!=(const OsrExitDestination& a, const OsrExitDestination& b)
    {
        return !(a.m_compositeValue == b.m_compositeValue && a.m_bytecodeIndex == b.m_bytecodeIndex);
    }

private:
    // An ArenaPtr<InlinedCallFrame>, bit 0 is stolen as a flag
    //
    uint32_t m_compositeValue;
    uint32_t m_bytecodeIndex;
};
static_assert(sizeof(OsrExitDestination) == 8);

// Describes a function that is inlined into a specific location
//
struct InlinedCallFrame
{
    MAKE_NONCOPYABLE(InlinedCallFrame);
    MAKE_NONMOVABLE(InlinedCallFrame);

private:
    friend class Arena;          // can only be alloc'ed in DFG arena
    InlinedCallFrame() = default;

public:
    bool IsRootFrame() { return m_caller.IsInvalid(); }
    CodeBlock* GetCodeBlock() { return m_codeBlock; }
    CodeOrigin GetCallerCodeOrigin() { TestAssert(!IsRootFrame()); return m_caller; }

    VirtualRegister GetVirtualRegisterForLocation(InterpreterFrameLocation ifLoc)
    {
        if (likely(ifLoc.IsLocal()))
        {
            return GetRegisterForLocalOrd(ifLoc.LocalOrd());
        }
        else if (ifLoc.IsVarArg())
        {
            return GetRegisterForVarArgOrd(ifLoc.VarArgOrd());
        }
        else if (ifLoc.IsFunctionObjectLoc())
        {
            return GetClosureCallFunctionObjectRegister();
        }
        else
        {
            TestAssert(ifLoc.IsNumVarArgsLoc());
            return GetNumVarArgsRegister();
        }
    }

    InterpreterSlot GetInterpreterSlotForLocation(InterpreterFrameLocation ifLoc)
    {
        if (likely(ifLoc.IsLocal()))
        {
            return GetInterpreterSlotForLocalOrd(ifLoc.LocalOrd());
        }
        else if (ifLoc.IsVarArg())
        {
            return GetInterpreterSlotForVariadicArgument(ifLoc.VarArgOrd());
        }
        else if (ifLoc.IsFunctionObjectLoc())
        {
            return GetInterpreterSlotForStackFrameHeader(0);
        }
        else
        {
            TestAssert(ifLoc.IsNumVarArgsLoc());
            return GetInterpreterSlotForStackFrameHeader(1);
        }
    }

    void AssertFrameLocationValid(InterpreterFrameLocation TESTBUILD_ONLY(ifLoc))
    {
        // If it's invalid, the operations below will fire assertion
        //
#ifdef TESTBUILD
        std::ignore = GetVirtualRegisterForLocation(ifLoc);
        std::ignore = GetInterpreterSlotForLocation(ifLoc);
#endif
    }

    InterpreterSlot GetInterpreterSlotForLocalOrd(size_t localOrd)
    {
        return InterpreterSlot(m_baseInterpreterFrameSlot + localOrd);
    }

    InterpreterSlot GetInterpreterSlotForStackFrameHeader(size_t slot)
    {
        TestAssert(!IsRootFrame());
        TestAssert(m_baseInterpreterFrameSlot >= x_numSlotsForStackFrameHeader);
        TestAssert(slot < x_numSlotsForStackFrameHeader);
        return InterpreterSlot(m_baseInterpreterFrameSlot - x_numSlotsForStackFrameHeader + slot);
    }

    // Only applies for non-root frame since we do not statically know how many variadic arguments the
    // root frame may take
    //
    InterpreterSlot GetInterpreterSlotForFrameStart()
    {
        TestAssert(!IsRootFrame());
        TestAssert(m_baseInterpreterFrameSlot >= x_numSlotsForStackFrameHeader + m_maxVariadicArguments);
        return InterpreterSlot(m_baseInterpreterFrameSlot - x_numSlotsForStackFrameHeader - m_maxVariadicArguments);
    }

    // Return the first interpreter slot that does not belong to this call frame
    //
    InterpreterSlot GetInterpreterSlotForFrameEnd()
    {
        return InterpreterSlot(m_baseInterpreterFrameSlot + m_numBytecodeLocals);
    }

    InterpreterSlot GetInterpreterSlotForStackFrameBase()
    {
        return InterpreterSlot(m_baseInterpreterFrameSlot);
    }

    // Note that in the DFG-reconstructed stack frame described by ShadowStores, the variadic arguments
    // is a fixed-length sequence before the stack frame header (since we must give each value a
    // statically-known ordinal; this is also why we have to speculate on the maximum number of variadic
    // arguments). However, the interpreter expects the variadic arguments to be right before the stack
    // frame header. So in an OSR-exit, after we reconstructed the stack frame, we must move the variadic
    // arguments to make the interpreter happy (and additionally, if every call in the call frame is a
    // tail call, we must get rid of the empty space to maintain the no-unbounded-growth property).
    //
    // This function returns the interpreter slot for the DFG-reconstructed stack frame.
    //
    InterpreterSlot GetInterpreterSlotForVariadicArgument(size_t vaOrd)
    {
        TestAssert(!IsRootFrame());
        TestAssert(vaOrd < m_maxVariadicArguments);
        TestAssert(m_baseInterpreterFrameSlot >= x_numSlotsForStackFrameHeader + m_maxVariadicArguments);
        size_t vaBase = m_baseInterpreterFrameSlot - x_numSlotsForStackFrameHeader - m_maxVariadicArguments;
        return InterpreterSlot(vaBase + vaOrd);
    }

    uint32_t GetNumBytecodeLocals() { return m_numBytecodeLocals; }

    bool IsDirectCall() { TestAssert(!IsRootFrame()); return m_isDirectCall; }
    bool IsTailCall() { TestAssert(!IsRootFrame()); return m_isTailCall; }
    bool StaticallyKnowsNumVarArgs() { TestAssert(!IsRootFrame()); return m_canStaticallyDetermineNumVarArgs; }

    size_t MaxVarArgsAllowed() { TestAssert(!IsRootFrame()); return m_maxVariadicArguments; }
    size_t GetNumVarArgs() { TestAssert(StaticallyKnowsNumVarArgs()); return m_maxVariadicArguments; }

    uint8_t GetCallerBytecodeCallSiteOrdinal() { TestAssert(!IsRootFrame()); return m_callerBcCallSiteOrdinal; }
    uint32_t GetInlineCallFrameOrdinal() { TestAssert(m_inlineCallFrameOrdinal != static_cast<uint32_t>(-1)); return m_inlineCallFrameOrdinal; }

    void SetInlineCallFrameOrdinal(uint32_t ordinal)
    {
        TestAssert(m_inlineCallFrameOrdinal == static_cast<uint32_t>(-1) && ordinal != static_cast<uint32_t>(-1));
        m_inlineCallFrameOrdinal = ordinal;
        TestAssertImp(!IsRootFrame(), m_inlineCallFrameOrdinal > m_caller.GetInlinedCallFrame()->GetInlineCallFrameOrdinal());
    }

    VirtualRegister GetRegisterForLocalOrd(size_t localOrd)
    {
        TestAssert(localOrd < m_numBytecodeLocals);
        TestAssert(m_localToRegisterMap[localOrd] != static_cast<uint32_t>(-1));
        return VirtualRegister(m_localToRegisterMap[localOrd]);
    }

    void SetRegisterForLocalOrd(size_t localOrd, VirtualRegister vreg)
    {
        TestAssert(localOrd < m_numBytecodeLocals);
        TestAssert(m_localToRegisterMap[localOrd] == static_cast<uint32_t>(-1));
        m_localToRegisterMap[localOrd] = SafeIntegerCast<uint32_t>(vreg.Value());
    }

    VirtualRegister GetRegisterForVarArgOrd(size_t varArgOrd)
    {
        TestAssert(!IsRootFrame());
        TestAssert(varArgOrd < MaxVarArgsAllowed());
        TestAssert(m_variadicArgumentToRegisterMap[varArgOrd] != static_cast<uint32_t>(-1));
        return VirtualRegister(m_variadicArgumentToRegisterMap[varArgOrd]);
    }

    void SetRegisterForVarArgOrd(size_t vaOrd, VirtualRegister vreg)
    {
        TestAssert(vaOrd < m_maxVariadicArguments);
        TestAssert(m_variadicArgumentToRegisterMap[vaOrd] == static_cast<uint32_t>(-1));
        m_variadicArgumentToRegisterMap[vaOrd] = SafeIntegerCast<uint32_t>(vreg.Value());
    }

    void SetClosureCallFunctionObjectRegister(VirtualRegister vreg)
    {
        TestAssert(!IsDirectCall());
        TestAssert(m_functionObjectVR == static_cast<uint32_t>(-1));
        m_functionObjectVR = SafeIntegerCast<uint32_t>(vreg.Value());
    }

    void SetDirectCallFunctionObject(FunctionObject* func)
    {
        TestAssert(IsDirectCall());
        TestAssert(m_functionObject == nullptr && func != nullptr);
        TestAssert(func->m_executable.As() == GetCodeBlock());
        m_functionObject = func;
    }

    VirtualRegister GetClosureCallFunctionObjectRegister()
    {
        TestAssert(!IsDirectCall() && m_functionObjectVR != static_cast<uint32_t>(-1));
        return VirtualRegister(m_functionObjectVR);
    }

    FunctionObject* GetDirectCallFunctionObject()
    {
        TestAssert(IsDirectCall() && m_functionObject != nullptr);
        return m_functionObject;
    }

    void SetNumVarArgsRegister(VirtualRegister vreg)
    {
        TestAssert(!StaticallyKnowsNumVarArgs() && m_numVarArgsVR == static_cast<uint32_t>(-1));
        m_numVarArgsVR = SafeIntegerCast<uint32_t>(vreg.Value());
    }

    VirtualRegister GetNumVarArgsRegister()
    {
        TestAssert(!StaticallyKnowsNumVarArgs() && m_numVarArgsVR != static_cast<uint32_t>(-1));
        return VirtualRegister(m_numVarArgsVR);
    }

    // If true, it means every call from the root frame to this frame is a proper tail call.
    // This means that when executing a tail call in this frame, we are required to abide to the proper tail
    // call semantic and overtake the position of the root frame, and return to the root frame's parent.
    //
    // Conversely, if false, it means that we can just execute a tail call in this frame as if it's a normal
    // call (followed a return that returns everything to our parent). This is because proper tail call only
    // requires us to provide no-unbounded-growth guarantee for infinite tail-call chains (and no infinite
    // tail-call chain involving the root function can form, as the root function already made a normal call
    // to reach this frame).
    //
    bool IsTailCallAllTheWayDown()
    {
        return m_parentFrameForReturn == nullptr;
    }

    InlinedCallFrame* GetParentFrame()
    {
        return GetCallerCodeOrigin().GetInlinedCallFrame();
    }

    InlinedCallFrame* GetParentFrameForReturn()
    {
        return m_parentFrameForReturn;
    }

    static InlinedCallFrame* WARN_UNUSED CreateRootFrame(CodeBlock* codeBlock,
                                                         VirtualRegisterAllocator& vra /*inout*/)
    {
        InlinedCallFrame* r = DfgAlloc()->AllocateObject<InlinedCallFrame>();
        r->m_codeBlock = codeBlock;
        r->m_caller = CodeOrigin();
        r->m_parentFrameForReturn = nullptr;
        r->m_isDirectCall = false;
        r->m_isTailCall = false;
        r->m_canStaticallyDetermineNumVarArgs = false;
        r->m_callerBcCallSiteOrdinal = static_cast<uint8_t>(-1);
        r->m_inlineCallFrameOrdinal = static_cast<uint32_t>(-1);
        r->m_maxVariadicArguments = 0;
        r->m_baseInterpreterFrameSlot = 0;
        size_t numLocals = codeBlock->m_stackFrameNumSlots;
        r->m_numBytecodeLocals = SafeIntegerCast<uint32_t>(numLocals);
        r->m_localToRegisterMap = DfgAlloc()->AllocateArray<uint32_t>(numLocals);
        for (size_t i = 0; i < numLocals; i++)
        {
            r->m_localToRegisterMap[i] = SafeIntegerCast<uint32_t>(vra.Allocate().Value());
            TestAssert(r->m_localToRegisterMap[i] == i);
        }
        r->m_variadicArgumentToRegisterMap = nullptr;
        r->m_functionObjectVR = static_cast<uint32_t>(-1);
        r->m_numVarArgsVR = static_cast<uint32_t>(-1);
        r->m_functionObject = nullptr;
        r->m_bytecodeLiveness = nullptr;
        r->m_virtualRegisterVectorLength = static_cast<uint32_t>(-1);
        r->m_isVirtualRegisterUsed = nullptr;
        r->m_virtualRegisterMappingBeforeThisFrame = nullptr;
        return r;
    }

    // Note that the VirtualRegister mapping information are not set up in this call
    //
    static InlinedCallFrame* WARN_UNUSED CreateInlinedFrame(CodeBlock* codeBlock,
                                                            CodeOrigin caller,
                                                            bool isDirectCall,
                                                            bool isTailCall,
                                                            uint8_t bcCallSiteOrd,
                                                            bool canStaticallyDetermineNumVarArgs,
                                                            uint32_t maxVariadicArguments,
                                                            InterpreterSlot baseInterpreterFrameSlot)
    {
        InlinedCallFrame* r = DfgAlloc()->AllocateObject<InlinedCallFrame>();
        r->m_codeBlock = codeBlock;
        r->m_caller = caller;
        TestAssert(!caller.IsInvalid());
        r->m_isDirectCall = isDirectCall;
        r->m_isTailCall = isTailCall;
        r->m_canStaticallyDetermineNumVarArgs = canStaticallyDetermineNumVarArgs;
        TestAssertImp(!codeBlock->m_owner->m_hasVariadicArguments, canStaticallyDetermineNumVarArgs && maxVariadicArguments == 0);
        r->m_callerBcCallSiteOrdinal = bcCallSiteOrd;
        r->m_inlineCallFrameOrdinal = static_cast<uint32_t>(-1);
        r->m_maxVariadicArguments = maxVariadicArguments;
        r->m_baseInterpreterFrameSlot = SafeIntegerCast<uint32_t>(baseInterpreterFrameSlot.Value());
        TestAssert(baseInterpreterFrameSlot.Value() >= x_numSlotsForStackFrameHeader + maxVariadicArguments);
        size_t numLocals = codeBlock->m_stackFrameNumSlots;
        r->m_numBytecodeLocals = SafeIntegerCast<uint32_t>(numLocals);
        r->m_localToRegisterMap = DfgAlloc()->AllocateArray<uint32_t>(numLocals);
        for (size_t i = 0; i < numLocals; i++)
        {
            r->m_localToRegisterMap[i] = static_cast<uint32_t>(-1);
        }
        if (maxVariadicArguments > 0)
        {
            r->m_variadicArgumentToRegisterMap = DfgAlloc()->AllocateArray<uint32_t>(maxVariadicArguments);
            for (size_t i = 0; i < maxVariadicArguments; i++)
            {
                r->m_variadicArgumentToRegisterMap[i] = static_cast<uint32_t>(-1);
            }
        }
        else
        {
            r->m_variadicArgumentToRegisterMap = nullptr;
        }
        r->m_functionObjectVR = static_cast<uint32_t>(-1);
        r->m_numVarArgsVR = static_cast<uint32_t>(-1);
        r->m_functionObject = nullptr;
        r->m_parentFrameForReturn = r->FindParentFrameForReturn();
        r->m_bytecodeLiveness = nullptr;
        r->m_virtualRegisterVectorLength = static_cast<uint32_t>(-1);
        r->m_isVirtualRegisterUsed = nullptr;
        r->m_virtualRegisterMappingBeforeThisFrame = DfgAlloc()->AllocateArray<VirtualRegisterMappingInfo>(r->m_baseInterpreterFrameSlot);
        return r;
    }

    // Assert that the VirtualRegister mapping in m_inlinedCallFrame is consistent with vrState
    //
    void AssertVirtualRegisterConsistency(const VirtualRegisterAllocator& TESTBUILD_ONLY(vrState))
    {
#ifdef TESTBUILD
        size_t n = vrState.GetVirtualRegisterVectorLength();
        TempArenaAllocator& alloc = vrState.GetFreeList().get_allocator().m_alloc;
        bool* inUse = alloc.AllocateArray<bool>(n);
        for (size_t i = 0; i < n; i++) { inUse[i] = true; }
        for (uint32_t ord : vrState.GetFreeList())
        {
            TestAssert(ord < n);
            TestAssert(inUse[ord]);
            inUse[ord] = false;
        }

        auto markVReg = [&](VirtualRegister vreg) ALWAYS_INLINE
        {
            TestAssert(vreg.Value() < n);
            TestAssert(inUse[vreg.Value()]);
            inUse[vreg.Value()] = false;
        };

        for (size_t i = 0; i < m_numBytecodeLocals; i++)
        {
            markVReg(GetRegisterForLocalOrd(i));
        }

        if (!IsRootFrame())
        {
            for (size_t i = 0; i < MaxVarArgsAllowed(); i++)
            {
                markVReg(GetRegisterForVarArgOrd(i));
            }
            if (!IsDirectCall())
            {
                markVReg(GetClosureCallFunctionObjectRegister());
            }
            if (!StaticallyKnowsNumVarArgs())
            {
                markVReg(GetNumVarArgsRegister());
            }
        }
#endif
    }

    // The high watermark of the VirtualRegister vector length for this function and all functions inlined into this function
    // Note that this info is only accurate after IR generation for this InlinedCallFrame has completed.
    //
    uint32_t GetVirtualRegisterVectorLength()
    {
        TestAssert(m_virtualRegisterVectorLength != static_cast<uint32_t>(-1));
        return m_virtualRegisterVectorLength;
    }

    void InitializeVirtualRegisterUsageArray(uint32_t vregVectorLength)
    {
        TestAssert(m_virtualRegisterVectorLength == static_cast<uint32_t>(-1));
        m_virtualRegisterVectorLength = vregVectorLength;
        TestAssert(m_isVirtualRegisterUsed == nullptr);
        m_isVirtualRegisterUsed = DfgAlloc()->AllocateArray<bool>(vregVectorLength);
        for (size_t i = 0; i < vregVectorLength; i++)
        {
            m_isVirtualRegisterUsed[i] = false;
        }

        auto markVReg = [&](VirtualRegister vreg) ALWAYS_INLINE
        {
            TestAssert(vreg.Value() < vregVectorLength);
            TestAssert(!m_isVirtualRegisterUsed[vreg.Value()]);
            m_isVirtualRegisterUsed[vreg.Value()] = true;
        };

        size_t numLocals = m_numBytecodeLocals;
        for (size_t i = 0; i < numLocals; i++)
        {
            markVReg(GetRegisterForLocalOrd(i));
        }

        if (!IsRootFrame())
        {
            size_t maxVarArgsAllowed = MaxVarArgsAllowed();
            for (size_t i = 0; i < maxVarArgsAllowed; i++)
            {
                markVReg(GetRegisterForVarArgOrd(i));
            }
            if (!IsDirectCall())
            {
                markVReg(GetClosureCallFunctionObjectRegister());
            }
            if (!StaticallyKnowsNumVarArgs())
            {
                markVReg(GetNumVarArgsRegister());
            }
        }
    }

    void UpdateVirtualRegisterUsageArray(InlinedCallFrame* callee)
    {
        TestAssert(callee->GetParentFrame() == this);
        TestAssert(m_virtualRegisterVectorLength != static_cast<uint32_t>(-1));
        TestAssert(callee->m_virtualRegisterVectorLength != static_cast<uint32_t>(-1));
        if (callee->m_virtualRegisterVectorLength > m_virtualRegisterVectorLength)
        {
            bool* newArray = DfgAlloc()->AllocateArray<bool>(callee->m_virtualRegisterVectorLength);
            memcpy(newArray, callee->m_isVirtualRegisterUsed, sizeof(bool) * callee->m_virtualRegisterVectorLength);
            for (size_t i = 0; i < m_virtualRegisterVectorLength; i++)
            {
                newArray[i] |= m_isVirtualRegisterUsed[i];
            }
            m_isVirtualRegisterUsed = newArray;
            m_virtualRegisterVectorLength = callee->m_virtualRegisterVectorLength;
        }
        else
        {
            for (size_t i = 0; i < callee->m_virtualRegisterVectorLength; i++)
            {
                m_isVirtualRegisterUsed[i] |= callee->m_isVirtualRegisterUsed[i];
            }
        }
    }

    // Return true if 'vreg' is used by this function or any function further inlined by this function
    // Note that this info is only accurate after IR generation for this InlinedCallFrame has completed.
    //
    bool IsVirtualRegisterUsedConsideringInlines(VirtualRegister vreg)
    {
        TestAssert(m_virtualRegisterVectorLength != static_cast<uint32_t>(-1));
        size_t ord = vreg.Value();
        if (ord > m_virtualRegisterVectorLength)
        {
            return false;
        }
        return m_isVirtualRegisterUsed[ord];
    }

    // This function is only supposed to be called by Graph::RegisterBytecodeLivenessInfo
    //
    void SetBytecodeLivenessInfo(BytecodeLiveness* ptr)
    {
        TestAssert(m_bytecodeLiveness == nullptr && ptr != nullptr);
        m_bytecodeLiveness = ptr;
    }

    BytecodeLiveness& BytecodeLivenessInfo()
    {
        TestAssert(m_bytecodeLiveness != nullptr);
        return *m_bytecodeLiveness;
    }

    void SetInterpreterSlotBeforeFrameBaseMapping(InterpreterSlot slot, VirtualRegisterMappingInfo info)
    {
        size_t slotOrd = slot.Value();
        TestAssert(slotOrd < m_baseInterpreterFrameSlot);
        TestAssert(!m_virtualRegisterMappingBeforeThisFrame[slotOrd].IsInitialized());
        TestAssert(info.IsInitialized());
        m_virtualRegisterMappingBeforeThisFrame[slotOrd] = info;
    }

    void AssertVirtualRegisterMappingBeforeThisFrameComplete()
    {
#ifdef TESTBUILD
        for (size_t i = 0; i < m_baseInterpreterFrameSlot; i++)
        {
            TestAssert(m_virtualRegisterMappingBeforeThisFrame[i].IsInitialized());
        }
#endif
    }

    VirtualRegisterMappingInfo GetVirtualRegisterInfoForInterpreterSlotBeforeFrameBase(InterpreterSlot slot)
    {
        size_t slotOrd = slot.Value();
        TestAssert(slotOrd < m_baseInterpreterFrameSlot);
        TestAssert(m_virtualRegisterMappingBeforeThisFrame[slotOrd].IsInitialized());
        return m_virtualRegisterMappingBeforeThisFrame[slotOrd];
    }

    bool IsInterpreterSlotBeforeFrameBaseLive(InterpreterSlot slot)
    {
        return GetVirtualRegisterInfoForInterpreterSlotBeforeFrameBase(slot).IsLive();
    }

private:
    InlinedCallFrame* WARN_UNUSED FindParentFrameForReturn()
    {
        InlinedCallFrame* frame = this;
        while (!frame->IsRootFrame())
        {
            InlinedCallFrame* parent = frame->GetCallerCodeOrigin().GetInlinedCallFrame();
            if (!frame->IsTailCall())
            {
                return parent;
            }
            frame = parent;
        }
        return nullptr;
    }

    // Our (i.e., the top frame's) codeBlock
    //
    CodeBlock* m_codeBlock;

    // Our caller, this is an invalid CodeOrigin if we are the root frame
    //
    CodeOrigin m_caller;

    // The caller frame where this frame should return to.
    // (Due to proper tail call, this is not necessarily the direct parent of this call frame).
    //
    // This is nullptr if every call from the root to this frame is a proper tail call.
    // In which case it means that this frame should return to the parent of the root frame.
    //
    InlinedCallFrame* m_parentFrameForReturn;

    // Whether the speculation is a direct call or a closure call
    // Only valid if we are not root frame
    //
    bool m_isDirectCall;

    // Whether the caller called us via a proper tail call
    // Only valid if we are not root frame
    //
    bool m_isTailCall;

    // Whether it is possible to statically determine the number of variadic arguments to this function
    //
    bool m_canStaticallyDetermineNumVarArgs;

    // The call site ordinal in the caller bytecode that called this function
    //
    uint8_t m_callerBcCallSiteOrdinal;

    // An unique ordinal with guarantee that m_parent's ordinal is always smaller
    //
    uint32_t m_inlineCallFrameOrdinal;

    // If we take variadic arguments, the maximum number of variadic arguments we are allowed to take
    // If the runtime passed number is more than this, we must OSR exit.
    //
    // If m_canStaticallyDetermineNumVarArgs is true, this is just the actual number of variadic arguments.
    //
    // Only valid if we are not root frame
    //
    uint32_t m_maxVariadicArguments;

    // The absolute interpreter slot number that corresponds to interpreter slot 0 in this call frame
    //
    uint32_t m_baseInterpreterFrameSlot;

    // An array of length m_codeBlock->m_numStackSlots, this is the map from local ordinal to the register ordinal in DFG
    //
    uint32_t* m_localToRegisterMap;

    // An array of length m_maxVariadicArguments, mapping each variadic argument to VirtualRegister ordinal
    //
    uint32_t* m_variadicArgumentToRegisterMap;

    // If this is a closure call, records the VirtualRegister ordinal for the function object
    //
    uint32_t m_functionObjectVR;

    // If this function accepts variadic arguments, and m_canStaticallyDetermineNumVarArgs is false,
    // records the VirtualRegister ordinal that stores the actual number of arguments
    //
    uint32_t m_numVarArgsVR;

    // If this is a direct call, records the function object
    //
    FunctionObject* m_functionObject;

    // The bytecode liveness information
    //
    BytecodeLiveness* m_bytecodeLiveness;

    // The high watermark of the VirtualRegister vector length for this function and all functions inlined into this function
    //
    uint32_t m_virtualRegisterVectorLength;

    // The number of bytecode locals in this frame
    //
    uint32_t m_numBytecodeLocals;

    // An array of length m_virtualRegisterVectorLength
    // Index i is true if VirtualRegister i is used by this function or any of the functions inlined into this function
    //
    bool* m_isVirtualRegisterUsed;

    // An array of length m_baseInterpreterFrameSlot
    // Index i stores the virtual register ordinal mapped for interpreter slot i
    // -1 means the interpreter slot is dead before the bytecode that resulted in this call frame
    //
    VirtualRegisterMappingInfo* m_virtualRegisterMappingBeforeThisFrame;
};

// The alignment must be >1 since OsrExitDestination steals its bit 0
//
static_assert(alignof(InlinedCallFrame) > 1);

// A simple helper class to query liveness info at a CodeOrigin, before the bytecode pointed by the CodeOrigin is executed
//
struct CodeOriginLivenessInfo
{
    CodeOriginLivenessInfo(CodeOrigin codeOrigin)
    {
        m_inlinedCallFrame = codeOrigin.GetInlinedCallFrame();
        m_bytecodeIndex = codeOrigin.GetBytecodeIndex();
        m_frameBase = m_inlinedCallFrame->GetInterpreterSlotForStackFrameBase().Value();
        m_numLocals = m_inlinedCallFrame->GetNumBytecodeLocals();
    }

    bool IsLive(InterpreterSlot slot)
    {
        size_t ord = slot.Value();
        if (ord < m_frameBase)
        {
            VirtualRegisterMappingInfo vrmi = m_inlinedCallFrame->GetVirtualRegisterInfoForInterpreterSlotBeforeFrameBase(slot);
            return vrmi.IsLive();
        }
        else
        {
            size_t bytecodeLocalOrd = ord - m_frameBase;
            if (bytecodeLocalOrd >= m_inlinedCallFrame->GetNumBytecodeLocals())
            {
                return false;
            }
            else
            {
                return m_inlinedCallFrame->BytecodeLivenessInfo().IsBytecodeLocalLive(m_bytecodeIndex, BytecodeLiveness::BeforeUse, bytecodeLocalOrd);
            }
        }
    }

    VirtualRegisterMappingInfo GetVirtualRegisterMappingInfo(InterpreterSlot slot)
    {
        size_t ord = slot.Value();
        if (ord < m_frameBase)
        {
            return m_inlinedCallFrame->GetVirtualRegisterInfoForInterpreterSlotBeforeFrameBase(slot);
        }
        else
        {
            size_t bytecodeLocalOrd = ord - m_frameBase;
            if (bytecodeLocalOrd >= m_inlinedCallFrame->GetNumBytecodeLocals())
            {
                return VirtualRegisterMappingInfo::Dead();
            }
            else
            {
                if (m_inlinedCallFrame->BytecodeLivenessInfo().IsBytecodeLocalLive(m_bytecodeIndex, BytecodeLiveness::BeforeUse, bytecodeLocalOrd))
                {
                    return VirtualRegisterMappingInfo::VReg(m_inlinedCallFrame->GetRegisterForLocalOrd(bytecodeLocalOrd));
                }
                else
                {
                    return VirtualRegisterMappingInfo::Dead();
                }
            }
        }
    }

    InlinedCallFrame* m_inlinedCallFrame;
    size_t m_bytecodeIndex;
    size_t m_frameBase;
    size_t m_numLocals;
};

}   // namespace dfg
