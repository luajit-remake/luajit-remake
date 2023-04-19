#pragma once

#include "common_utils.h"
#include "misc_llvm_helper.h"
#include "deegen_bytecode_metadata.h"
#include "tvalue.h"

namespace dast {

class InterpreterBytecodeImplCreator;
class DeegenBytecodeImplCreatorBase;
class BaselineJitImplCreator;

class InterpreterCallIcMetadata
{
public:
    InterpreterCallIcMetadata()
        : m_icStruct(nullptr)
        , m_cachedTValue(nullptr)
        , m_cachedCodePointer(nullptr)
    { }

    bool IcExists()
    {
        return m_icStruct != nullptr;
    }

    // Note that we always cache the TValue of the callee, not the function object pointer,
    // this is because we want to hoist the IC check to eliminate user's the IsFunction check if possible.
    //
    // In our boxing scheme, the TValue and the HeapPtr<FunctionObject> have the same bit pattern,
    // so the two do not have any difference. However, hypothetically, in a different boxing scheme,
    // this would make a difference.
    //
    BytecodeMetadataElement* GetCachedTValue()
    {
        ReleaseAssert(IcExists());
        return m_cachedTValue;
    }

    BytecodeMetadataElement* GetCachedCodePointer()
    {
        ReleaseAssert(IcExists());
        return m_cachedCodePointer;
    }

    static std::pair<std::unique_ptr<BytecodeMetadataStruct>, InterpreterCallIcMetadata> WARN_UNUSED Create()
    {
        std::unique_ptr<BytecodeMetadataStruct> s = std::make_unique<BytecodeMetadataStruct>();
        BytecodeMetadataElement* cachedFn = s->AddElement(1 /*alignment*/, sizeof(TValue) /*size*/);
        BytecodeMetadataElement* codePtr = s->AddElement(1 /*alignment*/, sizeof(void*) /*size*/);
        cachedFn->SetInitValue(TValue::CreateImpossibleValue().m_value);
        InterpreterCallIcMetadata r;
        r.m_icStruct = s.get();
        r.m_cachedTValue = cachedFn;
        r.m_cachedCodePointer = codePtr;
        return std::make_pair(std::move(s), r);
    }

private:
    BytecodeMetadataStruct* m_icStruct;
    BytecodeMetadataElement* m_cachedTValue;
    BytecodeMetadataElement* m_cachedCodePointer;
};

struct DeegenCallIcLogicCreator
{
    // Emit generic logic that always works but is rather slow
    //
    static void EmitGenericGetCallTargetLogic(
        DeegenBytecodeImplCreatorBase* ifi,
        llvm::Value* functionObject,
        llvm::Value*& calleeCbHeapPtr /*out*/,
        llvm::Value*& codePointer /*out*/,
        llvm::Instruction* insertBefore);

    // Emit logic for interpreter, employing Call IC if possible
    //
    static void EmitForInterpreter(
        InterpreterBytecodeImplCreator* ifi,
        llvm::Value* functionObject,
        llvm::Value*& calleeCbHeapPtr /*out*/,
        llvm::Value*& codePointer /*out*/,
        llvm::Instruction* insertBefore);

    // The baseline JIT lowering splits one MakeCall into multiple paths for IC
    // Each path is described by the struct below
    //
    struct BaselineJitLoweringResult
    {
        llvm::Value* calleeCbHeapPtr;
        llvm::Value* codePointer;
        // The 'MakeCall' API in the BB for this path
        //
        llvm::Instruction* origin;
    };

    // Emit logic for baselineJIT, employing Call IC if possible
    // Note that after the call, the passed-in 'origin' is invalidated! One should instead look at
    // the 'origin' fields in the returned vector.
    //
    static std::vector<BaselineJitLoweringResult> WARN_UNUSED EmitForBaselineJIT(
        BaselineJitImplCreator* ifi,
        llvm::Value* functionObject,
        uint64_t unique_ord,
        llvm::Instruction* origin);
};

}   // namespace dast
