#pragma once

#include "common.h"
#include "tvalue.h"
#include "simple_string_stream.h"

enum StrFmtError
{
    StrFmtNoError,
    StrFmtError_BadFmt,
    StrFmtError_TooFewArgs,
    StrFmtError_NotNumber,
    StrFmtError_NotString
};

// Implementation for Lua string.format
//
StrFmtError WARN_UNUSED StringFormatterWithLuaSemantics(SimpleTempStringStream* sb, const char* fmt, size_t fmtLen, TValue* argBegin, size_t narg);
