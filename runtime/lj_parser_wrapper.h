#pragma once

#include "common.h"
#include "runtime_utils.h"

struct ParseResult
{
    std::unique_ptr<ScriptModule> m_scriptModule;
    int errorCode;
    int errorTok;
    const char* errMsg;
};

using lua_Reader = const char*(*)(CoroutineRuntimeContext*, void*, size_t*);

void lj_lex_init(VM* vm);
ParseResult WARN_UNUSED ParseLuaScript(CoroutineRuntimeContext* ctx, lua_Reader rd, void* ud);
ParseResult WARN_UNUSED ParseLuaScript(CoroutineRuntimeContext* ctx, const std::string& str);
