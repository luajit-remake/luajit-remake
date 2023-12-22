#include "api_define_bytecode.h"
#include "deegen_api.h"

#include "runtime_utils.h"

static void NO_RETURN NewClosureImpl(TValue tvucb)
{
    // This is a bit hacky but 'tvucb' is always a pointer in the constant table, disguised as a TValue..
    //
    UnlinkedCodeBlock* ucb = reinterpret_cast<UnlinkedCodeBlock*>(tvucb.m_value);
    CodeBlock* cb = ucb->GetCodeBlock(GetFEnvGlobalObject());
    HeapPtr<FunctionObject> func = CreateNewClosure(cb, GetOutputBytecodeSlotOrdinal());
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
    DeclareAsIntrinsic<Intrinsic::CreateClosure>({
        .proto = Op("unlinkedCb")
    });
}

DEEGEN_END_BYTECODE_DEFINITIONS
