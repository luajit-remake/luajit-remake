#include "api_define_bytecode.h"
#include "deegen_api.h"

#include "runtime_utils.h"

namespace ConcatBytecodeHelper {

struct ScanForMetamethodCallResult
{
    // If true, 'm_lhsValue' stores the final value, and no more metamethod call happens
    // If false, 'm_lhsValue' and 'm_rhsValue' shall be passed to metamethod call
    //
    bool m_exhausted;
    // The slot offset minus one where 'm_lhsValue' is found, if m_exhausted == false
    //
    int32_t m_endOffset;
    // The value pair for metamethod call
    //
    TValue m_lhsValue;
    TValue m_rhsValue;
};

inline HeapPtr<HeapString> WARN_UNUSED StringifyDoubleToStringObject(double value)
{
    char buf[x_default_tostring_buffersize_double];
    char* bufEnd = StringifyDoubleUsingDefaultLuaFormattingOptions(buf /*out*/, value);
    return TranslateToHeapPtr(VM::GetActiveVMForCurrentThread()->CreateStringObjectFromRawString(buf, static_cast<uint32_t>(bufEnd - buf)).As());
}

inline HeapPtr<HeapString> WARN_UNUSED StringifyInt32ToStringObject(int32_t value)
{
    char buf[x_default_tostring_buffersize_int];
    char* bufEnd = StringifyInt32UsingDefaultLuaFormattingOptions(buf /*out*/, value);
    return TranslateToHeapPtr(VM::GetActiveVMForCurrentThread()->CreateStringObjectFromRawString(buf, static_cast<uint32_t>(bufEnd - buf)).As());
}

inline std::optional<HeapPtr<HeapString>> WARN_UNUSED TryGetStringOrConvertNumberToString(TValue value)
{
    if (value.Is<tString>())
    {
        return value.As<tString>();
    }
    if (value.Is<tDouble>())
    {
        return StringifyDoubleToStringObject(value.As<tDouble>());
    }
    if (value.Is<tInt32>())
    {
        return StringifyInt32ToStringObject(value.As<tInt32>());
    }
    return {};
}

// Try to execute loop: while startOffset != -1: curValue = base[startOffset] .. curValue, startOffset -= 1
// Until the loop ends or it encounters an expression where a metamethod call is needed.
// Returns the final value if the loop ends, or the value pair for the metamethod call.
//
inline ScanForMetamethodCallResult WARN_UNUSED ScanForMetamethodCall(TValue* base, int32_t startOffset, TValue curValue)
{
    assert(startOffset >= 0);

    std::optional<HeapPtr<HeapString>> optStr = TryGetStringOrConvertNumberToString(curValue);
    if (!optStr)
    {
        return {
            .m_exhausted = false,
            .m_endOffset = startOffset - 1,
            .m_lhsValue = base[startOffset],
            .m_rhsValue = curValue
        };
    }

    // This is tricky: curString and curValue are out of sync at the beginning of the loop (curValue might be number while curString must be string),
    // but are always kept in sync in every later iteration (see end of the loop below), and the return inside the loop returns 'curValue'.
    // This is required, because if no concatenation happens before metamethod call,
    // the metamethod should see the original parameter, not the coerced-to-string parameter.
    //
    HeapPtr<HeapString> curString = optStr.value();
    while (startOffset >= 0)
    {
        std::optional<HeapPtr<HeapString>> lhs = TryGetStringOrConvertNumberToString(base[startOffset]);
        if (!lhs)
        {
            return {
                .m_exhausted = false,
                .m_endOffset = startOffset - 1,
                .m_lhsValue = base[startOffset],
                .m_rhsValue = curValue
            };
        }

        startOffset--;
        TValue tmp[2];
        tmp[0] = TValue::Create<tString>(lhs.value());
        tmp[1] = TValue::Create<tString>(curString);
        curString = TranslateToHeapPtr(VM::GetActiveVMForCurrentThread()->CreateStringObjectFromConcatenation(tmp, 2 /*len*/).As());
        curValue = TValue::Create<tString>(curString);
    }

    return {
        .m_exhausted = true,
        .m_lhsValue = curValue
    };
}

inline std::pair<bool, TValue> WARN_UNUSED NO_INLINE TryConcatFastPath(TValue* base, uint32_t num)
{
    bool success = true;
    bool needNumberConversion = false;
    for (uint32_t i = 0; i < num; i++)
    {
        TValue val = base[i];
        if (val.Is<tString>())
        {
            continue;
        }
        if (val.Is<tDouble>() || val.Is<tInt32>())
        {
            needNumberConversion = true;
            continue;
        }
        success = false;
        break;
    }

    if (likely(success))
    {
        VM* vm = VM::GetActiveVMForCurrentThread();
        if (needNumberConversion)
        {
            for (uint32_t i = 0; i < num; i++)
            {
                TValue val = base[i];
                if (val.Is<tDouble>())
                {
                    base[i] = TValue::Create<tString>(StringifyDoubleToStringObject(val.As<tDouble>()));
                }
                else if (val.Is<tInt32>())
                {
                    base[i] = TValue::Create<tString>(StringifyInt32ToStringObject(val.As<tInt32>()));
                }
            }
        }

        TValue result = TValue::Create<tString>(TranslateToHeapPtr(vm->CreateStringObjectFromConcatenation(base, num).As()));
        return std::make_pair(true, result);
    }
    else
    {
        return std::make_pair(false, TValue());
    }
}

}   // namespace ConcatBytecodeHelper

static void NO_RETURN ConcatOnMetamethodReturnContinuation(TValue* base, uint16_t num)
{
    using namespace ConcatBytecodeHelper;

    TValue curValue = GetReturnValue(0);
    assert(base[num - 1].Is<tInt32>());
    int32_t nextSlotToConcat = base[num - 1].As<tInt32>();
    assert(nextSlotToConcat >= -1 && nextSlotToConcat < static_cast<int32_t>(num) - 2);
    __builtin_assume(nextSlotToConcat < static_cast<int32_t>(num) - 2);
    if (nextSlotToConcat < 0)
    {
        Return(curValue);
    }

    ScanForMetamethodCallResult fsr = ScanForMetamethodCall(base, nextSlotToConcat, curValue);
    if (fsr.m_exhausted)
    {
        Return(fsr.m_lhsValue);
    }

    base[num - 1] = TValue::Create<tInt32>(fsr.m_endOffset);

    // Call metamethod
    //
    TValue metamethod = GetMetamethodForBinaryArithmeticOperation(fsr.m_lhsValue, fsr.m_rhsValue, LuaMetamethodKind::Concat);
    if (likely(metamethod.Is<tFunction>()))
    {
        MakeCall(TranslateToRawPointer(metamethod.As<tFunction>()), fsr.m_lhsValue, fsr.m_rhsValue, ConcatOnMetamethodReturnContinuation);
    }

    if (unlikely(metamethod.Is<tNil>()))
    {
        // TODO: make this error consistent with Lua
        //
        ThrowError("Invalid types for concat");
    }

    FunctionObject* callTarget = GetCallTargetViaMetatable(metamethod);
    if (unlikely(callTarget == nullptr))
    {
        ThrowError(MakeErrorMessageForUnableToCall(metamethod));
    }

    MakeCall(callTarget, metamethod, fsr.m_lhsValue, fsr.m_rhsValue, ConcatOnMetamethodReturnContinuation);
}

static void NO_RETURN ConcatCallMetatableSlowPath(TValue* base, uint16_t num)
{
    using namespace ConcatBytecodeHelper;

    // Need to call metamethod
    // Note that this must be executed from right to left (this semantic is expected by Lua)
    // Do primitive concatenation until we found the first location to call metamethod
    //
    ScanForMetamethodCallResult fsr = ScanForMetamethodCall(base, static_cast<int32_t>(num) - 2, base[num - 1] /*initialValue*/);
    assert(!fsr.m_exhausted);

    // Store the next slot to concat on metamethod return on the last slot of the values to concat
    // This slot is clobberable (confirmed by checking LuaJIT source code).
    //
    base[num - 1] = TValue::Create<tInt32>(fsr.m_endOffset);

    // Call metamethod
    //
    TValue metamethod = GetMetamethodForBinaryArithmeticOperation(fsr.m_lhsValue, fsr.m_rhsValue, LuaMetamethodKind::Concat);
    if (likely(metamethod.Is<tFunction>()))
    {
        MakeCall(TranslateToRawPointer(metamethod.As<tFunction>()), fsr.m_lhsValue, fsr.m_rhsValue, ConcatOnMetamethodReturnContinuation);
    }

    if (unlikely(metamethod.Is<tNil>()))
    {
        // TODO: make this error consistent with Lua
        //
        ThrowError("Invalid types for concat");
    }

    FunctionObject* callTarget = GetCallTargetViaMetatable(metamethod);
    if (unlikely(callTarget == nullptr))
    {
        ThrowError(MakeErrorMessageForUnableToCall(metamethod));
    }

    MakeCall(callTarget, metamethod, fsr.m_lhsValue, fsr.m_rhsValue, ConcatOnMetamethodReturnContinuation);
}

static void NO_RETURN ConcatImpl(TValue* base, uint16_t num)
{
    auto [success, result] = ConcatBytecodeHelper::TryConcatFastPath(base, num);
    if (likely(success))
    {
        Return(result);
    }
    else
    {
        EnterSlowPath<ConcatCallMetatableSlowPath>();
    }
}

DEEGEN_DEFINE_BYTECODE(Concat)
{
    Operands(
        BytecodeRangeBaseRW("base"),
        Literal<uint16_t>("num")
    );
    Result(BytecodeValue);
    Implementation(ConcatImpl);
    Variant(Op("num").HasValue(2));
    Variant();
    DeclareReads(Range(Op("base"), Op("num")));
    DeclareWrites();
    DeclareClobbers(Range(Op("base") + Op("num") - 1, 1));
}

DEEGEN_END_BYTECODE_DEFINITIONS
