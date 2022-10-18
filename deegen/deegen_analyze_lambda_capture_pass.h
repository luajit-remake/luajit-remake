#pragma once

#include "misc_llvm_helper.h"

namespace dast {

// This pass analyzes the captured state of each C++ lambda
// Specifically, for each lambda where 'DeegenGetLambdaClosureAddr' is called,
// a dummy call inst will be created before that call, which conveys information about the captured state of this lambda.
//
// The dummy call takes 3n+1 parameters, where n is the # of captured values. The first parameter is always the closure alloca.
// The next 3n parameters describe the n captures of the alloca in the same order as in the closure alloca struct.
//
// Each captured value is described by a 3-value tuple <ordInStruct, captureKind, AllocaInst/OrdInParentCapture>
//     i64 ordInStruct:
//         The ord in the capture struct. Due to padding, some elements in the capture struct represent padding, thus will not show up
//     CaptureKind captureKind:
//         See enum 'CaptureKind'.
//     ptr/i64 value:
//         If 'captureKind' is a local var capture kind, this is a ptr parameter, which is the AllocaInst of the local variable.
//         Otherwise, this is a i64 parameter, which is the ordinal in the capture of the enclosing lambda.
//
struct DeegenAnalyzeLambdaCapturePass
{
    enum class CaptureKind : uint64_t
    {
        // This is a by-value capture of a local variable
        //
        ByValueCaptureOfLocalVar,
        // This is a by-ref capture of a local variable
        //
        ByRefCaptureOfLocalVar,
        // This is a capture that captures a capture of the enclosing lambda.
        // The enclosing lambda captured some value by reference, while the current lambda captured that value by value
        //
        ByValueCaptureOfByRef,
        // This is a capture that captures a capture of the enclosing lambda.
        // The enclosing lambda captured some value by reference, while the current lambda captured that value by value
        //
        ByRefCaptureOfByValue,
        // This is a capture that captures a capture of the enclosing lambda.
        // It has the same capture kind as 'ordinalInParentCapture'
        // This state rises because the following two cases generates identical IR for the inner lambda:
        //     (1) void* ptr; [ptr](){ ... [ptr]() { ... } }
        //     (2) SomeClass t; [&t]() { ... [&t]() { ... } }
        // In both cases, the outer lambda stores the captured value as a pointer, and the inner lambda simply loads the pointer value as its own capture.
        //
        SameCaptureKindAsEnclosingLambda,
        // Must be last element
        //
        Invalid
    };

    // This must be called before ANY transformation (including inlining always_inline functions) is performed on the module!
    //
    static void AddAnnotations(llvm::Module* module);

    // Remove any dummy call annotations added by 'AddAnnotations'
    //
    static void RemoveAnnotations(llvm::Module* module);

    static bool WARN_UNUSED IsAnnotationFunction(const std::string& fnName)
    {
        if (!fnName.starts_with(x_annotationFnPrefix))
        {
            return false;
        }
        ReleaseAssert(fnName.length() > strlen(x_annotationFnPrefix));
        for (size_t i = strlen(x_annotationFnPrefix); i < fnName.length(); i++)
        {
            ReleaseAssert('0' <= fnName[i] && fnName[i] <= '9');
        }
        return true;
    }

    static constexpr const char* x_annotationFnPrefix = "__deegen_analyzer_annotate_lambda_capture_info_";
};

}   // namespace dast
