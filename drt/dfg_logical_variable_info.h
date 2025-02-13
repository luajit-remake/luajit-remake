#pragma once

#include "dfg_arena.h"
#include "dfg_virtual_register.h"
#include "tvalue.h"

namespace dfg {

struct LogicalVariableInfo
{
    static LogicalVariableInfo* WARN_UNUSED Create(VirtualRegister vreg, InterpreterSlot islot)
    {
        LogicalVariableInfo* r = DfgAlloc()->AllocateObject<LogicalVariableInfo>(vreg, islot);
        return r;
    }

    LogicalVariableInfo(VirtualRegister vreg, InterpreterSlot islot)
        : m_localOrd(SafeIntegerCast<uint32_t>(vreg.Value()))
        , m_interpreterSlotOrd(SafeIntegerCast<uint32_t>(islot.Value()))
        , m_logicalVariableOrdinal(static_cast<uint32_t>(-1))
        , m_speculationMask(x_typeMaskFor<tBottom>)
    { }

    void SetLogicalVariableOrdinal(uint32_t ord)
    {
        TestAssert(m_logicalVariableOrdinal == static_cast<uint32_t>(-1) && ord != static_cast<uint32_t>(-1));
        m_logicalVariableOrdinal = ord;
    }

    uint32_t GetLogicalVariableOrdinal()
    {
        TestAssert(m_logicalVariableOrdinal != static_cast<uint32_t>(-1));
        return m_logicalVariableOrdinal;
    }

    InterpreterSlot GetInterpreterSlot() { return InterpreterSlot(m_interpreterSlotOrd); }
    VirtualRegister GetVirtualRegister() { return VirtualRegister(m_localOrd); }

    // The DFG slot ordinal
    //
    uint32_t m_localOrd;
    // The interpreter slot ordinal
    //
    uint32_t m_interpreterSlotOrd;
    // Each LogicalVariableInfo is assigned an unique ordinal
    //
    uint32_t m_logicalVariableOrdinal;

    // Any GetLocal is *proven* to yield a value which type is within this mask
    //
    TypeMask m_speculationMask;
};

struct CapturedVarLogicalVariableInfo
{
    static CapturedVarLogicalVariableInfo* WARN_UNUSED Create(uint32_t localOrdForOsrExit)
    {
        CapturedVarLogicalVariableInfo* r = DfgAlloc()->AllocateObject<CapturedVarLogicalVariableInfo>(localOrdForOsrExit);
        return r;
    }

    CapturedVarLogicalVariableInfo(uint32_t localOrdForOsrExit)
        : m_localOrdForOsrExit(localOrdForOsrExit)
        , m_logicalVariableOrdinal(static_cast<uint32_t>(-1))
    { }

    void SetLogicalVariableOrdinal(uint32_t ord)
    {
        TestAssert(m_logicalVariableOrdinal == static_cast<uint32_t>(-1) && ord != static_cast<uint32_t>(-1));
        m_logicalVariableOrdinal = ord;
    }

    uint32_t GetLogicalVariableOrdinal()
    {
        TestAssert(m_logicalVariableOrdinal != static_cast<uint32_t>(-1));
        return m_logicalVariableOrdinal;
    }

    // The local ordinal that the value of this CapturedVar should be written back to on OSR exit
    //
    uint32_t m_localOrdForOsrExit;

    // Each CapturedVarLogicalVariableInfo is assigned an unique ordinal
    //
    uint32_t m_logicalVariableOrdinal;
};

}   // namespace dfg
