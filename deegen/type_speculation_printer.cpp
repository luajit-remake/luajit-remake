#include "tvalue.h"

#include <sstream>

std::string DumpHumanReadableTypeSpeculationDefinitions()
{
    using ElementT = std::pair<TypeMaskTy, std::string_view>;
    constexpr size_t n = detail::get_type_speculation_defs<TypeSpecializationList>::count;
    std::array<ElementT, n> arr = detail::get_type_speculation_defs<TypeSpecializationList>::value;

    std::sort(arr.begin(), arr.end(), [](const ElementT& lhs, const ElementT& rhs) -> bool {
        int lhsOnes = std::popcount(lhs.first);
        int rhsOnes = std::popcount(rhs.first);
        if (lhsOnes != rhsOnes)
        {
            return lhsOnes < rhsOnes;
        }
        if (lhs.first != rhs.first)
        {
            return lhs.first < rhs.first;
        }
        return lhs.second < rhs.second;
    });

    std::string_view* names[n];
    for (size_t i = 0; i < n; i++) { names[i] = nullptr; }

    int maxk = 0;
    for (size_t i = 0; i < n; i++)
    {
        if (std::popcount(arr[i].first) == 1)
        {
            int k = std::countr_zero(arr[i].first);
            Assert(0 <= k && k < static_cast<int>(n));
            Assert(names[k] == nullptr);
            names[k] = &arr[i].second;
            maxk = std::max(maxk, k + 1);
        }
    }
    for (int i = 0; i < maxk; i++)
    {
        Assert(names[i] != nullptr);
    }

    std::stringstream ss;
    ss << "== Type Speculation Mask Defintions ==" << std::endl << std::endl;
    ss << "Defintion for each bit:" << std::endl;
    for (size_t i = 0; i < n; i++)
    {
        if (arr[i].first == 0)
        {
            // skip tBottom
            //
            continue;
        }
        int numOnes = std::popcount(arr[i].first);
        if (numOnes == 1)
        {
            ss << "Bit " << std::countr_zero(arr[i].first) << ": " << arr[i].second << " (0x" << std::hex << arr[i].first << std::dec << ")" << std::endl;
        }
        else
        {
            if (i > 0 && std::popcount(arr[i - 1].first) == 1)
            {
                ss << std::endl << "Compound Mask Definitions:" << std::endl;
            }
            ss << arr[i].second << " (0x" << std::hex << arr[i].first << std::dec << "): ";

            bool first = true;
            for (int k = 0; k < maxk; k++)
            {
                if (arr[i].first & (static_cast<TypeMaskTy>(1) << k))
                {
                    if (!first)
                    {
                        ss << " | ";
                    }
                    first = false;
                    ss << *names[k];
                }
            }
            ss << std::endl;
        }
    }
    std::string result = ss.str();
    return result;
}

std::string WARN_UNUSED DumpHumanReadableTypeSpeculation(TypeMaskTy mask, bool printMaskValue)
{
    constexpr auto arr = x_list_of_type_speculation_mask_and_name;
    constexpr size_t n = arr.size();

    std::stringstream ss;
    if (mask == 0)
    {
        if (printMaskValue)
        {
            ss << "tBottom (0x0)";
        }
        else
        {
            ss << "tBottom";
        }
    }
    else
    {
        TypeMaskTy originalVal = mask;
        bool first = true;
        for (size_t i = 0; i < n; i++)
        {
            TypeMaskTy curMask = arr[i].first;
            if (curMask == 0) { continue; }
            if ((mask & curMask) == curMask)
            {
                if (!first)
                {
                    ss << " | ";
                }
                first = false;
                ss << arr[i].second;
                mask ^= curMask;
            }
        }
        ReleaseAssert(mask == 0);
        if (printMaskValue)
        {
            ss << " (0x" << std::hex << originalVal << std::dec << ")";
        }
    }
    std::string result = ss.str();
    return result;
}
