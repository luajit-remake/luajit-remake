#include "force_release_build.h"

#include "define_deegen_common_snippet.h"
#include "runtime_utils.h"

// Returns the fixed up stack frame base
//
static uint64_t* DeegenSnippet_FixupStackFrameForVariadicArgFunction(uint64_t* paramStart, uint64_t numProvidedParams, uint64_t numFixedParams, uint64_t nilValue, uint64_t isMustTail)
{
    if (numProvidedParams < numFixedParams)
    {
        // This part is copied from 'PopulateNilToUnprovidedParams'..
        //
        if (!__builtin_constant_p(numFixedParams))
        {
            // Don't unroll and generate a big pile of code
            //
#pragma clang loop unroll(disable)
#pragma clang loop vectorize(disable)
            while (numProvidedParams < numFixedParams)
            {
                paramStart[numProvidedParams] = nilValue;
                numProvidedParams++;
            }
        }
        else
        {
            // 'numFixedParams' is a constant which means the user intends us to unroll the loop and let LLVM optimize it
            // We also take advantage of the fact that we can write a few more values without problem, to reduce the # of branches
            //
            while (numProvidedParams < numFixedParams)
            {
                paramStart[numProvidedParams] = nilValue;
                paramStart[numProvidedParams + 1] = nilValue;
                numProvidedParams += 2;
            }
        }
        return paramStart;
    }
    else if (numProvidedParams > numFixedParams)
    {
        // We want to transform from
        //     [ StackFrameHeader ] [ ........ Args ....... ]
        //                          ^ paramStart
        // to
        //     [  xxxxxxxxxxxxxxxxxxxxxx  ] [ .. VarArgs .. ] [ StackFrameHeader ] [ .. FixedArgs .. ]
        //             (unused junk)                                               ^ newParamsStart
        //
        uint64_t* callFrameBegin = paramStart - x_numSlotsForStackFrameHeader;
        uint64_t* newStackFrameHeader = paramStart + numProvidedParams;
        uint64_t numU64ToCopy = x_numSlotsForStackFrameHeader + numFixedParams;
        memcpy(newStackFrameHeader, callFrameBegin, sizeof(uint64_t) * numU64ToCopy);
        size_t numVariadicArgs = numProvidedParams - numFixedParams;
        reinterpret_cast<StackFrameHeader*>(newStackFrameHeader)->m_numVariadicArguments = static_cast<uint32_t>(numVariadicArgs);

        // But if the call is a must-tail call, introducing the junk area is unacceptable because
        // this breaks the no-unbounded-stack-growth guarantee.
        //
        // So in this case we will perform a memmove to eliminate the junk area.
        // This is not efficient, but only happens for tail calls to a function that accepts variadic arguments
        // and actually took non-empty variadic arguments, which is supposely rare.
        //
        if (unlikely(isMustTail))
        {
            // Note that the size of the junk area is also 'numU64ToCopy' uint64_t
            //
            memmove(callFrameBegin, callFrameBegin + numU64ToCopy, sizeof(uint64_t) * (x_numSlotsForStackFrameHeader + numProvidedParams));
            return paramStart + numVariadicArgs;
        }
        else
        {
            return newStackFrameHeader + x_numSlotsForStackFrameHeader;
        }
    }
    else
    {
        return paramStart;
    }
}

DEFINE_DEEGEN_COMMON_SNIPPET("FixupStackFrameForVariadicArgFunction", DeegenSnippet_FixupStackFrameForVariadicArgFunction)

// Do not run optimization, extract directly, so that '__builtin_constant_p' is not prematurely lowered
//
DEEGEN_COMMON_SNIPPET_OPTION_DO_NOT_OPTIMIZE_BEFORE_EXTRACT
