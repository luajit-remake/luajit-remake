#include "force_release_build.h"

#include "define_deegen_common_snippet.h"
#include "bytecode.h"

static void DeegenSnippet_PopulateNilForReturnValues(uint64_t* retStart, uint64_t numRets)
{
    uint64_t nilValue = TValue::Nil().m_value;
    while (numRets < x_minNilFillReturnValues)
    {
        retStart[numRets] = nilValue;
        numRets++;
    }
}

DEFINE_DEEGEN_COMMON_SNIPPET("PopulateNilForReturnValues", DeegenSnippet_PopulateNilForReturnValues)

DEEGEN_COMMON_SNIPPET_OPTION_DO_NOT_OPTIMIZE_BEFORE_EXTRACT
