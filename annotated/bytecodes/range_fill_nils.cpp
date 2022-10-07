#include "bytecode_definition_utils.h"
#include "deegen_api.h"

#include "runtime_utils.h"

static void NO_RETURN RangeFillNilsImpl(TValue* base, uint16_t numToPut)
{
    TValue* addrEnd = base + numToPut;
    TValue val = TValue::Create<tNil>();
    while (base < addrEnd)
    {
        *base = val;
        base++;
    }
    Return();
}

DEEGEN_DEFINE_BYTECODE(RangeFillNils)
{
    Operands(
        BytecodeRangeBaseRW("base"),
        Literal<uint16_t>("numToPut")
    );
    Result(NoOutput);
    Implementation(RangeFillNilsImpl);
    Variant();
}

DEEGEN_END_BYTECODE_DEFINITIONS


