#pragma once

#include "common_utils.h"
#include "tvalue.h"
#include "temp_arena_allocator.h"
#include "dfg_codegen_operation_base.h"
#include "dfg_codegen_operations.h"
#include "dfg_node.h"
#include "dfg_variant_trait_table.h"
#include "dfg_reg_alloc_decision_maker.h"
#include "dfg_slowpath_register_config_helper.h"

namespace dfg {

// The log that records all the codegen operations that we shall perform
//
struct CodegenOperationLog
{
    MAKE_NONCOPYABLE(CodegenOperationLog);
    MAKE_NONMOVABLE(CodegenOperationLog);

    CodegenOperationLog(TempArenaAllocator& alloc, uint16_t firstRegSpillPhysicalSlot)
        : m_alloc(alloc)
        , m_firstRegisterSpillSlot(firstRegSpillPhysicalSlot)
    { }

    // Note that JITCodeSizeInfo is not reset since it tracks the total size
    //
    void ResetLog()
    {
        m_log.m_length = 0;
        m_slowPathDataRegConfigEntries.m_length = 0;
    }

    void EmitRegRegMove(X64Reg srcReg, X64Reg dstReg)
    {
        CodegenRegMove* op = m_log.Append<CodegenRegMove>(m_alloc, sizeof(CodegenRegMove));
        ConstructInPlace(op, srcReg, dstReg);
        op->UpdateJITCodeSize(m_jitCodeSize /*inout*/);
    }

    // Emit instruction to spill reg to the given physical slot
    //
    void EmitRegSpill(X64Reg reg, uint16_t physicalSlot)
    {
        uint32_t bytesPerSlot = static_cast<uint32_t>(sizeof(TValue));
        uint32_t byteOffset = static_cast<uint32_t>(physicalSlot) * bytesPerSlot;
        CodegenRegSpill* op = m_log.Append<CodegenRegSpill>(m_alloc, sizeof(CodegenRegSpill));
        ConstructInPlace(op, reg, x_dfg_stack_base_register, byteOffset);
        op->UpdateJITCodeSize(m_jitCodeSize /*inout*/);
    }

    // Stores srcReg to [baseReg + byteOffset]
    //
    void EmitRegStoreToBaseOffsetBytes(X64Reg srcReg, X64Reg baseReg, uint32_t byteOffset)
    {
        CodegenRegSpill* op = m_log.Append<CodegenRegSpill>(m_alloc, sizeof(CodegenRegSpill));
        ConstructInPlace(op, srcReg, baseReg, byteOffset);
        op->UpdateJITCodeSize(m_jitCodeSize /*inout*/);
    }

    // Emit instruction to load reg from a physical slot
    //
    void EmitRegLoad(uint16_t physicalSlot, X64Reg reg)
    {
        uint32_t bytesPerSlot = static_cast<uint32_t>(sizeof(TValue));
        uint32_t byteOffset = static_cast<uint32_t>(physicalSlot) * bytesPerSlot;
        CodegenRegLoad* op = m_log.Append<CodegenRegLoad>(m_alloc, sizeof(CodegenRegLoad));
        ConstructInPlace(op, byteOffset, reg);
        op->UpdateJITCodeSize(m_jitCodeSize /*inout*/);
    }

    // The passed-in 'func' should populate all necessary data into the CodegenOpRegAllocEnabled struct
    //
    template<typename Func>
    void ALWAYS_INLINE EmitCodegenOpWithRegAlloc(size_t numInputOperands, const Func& func)
    {
        CodegenOpRegAllocEnabled* op = m_log.Append<CodegenOpRegAllocEnabled>(m_alloc, CodegenOpRegAllocEnabled::ComputeAllocationSize(numInputOperands));
        ConstructInPlace(op);
        func(op);
        TestAssert(op->m_operandConfig.m_numInputOperands == numInputOperands);
        op->UpdateJITCodeSize(m_jitCodeSize /*inout*/);
    }

    template<typename Func>
    void ALWAYS_INLINE EmitCodegenOpWithoutRegAlloc(size_t numInputOperands, const Func& func)
    {
        CodegenOpRegAllocDisabled* op = m_log.Append<CodegenOpRegAllocDisabled>(m_alloc, CodegenOpRegAllocDisabled::ComputeAllocationSize(numInputOperands));
        ConstructInPlace(op);
        func(op);
        TestAssert(op->m_operandConfig.m_numInputOperands == numInputOperands);
        op->UpdateJITCodeSize(m_jitCodeSize /*inout*/);
    }

    template<typename Func>
    void ALWAYS_INLINE EmitCodegenCustomOpWithRegAlloc(size_t numInputOperands, const Func& func)
    {
        CodegenCustomOpRegAllocEnabled* op = m_log.Append<CodegenCustomOpRegAllocEnabled>(m_alloc, CodegenCustomOpRegAllocEnabled::ComputeAllocationSize(numInputOperands));
        ConstructInPlace(op);
        func(op);
        TestAssert(op->m_operandConfig.m_numInputOperands == numInputOperands);
        op->UpdateJITCodeSize(m_jitCodeSize /*inout*/);
    }

    template<typename Func>
    void ALWAYS_INLINE EmitCodegenCustomOpWithoutRegAlloc(size_t numInputOperands, const Func& func)
    {
        CodegenCustomOpRegAllocDisabled* op = m_log.Append<CodegenCustomOpRegAllocDisabled>(m_alloc, CodegenCustomOpRegAllocDisabled::ComputeAllocationSize(numInputOperands));
        ConstructInPlace(op);
        func(op);
        TestAssert(op->m_operandConfig.m_numInputOperands == numInputOperands);
        op->UpdateJITCodeSize(m_jitCodeSize /*inout*/);
    }

    // Emit logic to materialize the value of a constant-like node into x_dfg_custom_purpose_temp_reg,
    // all regs that participates in reg alloc are preserved
    //
    void EmitMaterializeConstantLikeNodeToTempReg(Node* node)
    {
        TestAssert(node->IsConstantLikeNode());
        TestAssertImp(node->IsConstantNode() || node->IsUnboxedConstantNode(), node->IsOrdInConstantTableAssigned());
        DfgCodegenFuncOrd funcOrd = GetCodegenFnForMaterializingConstant(node->GetNodeKind(), ConstantLikeNodeMaterializeLocation::TempReg);

        EmitCodegenOpWithRegAlloc(
            0 /*numInputOperands*/,
            [&](CodegenOpRegAllocEnabled* op) ALWAYS_INLINE
            {
                op->m_nsd = node->GetNodeSpecificDataOrNullptr();

                op->m_regConfig.m_group1ScratchGprIdxMask = 0;
                op->m_regConfig.m_group2ScratchGprIdxMask = 0;
                op->m_regConfig.m_scratchFprIdxMask = 0;
                op->m_regConfig.m_group1PassthruGprIdxMask = static_cast<uint8_t>((1U << x_dfg_reg_alloc_num_group1_gprs) - 1);
                op->m_regConfig.m_group2PassthruGprIdxMask = static_cast<uint8_t>((1U << (x_dfg_reg_alloc_num_gprs - x_dfg_reg_alloc_num_group1_gprs)) - 1);
                op->m_regConfig.m_passthruFprIdxMask = static_cast<uint8_t>((1U << x_dfg_reg_alloc_num_fprs) - 1);
                op->m_regConfig.AssertConsistency();

                op->m_operandConfig.m_codegenFuncOrd = funcOrd;
                op->m_operandConfig.m_numInputOperands = 0;
            });
    }

    template<bool forGprState>
    void EmitMaterializeConstantLikeNodeToRegAllocReg(Node* node, size_t regIdx)
    {
        TestAssert(node->IsConstantLikeNode());
        TestAssertImp(node->IsConstantNode() || node->IsUnboxedConstantNode(), node->IsOrdInConstantTableAssigned());

        EmitCodegenOpWithRegAlloc(
            0 /*numInputOperands*/,
            [&](CodegenOpRegAllocEnabled* op) ALWAYS_INLINE
            {
                op->m_nsd = node->GetNodeSpecificDataOrNullptr();

                op->m_regConfig.m_group1ScratchGprIdxMask = 0;
                op->m_regConfig.m_group2ScratchGprIdxMask = 0;
                op->m_regConfig.m_scratchFprIdxMask = 0;
                op->m_regConfig.m_group1PassthruGprIdxMask = static_cast<uint8_t>((1U << x_dfg_reg_alloc_num_group1_gprs) - 1);
                op->m_regConfig.m_group2PassthruGprIdxMask = static_cast<uint8_t>((1U << (x_dfg_reg_alloc_num_gprs - x_dfg_reg_alloc_num_group1_gprs)) - 1);
                op->m_regConfig.m_passthruFprIdxMask = static_cast<uint8_t>((1U << x_dfg_reg_alloc_num_fprs) - 1);

                DfgCodegenFuncOrd funcOrd;
                uint16_t outputPhysicalSlot;
                if (forGprState)
                {
                    TestAssert(regIdx < x_dfg_reg_alloc_num_gprs);
                    if (regIdx < x_dfg_reg_alloc_num_gprs - x_dfg_reg_alloc_num_group1_gprs)
                    {
                        TestAssert((op->m_regConfig.m_group2PassthruGprIdxMask & (1U << regIdx)) > 0);
                        op->m_regConfig.m_group2PassthruGprIdxMask ^= static_cast<uint8_t>(1U << regIdx);

                        funcOrd = GetCodegenFnForMaterializingConstant(node->GetNodeKind(), ConstantLikeNodeMaterializeLocation::GprGroup2);
                    }
                    else
                    {
                        size_t k = regIdx - (x_dfg_reg_alloc_num_gprs - x_dfg_reg_alloc_num_group1_gprs);
                        TestAssert((op->m_regConfig.m_group1PassthruGprIdxMask & (1U << k)) > 0);
                        op->m_regConfig.m_group1PassthruGprIdxMask ^= static_cast<uint8_t>(1U << k);

                        funcOrd = GetCodegenFnForMaterializingConstant(node->GetNodeKind(), ConstantLikeNodeMaterializeLocation::GprGroup1);
                    }

                    outputPhysicalSlot = SafeIntegerCast<uint16_t>(m_firstRegisterSpillSlot + regIdx);

                    X64Reg reg = x_dfg_reg_alloc_gprs[regIdx];
                    op->m_regConfig.SetOperandReg(0 /*raIdx*/, reg);
                }
                else
                {
                    TestAssert(regIdx < x_dfg_reg_alloc_num_fprs);
                    TestAssert((op->m_regConfig.m_passthruFprIdxMask & (1U << regIdx)) > 0);
                    op->m_regConfig.m_passthruFprIdxMask ^= static_cast<uint8_t>(1U << regIdx);

                    funcOrd = GetCodegenFnForMaterializingConstant(node->GetNodeKind(), ConstantLikeNodeMaterializeLocation::Fpr);
                    outputPhysicalSlot = SafeIntegerCast<uint16_t>(m_firstRegisterSpillSlot + x_dfg_reg_alloc_num_gprs + regIdx);

                    X64Reg reg = x_dfg_reg_alloc_fprs[regIdx];
                    op->m_regConfig.SetOperandReg(0 /*raIdx*/, reg);
                }

                op->m_regConfig.AssertConsistency();

                op->m_operandConfig.m_codegenFuncOrd = funcOrd;
                op->m_operandConfig.m_outputPhysicalSlot = outputPhysicalSlot;
                op->m_operandConfig.m_numInputOperands = 0;
            });
    }

    static void ALWAYS_INLINE InitRegPassthruAndScratchInfo(RegAllocRegConfig& regInfo /*out*/,
                                                            RegAllocPassthruAndScratchRegInfo gprInfo,
                                                            RegAllocPassthruAndScratchRegInfo fprInfo)
    {
        regInfo.m_group1ScratchGprIdxMask = gprInfo.m_group1ScratchIdxMask;
        regInfo.m_group1PassthruGprIdxMask = gprInfo.m_group1PassthruIdxMask;
        regInfo.m_group2ScratchGprIdxMask = gprInfo.m_group2ScratchIdxMask;
        regInfo.m_group2PassthruGprIdxMask = gprInfo.m_group2PassthruIdxMask;
        regInfo.m_scratchFprIdxMask = fprInfo.m_group1ScratchIdxMask;
        regInfo.m_passthruFprIdxMask = fprInfo.m_group1PassthruIdxMask;
    }

    void EmitTypeCheck(DfgCodegenFuncOrd cgFnOrd, X64Reg opReg, RegAllocPassthruAndScratchRegInfo gprInfo, RegAllocPassthruAndScratchRegInfo fprInfo)
    {
        TestAssert(IsRegisterUsedForDfgRegAllocation(opReg));

        EmitCodegenOpWithRegAlloc(
            1 /*numInputOperands*/,
            [&](CodegenOpRegAllocEnabled* op) ALWAYS_INLINE
            {
                op->m_nsd = nullptr;
                InitRegPassthruAndScratchInfo(op->m_regConfig /*out*/, gprInfo, fprInfo);
                op->m_regConfig.SetOperandReg(0 /*raOpIdx*/, opReg);
                op->m_regConfig.AssertConsistency();

                op->m_operandConfig.m_codegenFuncOrd = cgFnOrd;
                op->m_operandConfig.m_numInputOperands = 1;
                op->m_operandConfig.m_inputOperandPhysicalSlots[0] = GetPhysicalSlotForReg(opReg);
            });
    }

    uint8_t* WARN_UNUSED AllocateNewSlowPathDataRegConfigEntry()
    {
        size_t sizeBytes = DfgSlowPathRegConfigDataTraits::x_slowPathDataCompactRegConfigInfoSizeBytes;
        return m_slowPathDataRegConfigEntries.AppendRaw(m_alloc, sizeBytes);
    }

    std::span<uint8_t> WARN_UNUSED CloneAndGetCodegenLog(TempArenaAllocator& alloc)
    {
        return m_log.Clone(alloc);
    }

    std::span<uint8_t> WARN_UNUSED CloneAndGetSlowPathDataRegConfigStream(TempArenaAllocator& alloc)
    {
        return m_slowPathDataRegConfigEntries.Clone(alloc);
    }

    void AppendOpaqueFastPathJitCode(uint32_t numBytes)
    {
        m_jitCodeSize.m_fastPathLength += numBytes;
    }

    // For initialization only
    //
    void SetJITCodeSizeInfo(JITCodeSizeInfo info)
    {
        m_jitCodeSize = info;
    }

    JITCodeSizeInfo WARN_UNUSED GetJitCodeSizeInfo()
    {
        return m_jitCodeSize;
    }

private:
    struct LogBuffer
    {
        LogBuffer() : m_length(0), m_capacity(0), m_buffer(nullptr) { }

        // Note that this only allocates the space, caller should use ConstructInPlace to construct the object
        //
        template<typename T>
        T* WARN_UNUSED Append(TempArenaAllocator& alloc, size_t numBytes)
        {
            static_assert(std::is_base_of_v<CodegenOpBase, T>);
            static_assert(alignof(T) == 1);
            return std::launder(reinterpret_cast<T*>(AppendRaw(alloc, numBytes)));
        }

        uint8_t* WARN_UNUSED AppendRaw(TempArenaAllocator& alloc, size_t numBytes)
        {
            size_t desiredBufferSize = m_length + numBytes;
            if (desiredBufferSize > m_capacity)
            {
                size_t newCapacity = std::max(m_capacity * 3 / 2, desiredBufferSize);
                uint8_t* newBuffer = alloc.AllocateArray<uint8_t>(newCapacity);
                memcpy(newBuffer, m_buffer, m_length);
                m_capacity = newCapacity;
                m_buffer = newBuffer;
            }

            TestAssert(m_capacity >= desiredBufferSize);
            uint8_t* res = m_buffer + m_length;
            m_length += numBytes;
            return res;
        }

        std::span<uint8_t> WARN_UNUSED Clone(TempArenaAllocator& alloc)
        {
            uint8_t* addr = alloc.AllocateArray<uint8_t>(m_length);
            memcpy(addr, m_buffer, m_length);
            return { addr, m_length };
        }

        size_t m_length;
        size_t m_capacity;
        uint8_t* m_buffer;
    };

    uint16_t WARN_UNUSED GetPhysicalSlotForReg(X64Reg reg)
    {
        TestAssert(IsRegisterUsedForDfgRegAllocation(reg));
        return static_cast<uint16_t>(m_firstRegisterSpillSlot + GetDfgRegAllocSequenceOrdForReg(reg));
    }

    TempArenaAllocator& m_alloc;
    LogBuffer m_log;
    LogBuffer m_slowPathDataRegConfigEntries;
    // This is the total JIT code size, it is not reset on new basic block
    // This also means that the order the basic blocks are generated must also be how they are physically laid out,
    // or the data section alignment will be broken.
    //
    JITCodeSizeInfo m_jitCodeSize;
    uint16_t m_firstRegisterSpillSlot;
};

}   // namespace dfg
