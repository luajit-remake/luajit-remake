#pragma once

#include "common_utils.h"
#include "misc_llvm_helper.h"
#include "deegen_bytecode_metadata.h"
#include "tvalue.h"
#include "deegen_parse_asm_text.h"
#include "deegen_stencil_creator.h"
#include "deegen_global_bytecode_trait_accessor.h"
#include "deegen_options.h"

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

    BytecodeMetadataElement* GetDoublyLink()
    {
        ReleaseAssert(IcExists());
        ReleaseAssert(m_doublyLink != nullptr);
        return m_doublyLink;
    }

    static std::pair<std::unique_ptr<BytecodeMetadataStruct>, InterpreterCallIcMetadata> WARN_UNUSED Create()
    {
        std::unique_ptr<BytecodeMetadataStruct> s = std::make_unique<BytecodeMetadataStruct>();
        BytecodeMetadataElement* cachedFn = s->AddElement(1 /*alignment*/, sizeof(TValue) /*size*/);
        cachedFn->SetInitValue(TValue::CreateImpossibleValue().m_value);

        // DEVNOTE: this is fragile, but we currently rely on the layout that codePtr is right before doublyLink
        //
        BytecodeMetadataElement* codePtr = s->AddElement(1 /*alignment*/, sizeof(void*) /*size*/);

        BytecodeMetadataElement* doublyLink = nullptr;
        if (x_allow_interpreter_tier_up_to_baseline_jit)
        {
            // Ugly: we do not want to do a check when updating the doubly link,
            // so we want to make the doubly link point to itself instead of storing nil.
            // However, we only support initialization of constant values,
            // so we cannot initialize the doubly link to point to itself.
            //
            // So here, we make the doubly link store the offset from 'self'...
            //
            // TODO: it is highly questionable whether interpreter call IC has any benefit in non-monomorphic
            // mode after all of these overheads.. We should rethink if we should just disable interpreter call IC
            // altogether when the JIT is enabled.
            //
            doublyLink = s->AddElement(1 /*alignment*/, 8 /*size*/);
            doublyLink->SetInitValue<uint64_t>(0);
        }

        InterpreterCallIcMetadata r;
        r.m_icStruct = s.get();
        r.m_cachedTValue = cachedFn;
        r.m_cachedCodePointer = codePtr;
        r.m_doublyLink = doublyLink;
        return std::make_pair(std::move(s), r);
    }

private:
    BytecodeMetadataStruct* m_icStruct;
    BytecodeMetadataElement* m_cachedTValue;
    BytecodeMetadataElement* m_cachedCodePointer;
    BytecodeMetadataElement* m_doublyLink;
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

    // The baseline JIT lowering splits one MakeCall into multiple paths for different IC cases
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

    struct JitAsmTransformResult
    {
        // The label for the SMC region
        //
        std::string m_labelForSMCRegion;
        // The entry label for the direct call IC hit logic
        //
        std::string m_labelForDirectCallIc;
        // The entry label for the closure call IC hit logic
        //
        std::string m_labelForClosureCallIc;
        // Label for the slow path
        // The patchable jump always jumps to here initially
        //
        std::string m_labelForCcIcMissLogic;
        std::string m_labelForDcIcMissLogic;
        // The unique ordinal of this call IC, always corresponds to the ordinal passed to EmitForBaselineJIT()
        //
        uint64_t m_uniqueOrd;

        // Special symbols which stores the results of label offset / distance computation (see EmitComputeLabelOffsetAndLengthSymbol)
        //
        std::string m_symbolNameForSMCLabelOffset;
        std::string m_symbolNameForSMCRegionLength;
        std::string m_symbolNameForCcIcMissLogicLabelOffset;
        std::string m_symbolNameForDcIcMissLogicLabelOffset;

        void FixupSMCRegionAfterCFGAnalysis(X64AsmFile* file);

        // We need to know the offset of certain labels / distance between certain labels in the machine code, specifically:
        // 1. The machine code offset and length of the SMC region in the fast path, so we can know how to repatch it
        // 2. The offset of CC/DC IC miss logic in the slow path, so we can know how to produce a jump to it
        //
        void EmitComputeLabelOffsetAndLengthSymbol(X64AsmFile* file);
    };

    // Perform the ASM transformation pass for Call IC lowering
    // See comments in CPP file for detail
    //
    static std::vector<JitAsmTransformResult> WARN_UNUSED DoJitAsmTransform(X64AsmFile* file);

    // Final result after all ASM-level lowering, produced by stencil lowering pipeline
    //
    struct JitAsmLoweringResult
    {
        // Assembly files for the extracted DirectCall and ClosureCall IC logic
        //
        std::string m_directCallLogicAsm;
        std::string m_closureCallLogicAsm;

        size_t m_directCallLogicAsmLineCount;
        size_t m_closureCallLogicAsmLineCount;

        // The special symbols holding the offset and length of the SMC region in the fast path
        //
        std::string m_symbolNameForSMCLabelOffset;
        std::string m_symbolNameForSMCRegionLength;

        // The special symbols holding the offset of CC/DC IC miss logic in the slow path
        //
        std::string m_symbolNameForCcIcMissLogicLabelOffset;
        std::string m_symbolNameForDcIcMissLogicLabelOffset;

        // The unique ordinal of this call IC, always corresponds to the ordinal passed to EmitForBaselineJIT()
        //
        uint64_t m_uniqueOrd;
    };

    struct BaselineJitCodegenResult
    {
        // Contains a function '__deegen_baseline_jit_codegen_<bytecodeId>_jit_call_ic_<ord>',
        // the final implementation of the call-IC-miss slowpath, doing everything needed.
        //
        // The function takes the following arguments:
        //    CodeBlock* codeBlock: the function CodeBlock
        //    uint64_t slowPathDataOffset: the offset of this bytecode's SlowPathData in the stream
        //    void* slowPathPtr: the JIT slowpath addr of this stencil (not this bytecode!)
        //    void* dataSecPtr: the JIT data section addr of this stencil (not this bytecode!)
        //    TValue tv: the function object being called, boxed into TValue but always a function object
        //
        std::unique_ptr<llvm::Module> m_module;
        std::string m_resultFnName;

        // Describes how to modify the codePtr for this IC, which is needed for tiering-up or code invalidation
        // Each item is a pair <offset, is64>, meaning the 32/64-bit value at address 'icAddr + offset' shall be patched
        // by adding (newCodePtr - oldCodePtr).
        //
        std::vector<std::pair<size_t /*offset*/, bool /*is64*/>> m_dcIcCodePtrPatchRecords;
        std::vector<std::pair<size_t /*offset*/, bool /*is64*/>> m_ccIcCodePtrPatchRecords;

        // The JIT code size of this IC in bytes
        // Note that for simplicity, currently we always put the data section (if exists) right after the code section,
        // so there is only one size which accounts for everything.
        //
        size_t m_dcIcSize;
        size_t m_ccIcSize;

        // Human-readable disassembly for audit purpose, note that it's inaccurate as all runtime constants are missing
        //
        std::string m_disasmForAudit;

        size_t m_uniqueOrd;
    };

    // Argument notes:
    //     stencilBaseOffsetInFastPath: the bytecode may consists of multiple stencils, we need the fast path offset
    //         of the stencil containing this IC in order to correctly patch address references to other stencils
    //
    // Creates the IC JIT'ter, and all needed traits about this IC
    //
    // Note that the mainLogicStencil will be modified, since the code in the SMC region of the input mainLogicStencil
    // is placeholder logic and not final. This function will rewrite the SMC region to the final logic.
    //
    static BaselineJitCodegenResult WARN_UNUSED CreateBaselineJitCallIcCreator(BaselineJitImplCreator* ifi,
                                                                               std::unordered_map<std::string, size_t> stencilToFastPathOffsetMap,
                                                                               DeegenStencil& mainLogicStencil /*inout*/,
                                                                               JitAsmLoweringResult& icInfo,
                                                                               const DeegenGlobalBytecodeTraitAccessor& bcTraitAccessor);
};

}   // namespace dast
