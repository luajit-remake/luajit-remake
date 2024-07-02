#include "deegen_api.h"
#include "lualib_tonumber_util.h"
#include "runtime_utils.h"
#include "lj_strfmt.h"

// string.byte -- https://www.lua.org/manual/5.1/manual.html#pdf-string.byte
//
// string.byte (s [, i [, j]])
// Returns the internal numerical codes of the characters s[i], s[i+1], ···, s[j]. The default value for i is 1; the default value for j is i.
//
// Note that numerical codes are not necessarily portable across platforms.
//
DEEGEN_DEFINE_LIB_FUNC(string_byte)
{
    size_t numArgs = GetNumArgs();
    if (unlikely(numArgs == 0))
    {
        ThrowError("bad argument #1 to 'byte' (string expected, got no value)");
    }

    GET_ARG_AS_STRING(byte, 1, ptr, ulen);

    int64_t lb;
    if (numArgs == 1)
    {
        lb = 1;
    }
    else
    {
        TValue tvLb = GetArg(1);
        auto [success, val] = LuaLib_ToNumber(tvLb);
        if (unlikely(!success))
        {
            ThrowError("bad argument #2 to 'byte' (number expected)");
        }
        lb = static_cast<int64_t>(val);
    }

    int64_t ub;
    if (numArgs < 3)
    {
        ub = lb;
    }
    else
    {
        TValue tvUb = GetArg(2);
        auto [success, val] = LuaLib_ToNumber(tvUb);
        if (unlikely(!success))
        {
            ThrowError("bad argument #3 to 'byte' (number expected)");
        }
        ub = static_cast<int64_t>(val);
    }

    int64_t len = static_cast<int64_t>(ulen);
    if (ub < 0) { ub += len + 1; }
    if (lb < 0) { lb += len + 1; }
    if (lb <= 0) { lb = 1; }
    if (ub > len) { ub = len; }

    if (unlikely(lb > ub))
    {
        Return();
    }

    TValue* sb = GetStackBase();
    ptr--;
    for (int64_t i = lb; i <= ub; i++)
    {
        uint8_t charVal = static_cast<uint8_t>(ptr[i]);
        sb[i - lb] = TValue::Create<tDouble>(charVal);
    }
    ReturnValueRange(sb, static_cast<size_t>(ub - lb + 1));
}

// Return -1 if not convertible to number, -2 if out of range
//
int64_t WARN_UNUSED NO_INLINE __attribute__((__preserve_most__)) TryConvertValueToStringCharNumericalCode(double tvDoubleView)
{
    TValue tv; tv.m_value = cxx2a_bit_cast<uint64_t>(tvDoubleView);
    if (!LuaLib_TVDoubleViewToNumberSlow(tvDoubleView /*inout*/))
    {
        return -1;
    }
    int64_t i64 = static_cast<int64_t>(tvDoubleView);
    if (i64 < 0 || i64 > 255)
    {
        return -2;
    }
    return i64;
}

// string.char -- https://www.lua.org/manual/5.1/manual.html#pdf-string.char
//
// string.char (···)
// Receives zero or more integers. Returns a string with length equal to the number of arguments, in which each character has the internal
// numerical code equal to its corresponding argument.
//
// Note that numerical codes are not necessarily portable across platforms.
//
DEEGEN_DEFINE_LIB_FUNC(string_char)
{
    size_t numArgs = GetNumArgs();
    TValue* sb = GetStackBase();
    SimpleTempStringStream ss;
    uint8_t* ptr = reinterpret_cast<uint8_t*>(ss.Reserve(numArgs + 1));
    for (size_t i = 0; i < numArgs; i++)
    {
        // If sb[i] is not a double, tvDoubleView will be NaN and i64 will never be within [0,255],
        // so we will go to slow path that does the full check, as desired.
        //
        double tvDoubleView = sb[i].ViewAsDouble();
        int64_t i64 = static_cast<int64_t>(tvDoubleView);
        if (unlikely(i64 < 0 || i64 > 255))
        {
            i64 = TryConvertValueToStringCharNumericalCode(tvDoubleView);
            if (unlikely(i64 < 0))
            {
                if (i64 == -1)
                {
                    ss.Destroy();
                    ThrowError("bad argument to 'char' (number expected)");
                }
                else
                {
                    assert(i64 == -2);
                    ss.Destroy();
                    ThrowError("bad argument to 'char' (invalid value)");
                }
            }
        }
        assert(0 <= i64 && i64 <= 255);
        ptr[i] = static_cast<uint8_t>(i64);
    }
    ptr[numArgs] = 0;

    HeapString* res = VM::GetActiveVMForCurrentThread()->CreateStringObjectFromRawString(ptr, static_cast<uint32_t>(numArgs) /*len*/).As();
    ss.Destroy();
    Return(TValue::Create<tString>(res));
}

// string.dump -- https://www.lua.org/manual/5.1/manual.html#pdf-string.dump
//
// string.dump (function)
// Returns a string containing a binary representation of the given function, so that a later loadstring on this string returns a copy
// of the function. function must be a Lua function without upvalues.
//
DEEGEN_DEFINE_LIB_FUNC(string_dump)
{
    ThrowError("Library function 'string.dump' is not implemented yet!");
}

// string.find -- https://www.lua.org/manual/5.1/manual.html#pdf-string.find
//
// string.find (s, pattern [, init [, plain]])
// Looks for the first match of pattern in the string s. If it finds a match, then find returns the indices of s where this occurrence
// starts and ends; otherwise, it returns nil. A third, optional numerical argument init specifies where to start the search; its default
// value is 1 and can be negative. A value of true as a fourth, optional argument plain turns off the pattern matching facilities, so the
// function does a plain "find substring" operation, with no characters in pattern being considered "magic". Note that if plain is given,
// then init must be given as well.
//
// If the pattern has captures, then in a successful match the captured values are also returned, after the two indices.
//
DEEGEN_DEFINE_LIB_FUNC(string_find)
{
    ThrowError("Library function 'string.find' is not implemented yet!");
}

// string.format -- https://www.lua.org/manual/5.1/manual.html#pdf-string.format
//
// string.format (formatstring, ···)
// Returns a formatted version of its variable number of arguments following the description given in its first argument (which must be
// a string). The format string follows the same rules as the printf family of standard C functions. The only differences are that the
// options/modifiers *, l, L, n, p, and h are not supported and that there is an extra option, q. The q option formats a string in a form
// suitable to be safely read back by the Lua interpreter: the string is written between double quotes, and all double quotes, newlines,
// embedded zeros, and backslashes in the string are correctly escaped when written. For instance, the call
//
//     string.format('%q', 'a string with "quotes" and \n new line')
// will produce the string:
//     "a string with \"quotes\" and \
//     new line"
//
// The options c, d, E, e, f, g, G, i, o, u, X, and x all expect a number as argument, whereas q and s expect a string.
//
// This function does not accept string values containing embedded zeros, except as arguments to the q option.
//
DEEGEN_DEFINE_LIB_FUNC(string_format)
{
    size_t numArgs = GetNumArgs();
    if (unlikely(numArgs == 0))
    {
        ThrowError("bad argument #1 to 'format' (string expected, got no value)");
    }

    VM* vm = VM::GetActiveVMForCurrentThread();
    TValue* sb = GetStackBase();
    GET_ARG_AS_STRING(format, 1, fmt, fmtLen);

    SimpleTempStringStream ss;
    StrFmtError resKind = StringFormatterWithLuaSemantics(&ss /*out*/, fmt, fmtLen, sb + 1 /*argBegin*/, numArgs - 1);
    if (likely(resKind == StrFmtNoError))
    {
        HeapString* s = vm->CreateStringObjectFromRawString(ss.m_bufferBegin, static_cast<uint32_t>(ss.m_bufferCur - ss.m_bufferBegin)).As();
        ss.Destroy();
        Return(TValue::Create<tString>(s));
    }

    ss.Destroy();

    switch (resKind)
    {
    case StrFmtError_BadFmt:
        ThrowError("bad format string to 'format'");
    case StrFmtError_TooFewArgs:
        ThrowError("not enough arguments for format string");
    case StrFmtError_NotNumber:
        ThrowError("bad argument to 'format' (number expected)");
    case StrFmtError_NotString:
        ThrowError("bad argument to 'format' (string expected)");
    case StrFmtNoError:
        assert(false);
        __builtin_unreachable();
    }   /* switch resKind */
}

// string.gmatch -- https://www.lua.org/manual/5.1/manual.html#pdf-string.gmatch
//
// string.gmatch (s, pattern)
// Returns an iterator function that, each time it is called, returns the next captures from pattern over string s. If pattern specifies
// no captures, then the whole match is produced in each call.
//
// As an example, the following loop
//     s = "hello world from Lua"
//     for w in string.gmatch(s, "%a+") do
//         print(w)
//     end
// will iterate over all the words from string s, printing one per line.

// The next example collects all pairs key=value from the given string into a table:
//     t = {}
//     s = "from=world, to=Lua"
//     for k, v in string.gmatch(s, "(%w+)=(%w+)") do
//         t[k] = v
//     end
// For this function, a '^' at the start of a pattern does not work as an anchor, as this would prevent the iteration.
//
DEEGEN_DEFINE_LIB_FUNC(string_gmatch)
{
    ThrowError("Library function 'string.gmatch' is not implemented yet!");
}

// string.gsub -- https://www.lua.org/manual/5.1/manual.html#pdf-string.gsub
//
// string.gsub (s, pattern, repl [, n])
// Returns a copy of s in which all (or the first n, if given) occurrences of the pattern have been replaced by a replacement string
// specified by repl, which can be a string, a table, or a function. gsub also returns, as its second value, the total number of
// matches that occurred.
//
// If repl is a string, then its value is used for replacement. The character % works as an escape character: any sequence in repl of
// the form %n, with n between 1 and 9, stands for the value of the n-th captured substring (see below). The sequence %0 stands for
// the whole match. The sequence %% stands for a single %.
//
// If repl is a table, then the table is queried for every match, using the first capture as the key; if the pattern specifies no captures,
// then the whole match is used as the key.
//
// If repl is a function, then this function is called every time a match occurs, with all captured substrings passed as arguments,
// in order; if the pattern specifies no captures, then the whole match is passed as a sole argument.
//
// If the value returned by the table query or by the function call is a string or a number, then it is used as the replacement string;
// otherwise, if it is false or nil, then there is no replacement (that is, the original match is kept in the string).
//
// Here are some examples:
//
//     x = string.gsub("hello world", "(%w+)", "%1 %1")
//     --> x="hello hello world world"
//
//     x = string.gsub("hello world", "%w+", "%0 %0", 1)
//     --> x="hello hello world"
//
//     x = string.gsub("hello world from Lua", "(%w+)%s*(%w+)", "%2 %1")
//     --> x="world hello Lua from"
//
//     x = string.gsub("home = $HOME, user = $USER", "%$(%w+)", os.getenv)
//     --> x="home = /home/roberto, user = roberto"
//
//     x = string.gsub("4+5 = $return 4+5$", "%$(.-)%$", function (s)
//           return loadstring(s)()
//         end)
//     --> x="4+5 = 9"
//
//     local t = {name="lua", version="5.1"}
//     x = string.gsub("$name-$version.tar.gz", "%$(%w+)", t)
//     --> x="lua-5.1.tar.gz"
//
DEEGEN_DEFINE_LIB_FUNC(string_gsub)
{
    ThrowError("Library function 'string.gsub' is not implemented yet!");
}

// string.len -- https://www.lua.org/manual/5.1/manual.html#pdf-string.len
//
// string.len (s)
// Receives a string and returns its length. The empty string "" has length 0. Embedded zeros are counted, so "a\000bc\000" has length 5.
//
DEEGEN_DEFINE_LIB_FUNC(string_len)
{
    if (unlikely(GetNumArgs() == 0))
    {
        ThrowError("bad argument #1 to 'len' (string expected, got no value)");
    }
    GET_ARG_AS_STRING(len, 1, ptr, length);
    std::ignore = ptr;
    Return(TValue::Create<tDouble>(static_cast<double>(length)));
}

// Perform the simple 'toupper' or 'tolower' that just changes 'a-z' to 'A-Z' or vice versa.
// lb must be _mm_set1_epi8(0x60) for 'toupper' or _mm_set1_epi8(0x40) for 'tolower'
// ub must be _mm_set1_epi8(0x7b) for 'toupper' or _mm_set1_epi8(0x5b) for 'tolower'
// msk must be _mm_set1_epi8(0x20)
//
static __m128i ALWAYS_INLINE AlphabeticalToUpperOrLowerSimd(__m128i lb, __m128i ub, __m128i msk, __m128i input)
{
    __m128i x1 = _mm_cmpgt_epi8(input, lb);
    __m128i x2 = _mm_cmplt_epi8(input, ub);
    __m128i x3 = _mm_and_si128(x1, x2);
    __m128i x4 = _mm_and_si128(msk, x3);
    __m128i x5 = _mm_xor_si128(input, x4);
    return x5;
}

// The behavior of toupper/tolower is locale dependent so we cannot simply change 'a-z' to 'A-Z' (or vice versa)
// However, we can provide a fastpath for common locales where the rule is indeed changing 'a-z' to 'A-Z',
// and a fastpath for the shared common case where the char code is < 128 (in which the locale does not affect the behavior).
//
template<bool isToUpper>
static void ALWAYS_INLINE FastToUpperOrLower(const char* ptrIn, size_t length, char* ptrOut /*out*/)
{
    constexpr auto stdImplFn = isToUpper ? toupper : tolower;

    // Only attempt to use the optimized implementation if the string is at least 16 bytes, as we need to
    // check the locale, and initialize some SIMD registers...
    // This also handles the edge case, so our vectorized implementation can safely assume length >= 16
    //
    if (length >= 16)
    {
        // When 'nullptr' is passed in to 'setlocale', it only returns the current locale
        //
        const char* locale = std::setlocale(LC_CTYPE, nullptr /*queryLocale*/);

        // Check for the good case: locale is 'C' or 'en_US.UTF-8', where toupper/tolower has no bizzare behaviors.
        //
        bool isSimpleLocale = (locale[0] == 'C' && locale[1] == '\0') || strcmp(locale, "en_US.UTF-8") == 0;

        __m128i lb = _mm_set1_epi8(isToUpper ? 0x60 : 0x40);   // 'a'/'A' - 1
        __m128i ub = _mm_set1_epi8(isToUpper ? 0x7b : 0x5b);   // 'z'/'Z' + 1
        __m128i msk = _mm_set1_epi8(0x20);

        if (likely(isSimpleLocale))
        {
            // The rule is to simply change 'a-z' to 'A-Z'
            //
            const char* src = ptrIn;
            const char* end = ptrIn + length;
            char* dst = ptrOut;
            while (src + 16 <= end)
            {
                __m128i input = _mm_loadu_si128(reinterpret_cast<const __m128i*>(src));
                __m128i res = AlphabeticalToUpperOrLowerSimd(lb, ub, msk, input);
                _mm_storeu_si128(reinterpret_cast<__m128i*>(dst), res);
                src += 16;
                dst += 16;
            }
            // This is correct since the string is at least 16 bytes
            //
            if (src < end)
            {
                __m128i input = _mm_loadu_si128(reinterpret_cast<const __m128i*>(end - 16));
                __m128i res = AlphabeticalToUpperOrLowerSimd(lb, ub, msk, input);
                dst = ptrOut + length - 16;
                _mm_storeu_si128(reinterpret_cast<__m128i*>(dst), res);
            }
        }
        else
        {
            // We do not recognize the locale, but locale cannot change the toupper/tolower behavior for char code < 128.
            // So we check this and use fastpath if possible.
            //
            const char* src = ptrIn;
            const char* end = ptrIn + length;
            char* dst = ptrOut;
            while (src + 16 <= end)
            {
                __m128i input = _mm_loadu_si128(reinterpret_cast<const __m128i*>(src));
                // Collect the MSB of each byte. If 'mask == 0', we know all characters are < 128
                //
                int mask = _mm_movemask_epi8(input);
                if (likely(mask == 0))
                {
                    __m128i res = AlphabeticalToUpperOrLowerSimd(lb, ub, msk, input);
                    _mm_storeu_si128(reinterpret_cast<__m128i*>(dst), res);
                }
                else
                {
                    // Just call std toupper/tolower
                    //
                    for (size_t i = 0; i < 16; i++)
                    {
                        dst[i] = static_cast<char>(stdImplFn(static_cast<unsigned char>(src[i])));
                    }
                }
                src += 16;
                dst += 16;
            }
            // This is correct since the string is at least 16 bytes
            //
            if (src < end)
            {
                __m128i input = _mm_loadu_si128(reinterpret_cast<const __m128i*>(end - 16));
                // Collect the MSB of each byte. If 'mask == 0', we know all characters are < 128
                //
                int mask = _mm_movemask_epi8(input);
                if (likely(mask == 0))
                {
                    __m128i res = AlphabeticalToUpperOrLowerSimd(lb, ub, msk, input);
                    dst = ptrOut + length - 16;
                    _mm_storeu_si128(reinterpret_cast<__m128i*>(dst), res);
                }
                else
                {
                    while (src < end)
                    {
                        *dst = static_cast<char>(stdImplFn(static_cast<unsigned char>(*src)));
                        src++;
                        dst++;
                    }
                }
            }
        }
        // In debug mode, assert that our optimized implementation produced the same result as std toupper/tolower
        //
#ifndef NDEBUG
        for (size_t i = 0; i < length; i++)
        {
            assert(ptrOut[i] == static_cast<char>(stdImplFn(static_cast<unsigned char>(ptrIn[i]))));
        }
#endif
    }
    else
    {
        for (size_t i = 0; i < length; i++)
        {
            ptrOut[i] = static_cast<char>(stdImplFn(static_cast<unsigned char>(ptrIn[i])));
        }
    }
}

// string.lower -- https://www.lua.org/manual/5.1/manual.html#pdf-string.lower
//
// string.lower (s)
// Receives a string and returns a copy of this string with all uppercase letters changed to lowercase. All other characters are left
// unchanged. The definition of what an uppercase letter is depends on the current locale.
//
DEEGEN_DEFINE_LIB_FUNC(string_lower)
{
    size_t numArgs = GetNumArgs();
    if (unlikely(numArgs == 0))
    {
        ThrowError("bad argument #1 to 'lower' (string expected, got no value)");
    }

    GET_ARG_AS_STRING(lower, 1, ptr, length);

    SimpleTempStringStream ss;
    char* buf = ss.Reserve(length);

    FastToUpperOrLower<false /*isToUpper*/>(ptr /*in*/, length, buf /*out*/);

    VM* vm = VM::GetActiveVMForCurrentThread();
    HeapString* res = vm->CreateStringObjectFromRawString(buf, static_cast<uint32_t>(length)).As();
    ss.Destroy();
    Return(TValue::Create<tString>(res));
}

// string.match -- https://www.lua.org/manual/5.1/manual.html#pdf-string.match
//
// string.match (s, pattern [, init])
// Looks for the first match of pattern in the string s. If it finds one, then match returns the captures from the pattern; otherwise
// it returns nil. If pattern specifies no captures, then the whole match is returned. A third, optional numerical argument init specifies
// where to start the search; its default value is 1 and can be negative.
//
DEEGEN_DEFINE_LIB_FUNC(string_match)
{
    ThrowError("Library function 'string.match' is not implemented yet!");
}

// string.rep -- https://www.lua.org/manual/5.1/manual.html#pdf-string.rep
//
// string.rep (s, n)
// Returns a string that is the concatenation of n copies of the string s.
//
DEEGEN_DEFINE_LIB_FUNC(string_rep)
{
    size_t numArgs = GetNumArgs();
    if (unlikely(numArgs < 2))
    {
        ThrowError("bad argument #2 to 'rep' (number expected, got no value)");
    }

    VM* vm = VM::GetActiveVMForCurrentThread();
    GET_ARG_AS_STRING(rep, 1, inputStr, inputStrLen);

    auto [success, numCopiesDbl] = LuaLib_ToNumber(GetArg(1));
    if (unlikely(!success))
    {
        ThrowError("bad argument #2 to 'rep' (number expected)");
    }

    int64_t numCopies = static_cast<int64_t>(numCopiesDbl);
    if (unlikely(numCopies <= 0))
    {
        Return(TValue::Create<tString>(vm->m_emptyString));
    }

    HeapString* res = vm->CreateStringObjectFromConcatenationOfSameString(inputStr, static_cast<uint32_t>(inputStrLen), static_cast<size_t>(numCopies)).As();
    Return(TValue::Create<tString>(res));
}

// string.reverse -- https://www.lua.org/manual/5.1/manual.html#pdf-string.reverse
//
// string.reverse (s)
// Returns a string that is the string s reversed.
//
DEEGEN_DEFINE_LIB_FUNC(string_reverse)
{
    if (unlikely(GetNumArgs() == 0))
    {
        ThrowError("bad argument #1 to 'reverse' (string expected, got no value)");
    }
    GET_ARG_AS_STRING(reverse, 1, ptr, length);

    VM* vm = VM::GetActiveVMForCurrentThread();
    if (length == 0)
    {
        Return(TValue::Create<tString>(vm->m_emptyString));
    }

    SimpleTempStringStream ss;
    char* out = ss.Reserve(length);

    if (length <= 8)
    {
        // Since we always allocate 8-byte-aligned memory, the string starts at 8-byte-aligned address in HeapString,
        // and the string is non-empty (we checked length == 0 case above), 8 bytes following the string is always
        // dereferenceable even if string is less than 8 bytes long.
        //
        uint64_t originalVal = UnalignedLoad<uint64_t>(ptr);
        uint64_t reversedVal = __builtin_bswap64(originalVal);
        // The useful bytes are at the low bytes (due to little-endianness), and after bswap they go to the high bytes.
        // Shift them to low bytes so we can store it as the result.
        //
        reversedVal = reversedVal >> (64 - length * 8);
        UnalignedStore<uint64_t>(out, reversedVal);
    }
    else
    {
        // Byte swap and store all the 8-byte chunks
        //
        const char* src = ptr;
        const char* end = ptr + length;
        char* dst = out + length;
        while (src + 8 <= end)
        {
            uint64_t originalVal = UnalignedLoad<uint64_t>(src);
            uint64_t reversedVal = __builtin_bswap64(originalVal);
            dst -= 8;
            UnalignedStore<uint64_t>(dst, reversedVal);
            src += 8;
        }
        // For the remaining tail, we read 8 bytes from 'end - 8', reverse it and store to the beginning.
        // This is correct because length >= 8
        //
        if (src < end)
        {
            uint64_t originalVal = UnalignedLoad<uint64_t>(end - 8);
            uint64_t reversedVal = __builtin_bswap64(originalVal);
            UnalignedStore<uint64_t>(out, reversedVal);
        }
    }

    HeapString* res = vm->CreateStringObjectFromRawString(out, static_cast<uint32_t>(length)).As();
    ss.Destroy();
    Return(TValue::Create<tString>(res));
}

// string.sub -- https://www.lua.org/manual/5.1/manual.html#pdf-string.sub
//
// string.sub (s, i [, j])
// Returns the substring of s that starts at i and continues until j; i and j can be negative. If j is absent, then it is assumed to be
// equal to -1 (which is the same as the string length). In particular, the call string.sub(s,1,j) returns a prefix of s with length j,
// and string.sub(s, -i) returns a suffix of s with length i.
//
DEEGEN_DEFINE_LIB_FUNC(string_sub)
{
    size_t numArgs = GetNumArgs();
    if (unlikely(numArgs == 0))
    {
        ThrowError("bad argument #1 to 'sub' (string expected, got no value)");
    }

    VM* vm = VM::GetActiveVMForCurrentThread();
    GET_ARG_AS_STRING(sub, 1, ptr, ulen);

    int64_t lb;
    if (numArgs == 1)
    {
        lb = 1;
    }
    else
    {
        TValue tvLb = GetArg(1);
        auto [success, val] = LuaLib_ToNumber(tvLb);
        if (unlikely(!success))
        {
            ThrowError("bad argument #2 to 'sub' (number expected)");
        }
        lb = static_cast<int64_t>(val);
    }

    int64_t ub;
    if (numArgs < 3)
    {
        ub = -1;
    }
    else
    {
        TValue tvUb = GetArg(2);
        auto [success, val] = LuaLib_ToNumber(tvUb);
        if (unlikely(!success))
        {
            ThrowError("bad argument #3 to 'sub' (number expected)");
        }
        ub = static_cast<int64_t>(val);
    }

    int64_t len = static_cast<int64_t>(ulen);
    if (ub < 0) { ub += len + 1; }
    if (lb < 0) { lb += len + 1; }
    if (lb <= 0) { lb = 1; }
    if (ub > len) { ub = len; }

    if (unlikely(lb > ub))
    {
        Return(TValue::Create<tString>(vm->m_emptyString));
    }
    else
    {
        Return(TValue::Create<tString>(vm->CreateStringObjectFromRawString(ptr + lb - 1, static_cast<uint32_t>(ub - lb + 1)).As()));
    }
}

// string.upper -- https://www.lua.org/manual/5.1/manual.html#pdf-string.upper
//
// string.upper (s)
// Receives a string and returns a copy of this string with all lowercase letters changed to uppercase. All other characters are left
// unchanged. The definition of what a lowercase letter is depends on the current locale.
//
DEEGEN_DEFINE_LIB_FUNC(string_upper)
{
    size_t numArgs = GetNumArgs();
    if (unlikely(numArgs == 0))
    {
        ThrowError("bad argument #1 to 'upper' (string expected, got no value)");
    }

    GET_ARG_AS_STRING(upper, 1, ptr, length);

    SimpleTempStringStream ss;
    char* buf = ss.Reserve(length);

    FastToUpperOrLower<true /*isToUpper*/>(ptr /*in*/, length, buf /*out*/);

    VM* vm = VM::GetActiveVMForCurrentThread();
    HeapString* res = vm->CreateStringObjectFromRawString(buf, static_cast<uint32_t>(length)).As();
    ss.Destroy();
    Return(TValue::Create<tString>(res));
}

DEEGEN_END_LIB_FUNC_DEFINITIONS
