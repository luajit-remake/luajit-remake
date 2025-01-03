#include "deegen_jit_runtime_constant_utils.h"
#include "deegen_stencil_runtime_constant_insertion_pass.h"

namespace dast {

std::string WARN_UNUSED DeegenPlaceholderUtils::GetRawRuntimeConstantPlaceholderName(size_t ordinal)
{
    return std::string("__deegen_constant_placeholder_bytecode_operand_") + std::to_string(ordinal);
}

llvm::CallInst* WARN_UNUSED DeegenPlaceholderUtils::CreateConstantPlaceholderForOperand(llvm::Module* module, size_t ordinal, llvm::Type* operandTy, llvm::Instruction* insertBefore)
{
    using namespace llvm;
    std::string placeholderName = GetRawRuntimeConstantPlaceholderName(ordinal);
    ReleaseAssert(module->getNamedValue(placeholderName) == nullptr);
    FunctionType* fty = FunctionType::get(operandTy, {}, false /*isVarArg*/);
    Function* func = Function::Create(fty, GlobalValue::ExternalLinkage, placeholderName, module);
    ReleaseAssert(func->getName() == placeholderName);
    func->addFnAttr(Attribute::NoUnwind);
    func->addFnAttr(Attribute::WillReturn);
    func->setDoesNotAccessMemory();
    return CallInst::Create(func, { }, "", insertBefore);
}

llvm::CallInst* WARN_UNUSED DeegenPlaceholderUtils::GetConstantPlaceholderForOperand(llvm::Module* module, size_t ordinal, llvm::Type* operandTy, llvm::Instruction* insertBefore)
{
    using namespace llvm;
    std::string placeholderName = GetRawRuntimeConstantPlaceholderName(ordinal);
    Function* func = module->getFunction(placeholderName);
    ReleaseAssert(func != nullptr);
    ReleaseAssert(func->getReturnType() == operandTy);
    ReleaseAssert(func->arg_size() == 0);
    return CallInst::Create(func, { }, "", insertBefore);
}

size_t WARN_UNUSED DeegenPlaceholderUtils::FindFallthroughPlaceholderOrd(const std::vector<CPRuntimeConstantNodeBase*>& rcDef)
{
    bool found = false;
    size_t ord = static_cast<size_t>(-1);
    for (size_t i = 0; i < rcDef.size(); i++)
    {
        CPRuntimeConstantNodeBase* def = rcDef[i];
        if (def->IsRawRuntimeConstant() && dynamic_cast<CPRawRuntimeConstant*>(def)->m_label == 101 /*fallthroughTarget*/)
        {
            ReleaseAssert(!found);
            found = true;
            ord = i;
        }
    }
    if (!found) { return static_cast<size_t>(-1); }
    return ord;
}

std::string WARN_UNUSED DeegenPlaceholderUtils::FindFallthroughPlaceholderSymbolName(const std::vector<CPRuntimeConstantNodeBase*>& rcDef)
{
    size_t ord = FindFallthroughPlaceholderOrd(rcDef);
    if (ord == static_cast<size_t>(-1))
    {
        return "";
    }
    return std::string("__deegen_cp_placeholder_") + std::to_string(ord);
}

}   // namespace dast
