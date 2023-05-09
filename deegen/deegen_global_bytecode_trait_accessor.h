#pragma once

#include "common.h"
#include "json_utils.h"

namespace dast {

struct BytecodeOpcodeRawValueMap;

// Normally, when Deegen process a bytecode, it only has access to the bytecode being processed,
// not any other bytecodes. This works fine in most cases as normally bytecodes traits are independent
// from each other, so only having local knowledge is enough.
//
// However, there are also cases where a bytecode trait requires *global* knowledge.
// As the simplest example, if we want a global dispatch table where each bytecode takes a slot,
// then in order to know the index of our own slot in the global table, we need to know the list
// of all the bytecodes and how they are ordered in the table.
//
// This class is designed to facilitate such use cases by allowing one to access certain traits of any bytecode.
//
// It currently works like the following:
// 1. The interpreter builder would produce a .json file for each bytecode
// 2. After the interpreter build stage finishes, a special build phase collects all those .json files,
//    collects needed trait info from them, and put the info together into a unfied .json file
// 3. This unified .json file will be passed to the later build stages, which will be parsed into this struct
//
// This also means that only the JIT builder stages have access to this utility, and in order for
// the JIT builder stages to have access to some info, the info has to be collected at the interpreter
// build stage and logged in the .json file
//
class DeegenGlobalBytecodeTraitAccessor
{
public:
    static DeegenGlobalBytecodeTraitAccessor WARN_UNUSED Build(BytecodeOpcodeRawValueMap& bytecodeOrderMap,
                                                               std::vector<json>& allJsonInfo);

    json WARN_UNUSED SaveToJson();
    static DeegenGlobalBytecodeTraitAccessor WARN_UNUSED LoadFromJson(json& j);
    static DeegenGlobalBytecodeTraitAccessor WARN_UNUSED ParseFromCommandLineArgs();

    // Note that all of the functions below only works for primary bytecodes (not fused-ic variants)
    //
    uint64_t GetBytecodeOpcodeOrd(const std::string& bytecodeName) const
    {
        Trait trait = GetTraitFromBytecodeName(bytecodeName);
        return trait.m_opcodeOrdinal;
    }

    uint64_t GetNumJitCallICSites(const std::string& bytecodeName) const
    {
        Trait trait = GetTraitFromBytecodeName(bytecodeName);
        return trait.m_numJitCallIC;
    }

    uint64_t GetJitCallIcTraitOrdStart(const std::string& bytecodeName) const
    {
        Trait trait = GetTraitFromBytecodeName(bytecodeName);
        return trait.m_jitCallIcTraitBaseOrdinal;
    }

    uint64_t GetJitCallIcTraitOrd(const std::string& bytecodeName, size_t icSiteOrd, bool isDirectCall) const
    {
        Trait trait = GetTraitFromBytecodeName(bytecodeName);
        ReleaseAssert(icSiteOrd < trait.m_numJitCallIC);
        return trait.m_jitCallIcTraitBaseOrdinal + icSiteOrd * 2 + (isDirectCall ? 0 : 1);
    }

    uint64_t GetNumInterpreterFusedIcVariants(const std::string& bytecodeName) const
    {
        Trait trait = GetTraitFromBytecodeName(bytecodeName);
        return trait.m_numInterpreterFusedIcVariants;
    }

    uint64_t GetNumTotalGenericIcEffectKinds(const std::string& bytecodeName) const
    {
        Trait trait = GetTraitFromBytecodeName(bytecodeName);
        return trait.m_numGenericIcTotalEffectKinds;
    }

    uint64_t GetGenericIcEffectTraitBaseOrdinal(const std::string& bytecodeName) const
    {
        Trait trait = GetTraitFromBytecodeName(bytecodeName);
        return trait.m_genericIcEffectTraitBaseOrdinal;
    }

    uint64_t GetJitCallIcTraitTableLength() const
    {
        uint64_t res = 0;
        for (const auto& it : m_traitSet)
        {
            res += it.second.m_numJitCallIC * 2;
        }
        return res;
    }

    uint64_t GetJitGenericIcEffectTraitTableLength() const
    {
        uint64_t res = 0;
        for (const auto& it : m_traitSet)
        {
            res += it.second.m_numGenericIcTotalEffectKinds;
        }
        return res;
    }

private:
    // The trait collected for each primary (non-fused-ic) bytecode
    //
    struct Trait
    {
        // The ordinal of this bytecode in the dispatch table
        //
        uint64_t m_opcodeOrdinal;
        // Number of interpreter fused-ic variants
        //
        uint64_t m_numInterpreterFusedIcVariants;
        // Number of JIT call IC sites in this bytecode
        //
        uint64_t m_numJitCallIC;
        // The base ordinal for the JIT call IC trait
        // So for call IC site #K in this bytecode, the direct-call IC trait ord is base + K * 2,
        // and the closure-call IC trait ord is base + K * 2 + 1
        //
        uint64_t m_jitCallIcTraitBaseOrdinal;
        // The total number of effect kinds of all Generic IC used in this bytecode
        //
        uint64_t m_numGenericIcTotalEffectKinds;
        // The base ordinal for the global ordering of all the Generic IC effects
        //
        uint64_t m_genericIcEffectTraitBaseOrdinal;
    };

    Trait WARN_UNUSED GetTraitFromBytecodeName(const std::string& bytecodeName) const
    {
        ReleaseAssert(m_traitSet.count(bytecodeName));
        return m_traitSet.find(bytecodeName)->second;
    }

    std::vector<std::string> m_bytecodeOrder;
    std::unordered_map<std::string, Trait> m_traitSet;
};

}   // namespace dast
