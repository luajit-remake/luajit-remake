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

    std::vector<uint8_t> WARN_UNUSED GenerateAutomata(size_t* automataMaxDepth = nullptr /*out*/)
    {
        return GenerateImpl(false /*forLeafOpted*/, automataMaxDepth /*out*/);
    }

    std::vector<uint8_t> WARN_UNUSED GenerateAutomataLeafOpted(size_t* automataMaxDepth = nullptr /*out*/)
    {
        return GenerateImpl(true /*forLeafOpted*/, automataMaxDepth /*out*/);
    }

    using ItemTy = std::pair<TypeMaskTy, uint16_t>;

    // Return the bitwise-and-closure of the given items.
    //
    std::vector<ItemTy> WARN_UNUSED MakeClosure();

private:
    std::vector<uint8_t> WARN_UNUSED GenerateImpl(bool forLeafOpted, size_t* automataMaxDepth /*out*/);

    std::vector<ItemTy> m_items;
};

}   // namespace dast
