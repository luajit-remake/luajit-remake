#pragma once

#include "common.h"
#include "runtime_utils.h"

struct ParseResult
{
    std::unique_ptr<ScriptModule> m_scriptModule;
    TValue errMsg;
};

using lua_Reader = const char*(*)(CoroutineRuntimeContext*, void*, size_t*);

void lj_lex_init(VM* vm);
ParseResult WARN_UNUSED ParseLuaScript(CoroutineRuntimeContext* ctx, lua_Reader rd, void* ud);

// Parse Lua script from the specified string
//
ParseResult WARN_UNUSED ParseLuaScript(CoroutineRuntimeContext* ctx, const std::string& str);
ParseResult WARN_UNUSED ParseLuaScript(CoroutineRuntimeContext* ctx, const char* data, size_t length);

// Parse Lua script obtained by tab[1] .. tab[length]
// Each TValue must be a string
//
ParseResult WARN_UNUSED ParseLuaScript(CoroutineRuntimeContext* ctx, HeapPtr<TableObject> tab, uint32_t length);

ParseResult WARN_UNUSED ParseLuaScriptFromFile(CoroutineRuntimeContext* ctx, const char* fileName);

