#pragma once

#include "common_utils.h"
#include "dfg_virtual_register.h"
#include "tvalue.h"

namespace dfg {

// The post-reconstruction phase that transforms the DFG-reconstructed stack frame to the actual
// layout expected by the interpreter / baseline JIT. A few things are involved:
//
// Restoring stack frame headers:
//     In the DFG-reconstructed stack frame, the stack frame header have the following contents
//     (populated by ShadowStore):
//         hdr[0]: an unboxed HeapPtr<FunctionObject>, the function object of the call
//         hdr[1]: an unboxed uint64_t, the number of variadic arguments
//         hdr[2]: an unboxed void* pointer, the return address (points to baseline JIT code)
//         hdr[3]: an unboxed uint64_t, the interpreter base slot number for the parent frame
//
//     If hdr[3] is 2^30-1, it means that the parent frame is the caller of the root frame (which is
//     possible in case of a tail call or chain of tail calls).
//     In that case, hdr[2] is also not valid. The real return address should be the address
//     stored in the root function's stack frame header.
//
//     We are responsible for transforming this information to the actual stack frame header
//     expected by the interpreter.
//
// Moving the variadic arguments:
//     In the DFG-reconstructed stack frame, the variadic arguments are fixed-length and sit before
//     the stack frame header. However, the runtime number of variadic arguments may be smaller than
//     this length, resulting a "gap" between the last valid variadic argument and the stack frame
//     header. The lower tiers doesn't expect this gap, so we have to move the variadic arguments
//     to close this gap as needed.
//
// Restoring the upvalues:
//     The DFG uses "CapturedVar" to implement UpValues, which are essentially closed UpValues (and
//     not on the upvalue list), and the corresponding local holds a boxed Upvalue object that
//     points to the upvalue. This is not what is expected by the lower tiers. So we have to restore
//     the format to what is expected by the lower tiers.
//
// Moving the VariadicResults:
//     If we have an active VariadicResults at this moment, it could potentially be clobbered by the
//     reconstructed stack frame. We must move it to a safe place if this is the case.
//
// Moving the built frames to the expected position:
//     If the first inlined call made by the root function is a tail call, the restored stack frames
//     needs to be moved to overlap the current root frame (to provide no-unbounded-growth guarantee).
//     Otherwise, the restored frames are the root frame's locals followed by the inlined callee's
//     frames, and should be moved to the interpreter base of the root frame.
//
struct OsrPostStackFrameReconstructionPhase
{
    static void Run(TValue* interpreterFrameBase, TValue* reconstructedFrameBase)
    {

    }
};

}   // namespace dfg
