#pragma once

#include "common_utils.h"

// Protocol info on how DFG runtime should use the Deegen-generated code generator functions
//
namespace dfg {

enum class DfgCodegenFuncOrd : uint16_t;

struct CodegenFnJitCodeSizeInfo
{
    uint16_t m_fastPathCodeLen;
    uint16_t m_slowPathCodeLen;
    uint16_t m_dataSecLen;
    uint16_t m_dataSecAlignment;
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

struct NodeOperandConfigData;
struct RegAllocStateForCodeGen;

using CodegenImplFn = void(*)(PrimaryCodegenState*, NodeOperandConfigData*, uint8_t* /*nsd*/, RegAllocStateForCodeGen*);

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

// Where to materialize a constant-like node
//
enum class ConstantLikeNodeMaterializeLocation
{
    GprGroup1,
    GprGroup2,
    Fpr,
    TempReg,
    X_END_OF_ENUM
};

}   // namespace dfg
