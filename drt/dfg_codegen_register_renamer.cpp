#include "dfg_codegen_register_renamer.h"
#include "dfg_reg_alloc_state.h"

namespace dfg {

void RunStencilRegisterPatchingPhase(RestrictPtr<uint8_t> code,
                                     RestrictPtr<RegAllocStateForCodeGen> regInfo,
                                     ConstRestrictPtr<uint16_t> patches)
{
    using RegClass = dast::StencilRegIdentClass;

    uint16_t numChunks = *patches;
    patches++;
    while (numChunks > 0)
    {
        numChunks--;
        uint16_t numPatches = *patches;
        patches++;
        const uint16_t* patchEnd = patches + numPatches;
        while (patches < patchEnd)
        {
            RuntimeStencilRegRenamingPatchItem item = RuntimeStencilRegRenamingPatchItem::Parse(*patches);
            patches++;

            TestAssert(item.regClass < static_cast<size_t>(RegClass::X_END_OF_ENUM));
            uint8_t reg = regInfo->Get(static_cast<RegClass>(item.regClass), item.ordInClass);
            TestAssert(reg < 8);
            item.Apply(code, reg);
        }
        code += RuntimeStencilRegRenamingPatchItem::x_bytesPerChunk;
    }
}

}   // namespace dfg
