#include "bytecode_definition_utils.h"
#include "deegen_api.h"

#include "bytecode.h"

static void NO_RETURN StoreVarArgsAsVariadicResultsOpcodeImpl()
{
    VarArgsAccessor::StoreAllVarArgsAsVariadicResults();
    Return();
}

DEEGEN_DEFINE_BYTECODE(StoreVarArgsAsVariadicResults)
{
    Operands();
    Result(NoOutput);
    Implementation(StoreVarArgsAsVariadicResultsOpcodeImpl);
    Variant();
}

static void ALWAYS_INLINE NaiveMemcpyTValue(TValue* dst, TValue* src, size_t numTValues)
{
    TValue* end = dst + numTValues;
    // Don't unroll
    //
#pragma clang loop unroll(disable)
#pragma clang loop vectorize(disable)
    while (dst < end)
    {
        *dst = *src;
        dst++;
        src++;
    }
}

static void NO_RETURN GetVarArgsPrefixImpl(TValue* base, uint16_t numToPut)
{
    TValue* vaBegin = VarArgsAccessor::GetPtr();
    size_t numVarArgs = VarArgsAccessor::GetNum();
    if (numVarArgs < numToPut)
    {
        NaiveMemcpyTValue(base, vaBegin, numVarArgs);

        TValue* addrEnd = base + numToPut;
        base += numVarArgs;
        TValue val = TValue::Create<tNil>();

        // Don't unroll
        //
#pragma clang loop unroll(disable)
#pragma clang loop vectorize(disable)
        while (base < addrEnd)
        {
            *base = val;
            base++;
        }
    }
    else
    {
        NaiveMemcpyTValue(base, vaBegin, numToPut);
    }
    Return();
}

DEEGEN_DEFINE_BYTECODE(GetVarArgsPrefix)
{
    Operands(
        BytecodeRangeBaseRW("base"),
        Literal<uint16_t>("numToPut")
    );
    Result(NoOutput);
    Implementation(GetVarArgsPrefixImpl);
    Variant();
}

DEEGEN_END_BYTECODE_DEFINITIONS


