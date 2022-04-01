#include "bytecode.h"

namespace ToyLang
{

using namespace CommonUtils;

const InterpreterFn x_interpreter_dispatches[static_cast<size_t>(Opcode::X_END_OF_ENUM)] = {
        BcReturn::Execute,
        BcCall::Execute,
        BcAddVV::Execute,
        BcSubVV::Execute,
        BcIsLTVV::Execute
};

}   // namespace ToyLang
