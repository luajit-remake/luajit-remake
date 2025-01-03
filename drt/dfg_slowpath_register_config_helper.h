#pragma once

#include "common_utils.h"
#include "dfg_reg_alloc_register_info.h"
#include "heap_ptr_utils.h"

namespace dfg {

// In SlowPathData, the reg alloc configuration is stored compactly:
// for each register participating in reg alloc, we store the StencilRegIdentClass using 3 bits and
// the ordinal within the purpose class (3 bits). We then use a uint32_t to store 5 register info.
//
struct DfgSlowPathRegConfigDataTraits
{
    using PackType = uint32_t;
    static constexpr size_t x_packSize = 5;
    static constexpr size_t x_numElements = RoundUpToMultipleOf<x_packSize>(x_dfg_reg_alloc_num_gprs + x_dfg_reg_alloc_num_fprs) / x_packSize;

    static constexpr size_t x_slowPathDataCompactRegConfigInfoSizeBytes = sizeof(PackType) * x_numElements;
};

struct DfgRegAllocState;
struct RegAllocStateForCodeGen;

// 'dest' should be of length sizeof(PackType) * DfgSlowPathRegConfigDataTraits::x_numElements bytes
// Note that everything in SlowPathData is not aligned, so it has to be passed as uint8_t*
//
void EncodeDfgRegAllocStateToSlowPathData(DfgRegAllocState& state, uint8_t* dest /*out*/);

// Similar to RunStencilRegisterPatchingPhase, except that the register configuration comes from SlowPathData
//
void RunStencilRegisterPatchingPhaseUsingRegInfoFromSlowPathData(RestrictPtr<uint8_t> code,
                                                                 ConstRestrictPtr<uint8_t> slowPathDataRegInfo,
                                                                 ConstRestrictPtr<uint16_t> patches);

}   // namespace dfg
