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
        m_forLeafOpted = false;
        m_usingCppGoto = false;
        m_cppFp = nullptr;
        m_cppLabelPrefix = "";
        m_cppInputVarName = "";
        m_cppCallback = nullptr;
        m_automataMaxDepthOut = automataMaxDepth;
        return GenerateImpl();
    }

    std::vector<uint8_t> WARN_UNUSED GenerateAutomataLeafOpted(size_t* automataMaxDepth = nullptr /*out*/)
    {
        m_forLeafOpted = true;
        m_usingCppGoto = false;
        m_cppFp = nullptr;
        m_cppLabelPrefix = "";
        m_cppInputVarName = "";
        m_cppCallback = nullptr;
        m_automataMaxDepthOut = automataMaxDepth;
        return GenerateImpl();
    }

    // Note that 'callback' must check for and handle result == -1 gracefully,
    // and the C++ code printed by 'callback' must not fallthrough to the next line!
    //
    void GenerateAutomataWithCppGoto(FILE* fp, std::string cppLabelPrefix, std::string cppInputVarName, std::function<void(uint16_t /*resultVal*/)> callback)
    {
        m_forLeafOpted = false;
        m_usingCppGoto = true;
        m_cppFp = fp;
        m_cppLabelPrefix = cppLabelPrefix;
        m_cppInputVarName = cppInputVarName;
        m_cppCallback = callback;
        m_automataMaxDepthOut = nullptr;
        ReleaseAssert(fp != nullptr && cppLabelPrefix != "" && cppInputVarName != "" && callback != nullptr);
        std::ignore = GenerateImpl();
    }

    using ItemTy = std::pair<TypeMaskTy, uint16_t>;

    // Return the bitwise-and-closure of the given items.
    //
    std::vector<ItemTy> WARN_UNUSED MakeClosure();

private:
    std::vector<uint8_t> WARN_UNUSED GenerateImpl();

    std::vector<ItemTy> m_items;

    // Configurations used by GenerateImpl()
    //
    bool m_forLeafOpted;
    bool m_usingCppGoto;
    FILE* m_cppFp;
    std::string m_cppLabelPrefix;
    std::string m_cppInputVarName;
    std::function<void(uint16_t /*resultVal*/)> m_cppCallback;
    size_t* m_automataMaxDepthOut;
};

// Utility to generate an automata where each mask is also associated with a cost,
// and given an input mask, it returns the covering mask with minimal cost (instead of the minimal covering mask)
//
// Internally, this is implemented as a wrapper over the TypemaskOverapproxAutomataGenerator
//
struct MinCostTypemaskOverapproxAutomataGenerator
{
    void AddItem(TypeMask mask, uint16_t identValue, size_t cost)
    {
        m_items.push_back({
            .m_identValue = identValue,
            .m_mask = mask,
            .m_cost = cost
        });
    }

    // Return the automata that does the job.
    //
    TypemaskOverapproxAutomataGenerator WARN_UNUSED GetAutomata();

private:
    // The candidate may be selected if the input mask \subset m_mask. The cost is m_cost
    //
    struct CandidateInfo
    {
        uint16_t m_identValue;
        TypeMask m_mask;
        size_t m_cost;
    };
    std::vector<CandidateInfo> m_items;
};

}   // namespace dast
