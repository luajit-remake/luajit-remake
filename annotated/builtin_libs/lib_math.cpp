#include "deegen_api.h"
#include "runtime_utils.h"

// Lua standard library math.sqrt
//
DEEGEN_DEFINE_LIB_FUNC(math_sqrt)
{
    if (GetNumArgs() < 1)
    {
        ThrowError("bad argument #1 to 'sqrt' (number expected, got no value)");
    }
    TValue input = GetArg(0);
    double inputDouble;
    if (input.Is<tDouble>())
    {
        inputDouble = input.As<tDouble>();
    }
    else if (input.Is<tInt32>())
    {
        inputDouble = input.As<tInt32>();
    }
    else
    {
        ThrowError("bad argument #1 to 'sqrt' (number expected)");
    }

    double result = sqrt(inputDouble);
    Return(TValue::Create<tDouble>(result));
}

DEEGEN_END_LIB_FUNC_DEFINITIONS
