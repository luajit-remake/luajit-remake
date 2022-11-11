#include "deegen_api.h"
#include "runtime_utils.h"

// math.abs -- https://www.lua.org/manual/5.1/manual.html#pdf-math.abs
//
// math.abs (x)
// Returns the absolute value of x.
//
DEEGEN_DEFINE_LIB_FUNC(math_abs)
{
    ThrowError("Library function 'math.abs' is not implemented yet!");
}

// math.acos -- https://www.lua.org/manual/5.1/manual.html#pdf-math.acos
//
// math.acos (x)
// Returns the arc cosine of x (in radians).
//
DEEGEN_DEFINE_LIB_FUNC(math_acos)
{
    ThrowError("Library function 'math.acos' is not implemented yet!");
}

// math.asin -- https://www.lua.org/manual/5.1/manual.html#pdf-math.asin
//
// math.asin (x)
// Returns the arc sine of x (in radians).
//
DEEGEN_DEFINE_LIB_FUNC(math_asin)
{
    ThrowError("Library function 'math.asin' is not implemented yet!");
}

// math.atan -- https://www.lua.org/manual/5.1/manual.html#pdf-math.atan
//
// math.atan (x)
// Returns the arc tangent of x (in radians).
//
DEEGEN_DEFINE_LIB_FUNC(math_atan)
{
    ThrowError("Library function 'math.atan' is not implemented yet!");
}

// math.atan2 -- https://www.lua.org/manual/5.1/manual.html#pdf-math.atan2
//
// math.atan2 (y, x)
// Returns the arc tangent of y/x (in radians), but uses the signs of both parameters to find the quadrant of the result.
// (It also handles correctly the case of x being zero.)
//
DEEGEN_DEFINE_LIB_FUNC(math_atan2)
{
    ThrowError("Library function 'math.atan2' is not implemented yet!");
}

// math.ceil -- https://www.lua.org/manual/5.1/manual.html#pdf-math.ceil
//
// math.ceil (x)
// Returns the smallest integer larger than or equal to x.
//
DEEGEN_DEFINE_LIB_FUNC(math_ceil)
{
    ThrowError("Library function 'math.ceil' is not implemented yet!");
}

// math.cos -- https://www.lua.org/manual/5.1/manual.html#pdf-math.cos
//
// math.cos (x)
// Returns the cosine of x (assumed to be in radians).
//
DEEGEN_DEFINE_LIB_FUNC(math_cos)
{
    ThrowError("Library function 'math.cos' is not implemented yet!");
}

// math.cosh -- https://www.lua.org/manual/5.1/manual.html#pdf-math.cosh
//
// math.cosh (x)
// Returns the hyperbolic cosine of x.
//
DEEGEN_DEFINE_LIB_FUNC(math_cosh)
{
    ThrowError("Library function 'math.cosh' is not implemented yet!");
}

// math.deg -- https://www.lua.org/manual/5.1/manual.html#pdf-math.deg
//
// math.deg (x)
// Returns the angle x (given in radians) in degrees.
//
DEEGEN_DEFINE_LIB_FUNC(math_deg)
{
    ThrowError("Library function 'math.deg' is not implemented yet!");
}

// math.exp -- https://www.lua.org/manual/5.1/manual.html#pdf-math.exp
//
// math.exp (x)
// Returns the value e^x.
//
DEEGEN_DEFINE_LIB_FUNC(math_exp)
{
    ThrowError("Library function 'math.exp' is not implemented yet!");
}

// math.floor -- https://www.lua.org/manual/5.1/manual.html#pdf-math.floor
//
// math.floor (x)
// Returns the largest integer smaller than or equal to x.
//
DEEGEN_DEFINE_LIB_FUNC(math_floor)
{
    ThrowError("Library function 'math.floor' is not implemented yet!");
}

// math.fmod -- https://www.lua.org/manual/5.1/manual.html#pdf-math.fmod
//
// math.fmod (x, y)
// Returns the remainder of the division of x by y that rounds the quotient towards zero.
//
DEEGEN_DEFINE_LIB_FUNC(math_fmod)
{
    ThrowError("Library function 'math.fmod' is not implemented yet!");
}

// math.frexp -- https://www.lua.org/manual/5.1/manual.html#pdf-math.frexp
//
// math.frexp (x)
// Returns m and e such that x = m2^e, e is an integer and the absolute value of m is in the range [0.5, 1) (or zero when x is zero).
//
DEEGEN_DEFINE_LIB_FUNC(math_frexp)
{
    ThrowError("Library function 'math.frexp' is not implemented yet!");
}

// math.ldexp -- https://www.lua.org/manual/5.1/manual.html#pdf-math.ldexp
//
// math.ldexp (m, e)
// Returns m2^e (e should be an integer).
//
DEEGEN_DEFINE_LIB_FUNC(math_ldexp)
{
    ThrowError("Library function 'math.ldexp' is not implemented yet!");
}

// math.log -- https://www.lua.org/manual/5.1/manual.html#pdf-math.log
//
// math.log (x)
// Returns the natural logarithm of x.
//
DEEGEN_DEFINE_LIB_FUNC(math_log)
{
    ThrowError("Library function 'math.log' is not implemented yet!");
}

// math.log10 -- https://www.lua.org/manual/5.1/manual.html#pdf-math.log10
//
// math.log10 (x)
// Returns the base-10 logarithm of x.
//
DEEGEN_DEFINE_LIB_FUNC(math_log10)
{
    ThrowError("Library function 'math.log10' is not implemented yet!");
}

// math.max -- https://www.lua.org/manual/5.1/manual.html#pdf-math.max
//
// math.max (x, ···)
// Returns the maximum value among its arguments.
//
DEEGEN_DEFINE_LIB_FUNC(math_max)
{
    ThrowError("Library function 'math.max' is not implemented yet!");
}

// math.min -- https://www.lua.org/manual/5.1/manual.html#pdf-math.min
//
// math.min (x, ···)
// Returns the minimum value among its arguments.
//
DEEGEN_DEFINE_LIB_FUNC(math_min)
{
    ThrowError("Library function 'math.min' is not implemented yet!");
}

// math.modf -- https://www.lua.org/manual/5.1/manual.html#pdf-math.modf
//
// math.modf (x)
// Returns two numbers, the integral part of x and the fractional part of x.
//
DEEGEN_DEFINE_LIB_FUNC(math_modf)
{
    ThrowError("Library function 'math.modf' is not implemented yet!");
}

// math.pow -- https://www.lua.org/manual/5.1/manual.html#pdf-math.pow
//
// math.pow (x, y)
// Returns x^y. (You can also use the expression x^y to compute this value.)
//
DEEGEN_DEFINE_LIB_FUNC(math_pow)
{
    ThrowError("Library function 'math.pow' is not implemented yet!");
}

// math.rad -- https://www.lua.org/manual/5.1/manual.html#pdf-math.rad
//
// math.rad (x)
// Returns the angle x (given in degrees) in radians.
//
DEEGEN_DEFINE_LIB_FUNC(math_rad)
{
    ThrowError("Library function 'math.rad' is not implemented yet!");
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
    ThrowError("Library function 'math.random' is not implemented yet!");
}

// math.randomseed -- https://www.lua.org/manual/5.1/manual.html#pdf-math.randomseed
//
// math.randomseed (x)
// Sets x as the "seed" for the pseudo-random generator: equal seeds produce equal sequences of numbers.
//
DEEGEN_DEFINE_LIB_FUNC(math_randomseed)
{
    ThrowError("Library function 'math.randomseed' is not implemented yet!");
}

// math.sin -- https://www.lua.org/manual/5.1/manual.html#pdf-math.sin
//
// math.sin (x)
// Returns the sine of x (assumed to be in radians).
//
DEEGEN_DEFINE_LIB_FUNC(math_sin)
{
    ThrowError("Library function 'math.sin' is not implemented yet!");
}

// math.sinh -- https://www.lua.org/manual/5.1/manual.html#pdf-math.sinh
//
// math.sinh (x)
// Returns the hyperbolic sine of x.
//
DEEGEN_DEFINE_LIB_FUNC(math_sinh)
{
    ThrowError("Library function 'math.sinh' is not implemented yet!");
}

// math.sqrt -- https://www.lua.org/manual/5.1/manual.html#pdf-math.sqrt
//
// math.sqrt (x)
// Returns the square root of x. (You can also use the expression x^0.5 to compute this value.)
//
// TODO: we need to properly handle tonumber case (e.g., math.sqrt("123") should work)
//
DEEGEN_DEFINE_LIB_FUNC(math_sqrt)
{
    if (GetNumArgs() < 1)
    {
        ThrowError("bad argument #1 to 'sqrt' (number expected, got no value)");
    }
    TValue input = GetArg(0);
    if (likely(input.Is<tDouble>()))
    {
        double val = input.As<tDouble>();
        Return(TValue::Create<tDouble>(sqrt(val)));
    }
#if 0
    else if (input.Is<tInt32>())
    {
        double val = input.As<tInt32>();
        Return(TValue::Create<tDouble>(sqrt(val)));
    }
#endif
    else
    {
        ThrowError("bad argument #1 to 'sqrt' (number expected)");
    }
}

// math.tan -- https://www.lua.org/manual/5.1/manual.html#pdf-math.tan
//
// math.tan (x)
// Returns the tangent of x (assumed to be in radians).
//
DEEGEN_DEFINE_LIB_FUNC(math_tan)
{
    ThrowError("Library function 'math.tan' is not implemented yet!");
}

// math.tanh -- https://www.lua.org/manual/5.1/manual.html#pdf-math.tanh
//
// math.tanh (x)
// Returns the hyperbolic tangent of x.
//
DEEGEN_DEFINE_LIB_FUNC(math_tanh)
{
    ThrowError("Library function 'math.tanh' is not implemented yet!");
}

DEEGEN_END_LIB_FUNC_DEFINITIONS
