#include "bytecode_definition_utils.h"
#include "deegen_api.h"

#include "bytecode.h"

static void NO_RETURN NewClosureImpl(TValue tvucb)
{
    // This is a bit hacky but 'tvucb' is always a pointer in the constant table, disguised as a TValue..
    //
    UnlinkedCodeBlock* ucb = reinterpret_cast<UnlinkedCodeBlock*>(tvucb.m_value);
    CodeBlock* cb = UnlinkedCodeBlock::GetCodeBlock(ucb, GetFEnvGlobalObject());
    HeapPtr<FunctionObject> func = CreateNewClosure(cb);
    Return(TValue::Create<tFunction>(func));
}

DEEGEN_DEFINE_BYTECODE(NewClosure)
{
    Operands(
        Constant("unlinkedCb")
    );
    Result(BytecodeValue);
    Implementation(NewClosureImpl);
    Variant();
}

DEEGEN_END_BYTECODE_DEFINITIONS
