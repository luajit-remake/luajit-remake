#include "deegen_api.h"

HeapPtr<FunctionObject> GetCallee(TValue lhs, TValue rhs);

__attribute__((__used__)) extern "C" void NO_RETURN testfn1(TValue lhs, TValue rhs)
{
    if (lhs.Is<tFunction>())
    {
        MakeCall(lhs.As<tFunction>(), lhs, rhs, +[](TValue /*lhs*/, TValue /*rhs*/, TValue* res, size_t /*numRes*/) {
            Return(*res);
        });
    }
    else if (rhs.Is<tFunction>())
    {
        MakeTailCall(rhs.As<tFunction>(), rhs, lhs);
    }
    else
    {
        MakeCallPassingVariadicRes(MakeCallOption::DontProfileInInterpreter, GetCallee(lhs, rhs), lhs, rhs, lhs, rhs, +[](TValue /*lhs*/, TValue /*rhs*/, TValue* res, size_t /*numRes*/) {
            Return(*res);
        });
    }
}

__attribute__((__used__)) extern "C" void NO_RETURN testfn2(TValue* vbegin, size_t numArgs)
{
    if (vbegin->Is<tFunction>())
    {
        MakeInPlaceCall(vbegin, numArgs, +[](TValue* /*vbegin*/, size_t /*numArgs*/, TValue* res, size_t /*numRes*/) {
            Return(*res);
        });
    }
    else
    {
        MakeTailCallPassingVariadicRes(GetCallee(vbegin[0], vbegin[1]), vbegin[2], vbegin, numArgs, vbegin[3]);
    }
}
