#include "gtest/gtest.h"

#include "misc_llvm_helper.h"
#include "typemask_overapprox_automata_generator.h"
#include "drt/dfg_variant_trait_table.h"
#include "tvalue_typecheck_optimization.h"

using namespace dast;
using namespace dfg;

namespace {

void TestOneCaseImpl(std::mt19937_64& rdgen, std::vector<TypeMaskTy> items, TypeMaskTy rmask)
{
    {
        std::unordered_set<TypeMaskTy> chk;
        for (TypeMaskTy mask : items)
        {
            ReleaseAssert(!chk.count(mask));
            chk.insert(mask);
        }
    }

    std::unordered_set<uint16_t> values;
    while (values.size() < items.size())
    {
        values.insert(static_cast<uint16_t>(rdgen() % 65535));
    }

    std::vector<std::pair<TypeMaskTy, uint16_t>> data;
    {
        auto it = values.begin();
        for (auto mask : items)
        {
            data.push_back(std::make_pair(mask, *it));
            it++;
        }
    }

    TypemaskOverapproxAutomataGenerator gen;
    for (auto& it : data)
    {
        gen.AddItem(it.first, it.second);
    }

    rmask &= x_typeMaskFor<tBoxedValueTop>;

    std::vector<TypeMaskTy> inputs;
    inputs.push_back(0);
    {
        TypeMaskTy x = rmask;
        while (x)
        {
            inputs.push_back(x);
            x = (x - 1) & rmask;
        }
    }

    for (size_t i = 0; i < 200; i++)
    {
        inputs.push_back(static_cast<TypeMaskTy>(rdgen() % (x_typeMaskFor<tBoxedValueTop> + 1)));
    }
    inputs.push_back(x_typeMaskFor<tBoxedValueTop>);

    std::vector<uint8_t> v1 = gen.GenerateAutomata();
    TypeMaskOverapproxAutomata a1(v1.data());

    std::vector<uint8_t> v2 = gen.GenerateAutomataLeafOpted();
    TypeMaskOverapproxAutomataLeafOpted a2(v2.data());

    auto validateAnswer = [&](TypeMaskTy input, uint16_t answer)
    {
        if (answer == static_cast<uint16_t>(-1))
        {
            for (TypeMaskTy mask : items)
            {
                ReleaseAssert((mask & input) != input);
            }
        }
        else
        {
            bool found = false;
            TypeMaskTy ansMask = 0;
            for (auto& it : data)
            {
                if (it.second == answer)
                {
                    ReleaseAssert(!found);
                    found = true;
                    ansMask = it.first;
                }
            }
            ReleaseAssert(found);
            ReleaseAssert((ansMask & input) == input);
            for (auto& it : data)
            {
                if ((it.first & input) == input)
                {
                    ReleaseAssert(it.first == ansMask || ((ansMask & it.first) != it.first));
                }
            }
        }
    };

    for (TypeMaskTy input : inputs)
    {
        validateAnswer(input, a1.RunAutomataMayFail(input));
        validateAnswer(input, a2.RunAutomataMayFail(input));
    }
}

void TestOneCase(std::mt19937_64& rdgen, std::vector<TypeMaskTy> items, TypeMaskTy rmask)
{
    TestOneCaseImpl(rdgen, items, rmask);

    std::unordered_set<TypeMaskTy> s;
    for (TypeMaskTy mask : items) { s.insert(mask); }
    while (true)
    {
        std::unordered_set<TypeMaskTy> newSet;
        for (TypeMaskTy v1 : s)
        {
            for (TypeMaskTy v2 : s)
            {
                newSet.insert(v1 & v2);
            }
        }

        bool changed = false;
        for (TypeMaskTy v : newSet)
        {
            if (!s.count(v))
            {
                s.insert(v);
                changed = true;
            }
        }
        if (!changed) break;
    }

    std::vector<TypeMaskTy> lis;
    for (TypeMaskTy mask : s) { lis.push_back(mask); }
    TestOneCaseImpl(rdgen, lis, rmask);
}

TypeMaskTy GetRandomTopMask(std::mt19937_64& rdgen, uint64_t numBits)
{
    if (x_numUsefulBitsInBytecodeTypeMask < numBits)
    {
        numBits = x_numUsefulBitsInBytecodeTypeMask;
    }

    std::unordered_set<size_t> vals;
    while (vals.size() < numBits)
    {
        vals.insert(rdgen() % x_numUsefulBitsInBytecodeTypeMask);
    }

    TypeMaskTy res = 0;
    for (size_t k : vals)
    {
        res |= static_cast<TypeMaskTy>(1) << k;
    }
    return res;
}

TypeMaskTy GetRandomBottomMask(std::mt19937_64& rdgen, TypeMaskTy topMask)
{
    TypeMaskTy val = static_cast<TypeMaskTy>(rdgen() % (x_typeMaskFor<tBoxedValueTop> + 1));
    return val & topMask;
}

void DoTest(std::mt19937_64& rdgen, TypeMaskTy topMask, TypeMaskTy bottomMask, size_t numElements)
{
    ReleaseAssert((topMask & bottomMask) == bottomMask);
    size_t maxNumElements = static_cast<uint64_t>(1) << (static_cast<size_t>(__builtin_popcountll(topMask ^ bottomMask)));
    numElements = std::min(numElements, maxNumElements);

    TypeMaskTy andMask = topMask ^ bottomMask;
    std::unordered_set<TypeMaskTy> vals;
    while (vals.size() < numElements)
    {
        TypeMaskTy mask = static_cast<TypeMaskTy>(rdgen() % (x_typeMaskFor<tBoxedValueTop> + 1));
        mask &= andMask;
        mask |= bottomMask;
        vals.insert(mask);
    }

    std::vector<TypeMaskTy> masks;
    for (TypeMaskTy x : vals)
    {
        ReleaseAssert((x & bottomMask) == bottomMask);
        ReleaseAssert((topMask & x) == x);
        masks.push_back(x);
    }
    ReleaseAssert(masks.size() == numElements);

    TestOneCase(rdgen, masks, topMask);
}

void DoTest2(std::mt19937_64& rdgen, size_t numElements)
{
    size_t maxNumElements = x_typeMaskFor<tBoxedValueTop> + 1;
    numElements = std::min(numElements, maxNumElements);

    std::unordered_set<TypeMaskTy> vals;
    if (rdgen() % 2 == 0)
    {
        while (vals.size() < numElements)
        {
            TypeMaskTy mask = static_cast<TypeMaskTy>(rdgen() % (x_typeMaskFor<tBoxedValueTop> + 1));
            vals.insert(mask);
        }
    }
    else
    {
        while (vals.size() < numElements)
        {
            TypeMaskTy mask = GetRandomTopMask(rdgen, rdgen() % 4);
            vals.insert(mask);
        }
    }

    std::vector<TypeMaskTy> masks;
    for (TypeMaskTy x : vals)
    {
        masks.push_back(x);
    }
    ReleaseAssert(masks.size() == numElements);

    TestOneCase(rdgen, masks, GetRandomTopMask(rdgen, 8));
}

}   // anonymous namespace

TEST(DfgTypemaskAutomataGen, Sanity)
{
    std::random_device rd;
    std::mt19937_64 rdgen(rd());

    // Test some edge cases first
    //
    TestOneCase(rdgen, { }, 255);
    TestOneCase(rdgen, { 0 }, 255);
    TestOneCase(rdgen, { 1 }, 255);
    TestOneCase(rdgen, { 0, 1 }, 255);
    TestOneCase(rdgen, { 0, x_typeMaskFor<tBoxedValueTop> }, 255);
    TestOneCase(rdgen, { 1, x_typeMaskFor<tBoxedValueTop> }, 255);
    TestOneCase(rdgen, { x_typeMaskFor<tBoxedValueTop> }, 255);

    DoTest(rdgen, 255, 0, 5);
    DoTest(rdgen, 255, 0, 10);
    DoTest(rdgen, 255, 0, 20);
    DoTest(rdgen, 255, 0, 50);
    DoTest(rdgen, 255, 0, 128);
    DoTest(rdgen, 255, 0, 230);
    DoTest(rdgen, 255, 0, 240);
    DoTest(rdgen, 255, 0, 255);

    for (size_t count = 0; count < 50; count++)
    {
        size_t maxBits = x_numUsefulBitsInBytecodeTypeMask;
        if (maxBits > 8) { maxBits = 8; }
        size_t numBits = rdgen() % maxBits + 1;

        TypeMaskTy topMask = GetRandomTopMask(rdgen, numBits);
        TypeMaskTy bottomMask = GetRandomBottomMask(rdgen, topMask);

        {
            size_t maxNumElements = static_cast<uint64_t>(1) << (static_cast<size_t>(__builtin_popcountll(topMask ^ bottomMask)));
            size_t numElements;
            if (rdgen() % 2 == 0)
            {
                numElements = rdgen() % 15 + 1;
            }
            else
            {
                numElements = rdgen() % maxNumElements + 1;
            }
            DoTest(rdgen, topMask, bottomMask, numElements);
        }

        {
            size_t maxNumElements = static_cast<uint64_t>(1) << numBits;
            size_t numElements;
            if (rdgen() % 2 == 0)
            {
                numElements = rdgen() % 15 + 1;
            }
            else
            {
                numElements = rdgen() % maxNumElements + 1;
            }
            DoTest(rdgen, topMask, 0, numElements);
        }
    }

    for (size_t count = 0; count < 30; count++)
    {
        for (size_t numElements = 0; numElements <= 8; numElements++)
        {
            DoTest2(rdgen, numElements);
        }
    }
}

TEST(DfgTypemaskAutomataGen, UseKindSolver)
{
    std::random_device rd;
    std::mt19937_64 rdgen(rd());

    TypeCheckFunctionSelector gold(x_dfg_typecheck_impl_info_list.data(), x_dfg_typecheck_impl_info_list.size());

    for (size_t idx = 0; idx < x_list_of_type_speculation_masks.size(); idx++)
    {
        std::vector<TypeMaskTy> testcases;
        for (size_t k = 0; k < 1500; k++)
        {
            testcases.push_back(rdgen() % (x_typeMaskFor<tBoxedValueTop> + 1));
        }
        testcases.push_back(x_typeMaskFor<tBottom>);
        testcases.push_back(x_typeMaskFor<tBoxedValueTop>);

        for (TypeMaskTy mask : testcases)
        {
            UseKind res = GetEdgeUseKindFromCheckAndPrecondition(static_cast<TypeMaskOrd>(idx), mask);
            TypeCheckFunctionSelector::QueryResult goldRes = gold.Query(x_list_of_type_speculation_masks[idx], mask);

            ReleaseAssert(goldRes.m_opKind != TypeCheckFunctionSelector::QueryResult::NoSolutionFound);
            if (mask == x_typeMaskFor<tBottom>)
            {
                ReleaseAssert(res == dfg::UseKind_Unreachable);
            }
            else if (goldRes.m_opKind == TypeCheckFunctionSelector::QueryResult::TriviallyTrue)
            {
                if (x_list_of_type_speculation_masks[idx] == x_typeMaskFor<tBoxedValueTop>)
                {
                    ReleaseAssert(res == dfg::UseKind_Untyped);
                }
                else
                {
                    ReleaseAssert(res == dfg::UseKind_FirstProvenUseKind + idx - 1);
                }
            }
            else if (goldRes.m_opKind == TypeCheckFunctionSelector::QueryResult::TriviallyFalse)
            {
                ReleaseAssert(res == dfg::UseKind_AlwaysOsrExit);
            }
            else
            {
                ReleaseAssert(goldRes.m_opKind == TypeCheckFunctionSelector::QueryResult::CallFunction ||
                              goldRes.m_opKind == TypeCheckFunctionSelector::QueryResult::CallFunctionAndFlipResult);
                ReleaseAssert(res >= dfg::UseKind_FirstUnprovenUseKind);
                size_t ord = res - dfg::UseKind_FirstUnprovenUseKind;
                size_t fnOrd = ord / 2;
                ReleaseAssert(fnOrd < x_dfg_typecheck_impl_info_list.size());
                ReleaseAssert(x_dfg_typecheck_impl_info_list[fnOrd].m_checkMask == goldRes.m_info->m_checkedMask);
                ReleaseAssert(x_dfg_typecheck_impl_info_list[fnOrd].m_precondMask == goldRes.m_info->m_precondMask);
                ReleaseAssert(x_dfg_typecheck_impl_info_list[fnOrd].m_cost == goldRes.m_info->m_estimatedCost);
                bool flipRes = (ord % 2 == 1);
                ReleaseAssertIff(flipRes, goldRes.m_opKind == TypeCheckFunctionSelector::QueryResult::CallFunctionAndFlipResult);
            }
        }
    }
}
