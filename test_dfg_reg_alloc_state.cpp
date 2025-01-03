#include "gtest/gtest.h"

#include "test_util_helper.h"

#include "dfg_reg_alloc_state.h"

using namespace dfg;

TEST(DfgRegAllocState, Sanity_1)
{
    TempArenaAllocator alloc;
    TempVector<RegAllocCodegenStateLogItem> logBuffer(alloc);
    DfgRegAllocState state(logBuffer);
    state.AssertConsistency();

    auto assertIsScratch = [&](DfgRegAllocState::RegClass regClass, X64Reg reg)
    {
        if (reg.IsGPR())
        {
            if (reg.MachineOrd() < 8)
            {
                ReleaseAssert(regClass == DfgRegAllocState::RegClass::ScNonExtG);
            }
            else
            {
                ReleaseAssert(regClass == DfgRegAllocState::RegClass::ScExtG);
            }
        }
        else
        {
            ReleaseAssert(regClass== DfgRegAllocState::RegClass::ScF);
        }
    };

    auto assertIsPassthru = [&](DfgRegAllocState::RegClass regClass, X64Reg reg)
    {
        if (reg.IsGPR())
        {
            if (reg.MachineOrd() < 8)
            {
                ReleaseAssert(regClass == DfgRegAllocState::RegClass::PtNonExtG);
            }
            else
            {
                ReleaseAssert(regClass == DfgRegAllocState::RegClass::PtExtG);
            }
        }
        else
        {
            ReleaseAssert(regClass== DfgRegAllocState::RegClass::PtF);
        }
    };

    {
        DfgRegAllocState::AllRegPurposeInfo info = state.GetAllRegPurposeInfo();
        for (size_t i = 0; i < x_dfg_reg_alloc_num_gprs + x_dfg_reg_alloc_num_fprs; i++)
        {
            X64Reg reg = GetDfgRegFromRegAllocSequenceOrd(i);
            assertIsScratch(info.m_data[i].first, reg);
        }
    }

    X64Reg op1 = state.UseFreeGprAsOperand(0);
    state.AssertConsistency();

    X64Reg op2 = state.UseFreeGprAsOperand(1);
    state.AssertConsistency();

    X64Reg op3 = state.UseFreeGprAsOperand(3);
    state.AssertConsistency();

    X64Reg op4 = state.UseFreeFprAsOperand(5);
    state.AssertConsistency();

    {
        DfgRegAllocState::AllRegPurposeInfo info = state.GetAllRegPurposeInfo();
        ReleaseAssert(info.m_data[GetDfgRegAllocSequenceOrdForReg(op1)].first == DfgRegAllocState::RegClass::Operand);
        ReleaseAssert(info.m_data[GetDfgRegAllocSequenceOrdForReg(op1)].second == 0);
        ReleaseAssert(info.m_data[GetDfgRegAllocSequenceOrdForReg(op2)].first == DfgRegAllocState::RegClass::Operand);
        ReleaseAssert(info.m_data[GetDfgRegAllocSequenceOrdForReg(op2)].second == 1);
        ReleaseAssert(info.m_data[GetDfgRegAllocSequenceOrdForReg(op3)].first == DfgRegAllocState::RegClass::Operand);
        ReleaseAssert(info.m_data[GetDfgRegAllocSequenceOrdForReg(op3)].second == 3);
        ReleaseAssert(info.m_data[GetDfgRegAllocSequenceOrdForReg(op4)].first == DfgRegAllocState::RegClass::Operand);
        ReleaseAssert(info.m_data[GetDfgRegAllocSequenceOrdForReg(op4)].second == 5);
    }

    state.ClearOperands(DfgRegAllocState::FillDummyPassthroughRecord());
    state.AssertConsistency();

    {
        DfgRegAllocState::AllRegPurposeInfo info = state.GetAllRegPurposeInfo();
        assertIsPassthru(info.m_data[GetDfgRegAllocSequenceOrdForReg(op1)].first, op1);
        assertIsPassthru(info.m_data[GetDfgRegAllocSequenceOrdForReg(op2)].first, op2);
        assertIsPassthru(info.m_data[GetDfgRegAllocSequenceOrdForReg(op3)].first, op3);
        assertIsPassthru(info.m_data[GetDfgRegAllocSequenceOrdForReg(op4)].first, op4);
    }

    ReleaseAssert(state.NumActiveGPRs() == 3);
    ReleaseAssert(state.NumActiveFPRs() == 1);

    state.FreeRegister(op1);
    state.AssertConsistency();

    ReleaseAssert(state.NumActiveGPRs() == 2);
    ReleaseAssert(state.NumActiveFPRs() == 1);

    {
        DfgRegAllocState::AllRegPurposeInfo info = state.GetAllRegPurposeInfo();
        assertIsScratch(info.m_data[GetDfgRegAllocSequenceOrdForReg(op1)].first, op1);
        assertIsPassthru(info.m_data[GetDfgRegAllocSequenceOrdForReg(op2)].first, op2);
        assertIsPassthru(info.m_data[GetDfgRegAllocSequenceOrdForReg(op3)].first, op3);
        assertIsPassthru(info.m_data[GetDfgRegAllocSequenceOrdForReg(op4)].first, op4);
    }

    state.FreeRegister(op3);
    state.AssertConsistency();

    ReleaseAssert(state.NumActiveGPRs() == 1);
    ReleaseAssert(state.NumActiveFPRs() == 1);

    {
        DfgRegAllocState::AllRegPurposeInfo info = state.GetAllRegPurposeInfo();
        assertIsScratch(info.m_data[GetDfgRegAllocSequenceOrdForReg(op1)].first, op1);
        assertIsPassthru(info.m_data[GetDfgRegAllocSequenceOrdForReg(op2)].first, op2);
        assertIsScratch(info.m_data[GetDfgRegAllocSequenceOrdForReg(op3)].first, op3);
        assertIsPassthru(info.m_data[GetDfgRegAllocSequenceOrdForReg(op4)].first, op4);
    }

    state.FreeRegister(op4);
    state.AssertConsistency();

    ReleaseAssert(state.NumActiveGPRs() == 1);
    ReleaseAssert(state.NumActiveFPRs() == 0);

    {
        DfgRegAllocState::AllRegPurposeInfo info = state.GetAllRegPurposeInfo();
        assertIsScratch(info.m_data[GetDfgRegAllocSequenceOrdForReg(op1)].first, op1);
        assertIsPassthru(info.m_data[GetDfgRegAllocSequenceOrdForReg(op2)].first, op2);
        assertIsScratch(info.m_data[GetDfgRegAllocSequenceOrdForReg(op3)].first, op3);
        assertIsScratch(info.m_data[GetDfgRegAllocSequenceOrdForReg(op4)].first, op4);
    }

    state.UseActiveRegAsOperand(op2, 2);

    ReleaseAssert(state.NumActiveGPRs() == 0);
    ReleaseAssert(state.NumActiveFPRs() == 0);

    {
        DfgRegAllocState::AllRegPurposeInfo info = state.GetAllRegPurposeInfo();
        assertIsScratch(info.m_data[GetDfgRegAllocSequenceOrdForReg(op1)].first, op1);
        ReleaseAssert(info.m_data[GetDfgRegAllocSequenceOrdForReg(op2)].first == DfgRegAllocState::RegClass::Operand);
        ReleaseAssert(info.m_data[GetDfgRegAllocSequenceOrdForReg(op2)].second == 2);
        assertIsScratch(info.m_data[GetDfgRegAllocSequenceOrdForReg(op3)].first, op3);
        assertIsScratch(info.m_data[GetDfgRegAllocSequenceOrdForReg(op4)].first, op4);
    }

    state.ClearOperands(DfgRegAllocState::FillDummyPassthroughRecord());
    state.AssertConsistency();

    {
        DfgRegAllocState::AllRegPurposeInfo info = state.GetAllRegPurposeInfo();
        assertIsScratch(info.m_data[GetDfgRegAllocSequenceOrdForReg(op1)].first, op1);
        assertIsPassthru(info.m_data[GetDfgRegAllocSequenceOrdForReg(op2)].first, op2);
        assertIsScratch(info.m_data[GetDfgRegAllocSequenceOrdForReg(op3)].first, op3);
        assertIsScratch(info.m_data[GetDfgRegAllocSequenceOrdForReg(op4)].first, op4);
    }
}
