#include "deegen_global_bytecode_trait_accessor.h"
#include "deegen_bytecode_operand.h"

namespace dast {

DeegenGlobalBytecodeTraitAccessor WARN_UNUSED DeegenGlobalBytecodeTraitAccessor::Build(BytecodeOpcodeRawValueMap& bytecodeOrderMap,
                                                                                       std::vector<json>& allJsonInfo)
{
    std::unordered_map<std::string, Trait> allTraits;
    for (json& oneJsonInfo : allJsonInfo)
    {
        ReleaseAssert(oneJsonInfo.count("all-bytecode-info"));
        json& bytecodeInfoListJson = oneJsonInfo["all-bytecode-info"];
        ReleaseAssert(bytecodeInfoListJson.is_array());
        for (size_t bytecodeDefOrd = 0; bytecodeDefOrd < bytecodeInfoListJson.size(); bytecodeDefOrd++)
        {
            json& curBytecodeInfoJson = bytecodeInfoListJson[bytecodeDefOrd];

            ReleaseAssert(curBytecodeInfoJson.count("bytecode_variant_definition"));
            std::unique_ptr<BytecodeVariantDefinition> bytecodeDef = std::make_unique<BytecodeVariantDefinition>(curBytecodeInfoJson["bytecode_variant_definition"]);

            std::string bytecodeId = bytecodeDef->GetBytecodeIdName();
            ReleaseAssert(!bytecodeOrderMap.IsFusedIcVariant(bytecodeId));

            ReleaseAssert(!allTraits.count(bytecodeId));
            allTraits[bytecodeId] = {
                .m_opcodeOrdinal = bytecodeOrderMap.GetOpcode(bytecodeId),
                .m_numInterpreterFusedIcVariants = bytecodeOrderMap.GetNumInterpreterFusedIcVariants(bytecodeId),
                .m_numJitCallIC = bytecodeDef->GetNumCallICsInJitTier(),
                .m_jitCallIcTraitBaseOrdinal = static_cast<uint64_t>(-1),                       // compute later
                .m_numGenericIcTotalEffectKinds = bytecodeDef->GetTotalGenericIcEffectKinds(),
                .m_genericIcEffectTraitBaseOrdinal = static_cast<uint64_t>(-1)                  // compute later
            };
        }
    }

    std::vector<std::string> bytecodeOrder = bytecodeOrderMap.GetPrimaryBytecodeList();

    // For sanity, validate that the set of bytecodes in bytecodeOrder is exactly the set of bytecodes in allTraits
    //
    {
        std::unordered_set<std::string> checkSet;
        for (std::string& bytecodeId : bytecodeOrder)
        {
            ReleaseAssert(!checkSet.count(bytecodeId));
            checkSet.insert(bytecodeId);
        }

        for (auto& it : allTraits)
        {
            std::string bytecodeId = it.first;
            ReleaseAssert(checkSet.count(bytecodeId));
            checkSet.erase(checkSet.find(bytecodeId));
        }

        ReleaseAssert(checkSet.empty());
    }

    // Iterate through each bytecode in order and compute global trait information
    //
    size_t curJitCallIcTraitOrd = 0;
    size_t curGenericIcEffectKindTraitOrd = 0;
    for (std::string& bytecodeId : bytecodeOrder)
    {
        ReleaseAssert(allTraits.count(bytecodeId));
        Trait& trait = allTraits[bytecodeId];
        trait.m_jitCallIcTraitBaseOrdinal = curJitCallIcTraitOrd;
        curJitCallIcTraitOrd += trait.m_numJitCallIC * 2;
        trait.m_genericIcEffectTraitBaseOrdinal = curGenericIcEffectKindTraitOrd;
        curGenericIcEffectKindTraitOrd += trait.m_numGenericIcTotalEffectKinds;
    }

    DeegenGlobalBytecodeTraitAccessor r;
    r.m_bytecodeOrder = bytecodeOrder;
    r.m_traitSet = std::move(allTraits);

    // For sanity, make sure everything is the same after a save/load
    //
    {
        json savedJson = r.SaveToJson();
        DeegenGlobalBytecodeTraitAccessor r2 = LoadFromJson(savedJson);
        ReleaseAssert(r.m_bytecodeOrder == r2.m_bytecodeOrder);
        ReleaseAssert(r2.m_traitSet.size() == r.m_traitSet.size());
        for (auto& it : r.m_traitSet)
        {
            std::string bytecodeId = it.first;
            ReleaseAssert(r2.m_traitSet.count(bytecodeId));
            Trait t1 = it.second;
            Trait t2 = r2.m_traitSet[bytecodeId];
            // This may generate false positive if the struct has padding, but for now..
            //
            ReleaseAssert(memcmp(&t1, &t2, sizeof(Trait)) == 0);
        }
    }

    return r;
}

json WARN_UNUSED DeegenGlobalBytecodeTraitAccessor::SaveToJson()
{
    std::vector<json> list;
    ReleaseAssert(m_bytecodeOrder.size() == m_traitSet.size());
    for (std::string& bytecodeId : m_bytecodeOrder)
    {
        ReleaseAssert(m_traitSet.count(bytecodeId));
        Trait trait = m_traitSet[bytecodeId];

        json j;
        j["bytecode_id"] = bytecodeId;
        j["opcode_ord"] = trait.m_opcodeOrdinal;
        j["num_fused_ic_variants"] = trait.m_numInterpreterFusedIcVariants;
        j["jit_num_call_ic"] = trait.m_numJitCallIC;
        j["jit_call_ic_base_ord"] = trait.m_jitCallIcTraitBaseOrdinal;
        j["num_generic_ic_total_effect_kinds"] = trait.m_numGenericIcTotalEffectKinds;
        j["generic_ic_effect_kind_base_ord"] = trait.m_genericIcEffectTraitBaseOrdinal;
        list.push_back(j);
    }

    return list;
}

DeegenGlobalBytecodeTraitAccessor WARN_UNUSED DeegenGlobalBytecodeTraitAccessor::LoadFromJson(json& j)
{
    std::vector<std::string> bytecodeOrder;
    std::unordered_map<std::string, Trait> allTraits;

    ReleaseAssert(j.is_array());
    for (size_t ord = 0; ord < j.size(); ord++)
    {
        json& info = j[ord];
        std::string bytecodeId;
        Trait trait;
        JSONCheckedGet(info, "bytecode_id", bytecodeId);
        JSONCheckedGet(info, "opcode_ord", trait.m_opcodeOrdinal);
        JSONCheckedGet(info, "num_fused_ic_variants", trait.m_numInterpreterFusedIcVariants);
        JSONCheckedGet(info, "jit_num_call_ic", trait.m_numJitCallIC);
        JSONCheckedGet(info, "jit_call_ic_base_ord", trait.m_jitCallIcTraitBaseOrdinal);
        JSONCheckedGet(info, "num_generic_ic_total_effect_kinds", trait.m_numGenericIcTotalEffectKinds);
        JSONCheckedGet(info, "generic_ic_effect_kind_base_ord", trait.m_genericIcEffectTraitBaseOrdinal);

        bytecodeOrder.push_back(bytecodeId);

        ReleaseAssert(!allTraits.count(bytecodeId));
        allTraits[bytecodeId] = trait;
    }

    ReleaseAssert(bytecodeOrder.size() == allTraits.size());

    DeegenGlobalBytecodeTraitAccessor r;
    r.m_bytecodeOrder = bytecodeOrder;
    r.m_traitSet = std::move(allTraits);
    return r;
}

}   // namespace dast
