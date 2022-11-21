Before we integrate LuaJIT's frontend into the source tree, the VM takes a hacky JSON-format bytecode dump as input. 

This legacy tool 'ljfrontend' is a grossly hacked LuaJIT that generates this JSON-format bytecode dump.

You can run './ljfrontend input.lua > input.lua.json' to generate the dump for a Lua file, 
and use ScriptModule::LegacyParseScriptFromJSONBytecodeDump to parse the dump.

This is no longer needed normally, and is only kept here for debugging purposes, in case the integration introduced a parser bug.

