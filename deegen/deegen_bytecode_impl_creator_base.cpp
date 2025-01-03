#include "deegen_bytecode_impl_creator_base.h"
#include "deegen_interpreter_bytecode_impl_creator.h"
#include "deegen_baseline_jit_impl_creator.h"
#include "deegen_dfg_jit_impl_creator.h"
#include "misc_llvm_helper.h"

namespace dast {

InterpreterBytecodeImplCreator* DeegenBytecodeImplCreatorBase::AsInterpreter()
{
    ReleaseAssert(IsInterpreter());
    return static_cast<InterpreterBytecodeImplCreator*>(this);
}

BaselineJitImplCreator* DeegenBytecodeImplCreatorBase::AsBaselineJIT()
{
    ReleaseAssert(IsBaselineJIT());
    return static_cast<BaselineJitImplCreator*>(this);
}

DfgJitImplCreator* DeegenBytecodeImplCreatorBase::AsDfgJIT()
{
    ReleaseAssert(IsDfgJIT());
    return static_cast<DfgJitImplCreator*>(this);
}

DeegenBytecodeImplCreatorBase::DeegenBytecodeImplCreatorBase(DeegenBytecodeImplCreatorBase* other)
    : DeegenBytecodeImplCreatorBase(other->m_bytecodeDef, other->m_processKind)
{
    using namespace llvm;
    ReleaseAssert(other->m_module != nullptr);
    m_module = CloneModule(*other->m_module.get());
    other->m_valuePreserver.Clone(m_valuePreserver /*out*/, m_module.get() /*newModule*/);
    if (other->m_execFnContext.get() != nullptr)
    {
        m_execFnContext = other->m_execFnContext->Clone(m_module.get() /*newModule*/);
    }
}

}   // namespace dast
