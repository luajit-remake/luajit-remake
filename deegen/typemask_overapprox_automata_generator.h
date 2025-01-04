#include "common_utils.h"

#include "drt/dfg_typemask_overapprox_automata.h"

namespace dast {

// See comments in drt/dfg_typemask_overapprox_automata.h
//
struct TypemaskOverapproxAutomataGenerator
{
    void AddItem(TypeMaskTy mask, uint16_t value)
    {
        m_items.push_back(std::make_pair(mask, value));
    }

    std::vector<uint8_t> WARN_UNUSED GenerateAutomata()
    {
        return GenerateImpl(false /*forLeafOpted*/);
    }

    std::vector<uint8_t> WARN_UNUSED GenerateAutomataLeafOpted()
    {
        return GenerateImpl(true /*forLeafOpted*/);
    }

private:
    using ItemTy = std::pair<TypeMaskTy, uint16_t>;

    std::vector<ItemTy> WARN_UNUSED MakeClosure();
    std::vector<uint8_t> WARN_UNUSED GenerateImpl(bool forLeafOpted);

    std::vector<ItemTy> m_items;
};

}   // namespace dast
