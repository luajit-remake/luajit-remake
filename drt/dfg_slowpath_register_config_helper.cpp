#include "dfg_slowpath_register_config_helper.h"
#include "dfg_reg_alloc_state.h"
#include "dfg_codegen_register_renamer.h"

namespace dfg {

void EncodeDfgRegAllocStateToSlowPathData(DfgRegAllocState& state, uint8_t* dest /*out*/)
{
    using Traits = DfgSlowPathRegConfigDataTraits;

    DfgRegAllocState::AllRegPurposeInfo info;
    [[clang::always_inline]] info = state.GetAllRegPurposeInfo();

    static_assert(info.x_length % Traits::x_packSize == 0);
    static_assert(Traits::x_numElements * Traits::x_packSize == info.x_length);

    static_assert(sizeof(Traits::PackType) * 8 >= 6 * Traits::x_packSize);

    for (size_t seqOrd = 0; seqOrd < info.x_length; seqOrd += Traits::x_packSize)
    {
        Traits::PackType value = 0;
        for (size_t i = 0; i < Traits::x_packSize; i++)
        {
            uint8_t regClass = static_cast<uint8_t>(info.m_data[seqOrd + i].first);
            TestAssert(regClass < 8);
            uint8_t ordInClass = info.m_data[seqOrd + i].second;
            TestAssert(ordInClass < 8);
            value |= static_cast<Traits::PackType>(regClass) << (i * 6 + 3);
            value |= static_cast<Traits::PackType>(ordInClass) << (i * 6);
        }
        UnalignedStore<Traits::PackType>(dest, value);
        dest += sizeof(Traits::PackType);
    }
}

constexpr std::array<X64Reg, DfgSlowPathRegConfigDataTraits::x_packSize * DfgSlowPathRegConfigDataTraits::x_numElements> x_dfg_reg_sequence_for_slow_path_data_reg_decode = []() {
    using Traits = DfgSlowPathRegConfigDataTraits;
    std::array<X64Reg, Traits::x_packSize * Traits::x_numElements> res;
    for (size_t i = 0; i < x_dfg_reg_alloc_num_gprs + x_dfg_reg_alloc_num_fprs; i++)
    {
        res[i] = GetDfgRegFromRegAllocSequenceOrd(i);
    }
    for (size_t i = x_dfg_reg_alloc_num_gprs + x_dfg_reg_alloc_num_fprs; i < Traits::x_packSize * Traits::x_numElements; i++)
    {
        res[i] = X64Reg::RAX;
    }
    return res;
}();

static void DecodeDfgSlowPathDataToRegAllocState(const uint8_t* slowPathData, RegAllocStateForCodeGen& state /*out*/)
{
    using RegClass = dast::StencilRegIdentClass;
    using Traits = DfgSlowPathRegConfigDataTraits;

    size_t curRegSeqOrd = 0;
    const uint8_t* dataEnd = slowPathData + sizeof(Traits::PackType) * Traits::x_numElements;
    while (slowPathData < dataEnd)
    {
        Traits::PackType value = UnalignedLoad<Traits::PackType>(slowPathData);
        slowPathData += sizeof(Traits::PackType);

        for (size_t i = 0; i < Traits::x_packSize; i++)
        {
            RegClass regClass = static_cast<RegClass>(value & 7);
            value >>= 3;
            uint8_t ordInClass = static_cast<uint8_t>(value & 7);
            value >>= 3;

            TestAssert(curRegSeqOrd < std::extent_v<decltype(x_dfg_reg_sequence_for_slow_path_data_reg_decode)>);
            X64Reg reg = x_dfg_reg_sequence_for_slow_path_data_reg_decode[curRegSeqOrd];

            TestAssertImp(regClass != RegClass::X_END_OF_ENUM, RegAllocStateForCodeGen::IsRegisterCompatibleWithRegClass(reg, regClass));
            TestAssertIff(regClass == RegClass::X_END_OF_ENUM, curRegSeqOrd >= x_dfg_reg_alloc_num_gprs + x_dfg_reg_alloc_num_fprs);
            state.Set(regClass, ordInClass, reg.MachineOrd() & 7);

            curRegSeqOrd++;
        }
    }

    TestAssert(slowPathData == dataEnd);
    TestAssert(curRegSeqOrd == Traits::x_packSize * Traits::x_numElements);
}

void RunStencilRegisterPatchingPhaseUsingRegInfoFromSlowPathData(RestrictPtr<uint8_t> code,
                                                                 ConstRestrictPtr<uint8_t> slowPathDataRegInfo,
                                                                 ConstRestrictPtr<uint16_t> patches)
{
    RegAllocStateForCodeGen state;
    DecodeDfgSlowPathDataToRegAllocState(slowPathDataRegInfo, state /*out*/);
    RunStencilRegisterPatchingPhase(code, &state, patches);
}

}   // namespace dfg
