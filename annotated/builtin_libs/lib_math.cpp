#include "deegen_api.h"
#include "lib_util.h"
#include "runtime_utils.h"
#include <emmintrin.h>

// For math unary functions, get the first argument to the function as a double value and store it into 'argName',
// throwing an error if the action cannot be performed ('funcName' is built into the error message)
//
#define MATH_LIB_UNARY_FN_GET_ARG(funcName, argName)                                \
    if (unlikely(GetNumArgs() < 1))                                                 \
    {                                                                               \
        ThrowError("bad argument #1 to '"                                           \
                   PP_STRINGIFY(funcName)                                           \
                   "' (number expected, got no value)");                            \
    }                                                                               \
    double argName;                                                                 \
    {                                                                               \
        auto [macrotmp_success, macrotmp_result] = LuaLib::ToNumber(GetArg(0));     \
        if (unlikely(!macrotmp_success))                                            \
        {                                                                           \
            /* TODO: make error consistent with Lua (need actual type) */           \
            ThrowError("bad argument #1 to '"                                       \
                       PP_STRINGIFY(funcName)                                       \
                       "' (number expected)");                                      \
        }                                                                           \
        argName = macrotmp_result;                                                  \
    }                                                                               \
    assert(true)    /* end with a statement so a comma can be added */

// Similar to above, but for math binary functions
//
#define MATH_LIB_BINARY_FN_GET_ARGS(funcName, argName1, argName2)                   \
    if (unlikely(GetNumArgs() < 2))                                                 \
    {                                                                               \
        ThrowError("bad argument #2 to '" PP_STRINGIFY(funcName)                    \
                   "' (number expected, got no value)");                            \
    }                                                                               \
    double argName1 = GetArg(0).ViewAsDouble();                                     \
    double argName2 = GetArg(1).ViewAsDouble();                                     \
    if (unlikely(IsNaN(argName1) || IsNaN(argName2)))                               \
    {                                                                               \
        if (!LuaLib::TVDoubleViewToNumberSlow(argName1 /*inout*/))                  \
        {                                                                           \
            /* TODO: make error consistent with Lua (need actual type) */           \
            ThrowError("bad argument #1 to '" PP_STRINGIFY(funcName)                \
                       "' (number expected)");                                      \
        }                                                                           \
        if (!LuaLib::TVDoubleViewToNumberSlow(argName2 /*inout*/))                  \
        {                                                                           \
            /* TODO: make error consistent with Lua (need actual type) */           \
            ThrowError("bad argument #2 to '" PP_STRINGIFY(funcName)                \
                       "' (number expected)");                                      \
        }                                                                           \
    }                                                                               \
    assert(true)    /* end with a statement so a comma can be added */

// math.abs -- https://www.lua.org/manual/5.1/manual.html#pdf-math.abs
//
// math.abs (x)
// Returns the absolute value of x.
//
DEEGEN_DEFINE_LIB_FUNC(math_abs)
{
    MATH_LIB_UNARY_FN_GET_ARG(abs, arg);
    Return(TValue::Create<tDouble>(abs(arg)));
}

// math.acos -- https://www.lua.org/manual/5.1/manual.html#pdf-math.acos
//
// math.acos (x)
// Returns the arc cosine of x (in radians).
//
DEEGEN_DEFINE_LIB_FUNC(math_acos)
{
    MATH_LIB_UNARY_FN_GET_ARG(acos, arg);
    Return(TValue::Create<tDouble>(acos(arg)));
}

// math.asin -- https://www.lua.org/manual/5.1/manual.html#pdf-math.asin
//
// math.asin (x)
// Returns the arc sine of x (in radians).
//
DEEGEN_DEFINE_LIB_FUNC(math_asin)
{
    MATH_LIB_UNARY_FN_GET_ARG(asin, arg);
    Return(TValue::Create<tDouble>(asin(arg)));
}

// math.atan -- https://www.lua.org/manual/5.1/manual.html#pdf-math.atan
//
// math.atan (x)
// Returns the arc tangent of x (in radians).
//
DEEGEN_DEFINE_LIB_FUNC(math_atan)
{
    MATH_LIB_UNARY_FN_GET_ARG(atan, arg);
    Return(TValue::Create<tDouble>(atan(arg)));
}

// math.atan2 -- https://www.lua.org/manual/5.1/manual.html#pdf-math.atan2
//
// math.atan2 (y, x)
// Returns the arc tangent of y/x (in radians), but uses the signs of both parameters to find the quadrant of the result.
// (It also handles correctly the case of x being zero.)
//
DEEGEN_DEFINE_LIB_FUNC(math_atan2)
{
    MATH_LIB_BINARY_FN_GET_ARGS(atan2, arg1, arg2);
    Return(TValue::Create<tDouble>(atan2(arg1, arg2)));
}

// math.ceil -- https://www.lua.org/manual/5.1/manual.html#pdf-math.ceil
//
// math.ceil (x)
// Returns the smallest integer larger than or equal to x.
//
DEEGEN_DEFINE_LIB_FUNC(math_ceil)
{
    MATH_LIB_UNARY_FN_GET_ARG(ceil, arg);
    Return(TValue::Create<tDouble>(ceil(arg)));
}

// math.cos -- https://www.lua.org/manual/5.1/manual.html#pdf-math.cos
//
// math.cos (x)
// Returns the cosine of x (assumed to be in radians).
//
DEEGEN_DEFINE_LIB_FUNC(math_cos)
{
    MATH_LIB_UNARY_FN_GET_ARG(cos, arg);
    Return(TValue::Create<tDouble>(cos(arg)));
}

// math.cosh -- https://www.lua.org/manual/5.1/manual.html#pdf-math.cosh
//
// math.cosh (x)
// Returns the hyperbolic cosine of x.
//
DEEGEN_DEFINE_LIB_FUNC(math_cosh)
{
    MATH_LIB_UNARY_FN_GET_ARG(cosh, arg);
    Return(TValue::Create<tDouble>(cosh(arg)));
}

// math.deg -- https://www.lua.org/manual/5.1/manual.html#pdf-math.deg
//
// math.deg (x)
// Returns the angle x (given in radians) in degrees.
//
DEEGEN_DEFINE_LIB_FUNC(math_deg)
{
    MATH_LIB_UNARY_FN_GET_ARG(deg, arg);
    double result = arg * 57.29577951308232;
    Return(TValue::Create<tDouble>(result));
}

// math.exp -- https://www.lua.org/manual/5.1/manual.html#pdf-math.exp
//
// math.exp (x)
// Returns the value e^x.
//
DEEGEN_DEFINE_LIB_FUNC(math_exp)
{
    MATH_LIB_UNARY_FN_GET_ARG(exp, arg);
    Return(TValue::Create<tDouble>(exp(arg)));
}

// math.floor -- https://www.lua.org/manual/5.1/manual.html#pdf-math.floor
//
// math.floor (x)
// Returns the largest integer smaller than or equal to x.
//
DEEGEN_DEFINE_LIB_FUNC(math_floor)
{
    MATH_LIB_UNARY_FN_GET_ARG(floor, arg);
    Return(TValue::Create<tDouble>(floor(arg)));
}

// math.fmod -- https://www.lua.org/manual/5.1/manual.html#pdf-math.fmod
//
// math.fmod (x, y)
// Returns the remainder of the division of x by y that rounds the quotient towards zero.
//
DEEGEN_DEFINE_LIB_FUNC(math_fmod)
{
    MATH_LIB_BINARY_FN_GET_ARGS(fmod, arg1, arg2);
    Return(TValue::Create<tDouble>(fmod(arg1, arg2)));
}

// math.frexp -- https://www.lua.org/manual/5.1/manual.html#pdf-math.frexp
//
// math.frexp (x)
// Returns m and e such that x = m2^e, e is an integer and the absolute value of m is in the range [0.5, 1) (or zero when x is zero).
//
DEEGEN_DEFINE_LIB_FUNC(math_frexp)
{
    MATH_LIB_UNARY_FN_GET_ARG(frexp, arg);
    int e;
    double m = frexp(arg, &e /*out*/);
    Return(TValue::Create<tDouble>(m), TValue::Create<tDouble>(e));
}

// math.ldexp -- https://www.lua.org/manual/5.1/manual.html#pdf-math.ldexp
//
// math.ldexp (m, e)
// Returns m2^e (e should be an integer).
//
DEEGEN_DEFINE_LIB_FUNC(math_ldexp)
{
    MATH_LIB_BINARY_FN_GET_ARGS(ldexp, arg1, arg2);
    // Lua 5.1 throws no error if arg2 is not integer. It just static cast it to integer (confirmed by source code).
    // Later lua versions throw error if arg2 is not integer, but we are targeting 5.1 right now.
    //
    int ex = static_cast<int>(arg2);
    Return(TValue::Create<tDouble>(ldexp(arg1, ex)));
}

// math.log -- https://www.lua.org/manual/5.1/manual.html#pdf-math.log
//
// math.log (x)
// Returns the natural logarithm of x.
//
DEEGEN_DEFINE_LIB_FUNC(math_log)
{
    MATH_LIB_UNARY_FN_GET_ARG(log, arg);
    Return(TValue::Create<tDouble>(log(arg)));
}

// math.log10 -- https://www.lua.org/manual/5.1/manual.html#pdf-math.log10
//
// math.log10 (x)
// Returns the base-10 logarithm of x.
//
DEEGEN_DEFINE_LIB_FUNC(math_log10)
{
    MATH_LIB_UNARY_FN_GET_ARG(log10, arg);
    Return(TValue::Create<tDouble>(log10(arg)));
}

// math.max -- https://www.lua.org/manual/5.1/manual.html#pdf-math.max
//
// math.max (x, ···)
// Returns the maximum value among its arguments.
//
DEEGEN_DEFINE_LIB_FUNC(math_max)
{
    if (likely(GetNumArgs() == 2))
    {
        MATH_LIB_BINARY_FN_GET_ARGS(max, arg1, arg2);
        Return(TValue::Create<tDouble>(std::max(arg1, arg2)));
    }
    else
    {
        MATH_LIB_UNARY_FN_GET_ARG(max, res);
        size_t numArgs = GetNumArgs();
        for (size_t i = 1; i < numArgs; i++)
        {
            auto [success, value] = LuaLib::ToNumber(GetArg(i));
            if (unlikely(!success))
            {
                /* TODO: make error consistent with Lua (need actual type) */
                ThrowError("bad argument #k to 'max' (number expected)");
            }
            res = std::max(res, value);
        }
        Return(TValue::Create<tDouble>(res));
    }
}

// math.min -- https://www.lua.org/manual/5.1/manual.html#pdf-math.min
//
// math.min (x, ···)
// Returns the minimum value among its arguments.
//
DEEGEN_DEFINE_LIB_FUNC(math_min)
{
    if (likely(GetNumArgs() == 2))
    {
        MATH_LIB_BINARY_FN_GET_ARGS(min, arg1, arg2);
        Return(TValue::Create<tDouble>(std::min(arg1, arg2)));
    }
    else
    {
        MATH_LIB_UNARY_FN_GET_ARG(min, res);
        size_t numArgs = GetNumArgs();
        for (size_t i = 1; i < numArgs; i++)
        {
            auto [success, value] = LuaLib::ToNumber(GetArg(i));
            if (unlikely(!success))
            {
                /* TODO: make error consistent with Lua (need actual type) */
                ThrowError("bad argument #k to 'min' (number expected)");
            }
            res = std::min(res, value);
        }
        Return(TValue::Create<tDouble>(res));
    }
}

// math.modf -- https://www.lua.org/manual/5.1/manual.html#pdf-math.modf
//
// math.modf (x)
// Returns two numbers, the integral part of x and the fractional part of x.
//
DEEGEN_DEFINE_LIB_FUNC(math_modf)
{
    MATH_LIB_UNARY_FN_GET_ARG(modf, arg);
    double integralPart;
    double fractionalPart = modf(arg, &integralPart /*out*/);
    Return(TValue::Create<tDouble>(integralPart), TValue::Create<tDouble>(fractionalPart));
}

// math.pow -- https://www.lua.org/manual/5.1/manual.html#pdf-math.pow
//
// math.pow (x, y)
// Returns x^y. (You can also use the expression x^y to compute this value.)
//
DEEGEN_DEFINE_LIB_FUNC(math_pow)
{
    MATH_LIB_BINARY_FN_GET_ARGS(pow, arg1, arg2);
    Return(TValue::Create<tDouble>(math_fast_pow(arg1, arg2)));
}

// math.rad -- https://www.lua.org/manual/5.1/manual.html#pdf-math.rad
//
// math.rad (x)
// Returns the angle x (given in degrees) in radians.
//
DEEGEN_DEFINE_LIB_FUNC(math_rad)
{
    MATH_LIB_UNARY_FN_GET_ARG(rad, arg);
    double result = arg * 0.017453292519943295;
    Return(TValue::Create<tDouble>(result));
}

// math.random -- https://www.lua.org/manual/5.1/manual.html#pdf-math.random
//
// math.random ([m [, n]])
// This function is an interface to the simple pseudo-random generator function rand provided by ANSI C. (No guarantees can be given
// for its statistical properties.)
// When called without arguments, returns a uniform pseudo-random real number in the range [0,1). When called with an integer number m,
// math.random returns a uniform pseudo-random integer in the range [1, m]. When called with two integer numbers m and n, math.random
// returns a uniform pseudo-random integer in the range [m, n].
//
DEEGEN_DEFINE_LIB_FUNC(math_random)
{
    std::mt19937* gen = VM::GetUserPRNG();
    static_assert(std::mt19937::word_size == 32);
    // Lua 5.1 official implementation always call the generator before checking parameter validity
    //
    double value = static_cast<double>(static_cast<int32_t>((*gen)() >> 1)) / 2147483648.0;
    size_t numArgs = GetNumArgs();
    if (numArgs == 0)
    {
        Return(TValue::Create<tDouble>(value));
    }
    else if (numArgs == 1)
    {
        MATH_LIB_UNARY_FN_GET_ARG(random, ub);
        if (unlikely(!(ub >= 1.0)))   // different from ub < 1.0 in the presense of NaN!
        {
            ThrowError("bad argument #1 to 'random' (interval is empty)");
        }
        // This is what official Lua 5.1 does
        //
        double result = floor(value * ub) + 1.0;
        Return(TValue::Create<tDouble>(result));
    }
    else if (numArgs == 2)
    {
        MATH_LIB_BINARY_FN_GET_ARGS(random, lb, ub);
        if (unlikely(!(lb <= ub)))
        {
            ThrowError("bad argument #2 to 'random' (interval is empty)");
        }
        // This is what official Lua 5.1 does
        //
        double result = floor(value * (ub - lb + 1)) + lb;
        Return(TValue::Create<tDouble>(result));
    }
    else
    {
        ThrowError("wrong number of arguments to 'random'");
    }
}

// math.randomseed -- https://www.lua.org/manual/5.1/manual.html#pdf-math.randomseed
//
// math.randomseed (x)
// Sets x as the "seed" for the pseudo-random generator: equal seeds produce equal sequences of numbers.
//
DEEGEN_DEFINE_LIB_FUNC(math_randomseed)
{
    MATH_LIB_UNARY_FN_GET_ARG(randomseed, arg);
    std::mt19937* gen = VM::GetUserPRNG();
    gen->seed(static_cast<uint32_t>(HashPrimitiveTypes(arg)));
    Return();
}

// math.sin -- https://www.lua.org/manual/5.1/manual.html#pdf-math.sin
//
// math.sin (x)
// Returns the sine of x (assumed to be in radians).
//
DEEGEN_DEFINE_LIB_FUNC(math_sin)
{
    MATH_LIB_UNARY_FN_GET_ARG(sin, arg);
    Return(TValue::Create<tDouble>(sin(arg)));
}

// math.sinh -- https://www.lua.org/manual/5.1/manual.html#pdf-math.sinh
//
// math.sinh (x)
// Returns the hyperbolic sine of x.
//
DEEGEN_DEFINE_LIB_FUNC(math_sinh)
{
    MATH_LIB_UNARY_FN_GET_ARG(sinh, arg);
    Return(TValue::Create<tDouble>(sinh(arg)));
}

// C library's sqrt sets errno, which is unfortunately slow and inhibits compiler optimization
// It seems like the only way to generate a sqrt without errno is to use Intel intrinsics, which is what this function does.
//
inline double WARN_UNUSED ALWAYS_INLINE sqrt_no_errno(double val)
{
    __m128d x; x[0] = val;
    // dst = _mm_sqrt_sd(a, b):
    //     dst[63:0] := SQRT(b[63:0])
    //     dst[127:64] := a[127:64]
    //
    x = _mm_sqrt_sd(x, x);
    return x[0];
}

// math.sqrt -- https://www.lua.org/manual/5.1/manual.html#pdf-math.sqrt
//
// math.sqrt (x)
// Returns the square root of x. (You can also use the expression x^0.5 to compute this value.)
//
DEEGEN_DEFINE_LIB_FUNC(math_sqrt)
{
    MATH_LIB_UNARY_FN_GET_ARG(sqrt, arg);
    Return(TValue::Create<tDouble>(sqrt_no_errno(arg)));
}

// math.tan -- https://www.lua.org/manual/5.1/manual.html#pdf-math.tan
//
// math.tan (x)
// Returns the tangent of x (assumed to be in radians).
//
DEEGEN_DEFINE_LIB_FUNC(math_tan)
{
    MATH_LIB_UNARY_FN_GET_ARG(tan, arg);
    Return(TValue::Create<tDouble>(tan(arg)));
}

// math.tanh -- https://www.lua.org/manual/5.1/manual.html#pdf-math.tanh
//
// math.tanh (x)
// Returns the hyperbolic tangent of x.
//
DEEGEN_DEFINE_LIB_FUNC(math_tanh)
{
    MATH_LIB_UNARY_FN_GET_ARG(tanh, arg);
    Return(TValue::Create<tDouble>(tanh(arg)));
}

DEEGEN_END_LIB_FUNC_DEFINITIONS

#undef MATH_LIB_UNARY_FN_GET_ARG
#undef MATH_LIB_BINARY_FN_GET_ARGS
