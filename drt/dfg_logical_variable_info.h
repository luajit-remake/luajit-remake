#pragma once

#include "dfg_arena.h"
#include "dfg_virtual_register.h"

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

    // The DFG slot ordinal
    //
    uint32_t m_localOrd;
    // The interpreter slot ordinal
    //
    uint32_t m_interpreterSlotOrd;
    // Each LogicalVariableInfo is assigned an unique ordinal
    //
    uint32_t m_logicalVariableOrdinal;
};

}   // namespace dfg
