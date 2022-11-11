#include "deegen_api.h"
#include "runtime_utils.h"

// package.loadlib -- https://www.lua.org/manual/5.1/manual.html#pdf-package.loadlib
//
// package.loadlib (libname, funcname)
// Dynamically links the host program with the C library libname. Inside this library, looks for a function funcname and returns this
// function as a C function. (So, funcname must follow the protocol (see lua_CFunction)).
//
// This is a low-level function. It completely bypasses the package and module system. Unlike require, it does not perform any path
// searching and does not automatically adds extensions. libname must be the complete file name of the C library, including if necessary
// a path and extension. funcname must be the exact name exported by the C library (which may depend on the C compiler and linker used).
//
// This function is not supported by ANSI C. As such, it is only available on some platforms (Windows, Linux, Mac OS X, Solaris, BSD,
// plus other Unix systems that support the dlfcn standard).
//
DEEGEN_DEFINE_LIB_FUNC(package_loadlib)
{
    ThrowError("Library function 'package.loadlib' is not implemented yet!");
}

// package.seeall -- https://www.lua.org/manual/5.1/manual.html#pdf-package.seeall
//
// package.seeall (module)
// Sets a metatable for module with its __index field referring to the global environment, so that this module inherits values from the
// global environment. To be used as an option to function module.
//
DEEGEN_DEFINE_LIB_FUNC(package_seeall)
{
    ThrowError("Library function 'package.seeall' is not implemented yet!");
}

DEEGEN_END_LIB_FUNC_DEFINITIONS
