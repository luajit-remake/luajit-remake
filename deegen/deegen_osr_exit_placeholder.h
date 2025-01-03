#pragma once

#include "common_utils.h"
#include "misc_llvm_helper.h"
#include "deegen_register_pinning_scheme.h"

namespace dast {

constexpr const char* x_osrExitPlaceholderName = "__deegen_osr_exit_placeholder";
constexpr const char* x_osrExitRealHandlerName = "__deegen_dfg_jit_osr_exit_handler";

inline llvm::Function* WARN_UNUSED GetOsrExitFunctionPlaceholder(llvm::Module* module)
{
    using namespace llvm;
    Function* func = module->getFunction(x_osrExitPlaceholderName);
    if (func == nullptr)
    {
        FunctionType* fty = FunctionType::get(llvm_type_of<void>(module->getContext()), false /*isVarArg*/);
        func = Function::Create(fty, GlobalValue::ExternalLinkage, x_osrExitPlaceholderName, module);
        ReleaseAssert(func->getName().str() == x_osrExitPlaceholderName);
        func->addFnAttr(Attribute::NoReturn);
        func->addFnAttr(Attribute::NoUnwind);
    }

    ReleaseAssert(func->hasFnAttribute(Attribute::NoReturn));
    ReleaseAssert(func->hasFnAttribute(Attribute::NoUnwind));
    ReleaseAssert(func->arg_size() == 0 && llvm_type_has_type<void>(func->getReturnType()));
    return func;
}

// The OSR exit handler function has interface JitAOTSlowPathSaveRegStubInterface
// The 'SlowPathDataPtr' holds the value of the OSR exit-point
//
inline llvm::Function* WARN_UNUSED CreateOrGetOsrExitHandlerFunction(llvm::Module* module)
{
    using namespace llvm;
    Function* func = module->getFunction(x_osrExitRealHandlerName);
    if (func == nullptr)
    {
        std::unique_ptr<ExecutorFunctionContext> funcCtx = ExecutorFunctionContext::CreateForDfgAOTSaveRegStub();
        return funcCtx->CreateFunction(module, x_osrExitRealHandlerName);
    }
    else
    {
        ReleaseAssert(func->getFunctionType() == RegisterPinningScheme::GetFunctionType(module->getContext()));
        return func;
    }
}

}   // namespace dast
