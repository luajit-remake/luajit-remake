#pragma once

#include "common_utils.h"

// Protocol info on how DFG runtime should use the Deegen-generated code generator functions
//
namespace dfg {

struct CodegenFnJitCodeSizeInfo
{
    uint16_t m_fastPathCodeLen;
    uint16_t m_slowPathCodeLen;
    uint16_t m_dataSecLen;
    uint16_t m_dataSecAlignment;
};

// TODO: move this elsewhere
//
struct JITCodeSizeInfo
{
    uint32_t m_fastPathLength;
    uint32_t m_slowPathLength;
    uint32_t m_dataSecLength;
};

struct PrimaryCodegenState
{
    // All fields except 'm_dfgCodeBlockLower32Bits' are updated by the codegen after generating code for one node
    //
    uint8_t* m_fastPathAddr;
    uint8_t* m_slowPathAddr;
    uint8_t* m_dataSecAddr;
    // The SlowPathData ptr and the offset of this pointer from the DfgCodeBlock
    //
    uint8_t* m_slowPathDataAddr;
    uint64_t m_slowPathDataOffset;
    // Only nodes that use IC needs CompactedRegConf in SlowPathData
    // Caller should have set up this array of CompactedRegConf in the same order as all the nodes that need them,
    // and codegen will increment this pointer each time a CompactRegConf is consumed
    //
    uint8_t* m_compactedRegConfAddr;
    uint64_t m_dfgCodeBlockLower32Bits;
};

struct NodeRegAllocInfo;
struct RegAllocStateForCodeGen;

using CodegenImplFn = void(*)(PrimaryCodegenState*, NodeRegAllocInfo*, uint8_t* /*nsd*/, RegAllocStateForCodeGen*);

struct BuiltinNodeOperandsInfoBase
{
    uint64_t GetOutputPhysicalSlot()
    {
        return m_outputPhysicalSlot;
    }

    uint64_t GetInputPhysicalSlot(size_t inputOrd)
    {
        uint16_t* inputs = reinterpret_cast<uint16_t*>(reinterpret_cast<uintptr_t>(this) + sizeof(BuiltinNodeOperandsInfoBase));
        return inputs[inputOrd];
    }

    uint16_t m_outputPhysicalSlot;
};

template<size_t numInputs>
struct BuiltinNodeOperandsInfo
{
    BuiltinNodeOperandsInfo()
    {
        static_assert(offsetof_member_v<&BuiltinNodeOperandsInfo::m_inputPhysicalSlot> == sizeof(BuiltinNodeOperandsInfoBase));
    }

    uint16_t m_inputPhysicalSlot[numInputs];
};

using CustomBuiltinNodeCodegenImplFn = void(*)(PrimaryCodegenState*, BuiltinNodeOperandsInfoBase*, uint64_t* /*literalOperands*/, RegAllocStateForCodeGen*);

}   // namespace dfg
