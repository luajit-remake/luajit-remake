#include "bytecode_definition_utils.h"
#include "deegen_api.h"

#include "bytecode.h"

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
    // TODO: add specialization
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
    Variant();
}

DEEGEN_END_BYTECODE_DEFINITIONS
