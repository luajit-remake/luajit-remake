#pragma once

#include "misc_llvm_helper.h"
#include "deegen_parse_asm_text.h"
#include "json_utils.h"
#include "x64_register_info.h"

namespace dast {

// We support reg alloc in DFG by renaming registers in stencils.
// This, however, cause issues to C function calls since, say, the callee expects argument 'foo' in register 'bar',
// if 'bar' were renamed by us to another register at runtime, this clearly won't work.
//
// Another complication is FPR state. Calling conventions do not save FPR state, so it has to be saved and restored
// by us. But this would be a lot of JIT code, which is undesirable.
//
// Note that we only need to support rare runtime calls in the fast path (e.g., IC body, but normal runtime functions
// often have an outlined slowpath as well), so we will only consider preserve_most calling convention here (which
// preserves most GPR but not FPR), but it is straightforward to extend the scheme below to support normal C conv,
// or calling conventions with different set of argument-passing registers.
//
// In System V ABI the arguments are passed in rdi, rsi, rdx, rcx, r8, r9, of which rdx and rcx will not be renamed
// under our reg alloc scheme, so we only need to consider rdi, rsi, r8, r9.
//
// Return values are stored in rax,rdx,xmm0,xmm1. All except xmm1 will not be renamed under our reg alloc scheme,
// which can be safely ignored. Gracefully handling xmm1 is possible by changing the rewrite below slightly, but
// for now for simplicity we simply disable regalloc if this is the case.
//
// The solution is as follows:
//
// For each 'call' to a runtime function in the stencil, we rewrite the 'call' instruction to the following sequence:
// (note that we only need to push/pop the regs that are actually used to pass args, the 4 pushes below is only for exposition)
//
//     pushq %rsi
//     pushq %rdi
//     pushq %r8
//     pushq %r9
//     callq wrapper
//     popq %r9
//     popq %r8
//     popq %rdi
//     popq %rsi
//
// Note that:
// 1. The push will not clobber any valid data, since right before the 'call' instruction, %rsp must be correctly pointing
//    at the top of the stack (since the call instruction will be executed right after)
// 2. System V 16-byte stack alignment requirement will be honored by the wrapper stub.
// 3. The JIT code size overhead is small: the 8 instructions we added are only 12 bytes total.
//
// So the rewrite itself is safe.
//
// Critically, register renaming will rename those push/pop register as well. So even after register renaming, the stack
// content still reflects the data expected by the callee: for example, at start of 'wrapper', rsp+8 is always the desired
// value of r9 expected by the callee.
//
// For FPR state, we always need to save/restore xmm1-6 (the set of FPRs we used for regalloc). But the JIT code may use
// additional registers, which must not be clobbered. So we will find all uses of those registers in JIT code and save/restore
// them as well in the AOT wrapper (Note that if xmm0 is used as return value then it must not be save/restored!)
//
// So we can generate an AOT wrapper to correctly set up r9,r8,rdi,rsi by loading rsp+8/16/24/32 (logically they are in the
// previous frame, but this is fine since the AOT wrapper will be an assembly snippet), save the FPR state, call the callee,
// restore FPR state and return. Specifically, it will be a piece of assembly like this:
//
// foobar_wrapper:
//     movq 8(%rsp), %r9
//     movq 16(%rsp), %r8
//     movq 24(%rsp), %rdi
//     movq 32(%rsp), %rsi
//     subq $104+16*#extraFPR, %rsp  # or 112 if we pushed an odd number of regs before, to honor System V ABI
//     callq prologue_stub
//     save extra FPR states
//     callq foobar
//     restore extra FPR states
//     callq epilogue_stub
//     addq $104+16*#extraFPR, %rsp
//     retq
//
// Note that the stub is accessing the values in the parent stack frames, but it's safe as they are precisely set up by us.
// Also, since the extra FPR state is bound to the stencil, a separate wrapper is needed for each
//
// And prologue_stub is a globally shared AOT piece of assembly that save FPR xmm1-6 state (and r11, which is not saved
// by preserve_most/preserve_all by default, though LLVM changes are needed if we want to use r11 in reg alloc as well).
//
// prologue_stub:
//     movq %r11, 8(%rsp)
//     movups %xmm1, 16(%rsp)
//     ...
//     movups %xmm6, 96(%rsp)
//     retq
//
// The epilogue stub does the reverse (note that r9,r8,rdi,rsi will be correctly restored by the JIT code):
//
// epilogue_stub:
//     movq 8(%rsp), %r11
//     movups 16(%rsp), %xmm1
//     ...
//     movups 96(%rsp), %xmm6
//     retq
//
// At build time, Deegen will collect all the runtime functions that need wrapper and generate the wrapper stubs for them.
// The globally shared prologue_stub and epilogue_stub are implemented in drt/dfg_jit_regalloc_rt_call_utils.s
//
// Another potential solution to this problem is to generate an additional "landing pad" for the wrapper that set up the
// 4 registers. The advantage is that there will be no JIT code size bloat at all, at the cost of an extra 4!*8=192 bytes
// of AOT code (the landing pad) for each runtime function, and slightly more complex codegen logic (since the codegen
// logic must patch the call to jump to the correct location in the landing pad of the wrapper). This should result in
// smaller overall (JIT+AOT) code size, but we do not implement it to avoid the extra codegen complexity for now.
//
struct DfgCCallFuncInfo
{
    std::string m_fnName;
    uint64_t m_argGprMask;
    uint64_t m_argFprMask;
    uint64_t m_retGprMask;
    uint64_t m_retFprMask;

    bool operator==(const DfgCCallFuncInfo&) const = default;

    // Return the list of argument registers that are also DFG reg alloc registers
    // These registers need to be shuffled to their correct registers
    //
    std::vector<X64Reg> WARN_UNUSED GetGprArgShuffleList();

    json_t WARN_UNUSED SaveToJSON();
    static DfgCCallFuncInfo WARN_UNUSED LoadFromJSON(json_t& j);
};

struct DfgRegAllocCCallAsmTransformPass
{
    DfgRegAllocCCallAsmTransformPass(X64AsmFile* file, llvm::Module* module)
        : m_file(file)
        , m_module(module)
        , m_passExecuted(false)
        , m_failReason(FailReason::Unknown)
    { }

    enum class FailReason
    {
        Unknown,
        HasIndirectCall,
        NotPreserveMostCC,
        // We disable reg alloc if one of the return regs is a reg that participates in DFG reg alloc,
        // so we don't have to generate the shuffle logic that moves it to the correct reg
        //
        ReturnRegConflict,
        // We disable reg alloc if one of the FPR used to pass argument is a reg that participates in DFG reg alloc.
        // Note that this limitation only applies to FPR, not to GPR. This is only for simplicity.
        //
        FprArgumentRegConflict
    };

    static const char* GetFailReasonStringName(FailReason reason)
    {
        switch (reason)
        {
        case FailReason::Unknown: { return "Unknown"; }
        case FailReason::HasIndirectCall: { return "HasIndirectCall"; }
        case FailReason::NotPreserveMostCC: { return "NotPreserveMostCC"; }
        case FailReason::ReturnRegConflict: { return "ReturnRegConflict"; }
        case FailReason::FprArgumentRegConflict: { return "FprArgumentRegConflict"; }
        }   /*switch*/
        ReleaseAssert(false);
    }

    // Rewrite all calls to preserve_most functions to preserve_all
    // Return false if the rewrite failed because of the presense of indirect calls
    //
    static bool WARN_UNUSED TryRewritePreserveMostToPreserveAll(llvm::Function* func, std::unordered_set<std::string>& rewrittenFnNames /*out*/);

    static constexpr const char* x_wrappedNameCommonPrefix = "deegen_dfg_rt_wrapper_";

    static bool WARN_UNUSED IsWrappedName(const std::string& name)
    {
        return name.starts_with(x_wrappedNameCommonPrefix);
    }

    static std::string WARN_UNUSED GetWrappedName(const std::string& wrapperPrefix, const std::string& originalName)
    {
        // This is terrible: we have a weird use case that we have to get the original name from the wrapped name.. so here it is!
        //
        return x_wrappedNameCommonPrefix + std::to_string(originalName.length()) + "_" + wrapperPrefix + originalName;
    }

    static std::string WARN_UNUSED GetOriginalNameFromWrappedName(const std::string& wrappedName)
    {
        ReleaseAssert(IsWrappedName(wrappedName));
        size_t pfxLen = strlen(x_wrappedNameCommonPrefix);
        size_t loc = wrappedName.find("_", pfxLen);
        ReleaseAssert(loc != std::string::npos);
        ReleaseAssert(loc > pfxLen);
        int originalNameLen = StoiOrFail(wrappedName.substr(pfxLen, loc - pfxLen));
        ReleaseAssert(originalNameLen > 0 && loc + static_cast<size_t>(originalNameLen) < wrappedName.length());
        return wrappedName.substr(wrappedName.length() - static_cast<size_t>(originalNameLen));
    }

    // Return true on success, false if reg alloc must be disabled
    // On return 'false', the asm file may have been changed
    //
    bool WARN_UNUSED RunPass(std::string wrapperPrefix);

    X64AsmFile* m_file;
    llvm::Module* m_module;
    bool m_passExecuted;
    FailReason m_failReason;
    std::vector<DfgCCallFuncInfo> m_calledFns;
};

struct DfgRegAllocCCallWrapperRequest
{
    std::string m_wrapperPrefix;
    DfgCCallFuncInfo m_info;
    // The mask for all FPR that are used in the JIT code (we conservatively assume that they potentially
    // store useful value so we need to save all of them)
    //
    uint64_t m_maskForAllUsedFprs;

    json_t WARN_UNUSED SaveToJSON();
    static DfgRegAllocCCallWrapperRequest WARN_UNUSED LoadFromJSON(json_t& j);

    std::string GetFuncName()
    {
        ReleaseAssert(m_wrapperPrefix != "" && m_info.m_fnName != "");
        return DfgRegAllocCCallAsmTransformPass::GetWrappedName(m_wrapperPrefix, m_info.m_fnName);
    }

    // We need to save an FPR if:
    // 1. It is in m_maskForAllUsedFprs
    // 2. It is not a DFG reg alloc register (the shared asm stub will save its value for us)
    // 3. It is not used to store return value of the call
    //
    uint64_t GetExtraFprMaskToSave();

    // The content of the assembly stub only depend on the value of this key,
    // so we can only print one stub for each unique key
    //
    using KeyTy = std::pair<std::string, uint64_t>;

    KeyTy GetKey()
    {
        return std::make_pair(m_info.m_fnName, GetExtraFprMaskToSave());
    }

    void PrintAssemblyImpl(FILE* file);
    void PrintAliasImpl(FILE* file, const std::string& aliasFnName);
};

}   // namespace dast
