#include "deegen_dfg_select_variant_logic_creator.h"
#include "anonymous_file.h"
#include "dfg_edge_use_kind.h"
#include "typemask_overapprox_automata_generator.h"

namespace dast {

static size_t WARN_UNUSED GetMaskOrdInSpeculationList(TypeMaskTy mask)
{
    for (size_t idx = 0; idx < x_list_of_type_speculation_masks.size(); idx++)
    {
        if (x_list_of_type_speculation_masks[idx] == mask)
        {
            return idx;
        }
    }
    fprintf(stderr, "Speculation mask %llu (%s) is used as a speculation, but it is not among the speculation mask list! "
                    "Please give it a name and add it to the list of masks in TypeSpecializationList.\n",
            static_cast<unsigned long long>(mask), DumpHumanReadableTypeSpeculation(mask).c_str());
    abort();
}

void GenerateSelectDfgVariantAndSetSpeculationLogic(std::vector<BytecodeVariantDefinition*> dfgVariants, FILE* hdrFp, FILE* cppFp)
{
    std::vector<size_t> ssaOperandOrds;
    std::vector<size_t> litOperandOrds;
    bool hasRangedOperand = false;
    ReleaseAssert(dfgVariants.size() > 0);
    for (size_t i = 0; i < dfgVariants[0]->m_list.size(); i++)
    {
        BcOperand* operand = dfgVariants[0]->m_list[i].get();
        // DFG variant should never have constant-typed operand
        //
        ReleaseAssert(operand->GetKind() != BcOperandKind::Constant);
        if (operand->GetKind() == BcOperandKind::Slot)
        {
            ssaOperandOrds.push_back(i);
        }
        if (operand->GetKind() == BcOperandKind::Literal || operand->GetKind() == BcOperandKind::SpecializedLiteral)
        {
            litOperandOrds.push_back(i);
        }
        if (operand->GetKind() == BcOperandKind::BytecodeRangeBase)
        {
            hasRangedOperand = true;
        }
    }

    // The map from each variant to the literal specialization configuration
    //
    std::unordered_map<BytecodeVariantDefinition*, std::vector<std::pair<bool /*isSpecialized*/, uint64_t>>> literalConfigMap;

    for (BytecodeVariantDefinition* bvd : dfgVariants)
    {
        std::vector<std::pair<bool /*isSpecialized*/, uint64_t>> litConfig;
        for (auto& operand : bvd->m_list)
        {
            if (operand->GetKind() == BcOperandKind::Literal)
            {
                litConfig.push_back(std::make_pair(false, 0));
            }
            else if (operand->GetKind() == BcOperandKind::SpecializedLiteral)
            {
                BcOpSpecializedLiteral* lit = assert_cast<BcOpSpecializedLiteral*>(operand.get());
                litConfig.push_back(std::make_pair(true, lit->m_concreteValue));
            }
        }
        ReleaseAssert(!literalConfigMap.count(bvd));
        literalConfigMap[bvd] = litConfig;
    }

    // The map from a list of type speculations to the list of variants for that list of type speculations
    //
    std::map<std::vector<TypeMaskTy>, std::vector<BytecodeVariantDefinition*>> variantMap;

    for (BytecodeVariantDefinition* bvd : dfgVariants)
    {
        std::vector<TypeMaskTy> specList;
        for (auto& operand : bvd->m_list)
        {
            if (operand->GetKind() == BcOperandKind::Slot)
            {
                BcOpSlot* slot = assert_cast<BcOpSlot*>(operand.get());
                if (slot->MaybeInvalidBoxedValue() && slot->HasDfgSpeculation())
                {
                    fprintf(stderr, "Cannot do speculation for operand that may not be a boxed value!\n");
                    abort();
                }
                specList.push_back(slot->HasDfgSpeculation() ? slot->GetDfgSpecMask() : x_typeMaskFor<tBoxedValueTop>);
            }
        }
        variantMap[specList].push_back(bvd);
    }

    // Return true if 'l1' is a superset of 'l2'
    //
    auto isSuperset = [&](const std::vector<TypeMaskTy>& l1, const std::vector<TypeMaskTy>& l2) WARN_UNUSED -> bool
    {
        ReleaseAssert(l1.size() == l2.size());
        for (size_t i = 0; i < l1.size(); i++)
        {
            if (!TypeMask(l1[i]).SupersetOf(l2[i]))
            {
                return false;
            }
        }
        return true;
    };

    // Return true if 'l1' is a superset of 'l2' and does not equal l2
    //
    auto isStrictSuperset = [&](const std::vector<TypeMaskTy>& l1, const std::vector<TypeMaskTy>& l2) WARN_UNUSED -> bool
    {
        ReleaseAssert(l1.size() == l2.size());
        if (l1 == l2)
        {
            return false;
        }
        return isSuperset(l1, l2);
    };

    // Assert that every literal configuration covered by 'bvd' is also covered by at least one variant in list
    //
    auto checkLiteralConfigCovered = [&](BytecodeVariantDefinition* bvd, const std::vector<BytecodeVariantDefinition*>& list) WARN_UNUSED -> bool
    {
        auto check = [](BytecodeVariantDefinition* b1, BytecodeVariantDefinition* b2) WARN_UNUSED -> bool
        {
            ReleaseAssert(b1->m_list.size() == b2->m_list.size());
            for (size_t i = 0; i < b1->m_list.size(); i++)
            {
                BcOperand* b1o = b1->m_list[i].get();
                BcOperand* b2o = b2->m_list[i].get();
                if (b1o->GetKind() == BcOperandKind::Literal || b1o->GetKind() == BcOperandKind::SpecializedLiteral)
                {
                    ReleaseAssert(b2o->GetKind() == BcOperandKind::Literal || b2o->GetKind() == BcOperandKind::SpecializedLiteral);
                    if (b1o->GetKind() == BcOperandKind::Literal)
                    {
                        if (b2o->GetKind() != BcOperandKind::Literal)
                        {
                            return false;
                        }
                    }
                    else
                    {
                        ReleaseAssert(b1o->GetKind() == BcOperandKind::SpecializedLiteral);
                        BcOpSpecializedLiteral* b1l = assert_cast<BcOpSpecializedLiteral*>(b1o);
                        if (b2o->GetKind() == BcOperandKind::SpecializedLiteral)
                        {
                            BcOpSpecializedLiteral* b2l = assert_cast<BcOpSpecializedLiteral*>(b2o);
                            if (b1l->m_concreteValue != b2l->m_concreteValue)
                            {
                                return false;
                            }
                        }
                    }
                }
            }
            return true;
        };
        for (BytecodeVariantDefinition* b : list)
        {
            if (check(bvd, b))
            {
                return true;
            }
        }
        return false;
    };

    // We require that any literal operand configuration that is available in a less type-specialized variant
    // must also be available in a more type specialized variant.
    // For example, if there is a variant where boxed value operand X is speculated to be tHeapEntity and literal operand Y has value 1,
    // then for any variant where boxed value operand X is speculated to be a subtype of tHeapEntity (e.g., tString),
    // there must be a variant that can handle Y=1.
    //
    // As a result of this requirement, we can always greedily choose the most specialized type speculation,
    // knowing that we will never need to "backtrack" to a less specialized type speculation due to that the literal configuration
    // cannot be supported by the variants available.
    //
    // Validate this requirement now.
    //
    for (auto& it1 : variantMap)
    {
        std::vector<TypeMaskTy> spec1 = it1.first;
        for (auto& it2 : variantMap)
        {
            std::vector<TypeMaskTy> spec2 = it2.first;

            if (isStrictSuperset(spec1, spec2))
            {
                // Every literal configuration covered by 'spec1' must also be covered by 'spec2'
                //
                for (BytecodeVariantDefinition* bvd : it1.second)
                {
                    if (!checkLiteralConfigCovered(bvd, it2.second))
                    {
                        auto getSpeculationStr = [&](const std::vector<TypeMaskTy>& l)
                        {
                            std::string r = "";
                            for (TypeMaskTy mask : l)
                            {
                                r += DumpHumanReadableTypeSpeculation(mask) + ",";
                            }
                            return r.substr(0, r.length() - 1);
                        };
                        fprintf(stderr, "The literal specialization in bytecode %s DFG variant %d (typespec: %s) "
                                        "cannot be covered by any variant with the strictly narrower typespec of '%s'!"
                                        "You should add a variant to cover this literal specialization.\n",
                                bvd->m_bytecodeName.c_str(),
                                static_cast<int>(bvd->m_variantOrd),
                                getSpeculationStr(spec1).c_str(),
                                getSpeculationStr(spec2).c_str());
                        abort();
                    }
                }
            }
        }
    }

    // We map each SSA operand to the least overapproximated specialization, then use a big array to map
    // the information vector of all the SSA and literal operands to the result
    //
    // TODO: in theory there can be cases where this caues a huge array, in which case we should
    // instead generate branchy C++ code directly.
    //
    std::vector<std::set<TypeMaskTy>> maskLists;
    maskLists.resize(ssaOperandOrds.size());
    for (auto& it : variantMap)
    {
        std::vector<TypeMaskTy> spec = it.first;
        ReleaseAssert(spec.size() == maskLists.size());
        for (size_t i = 0; i < spec.size(); i++)
        {
            maskLists[i].insert(spec[i]);
        }
    }

    std::vector<std::set<uint64_t>> literalSpecializations;
    literalSpecializations.resize(literalConfigMap[dfgVariants[0]].size());
    for (auto& it : literalConfigMap)
    {
        std::vector<std::pair<bool, uint64_t>>& cfg = it.second;
        ReleaseAssert(cfg.size() == literalSpecializations.size());
        for (size_t i = 0; i < cfg.size(); i++)
        {
            if (cfg[i].first)
            {
                literalSpecializations[i].insert(cfg[i].second);
            }
        }
    }

    ReleaseAssert(literalSpecializations.size() == litOperandOrds.size());

    size_t litSpecArrSize = 1;
    for (auto& it : literalSpecializations)
    {
        ReleaseAssert(litSpecArrSize <= 1000000000);
        litSpecArrSize *= (it.size() + 1);
    }

    if (litSpecArrSize * variantMap.size() > 2000)
    {
        fprintf(stderr, "[Lockdown] the variant selection logic caused a huge array, an alternate strategy needs to be implemented!\n");
        abort();
    }

    // If true, we know there's no literal specializations at all, so we don't need the indirection of the literal specialization array
    //
    bool noLiteralSpecializations = (litSpecArrSize == 1);

    // Print the array for the SSA type speculations
    //
    size_t tySpecArrSize = 1;
    for (auto& it : maskLists)
    {
        ReleaseAssert(tySpecArrSize <= 1000000000);
        tySpecArrSize *= it.size();
    }

    if (tySpecArrSize > 2000)
    {
        fprintf(stderr, "[Lockdown] the variant selection logic caused a huge array, an alternate strategy needs to be implemented!\n");
        abort();
    }

    ReleaseAssert(variantMap.size() < 255);

    std::vector<std::vector<TypeMaskTy>> typeSpecLists;
    for (auto& it : variantMap)
    {
        typeSpecLists.push_back(it.first);
    }

    // Currently we use 7 bits in dfg::Node to store the variant ordinal
    //
    ReleaseAssert(dfgVariants.size() < 127);

    std::string tySpecArrName = "x_deegen_dfg_variant_selection_" + dfgVariants[0]->m_bytecodeName + "_type_spec_map_arr";

    std::vector<std::vector<TypeMaskTy>> maskChoices;
    for (auto& item : maskLists)
    {
        std::vector<TypeMaskTy> masks;
        for (TypeMaskTy mask : item)
        {
            masks.push_back(mask);
        }
        maskChoices.push_back(masks);
    }

    AnonymousFile file;
    FILE* fp = file.GetFStream("w");

    fprintf(fp, "template<typename NodeInfoAccessorImpl>\n");
    // Intentionally no reference: nodeInfo is a trivial word-sized struct
    //
    fprintf(fp, "void deegen_dfg_variant_selection_generated_impl_%s(NodeInfoAccessorImpl nodeInfo)\n", dfgVariants[0]->m_bytecodeName.c_str());
    fprintf(fp, "{\n");

    {
        fprintf(hdrFp, "inline constexpr std::array<uint8_t, %d> %s = {\n    ", static_cast<int>(tySpecArrSize), tySpecArrName.c_str());

        std::vector<std::optional<uint8_t>> tySpecArrValues;
        tySpecArrValues.resize(tySpecArrSize, std::nullopt);

        std::function<void(const std::vector<size_t>&)> dfs = [&](const std::vector<size_t>& choices)
        {
            ReleaseAssert(choices.size() <= maskChoices.size());
            if (choices.size() == maskChoices.size())
            {
                size_t arrIdx = 0;
                std::vector<TypeMaskTy> masks;
                for (size_t i = 0; i < choices.size(); i++)
                {
                    ReleaseAssert(choices[i] < maskChoices[i].size());
                    arrIdx = arrIdx * maskChoices[i].size() + choices[i];
                    masks.push_back(maskChoices[i][choices[i]]);
                }
                ReleaseAssert(arrIdx < tySpecArrValues.size());

                size_t bestId = static_cast<size_t>(-1);
                for (size_t k = 0; k < typeSpecLists.size(); k++)
                {
                    if (isSuperset(typeSpecLists[k], masks))
                    {
                        if (bestId == static_cast<size_t>(-1))
                        {
                            bestId = k;
                        }
                        else if (isStrictSuperset(typeSpecLists[bestId], typeSpecLists[k]))
                        {
                            bestId = k;
                        }
                    }
                }
                uint8_t resVal;
                if (bestId == static_cast<size_t>(-1))
                {
                    resVal = 255;
                }
                else
                {
                    if (noLiteralSpecializations)
                    {
                        ReleaseAssert(variantMap.count(typeSpecLists[bestId]));
                        auto& vlist = variantMap[typeSpecLists[bestId]];
                        ReleaseAssert(vlist.size() == 1);
                        ReleaseAssert(vlist[0]->m_variantOrd < dfgVariants.size());
                        resVal = SafeIntegerCast<uint8_t>(vlist[0]->m_variantOrd);
                    }
                    else
                    {
                        ReleaseAssert(bestId < 255);
                        resVal = SafeIntegerCast<uint8_t>(bestId);
                    }
                }
                tySpecArrValues[arrIdx] = resVal;
            }
            else
            {
                size_t choiceIdx = choices.size();
                for (size_t i = 0; i < maskChoices[choiceIdx].size(); i++)
                {
                    std::vector<size_t> next = choices;
                    next.push_back(i);
                    dfs(next);
                }
            }
        };
        dfs(std::vector<size_t>{});

        ReleaseAssert(tySpecArrValues.size() == tySpecArrSize);
        for (size_t idx = 0; idx < tySpecArrValues.size(); idx++)
        {
            ReleaseAssert(tySpecArrValues[idx].has_value());
            fprintf(hdrFp, "%d", static_cast<int>(tySpecArrValues[idx].value()));
            if (idx + 1 < tySpecArrValues.size())
            {
                fprintf(hdrFp, ", ");
                if (idx % 8 == 7) { fprintf(hdrFp, "\n    "); }
            }
        }
        fprintf(hdrFp, "\n};\n\n");
    }

    // Print the logic that select the type speculations
    //
    BytecodeVariantDefinition::DfgNsdLayout nsdLayout = dfgVariants[0]->ComputeDfgNsdLayout();
    if (nsdLayout.m_nsdLength > 8)
    {
        fprintf(fp, "    [[maybe_unused]] uint8_t* nsd = nodeInfo.GetOutlinedNsd();\n");
    }
    else
    {
        fprintf(fp, "    [[maybe_unused]] uint8_t* nsd = nodeInfo.GetInlinedNsd();\n");
    }

    std::vector<bool> ssaOperandMayBeInvalidBoxedValue;
    for (size_t idx = 0; idx < ssaOperandOrds.size(); idx++)
    {
        if (!hasRangedOperand)
        {
            fprintf(fp, "    TypeMaskTy originalOpMask%d = nodeInfo.template GetPredictionForNodesWithFixedNumInputs<%d>(%d);\n",
                    static_cast<int>(idx), static_cast<int>(ssaOperandOrds.size()), static_cast<int>(idx));
        }
        else
        {
            fprintf(fp, "    TypeMaskTy originalOpMask%d = nodeInfo.GetPredictionForInput(%d);\n",
                    static_cast<int>(idx), static_cast<int>(idx));
        }
        BcOperand* op = dfgVariants[0]->m_list[ssaOperandOrds[idx]].get();
        ReleaseAssert(op->GetKind() == BcOperandKind::Slot);
        BcOpSlot* opSlot = assert_cast<BcOpSlot*>(op);
        ssaOperandMayBeInvalidBoxedValue.push_back(opSlot->MaybeInvalidBoxedValue());
        if (opSlot->MaybeInvalidBoxedValue())
        {
            fprintf(fp, "    TypeMaskTy opMask%d = originalOpMask%d;\n", static_cast<int>(idx), static_cast<int>(idx));
        }
        else
        {
            fprintf(fp, "    TypeMaskTy opMask%d = (originalOpMask%d & x_typeMaskFor<tBoxedValueTop>);\n", static_cast<int>(idx), static_cast<int>(idx));
        }
    }
    ReleaseAssert(ssaOperandMayBeInvalidBoxedValue.size() == ssaOperandOrds.size());
    ReleaseAssert(ssaOperandMayBeInvalidBoxedValue.size() == maskChoices.size());

    fprintf(fp, "    uint16_t typeSpecId = 0;\n");
    {
        ReleaseAssert(tySpecArrSize <= 65534);
        bool needLabel = false;
        for (size_t idx = 0; idx < maskChoices.size(); idx++)
        {
            size_t weight = 1;
            for (size_t i = idx + 1; i < maskChoices.size(); i++)
            {
                weight *= maskChoices[i].size();
            }

            if (needLabel)
            {
                fprintf(fp, "handle_operand_%d:\n", static_cast<int>(idx));
            }

            fprintf(fp, "    {\n");

            std::vector<TypeMaskTy> masks = maskChoices[idx];

            if (ssaOperandMayBeInvalidBoxedValue[idx])
            {
                ReleaseAssert(masks.size() == 1);
                ReleaseAssert(masks[0] == x_typeMaskFor<tBoxedValueTop>);
                fprintf(fp, "    typeSpecId += 0;\n");
                fprintf(fp, "    std::ignore = opMask%d;\n", static_cast<int>(idx));
                needLabel = false;
            }
            else
            {
                TypemaskOverapproxAutomataGenerator gen;
                for (size_t i = 0; i < masks.size(); i++)
                {
                    size_t value = weight * i;
                    ReleaseAssert(value <= 65534);
                    gen.AddItem(masks[i], static_cast<uint16_t>(value));
                }
                if (masks.size() > 3)
                {
                    // Use data-based automata implementation
                    //
                    std::vector<uint8_t> automata = gen.GenerateAutomata();
                    std::string varName = "x_deegen_dfg_variant_selection_" + dfgVariants[0]->m_bytecodeName + "_automata_" + std::to_string(idx);
                    PrintCppCodeToDefineUInt8Array(hdrFp, cppFp, automata, varName);

                    fprintf(fp, "    typeSpecId += TypeMaskOverapproxAutomata(%s).RunAutomata(opMask%d);\n",
                            varName.c_str(), static_cast<int>(idx));

                    needLabel = false;
                }
                else
                {
                    // Use direct C++ automata implementation
                    //
                    needLabel = true;
                    auto callback = [&](uint16_t resVal)
                    {
                        if (resVal == static_cast<uint16_t>(-1))
                        {
                            fprintf(fp, "    TestAssert(false); __builtin_unreachable();\n");
                        }
                        else
                        {
                            fprintf(fp, "    typeSpecId += %d;\n", static_cast<uint16_t>(resVal));
                            fprintf(fp, "    goto handle_operand_%d;\n", static_cast<int>(idx + 1));
                        }
                    };
                    gen.GenerateAutomataWithCppGoto(
                        fp,
                        "handle_operand_dfa_" + std::to_string(idx) + "_" /*labelPrefix*/,
                        "opMask" + std::to_string(idx) /*varName*/,
                        callback);
                }
            }
            fprintf(fp, "    }\n");
        }
        if (needLabel)
        {
            fprintf(fp, "handle_operand_%d:\n", static_cast<int>(maskChoices.size()));
        }
    }

    fprintf(fp, "    TestAssert(typeSpecId < %d);\n", static_cast<int>(tySpecArrSize));

    if (noLiteralSpecializations)
    {
        fprintf(fp, "    uint8_t variantOrd = %s[typeSpecId];\n", tySpecArrName.c_str());
    }
    else
    {
        fprintf(fp, "    uint8_t tsIdx = %s[typeSpecId];\n", tySpecArrName.c_str());
        fprintf(fp, "    TestAssert(tsIdx != 255);\n");

        fprintf(fp, "    size_t litIdx = 0;\n");

        std::vector<std::vector<uint64_t>> litChoices;
        for (auto& item : literalSpecializations)
        {
            std::vector<uint64_t> v;
            for (uint64_t val : item) { v.push_back(val); }
            litChoices.push_back(v);
        }

        for (size_t idx = 0; idx < litChoices.size(); idx++)
        {
            if (litChoices[idx].size() > 0)
            {
                size_t operandOrd = litOperandOrds[idx];
                ReleaseAssert(nsdLayout.m_operandOffsets.count(operandOrd));
                size_t offsetInNsd = nsdLayout.m_operandOffsets[operandOrd];
                BcOperand* operand = dfgVariants[0]->m_list[operandOrd].get();
                ReleaseAssert(operand->GetKind() == BcOperandKind::Literal || operand->GetKind() == BcOperandKind::SpecializedLiteral);
                BcOpLiteral* lit = assert_cast<BcOpLiteral*>(operand);
                fprintf(fp, "    uint64_t litval%d = static_cast<uint64_t>(static_cast<%sint64_t>(UnalignedLoad<%sint%d_t>(nsd + %d)));\n",
                        static_cast<int>(idx),
                        (lit->IsSignedValue() ? "" : "u"),
                        (lit->IsSignedValue() ? "" : "u"),
                        static_cast<int>(lit->m_numBytes * 8),
                        static_cast<int>(offsetInNsd));

                size_t weight = 1;
                for (size_t i = idx + 1; i < litChoices.size(); i++)
                {
                    weight *= (litChoices[i].size() + 1);
                }

                std::vector<uint64_t> elements = litChoices[idx];

                // Detect fastpath that litChoices[idx] is an arithmetic progression with power-of-two steps
                // LLVM cannot reliably detect this and rewrite the switch, so do it ourselves.
                //
                bool isArithmeticProgression = (elements.size() >= 3);
                for (size_t i = 2; i < elements.size(); i++)
                {
                    if (elements[i] - elements[i - 1] != elements[1] - elements[0])
                    {
                        isArithmeticProgression = false;
                        break;
                    }
                }
                if (isArithmeticProgression && is_power_of_2(elements[1] - elements[0]))
                {
                    size_t diff = elements[1] - elements[0];
                    fprintf(fp, "    {\n");
                    fprintf(fp, "    uint64_t seqStart = %llu, seqEnd = %llu, seqStep = %llu;\n",
                            static_cast<unsigned long long>(elements[0]),
                            static_cast<unsigned long long>(elements.back()),
                            static_cast<unsigned long long>(diff));
                    fprintf(fp, "    if (seqStart <= litval%d && litval%d <= seqEnd && (litval%d - seqStart) %% seqStep == 0) {\n",
                            static_cast<int>(idx),
                            static_cast<int>(idx),
                            static_cast<int>(idx));
                    fprintf(fp, "        size_t seqOrd = (litval%d - seqStart) / seqStep;\n",
                            static_cast<int>(idx));
                    fprintf(fp, "        litIdx += (seqOrd + 1) * %d;\n", static_cast<int>(weight));
                    fprintf(fp, "    }\n");
                    fprintf(fp, "    }\n");
                }
                else
                {
                    fprintf(fp, "    switch (litval%d) {\n", static_cast<int>(idx));
                    size_t curW = 0;
                    for (uint64_t specializedVal : elements)
                    {
                        curW += weight;
                        fprintf(fp, "    case %llu: { litIdx += %d; break; }\n",
                                static_cast<unsigned long long>(specializedVal),
                                static_cast<int>(curW));
                    }
                    fprintf(fp, "    default: { break; }\n");
                    fprintf(fp, "    } /*switch*/\n");
                }
            }
        }

        std::string varName = "x_deegen_dfg_variant_selection_" + dfgVariants[0]->m_bytecodeName + "_variant_map_arr";
        fprintf(hdrFp, "inline constexpr std::array<uint8_t, %d> %s = {\n    ",
                static_cast<int>(litSpecArrSize * variantMap.size()),
                varName.c_str());

        bool isFirst = true;
        for (auto& item : variantMap)
        {
            if (!isFirst) { fprintf(hdrFp, ",\n    "); }
            isFirst = false;

            std::vector<BytecodeVariantDefinition*> variants = item.second;
            for (size_t k = 0; k < litSpecArrSize; k++)
            {
                std::vector<bool> isSpecialized;
                isSpecialized.resize(litChoices.size());
                std::vector<uint64_t> specializedVal;
                specializedVal.resize(litChoices.size());

                size_t tmp = k;
                for (size_t idx = litChoices.size(); idx--;)
                {
                    size_t x = tmp % (litChoices[idx].size() + 1);
                    tmp /= (litChoices[idx].size() + 1);
                    if (x == 0)
                    {
                        isSpecialized[idx] = false;
                        specializedVal[idx] = 0;
                    }
                    else
                    {
                        isSpecialized[idx] = true;
                        specializedVal[idx] = litChoices[idx][x - 1];
                    }
                }
                ReleaseAssert(tmp == 0);

                BytecodeVariantDefinition* bestMatch = nullptr;
                size_t numSpecializedValsInBestMatch = 0;
                for (BytecodeVariantDefinition* bvd : variants)
                {
                    ReleaseAssert(literalConfigMap.count(bvd));
                    std::vector<std::pair<bool, uint64_t>>& specInfo = literalConfigMap[bvd];
                    ReleaseAssert(specInfo.size() == litChoices.size());
                    bool match = true;
                    size_t numSpecializedVals = 0;
                    for (size_t i = 0; i < litChoices.size(); i++)
                    {
                        if (!isSpecialized[i])
                        {
                            if (specInfo[i].first)
                            {
                                match = false;
                                break;
                            }
                        }
                        else
                        {
                            if (specInfo[i].first && specializedVal[i] != specInfo[i].second)
                            {
                                match = false;
                                break;
                            }
                            if (specInfo[i].first)
                            {
                                numSpecializedVals++;
                            }
                        }
                    }
                    if (match)
                    {
                        if (bestMatch == nullptr || numSpecializedVals > numSpecializedValsInBestMatch)
                        {
                            bestMatch = bvd;
                            numSpecializedValsInBestMatch = numSpecializedVals;
                        }
                    }
                }

                uint8_t resVal = 255;
                if (bestMatch != nullptr)
                {
                    ReleaseAssert(bestMatch->m_variantOrd < dfgVariants.size());
                    resVal = SafeIntegerCast<uint8_t>(bestMatch->m_variantOrd);
                }

                fprintf(hdrFp, "%d", static_cast<int>(resVal));
                if (k + 1 < litSpecArrSize)
                {
                    fprintf(hdrFp, ", ");
                    if (k % 8 == 7) { fprintf(hdrFp, "\n    "); }
                }
            }
        }
        fprintf(hdrFp, "\n};\n\n");

        fprintf(fp, "    size_t entryIdx = static_cast<size_t>(tsIdx) * %d + litIdx;\n", static_cast<int>(litSpecArrSize));
        fprintf(fp, "    TestAssert(entryIdx < %s.size());\n", varName.c_str());
        fprintf(fp, "    uint8_t variantOrd = %s[entryIdx];\n", varName.c_str());
    }

    fprintf(fp, "    TestAssert(variantOrd != 255);\n");
    fprintf(fp, "    TestAssert(variantOrd < %d);\n", static_cast<int>(dfgVariants.size()));

    // Set up the variant ordinal
    //
    fprintf(fp, "    nodeInfo.SetVariantOrd(variantOrd);\n");

    // Set up the speculation type for each input edge.
    //
    // If the SSA operand may be invalid boxed value, the use kind should simply be UseKind_FullTop.
    //
    // If the SSA operand is always a valid boxed value, we can speculate as long as our speculation satisfies that:
    //     1. Our speculation must be a superset of the prediction (so we will not OSR exit on types we know we might see)
    //     2. Our speculation must be a subset of the speculation used by the chosen variant (required for correctness)
    //
    // That is, any specMask such that predictionMask \subset specMask \subset nodeSpecMask will work.
    // But they will have different performance characteristics.
    // It is unclear to me what is the best approach here. I can think of two approaches:
    //     1. Always choose the widest mask (that is, simply nodeSpecMask)
    //     2. Choose the cheapest mask to check that satisfies the requirement.
    //
    // In most cases the two criterions agree, since in most cases, a wider mask is also cheaper to check.
    //
    // The advantage of (1) is that a wider check is more likely to be proven and eliminated completely.
    // The advantage of (2) is that the check itself is cheaper, and choosing a narrower mask can perhaps give
    // a better proof, which ends up the check being eliminated as well or even eliminate more checks.
    // But if we choose a narrower mask and only ends up proving nodeSpecMask, we would be doing worse than (1).
    //
    // The main case where this matters is that it is cheaper to check for tDoubleNotNaN than tDouble,
    // and also avoids an expensive GPR->FPR move later.
    //
    // So for now, we use the following middle-ground solution:
    //     1. We will always choose the widest mask (that is, simply nodeSpecMask).
    //     2. Nevertheless, if predictionMask is tDoubleNotNaN, we will record this fact using a special bit in the edge.
    // If we end up being unable to prove the check, we can inspect this bit and turn the check to a tDoubleNotNaN check
    // (which is always correct since we are making the check stronger).
    //
    for (size_t idx = 0; idx < ssaOperandOrds.size(); idx++)
    {
        fprintf(fp, "    {\n");
        BcOperand* operand = dfgVariants[0]->m_list[ssaOperandOrds[idx]].get();
        ReleaseAssert(operand->GetKind() == BcOperandKind::Slot);
        BcOpSlot* opSlot = assert_cast<BcOpSlot*>(operand);

        std::map<size_t, size_t> m;
        for (auto& item : variantMap)
        {
            ReleaseAssert(idx < item.first.size());
            TypeMaskTy mask = item.first[idx];
            size_t maskOrd = GetMaskOrdInSpeculationList(mask);

            for (BytecodeVariantDefinition* bvd : item.second)
            {
                ReleaseAssert(!m.count(bvd->m_variantOrd));
                m[bvd->m_variantOrd] = maskOrd;
            }
        }

        std::vector<size_t> v;
        ReleaseAssert(m.size() == dfgVariants.size());
        for (size_t i = 0; i < dfgVariants.size(); i++)
        {
            ReleaseAssert(m.count(i));
            v.push_back(m[i]);
        }

        bool isAllSame = true;
        for (size_t i = 1; i < v.size(); i++)
        {
            if (v[i] != v[0])
            {
                isAllSame = false;
            }
        }

        auto getUseKindFromCheckMaskOrd = [&](size_t checkMaskOrd) WARN_UNUSED -> dfg::UseKind
        {
            ReleaseAssert(checkMaskOrd < x_list_of_type_speculation_masks.size());
            TypeMask checkMask = x_list_of_type_speculation_masks[checkMaskOrd];

            // The 'checkMask' here is the checkMask of a DFG variant, which never makes sense to be empty
            //
            ReleaseAssert(!checkMask.Empty());

            if (checkMask == x_typeMaskFor<tBoxedValueTop>)
            {
                return dfg::UseKind_Untyped;
            }
            else
            {
                ReleaseAssert(0 < checkMask.m_mask && checkMask.m_mask < x_typeMaskFor<tBoxedValueTop>);
                ReleaseAssert(1 <= checkMaskOrd && checkMaskOrd + 1 < x_list_of_type_speculation_masks.size());
                size_t val = dfg::UseKind_FirstUnprovenUseKind + 2 * checkMaskOrd - 2;
                ReleaseAssert(val < 65535);
                return static_cast<dfg::UseKind>(val);
            }
        };

        if (!opSlot->MaybeInvalidBoxedValue())
        {
            if (!isAllSame)
            {
                std::string varName = "x_deegen_dfg_variant_selection_" + dfgVariants[0]->m_bytecodeName + "_variant_spec_info_" + std::to_string(idx);
                fprintf(hdrFp, "inline constexpr std::array<UseKind, %d> %s = {\n    ",
                        static_cast<int>(dfgVariants.size()), varName.c_str());
                for (size_t i = 0; i < v.size(); i++)
                {
                    dfg::UseKind useKind = getUseKindFromCheckMaskOrd(v[i]);
                    fprintf(hdrFp, "static_cast<UseKind>(%d)", static_cast<int>(useKind));
                    if (i + 1 < v.size())
                    {
                        fprintf(hdrFp, ", ");
                        if (i % 8 == 7)
                        {
                            fprintf(hdrFp, "\n    ");
                        }
                    }
                }
                fprintf(hdrFp, "\n};\n");

                fprintf(fp, "    UseKind useKind = %s[variantOrd];\n", varName.c_str());
            }
            else
            {
                dfg::UseKind useKind = getUseKindFromCheckMaskOrd(v[0]);
                fprintf(fp, "    UseKind useKind = static_cast<UseKind>(%d);\n", static_cast<int>(useKind));
            }

            // Assign use kind: if 'opMask' is tBottom, assign UseKind_Unreachable
            // Otherwise assign 'useKind'
            //
            fprintf(fp, "    if (opMask%d == 0) { useKind = UseKind_Unreachable; }\n", static_cast<int>(idx));

            fprintf(fp, "    bool predictionIsDoubleNotNaN = (opMask%d != 0) && ((x_typeMaskFor<tDoubleNotNaN> & opMask%d) == opMask%d);\n",
                    static_cast<int>(idx), static_cast<int>(idx), static_cast<int>(idx));
        }
        else
        {
            ReleaseAssert(isAllSame);
            ReleaseAssert(v[0] == GetMaskOrdInSpeculationList(x_typeMaskFor<tBoxedValueTop>));

            // Assign use kind: if 'originalOpMask' is tBottom, assign UseKind_Unreachable
            // Otherwise assign UseKind_FullTop
            //
            fprintf(fp, "    UseKind useKind = ((originalOpMask%d == 0) ? UseKind_Unreachable : UseKind_FullTop);\n", static_cast<int>(idx));
            fprintf(fp, "    bool predictionIsDoubleNotNaN = false;\n");
        }
        if (!hasRangedOperand)
        {
            fprintf(fp, "    nodeInfo.template AssignUseKindForNodeWithFixedNumInputs<%d>(%d, useKind, predictionIsDoubleNotNaN);\n",
                    static_cast<int>(ssaOperandOrds.size()), static_cast<int>(idx));
        }
        else
        {
            fprintf(fp, "    nodeInfo.AssignUseKind(%d, useKind, predictionIsDoubleNotNaN);\n", static_cast<int>(idx));
        }
        fprintf(fp, "    }\n");
    }

    // In test build, assert that the selected variant meets all assumptions
    //
    fprintf(fp, "#ifdef TESTBUILD\n");
    fprintf(fp, "    switch (variantOrd) {\n");
    for (BytecodeVariantDefinition* bvd : dfgVariants)
    {
        fprintf(fp, "    case %d: {\n", static_cast<int>(bvd->m_variantOrd));
        size_t edgeOrd = 0;
        for (size_t idx = 0; idx < bvd->m_list.size(); idx++)
        {
            BcOperand* operand = bvd->m_list[idx].get();
            if (operand->GetKind() == BcOperandKind::Slot)
            {
                BcOpSlot* slot = assert_cast<BcOpSlot*>(operand);
                if (!slot->MaybeInvalidBoxedValue())
                {
                    TypeMaskTy specMask = slot->HasDfgSpeculation() ? slot->GetDfgSpecMask() : x_typeMaskFor<tBoxedValueTop>;
                    fprintf(fp, "    nodeInfo.AssertEdgeFulfillsMask(%d, opMask%d, static_cast<TypeMaskTy>(%llu));\n",
                            static_cast<int>(edgeOrd),
                            static_cast<int>(edgeOrd),
                            static_cast<unsigned long long>(specMask));
                }
                else
                {
                    ReleaseAssert(!slot->HasDfgSpeculation());
                    fprintf(fp, "    nodeInfo.AssertEdgeFulfillsMask(%d, originalOpMask%d, x_typeMaskFor<tFullTop>);\n",
                            static_cast<int>(edgeOrd),
                            static_cast<int>(edgeOrd));
                }
                edgeOrd++;
            }
            else if (operand->GetKind() == BcOperandKind::SpecializedLiteral)
            {
                BcOpSpecializedLiteral* lit = assert_cast<BcOpSpecializedLiteral*>(operand);
                ReleaseAssert(nsdLayout.m_operandOffsets.count(idx));
                size_t offsetInNsd = nsdLayout.m_operandOffsets[idx];
                fprintf(fp, "    TestAssert(static_cast<uint64_t>(static_cast<%sint64_t>(UnalignedLoad<%sint%d_t>(nsd + %d))) == %llu);\n",
                        (lit->IsSignedValue() ? "" : "u"),
                        (lit->IsSignedValue() ? "" : "u"),
                        static_cast<int>(lit->m_numBytes * 8),
                        static_cast<int>(offsetInNsd),
                        static_cast<unsigned long long>(lit->m_concreteValue));
            }
        }
        ReleaseAssert(edgeOrd == ssaOperandOrds.size());
        fprintf(fp, "    break;\n");
        fprintf(fp, "    }\n");
    }
    fprintf(fp, "    default: { TestAssert(false); }\n");
    fprintf(fp, "    } /*switch*/\n");
    fprintf(fp, "#endif\n");

    fprintf(fp, "}\n");

    fclose(fp);
    fprintf(hdrFp, "%s\n", file.GetFileContents().c_str());
}

void GenerateGetSpeculationFromPredictionMaskAutomata(FILE* hdrFp, FILE* cppFp)
{
    TypemaskOverapproxAutomataGenerator gen;
    for (size_t i = 0; i < x_list_of_type_speculation_masks.size(); i++)
    {
        gen.AddItem(x_list_of_type_speculation_masks[i], SafeIntegerCast<uint16_t>(i));
    }
    size_t automataDepth = 0;
    std::vector<uint8_t> automata = gen.GenerateAutomata(&automataDepth);
    fprintf(hdrFp, "// Conservative automata max depth: %d\n", static_cast<int>(automataDepth));
    PrintCppCodeToDefineUInt8Array(hdrFp, cppFp, automata, "x_deegen_dfg_find_minimal_speculation_covering_prediction_mask_automata");
}

void GenerateFindCheapestSpecWithinMaskCoveringExistingSpecAutomatas(std::vector<TypecheckStrengthReductionCandidate> tcInfoList, FILE* hdrFp, FILE* cppFp)
{
    ReleaseAssert(tcInfoList.size() >= x_list_of_type_speculation_masks.size() - 2);
    for (size_t idx = 0; idx < x_list_of_type_speculation_masks.size() - 2; idx++)
    {
        ReleaseAssert(tcInfoList[idx].m_precondMask == x_typeMaskFor<tBoxedValueTop>);
        ReleaseAssert(tcInfoList[idx].m_checkedMask == x_list_of_type_speculation_masks[idx + 1]);
    }

    for (size_t idx = 0; idx < x_list_of_type_speculation_masks.size(); idx++)
    {
        TypeMask baseMask = x_list_of_type_speculation_masks[idx];

        MinCostTypemaskOverapproxAutomataGenerator mcaGen;

        if (baseMask.Empty())
        {
            mcaGen.AddItem(
                x_typeMaskFor<tBoxedValueTop>,
                dfg::UseKind_Unreachable,
                0 /*cost*/);
        }

        if (baseMask.SubsetOf(x_typeMaskFor<tBoxedValueTop>))
        {
            mcaGen.AddItem(
                x_typeMaskFor<tBottom>,
                dfg::UseKind_Untyped,
                1 /*cost*/);
        }

        for (size_t k = 0; k < x_list_of_type_speculation_masks.size() - 2; k++)
        {
            TypeMask checkMask = tcInfoList[k].m_checkedMask;
            size_t checkCost = tcInfoList[k].m_estimatedCost + 2;
            if (baseMask.SubsetOf(checkMask))
            {
                size_t val = static_cast<size_t>(dfg::UseKind_FirstUnprovenUseKind) + 2 * k;
                mcaGen.AddItem(
                    x_typeMaskFor<tBoxedValueTop> ^ checkMask.m_mask,
                    SafeIntegerCast<uint16_t>(val),
                    checkCost);
            }
        }

        TypemaskOverapproxAutomataGenerator gen = mcaGen.GetAutomata();

        std::string varName = "x_deegen_dfg_find_cheapest_spec_within_mask_" + std::to_string(idx);

        size_t automataDepth = 0;
        std::vector<uint8_t> automata = gen.GenerateAutomata(&automataDepth);
        fprintf(hdrFp, "// Conservative automata max depth: %d\n", static_cast<int>(automataDepth));
        PrintCppCodeToDefineUInt8Array(hdrFp, cppFp, automata, varName);
    }

    fprintf(hdrFp, "inline constexpr std::array<const uint8_t*, %d> x_deegen_dfg_find_cheapest_spec_within_mask_automatas = {\n",
            static_cast<int>(x_list_of_type_speculation_masks.size()));

    for (size_t idx = 0; idx < x_list_of_type_speculation_masks.size(); idx++)
    {
        std::string varName = "x_deegen_dfg_find_cheapest_spec_within_mask_" + std::to_string(idx);
        fprintf(hdrFp, "    %s", varName.c_str());
        if (idx + 1 < x_list_of_type_speculation_masks.size()) { fprintf(hdrFp, ",\n"); }
    }
    fprintf(hdrFp, "\n};\n");
}

}   // namespace dast
