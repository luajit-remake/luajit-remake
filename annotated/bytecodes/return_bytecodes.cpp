#include "api_define_bytecode.h"
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
    DeclareAsIntrinsic<Intrinsic::FunctionReturn0>({});
}

static void NO_RETURN ReturnImpl(const TValue* retStart, uint16_t numRet)
{
    GuestLanguageFunctionReturn(retStart, numRet);
}

DEEGEN_DEFINE_BYTECODE(Ret)
{
    Operands(
        BytecodeRangeBaseRO("retStart"),
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
    DeclareReads(Range(Op("retStart"), Op("numRet")));
    DeclareAsIntrinsic<Intrinsic::FunctionReturn>({
        .start = Op("retStart"),
        .length = Op("numRet")
    });
}

static void NO_RETURN ReturnAppendingVariadicResultsImpl(const TValue* retStart, uint16_t numRet)
{
    GuestLanguageFunctionReturnAppendingVariadicResults(retStart, numRet);
}

DEEGEN_DEFINE_BYTECODE(RetM)
{
    Operands(
        BytecodeRangeBaseRO("retStart"),
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
    DeclareReads(
        Range(Op("retStart"), Op("numRet")),
        VariadicResults()
    );
    DeclareAsIntrinsic<Intrinsic::FunctionReturnAppendingVarRet>({
        .start = Op("retStart"),
        .length = Op("numRet")
    });
}

DEEGEN_END_BYTECODE_DEFINITIONS
