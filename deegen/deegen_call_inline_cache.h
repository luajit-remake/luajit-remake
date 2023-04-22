#pragma once

#include "common_utils.h"
#include "misc_llvm_helper.h"
#include "deegen_bytecode_metadata.h"
#include "tvalue.h"
#include "deegen_parse_asm_text.h"

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
    struct BaselineJitLLVMLoweringResult
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
    static std::vector<BaselineJitLLVMLoweringResult> WARN_UNUSED EmitForBaselineJIT(
        BaselineJitImplCreator* ifi,
        llvm::Value* functionObject,
        uint64_t unique_ord,
        llvm::Instruction* origin);

    struct BaselineJitAsmLoweringResult
    {
        // The label for the ASM block that contains the patchable jump
        // The block must contain exactly 1 instruction: jmp ic_miss_slowpath
        //
        std::string m_labelForPatchableJump;
        // The entry label for the direct call IC hit logic
        //
        std::string m_labelForDirectCallIc;
        // The entry label for the closure call IC hit logic
        //
        std::string m_labelForClosureCallIc;
        // Label for the slow path
        // The patchable jump always jumps to here initially
        //
        std::string m_labelForIcMissLogic;
        // The unique ordinal of this call IC, always corresponds to the ordinal passed to EmitForBaselineJIT()
        //
        uint64_t m_uniqueOrd;
    };

    // See comments in CPP file
    //
    static std::vector<BaselineJitAsmLoweringResult> WARN_UNUSED DoBaselineJitAsmLowering(X64AsmFile* file);
};

}   // namespace dast
