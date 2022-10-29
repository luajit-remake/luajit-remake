#pragma once

#include "misc_llvm_helper.h"

namespace dast {

// This is a very crude transform that transforms a closure (a function with a capture) call to a normal function call.
//
// The transform looks like the following: before transform we have IR
//    %capture = alloca ...
//    ... populate capture ...
//    # the call to the closure would be %lambda(%capture)
//
// and we transform it to:
//    function lambda_wrapper(all args stored to capture)
//        %capture = alloca ...
//        ... populate capture ...
//        return %lambda(%capture)
//
//    # now the call to the closure would be %lambda_wrapper(%args...)
//    # where 'args' is listed in the 'Result' struct returned by this function
//
// This function expects that the only uses of 'capture' are the instructions that fill its value
//
struct RewriteClosureToFunctionCall
{
    struct Result
    {
        llvm::Function* m_newFunc;
        std::vector<llvm::Value*> m_args;
    };

    static Result WARN_UNUSED Run(llvm::Function* lambda, llvm::AllocaInst* capture, const std::string& newFnName);
};

}   // namespace dast
