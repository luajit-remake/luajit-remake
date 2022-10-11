#include "bytecode_definition_utils.h"
#include "deegen_api.h"

#include "runtime_utils.h"

static void NO_RETURN ReturnNoneImpl()
{
    GuestLanguageFunctionReturn();
}

DEEGEN_DEFINE_BYTECODE(Ret0)
{
    Operands();
    Result(NoOutput);
    Implementation(ReturnNoneImpl);
    Variant();
}

static void NO_RETURN ReturnImpl(TValue* retStart, uint16_t numRet)
{
    GuestLanguageFunctionReturn(retStart, numRet);
}

DEEGEN_DEFINE_BYTECODE(Ret)
{
    Operands(
        BytecodeRangeBaseRW("retStart"),
        Literal<uint16_t>("numRet")
    );
    Result(NoOutput);
    Implementation(ReturnImpl);
    Variant(Op("numRet").HasValue(0));
    Variant(Op("numRet").HasValue(1));
    Variant(Op("numRet").HasValue(2));
    Variant(Op("numRet").HasValue(3));
    Variant(Op("numRet").HasValue(4));
    Variant(Op("numRet").HasValue(5));
    Variant();
}

static void NO_RETURN ReturnAppendingVariadicResultsImpl(TValue* retStart, uint16_t numRet)
{
    GuestLanguageFunctionReturnAppendingVariadicResults(retStart, numRet);
}

DEEGEN_DEFINE_BYTECODE(RetM)
{
    Operands(
        BytecodeRangeBaseRW("retStart"),
        Literal<uint16_t>("numRet")
    );
    Result(NoOutput);
    Implementation(ReturnAppendingVariadicResultsImpl);
    Variant(Op("numRet").HasValue(0));
    Variant(Op("numRet").HasValue(1));
    Variant(Op("numRet").HasValue(2));
    Variant(Op("numRet").HasValue(3));
    Variant(Op("numRet").HasValue(4));
    Variant();
}

DEEGEN_END_BYTECODE_DEFINITIONS
