#pragma once

#include "tvalue.h"
#include "runtime_utils.h"

class CoroutineRuntimeContext;
class CodeBlock;

// Hardcoded in assembly (deegen_internal_enter_exit_vm.s), do not change!
// The order of the two members is also hardcoded!
//
struct DeegenInternalEnterVMFromCReturnResults
{
    TValue* m_retStart;
    uint64_t m_numRets;
};

// Do not call this directly: use DeegenEnterVMFromC instead
//
extern "C" DeegenInternalEnterVMFromCReturnResults deegen_enter_vm_from_c_impl(
    CoroutineRuntimeContext* coroCtx,
    void* ghcFn,
    uint64_t* stackBase,
    uint64_t numArgs,
    CodeBlock* cb);

// This function should never be called from C/C++ code!
//
struct DeegenPreventMagicFunctionFromBeingCalled;
extern "C" void deegen_internal_use_only_exit_vm_epilogue(DeegenPreventMagicFunctionFromBeingCalled*);

// Call a VM function from C++ code, returns the values returned by the VM function.
// The returned values are on the VM stack, so they can be clobbered by any future VM function calls.
//
inline std::pair<TValue* /*retStart*/, uint64_t /*numRet*/> DeegenEnterVMFromC(
    CoroutineRuntimeContext* coroCtx,
    HeapPtr<FunctionObject> func,
    TValue* lowestAvailableVMStackAddress,
    TValue* args = nullptr,
    size_t numArgs = 0)
{
    StackFrameHeader* hdr = reinterpret_cast<StackFrameHeader*>(lowestAvailableVMStackAddress);
    hdr->m_caller = nullptr;
    hdr->m_retAddr = reinterpret_cast<void*>(deegen_internal_use_only_exit_vm_epilogue);
    hdr->m_func = TranslateToRawPointer(func);
    hdr->m_callerBytecodePtr = 0;
    hdr->m_numVariadicArguments = 0;
    uint64_t* stackBase = reinterpret_cast<uint64_t*>(hdr + 1);
    if (numArgs > 0)
    {
        memcpy(stackBase, args, sizeof(TValue) * numArgs);
    }
    CodeBlock* cb = static_cast<CodeBlock*>(TCGet(func->m_executable).As());
    void* ghcFn = cb->m_bestEntryPoint;
    DeegenInternalEnterVMFromCReturnResults result = deegen_enter_vm_from_c_impl(coroCtx, ghcFn, stackBase, numArgs, cb);
    return std::make_pair(result.m_retStart, result.m_numRets);
}
