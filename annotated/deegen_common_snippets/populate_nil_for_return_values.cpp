#include "define_deegen_common_snippet.h"
#include "runtime_utils.h"

static void DeegenSnippet_PopulateNilForReturnValues(uint64_t* retStart, uint64_t numRets)
{
    // This function is not pre-optimized (since we are using __builtin_constant_p hack),
    // and due to a limitation of our LLVMRepeatedInliningInhibitor scheme, such functions
    // must not have any function calls that needs to be inlined.
    //
    // So it's important to make it constexpr here.
    //
    constexpr uint64_t nilValue = TValue::Nil().m_value;

    // Conceptually we just want to execute the following logic:
    //     while (numRets < x_minNilFillReturnValues) { retStart[numRets] = nilValue; numRets++; }
    //
    // However, we do not want to emit branches here.
    // We take advantage of the fact that it is always fine to overwrite 3 slots after the return values.
    // So we could have done: retStart[numRet] = retStart[numRet+1] = retStart[numRet+2] = nil
    //
    // But we do not want to unconditionally write 3 slots either, since in many cases we can deduce
    // that numRet is already at least some value. So we use this __builtin_constant_p hack to
    // write less slots if LLVM can reason about the value of 'numRet'. It's ugly, but should be fine..
    //
    static_assert(x_minNilFillReturnValues == 3);

    bool canDeduceAtLeast3 = __builtin_constant_p(numRets >= 3) && (numRets >= 3);
    if (canDeduceAtLeast3)
    {
        return;
    }

    bool canDeduceAtLeast2 = __builtin_constant_p(numRets >= 2) && (numRets >= 2);
    if (canDeduceAtLeast2)
    {
        retStart[numRets] = nilValue;
        return;
    }

    bool canDeduceAtLeast1 = __builtin_constant_p(numRets >= 1) && (numRets >= 1);
    if (canDeduceAtLeast1)
    {
        retStart[numRets] = nilValue;
        retStart[numRets + 1] = nilValue;
        return;
    }

    retStart[numRets] = nilValue;
    retStart[numRets + 1] = nilValue;
    retStart[numRets + 2] = nilValue;
}

DEFINE_DEEGEN_COMMON_SNIPPET("PopulateNilForReturnValues", DeegenSnippet_PopulateNilForReturnValues)

// Do not run optimization, extract directly, so that '__builtin_constant_p' is not prematurely lowered
//
DEEGEN_COMMON_SNIPPET_OPTION_DO_NOT_OPTIMIZE_BEFORE_EXTRACT
