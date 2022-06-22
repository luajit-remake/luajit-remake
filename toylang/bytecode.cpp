#include "bytecode.h"

namespace ToyLang
{

using namespace CommonUtils;

const InterpreterFn x_interpreter_dispatches[x_numOpcodes] = {
#define macro(opcodeCppName) &opcodeCppName::Execute,
    PP_FOR_EACH(macro, OPCODE_LIST)
#undef macro
};

}   // namespace ToyLang
