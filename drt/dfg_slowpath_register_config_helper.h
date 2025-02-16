#pragma once

#include "common_utils.h"
#include "dfg_reg_alloc_register_info.h"
#include "heap_ptr_utils.h"

namespace dfg {

// In SlowPathData, the reg alloc configuration is stored compactly:
// for each register participating in reg alloc, we store the StencilRegIdentClass using 3 bits and
// the ordinal within the purpose class (3 bits).
//
struct DfgSlowPathRegConfigDataTraits
{
    // Each register info only needs 6 bits.
    // We support bit-packing multiple register info together to save space,
    // but it's not needed now since we currently have total 13 regs,
    // so packing 5 regs into a uint32_t only saves 1 byte (3*4=12 bytes after packing) which is not worth it
    //
    using PackType = uint8_t;
    static constexpr size_t x_packSize = 1;
    static constexpr size_t x_numElements = RoundUpToMultipleOf<x_packSize>(x_dfg_reg_alloc_num_gprs + x_dfg_reg_alloc_num_fprs) / x_packSize;

    static constexpr size_t x_slowPathDataCompactRegConfigInfoSizeBytes = sizeof(PackType) * x_numElements;
};

struct RegAllocAllRegPurposeInfo;

// 'dest' should be of length sizeof(PackType) * DfgSlowPathRegConfigDataTraits::x_numElements bytes
// Note that everything in SlowPathData is not aligned, so it has to be passed as uint8_t*
//
void EncodeDfgRegAllocStateToSlowPathData(const RegAllocAllRegPurposeInfo& regPurposeInfo, uint8_t* dest /*out*/);

// Similar to RunStencilRegisterPatchingPhase, except that the register configuration comes from SlowPathData
//
void RunStencilRegisterPatchingPhaseUsingRegInfoFromSlowPathData(RestrictPtr<uint8_t> code,
                                                                 ConstRestrictPtr<uint8_t> slowPathDataRegInfo,
                                                                 ConstRestrictPtr<uint16_t> patches);

}   // namespace dfg
