#pragma once

#include "common_utils.h"
#include "dfg_codegen_operation_base.h"
#include "dfg_reg_move_inst_generator.h"
#include "dfg_reg_alloc_codegen_state.h"
#include "dfg_variant_trait_table.h"
#include "dfg_ir_dump.h"

namespace dfg {

// Context used for debug dump of human-readable codegen log
//
struct CodegenLogDumpContext
{
    CodegenLogDumpContext()
        : m_file(nullptr)
        , m_firstRegSpillPhysicalSlot(static_cast<uint16_t>(-1))
        , m_constantTable(nullptr)
        , m_constantTableSize(0)
    { }

    FILE* m_file;
    uint16_t m_firstRegSpillPhysicalSlot;
    uint64_t* m_constantTable;
    size_t m_constantTableSize;
};

struct __attribute__((__packed__)) CodegenRegMove final : CodegenOpBase
{
    CodegenRegMove(X64Reg srcReg, X64Reg dstReg)
        : CodegenOpBase(this), m_srcReg(srcReg), m_dstReg(dstReg)
    { }

    void* WARN_UNUSED GetStructEnd() { return this + 1; }

    void UpdateJITCodeSize(JITCodeSizeInfo& info /*inout*/)
    {
        info.m_fastPathLength += GetRegisterMoveInstructionLength(m_srcReg, m_dstReg);
    }

    void DoCodegen(PrimaryCodegenState& pcs /*inout*/)
    {
        EmitRegisterRegisterMoveInst(pcs.m_fastPathAddr /*inout*/, m_srcReg, m_dstReg);
    }

#ifdef TESTBUILD
    void DumpHumanReadableLog(CodegenLogDumpContext& ctx)
    {
        fprintf(ctx.m_file, "  %s := %s\n", m_dstReg.GetName(), m_srcReg.GetName());
    }
#endif

    X64Reg m_srcReg;
    X64Reg m_dstReg;
};

struct __attribute__((__packed__)) CodegenRegLoad final : CodegenOpBase
{
    CodegenRegLoad(uint32_t stackOffsetBytes, X64Reg reg)
        : CodegenOpBase(this), m_reg(reg), m_stackOffsetBytes(stackOffsetBytes)
    {
        TestAssert(m_stackOffsetBytes <= 0x7fffffff);
    }

    void* WARN_UNUSED GetStructEnd() { return this + 1; }

    void UpdateJITCodeSize(JITCodeSizeInfo& info /*inout*/)
    {
        info.m_fastPathLength += GetRegisterSpillOrLoadInstructionLength(m_reg, m_stackOffsetBytes);
    }

    void DoCodegen(PrimaryCodegenState& pcs /*inout*/)
    {
        EmitRegisterLoadFromStackInst(pcs.m_fastPathAddr /*inout*/, m_reg, m_stackOffsetBytes);
    }

#ifdef TESTBUILD
    void DumpHumanReadableLog(CodegenLogDumpContext& ctx)
    {
        TestAssert(m_stackOffsetBytes % sizeof(TValue) == 0);
        fprintf(ctx.m_file, "  %s := stk[%u]\n", m_reg.GetName(), static_cast<unsigned int>(m_stackOffsetBytes / sizeof(TValue)));
    }
#endif

    X64Reg m_reg;
    uint32_t m_stackOffsetBytes;
};

struct __attribute__((__packed__)) CodegenRegSpill final : CodegenOpBase
{
    CodegenRegSpill(X64Reg srcReg, X64Reg baseReg, uint32_t offsetBytes)
        : CodegenOpBase(this), m_srcReg(srcReg), m_baseReg(baseReg), m_offsetBytes(offsetBytes)
    {
        TestAssert(m_baseReg.IsGPR());
        TestAssert(m_baseReg != X64Reg::RSP && m_baseReg != X64Reg::R12);
        TestAssert(m_offsetBytes <= 0x7fffffff);
    }

    void* WARN_UNUSED GetStructEnd() { return this + 1; }

    void UpdateJITCodeSize(JITCodeSizeInfo& info /*inout*/)
    {
        info.m_fastPathLength += GetRegisterStoreToMemBaseOffsetInstLength(m_srcReg, m_baseReg, m_offsetBytes);
    }

    void DoCodegen(PrimaryCodegenState& pcs /*inout*/)
    {
        EmitRegisterStoreToMemBaseOffsetInstruction(pcs.m_fastPathAddr /*inout*/, m_srcReg, m_baseReg, m_offsetBytes);
    }

#ifdef TESTBUILD
    void DumpHumanReadableLog(CodegenLogDumpContext& ctx)
    {
        if (m_baseReg == x_dfg_stack_base_register)
        {
            TestAssert(m_offsetBytes % sizeof(TValue) == 0);
            fprintf(ctx.m_file, "  stk[%u] := %s\n", static_cast<unsigned int>(m_offsetBytes / sizeof(TValue)), m_srcReg.GetName());
        }
        else
        {
            fprintf(ctx.m_file, "  %u(%s) := %s\n", static_cast<unsigned int>(m_offsetBytes), m_baseReg.GetName(), m_srcReg.GetName());
        }
    }
#endif

    X64Reg m_srcReg;
    X64Reg m_baseReg;
    uint32_t m_offsetBytes;
};

// Update the JIT code size after appending a standard-interface stencil into the JIT code with ordinal 'funcOrd'
//
inline void UpdateJITCodeSizeForStandardInterfaceStencil(JITCodeSizeInfo& info /*inout*/, DfgCodegenFuncOrd funcOrd)
{
    size_t idx = static_cast<size_t>(funcOrd);
    TestAssert(idx < x_dfgOpcodeJitCodeSizeInfoTable.size());
    info.Update(x_dfgOpcodeJitCodeSizeInfoTable[idx]);
}

// Update the JIT code size after appending a custom-interface stencil into the JIT code with ordinal 'funcOrd'
//
inline void UpdateJITCodeSizeForCustomInterfaceStencil(JITCodeSizeInfo& info /*inout*/, DfgCodegenFuncOrd funcOrd)
{
    size_t idx = static_cast<size_t>(funcOrd);
    TestAssert(idx < x_dfgBuiltinNodeCustomCgFnJitCodeSizeArray.size());
    info.Update(x_dfgBuiltinNodeCustomCgFnJitCodeSizeArray[idx]);
}

inline CodegenImplFn WARN_UNUSED GetCodegenImplFnForStandardInterfaceStencil(DfgCodegenFuncOrd funcOrd)
{
    size_t idx = static_cast<size_t>(funcOrd);
    TestAssert(idx < x_dfgOpcodeCodegenImplFnTable.size());
    return x_dfgOpcodeCodegenImplFnTable[idx];
}

inline CustomBuiltinNodeCodegenImplFn WARN_UNUSED GetCodegenImplFnForCustomInterfaceStencil(DfgCodegenFuncOrd funcOrd)
{
    size_t idx = static_cast<size_t>(funcOrd);
    TestAssert(idx < x_dfgBuiltinNodeCustomCgFnArray.size());
    return x_dfgBuiltinNodeCustomCgFnArray[idx];
}

inline const char* WARN_UNUSED GetCodegenFnDebugNameForStandardInterfaceStencil(DfgCodegenFuncOrd funcOrd)
{
    size_t idx = static_cast<size_t>(funcOrd);
    TestAssert(idx < x_dfgOpcodeCodegenFnDebugNameTable.size());
    return x_dfgOpcodeCodegenFnDebugNameTable[idx];
}

inline const char* WARN_UNUSED GetCodegenFnDebugNameForCustomInterfaceStencil(DfgCodegenFuncOrd funcOrd)
{
    size_t idx = static_cast<size_t>(funcOrd);
    TestAssert(idx < x_dfgBuiltinNodeCustomCgFnDebugNameArray.size());
    return x_dfgBuiltinNodeCustomCgFnDebugNameArray[idx];
}

struct __attribute__((__packed__)) CodegenOpRegAllocEnabled final : CodegenOpBase
{
    CodegenOpRegAllocEnabled() : CodegenOpBase(this) { }

    static constexpr size_t ComputeAllocationSize(size_t numInputOperands)
    {
        return offsetof_member_v<&CodegenOpRegAllocEnabled::m_operandConfig> + NodeOperandConfigData::GetAllocationSize(numInputOperands);
    }

    void* WARN_UNUSED GetStructEnd() { return m_operandConfig.GetStructEnd(); }

    void UpdateJITCodeSize(JITCodeSizeInfo& info /*inout*/)
    {
        UpdateJITCodeSizeForStandardInterfaceStencil(info /*inout*/, m_operandConfig.GetCodegenFuncOrd());
    }

    void DoCodegen(PrimaryCodegenState& pcs /*inout*/)
    {
        CodegenImplFn implFn = GetCodegenImplFnForStandardInterfaceStencil(m_operandConfig.GetCodegenFuncOrd());
        m_regConfig.AssertConsistency();
        RegAllocStateForCodeGen cgState;
        m_regConfig.PopulateCodegenState(cgState /*out*/);
        implFn(&pcs, &m_operandConfig, m_nsd, &cgState);
    }

#ifdef TESTBUILD
    void DumpHumanReadableLog(CodegenLogDumpContext& ctx)
    {
        fprintf(ctx.m_file, "  ");

        const char* nodeName = GetCodegenFnDebugNameForStandardInterfaceStencil(m_operandConfig.GetCodegenFuncOrd());
        m_operandConfig.DumpKnowingRegAllocEnabled(ctx.m_file, nodeName, ctx.m_firstRegSpillPhysicalSlot);

        // Ugly: do special check for built-in nodes (e.g., constant/unboxed constant) to print out their arguments,
        // which is important for a readable log
        //
        // Check if the codegen function is for a built-in node first
        //
        if (static_cast<size_t>(m_operandConfig.GetCodegenFuncOrd()) >= x_totalNumDfgUserNodeCodegenFuncs)
        {
            auto startsWith = [&](const char* s, const char* prefix) -> bool
            {
                if (strlen(s) < strlen(prefix)) { return false; }
                return memcmp(s, prefix, strlen(prefix)) == 0;
            };

            // If so, check what kind of built-in node it is by the debug name
            // It's not beautiful, but works..
            //
            if (startsWith(nodeName, "Constant_"))
            {
                TestAssert(m_nsd != nullptr);
                int64_t constantTableOrd = UnalignedLoad<int64_t>(m_nsd);
                TestAssert(constantTableOrd < 0);
                uint64_t idx = static_cast<uint64_t>(-constantTableOrd) - 1;
                TestAssert(idx < ctx.m_constantTableSize);
                TValue val;
                val.m_value = ctx.m_constantTable[idx];
                fprintf(ctx.m_file, " [CstTableOrd=%lld, Value=", static_cast<long long>(constantTableOrd));
                DumpTValueValue(ctx.m_file, val);
                fprintf(ctx.m_file, "]");
            }
            else if (startsWith(nodeName, "UnboxedConstant_"))
            {
                TestAssert(m_nsd != nullptr);
                int64_t constantTableOrd = UnalignedLoad<int64_t>(m_nsd);
                TestAssert(constantTableOrd < 0);
                uint64_t idx = static_cast<uint64_t>(-constantTableOrd) - 1;
                TestAssert(idx < ctx.m_constantTableSize);
                fprintf(ctx.m_file, " [CstTableOrd=%lld, Value=%llu]",
                        static_cast<long long>(constantTableOrd), static_cast<unsigned long long>(ctx.m_constantTable[idx]));
            }
            else if (startsWith(nodeName, "Argument_"))
            {
                uint64_t argOrd = UnalignedLoad<uint64_t>(m_nsd);
                fprintf(ctx.m_file, " [Argument #%llu]", static_cast<unsigned long long>(argOrd));
            }
            else if (startsWith(nodeName, "GetKthVariadicArg_"))
            {
                uint64_t argOrd = UnalignedLoad<uint64_t>(m_nsd);
                fprintf(ctx.m_file, " [VarArg #%llu]", static_cast<unsigned long long>(argOrd));
            }
            else if (startsWith(nodeName, "GetLocal_") || startsWith(nodeName, "SetLocal_"))
            {
                uint64_t physicalSlot = UnalignedLoad<uint64_t>(m_nsd);
                fprintf(ctx.m_file, " [Local #%llu]", static_cast<unsigned long long>(physicalSlot));
            }
            else if (startsWith(nodeName, "GetKthVariadicRes_"))
            {
                uint64_t resOrd = UnalignedLoad<uint64_t>(m_nsd);
                fprintf(ctx.m_file, " [VarRes #%llu]", static_cast<unsigned long long>(resOrd));
            }
            else if (startsWith(nodeName, "CheckU64InBound_"))
            {
                uint64_t val = UnalignedLoad<uint64_t>(m_nsd);
                fprintf(ctx.m_file, " [BoundVal=%llu]", static_cast<unsigned long long>(val));
            }
            else if (startsWith(nodeName, "I64SubSaturateToZero_"))
            {
                uint64_t val = UnalignedLoad<uint64_t>(m_nsd);
                fprintf(ctx.m_file, " [ValToSub=%llu]", static_cast<unsigned long long>(val));
            }
            else if (startsWith(nodeName, "GetUpvalueImmutable_") || startsWith(nodeName, "GetUpvalueMutable_") || startsWith(nodeName, "SetUpvalue_"))
            {
                uint64_t upvalOrd = UnalignedLoad<uint64_t>(m_nsd);
                fprintf(ctx.m_file, " [UpvalOrd #%llu]", static_cast<unsigned long long>(upvalOrd));
            }
        }

        m_regConfig.DumpScratchedRegisterInfo(ctx.m_file);
        fprintf(ctx.m_file, "\n");
    }
#endif

    uint8_t* m_nsd;
    RegAllocRegConfig m_regConfig;
    // Must be last member since it has a trailing array
    //
    NodeOperandConfigData m_operandConfig;
};

struct __attribute__((__packed__)) CodegenOpRegAllocDisabled final : CodegenOpBase
{
    CodegenOpRegAllocDisabled() : CodegenOpBase(this) { }

    static constexpr size_t ComputeAllocationSize(size_t numInputOperands)
    {
        return offsetof_member_v<&CodegenOpRegAllocDisabled::m_operandConfig> + NodeOperandConfigData::GetAllocationSize(numInputOperands);
    }

    void* WARN_UNUSED GetStructEnd() { return m_operandConfig.GetStructEnd(); }

    void UpdateJITCodeSize(JITCodeSizeInfo& info /*inout*/)
    {
        UpdateJITCodeSizeForStandardInterfaceStencil(info /*inout*/, m_operandConfig.GetCodegenFuncOrd());
    }

    void DoCodegen(PrimaryCodegenState& pcs /*inout*/)
    {
        CodegenImplFn implFn = GetCodegenImplFnForStandardInterfaceStencil(m_operandConfig.GetCodegenFuncOrd());
        implFn(&pcs, &m_operandConfig, m_nsd, nullptr /*RegAllocStateForCodegen*/);
    }

#ifdef TESTBUILD
    void DumpHumanReadableLog(CodegenLogDumpContext& ctx)
    {
        fprintf(ctx.m_file, "  ");
        const char* nodeName = GetCodegenFnDebugNameForStandardInterfaceStencil(m_operandConfig.GetCodegenFuncOrd());
        m_operandConfig.DumpKnowingRegAllocDisabled(ctx.m_file, nodeName);
        fprintf(ctx.m_file, " [Clobbers all regs]\n");
    }
#endif

    uint8_t* m_nsd;
    // Must be last member since it has a trailing array
    //
    NodeOperandConfigData m_operandConfig;
};

struct __attribute__((__packed__)) CodegenCustomOpRegAllocEnabled final : CodegenOpBase
{
    CodegenCustomOpRegAllocEnabled() : CodegenOpBase(this) { }

    static constexpr size_t ComputeAllocationSize(size_t numInputOperands)
    {
        return offsetof_member_v<&CodegenCustomOpRegAllocEnabled::m_operandConfig> + NodeOperandConfigData::GetAllocationSize(numInputOperands);
    }

    void* WARN_UNUSED GetStructEnd() { return m_operandConfig.GetStructEnd(); }

    void UpdateJITCodeSize(JITCodeSizeInfo& info /*inout*/)
    {
        UpdateJITCodeSizeForCustomInterfaceStencil(info /*inout*/, m_operandConfig.GetCodegenFuncOrd());
    }

    void DoCodegen(PrimaryCodegenState& pcs /*inout*/)
    {
        CustomBuiltinNodeCodegenImplFn implFn = GetCodegenImplFnForCustomInterfaceStencil(m_operandConfig.GetCodegenFuncOrd());
        m_regConfig.AssertConsistency();
        RegAllocStateForCodeGen cgState;
        m_regConfig.PopulateCodegenState(cgState /*out*/);
        implFn(&pcs, &m_operandConfig, m_literalData, &cgState);
    }

#ifdef TESTBUILD
    void DumpHumanReadableLog(CodegenLogDumpContext& ctx)
    {
        fprintf(ctx.m_file, "  ");
        const char* nodeName = GetCodegenFnDebugNameForCustomInterfaceStencil(m_operandConfig.GetCodegenFuncOrd());
        m_operandConfig.DumpKnowingRegAllocEnabled(ctx.m_file, nodeName, ctx.m_firstRegSpillPhysicalSlot);
        m_regConfig.DumpScratchedRegisterInfo(ctx.m_file);
        fprintf(ctx.m_file, "\n");
    }
#endif

    uint64_t* m_literalData;
    RegAllocRegConfig m_regConfig;
    // Must be last member since it has a trailing array
    //
    NodeOperandConfigData m_operandConfig;
};

struct __attribute__((__packed__)) CodegenCustomOpRegAllocDisabled final : CodegenOpBase
{
    CodegenCustomOpRegAllocDisabled() : CodegenOpBase(this) { }

    static constexpr size_t ComputeAllocationSize(size_t numInputOperands)
    {
        return offsetof_member_v<&CodegenCustomOpRegAllocDisabled::m_operandConfig> + NodeOperandConfigData::GetAllocationSize(numInputOperands);
    }

    void* WARN_UNUSED GetStructEnd() { return m_operandConfig.GetStructEnd(); }

    void UpdateJITCodeSize(JITCodeSizeInfo& info /*inout*/)
    {
        UpdateJITCodeSizeForCustomInterfaceStencil(info /*inout*/, m_operandConfig.GetCodegenFuncOrd());
    }

    void DoCodegen(PrimaryCodegenState& pcs /*inout*/)
    {
        CustomBuiltinNodeCodegenImplFn implFn = GetCodegenImplFnForCustomInterfaceStencil(m_operandConfig.GetCodegenFuncOrd());
        implFn(&pcs, &m_operandConfig, m_literalData, nullptr /*RegAllocStateForCodeGen*/);
    }

#ifdef TESTBUILD
    void DumpHumanReadableLog(CodegenLogDumpContext& ctx)
    {
        fprintf(ctx.m_file, "  ");
        const char* nodeName = GetCodegenFnDebugNameForCustomInterfaceStencil(m_operandConfig.GetCodegenFuncOrd());
        m_operandConfig.DumpKnowingRegAllocDisabled(ctx.m_file, nodeName);
        fprintf(ctx.m_file, " [Clobbers all regs]\n");
    }
#endif

    uint64_t* m_literalData;
    // Must be last member since it has a trailing array
    //
    NodeOperandConfigData m_operandConfig;
};

}   // namespace dfg
