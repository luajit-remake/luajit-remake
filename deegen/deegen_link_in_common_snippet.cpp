#include "annotated/deegen_common_snippets/deegen_common_snippet_ir_accessor.h"
#include "annotated/deegen_common_snippets/define_deegen_common_snippet.h"

#include "misc_llvm_helper.h"
#include "llvm/Linker/Linker.h"

namespace dast {

std::pair<std::unique_ptr<llvm::Module>, llvm::Function*> WARN_UNUSED GetDeegenCommonSnippetModule(llvm::LLVMContext& ctx, const std::string& snippetName)
{
    using namespace llvm;

    std::unique_ptr<Module> module = GetDeegenCommonSnippetLLVMIR(ctx, snippetName, 0 /*expectedKind = snippet*/);
    std::string fnName = std::string(x_deegen_common_snippet_function_name_prefix) + snippetName;
    Function* func = module->getFunction(fnName);
    ReleaseAssert(func != nullptr);
    ReleaseAssert(!func->empty());
    return std::make_pair(std::move(module), func);
}

llvm::Function* WARN_UNUSED LinkInDeegenCommonSnippet(llvm::Module* module /*inout*/, const std::string& snippetName)
{
    using namespace llvm;
    LLVMContext& ctx = module->getContext();

    std::string fnName = std::string(x_deegen_common_snippet_function_name_prefix) + snippetName;
    Function* func = module->getFunction(fnName);
    if (func != nullptr)
    {
        ReleaseAssert(!func->empty());
        return func;
    }

    ReleaseAssert(module->getNamedValue(fnName) == nullptr);

    // Now we need to link in the function
    //
    std::unique_ptr<Module> snippetModule = GetDeegenCommonSnippetModule(ctx, snippetName).first;

    {
        Linker linker(*module);
        // linkInModule returns true on error
        //
        ReleaseAssert(linker.linkInModule(std::move(snippetModule)) == false);
    }

    func = module->getFunction(fnName);
    ReleaseAssert(func != nullptr);
    ReleaseAssert(!func->empty());

    ReleaseAssert(func->getLinkage() == GlobalValue::LinkageTypes::ExternalLinkage);
    func->setLinkage(GlobalValue::LinkageTypes::InternalLinkage);

    if (func->hasFnAttribute(Attribute::NoInline))
    {
        func->removeFnAttr(Attribute::NoInline);
    }
    func->addFnAttr(Attribute::AlwaysInline);

    return func;
}

llvm::Function* WARN_UNUSED DeegenImportRuntimeFunctionDeclaration(llvm::Module* module /*inout*/, const std::string& snippetName)
{
    using namespace llvm;
    LLVMContext& ctx = module->getContext();

    std::unique_ptr<Module> snippetModule = GetDeegenCommonSnippetLLVMIR(ctx, snippetName, 1 /*expectedKind = importableDecl*/);
    ReleaseAssert(snippetModule->functions().begin() != snippetModule->functions().end());
    ReleaseAssert(++snippetModule->functions().begin() == snippetModule->functions().end());
    Function* func = &(*snippetModule->functions().begin());
    std::string funcName = func->getName().str();
    ReleaseAssert(funcName != "");

    Function* found = module->getFunction(funcName);
    if (found != nullptr)
    {
        return found;
    }
    else
    {
        ReleaseAssert(module->getNamedValue(funcName) == nullptr);

        // If the function is a declaration, create a fake body so the function can be linked into the module
        //
        bool shouldDeleteBody = false;
        if (func->empty())
        {
            shouldDeleteBody = true;
            BasicBlock* bb = BasicBlock::Create(module->getContext(), "", func);
            std::ignore = new UnreachableInst(module->getContext(), bb);
            ReleaseAssert(!func->empty());
        }

        {
            Linker linker(*module);
            // linkInModule returns true on error
            //
            ReleaseAssert(linker.linkInModule(std::move(snippetModule)) == false);
        }

        Function* linkedInFn = module->getFunction(funcName);
        ReleaseAssert(linkedInFn != nullptr);
        ReleaseAssert(linkedInFn->getLinkage() == GlobalValue::ExternalLinkage);
        ReleaseAssert(linkedInFn->getName() == funcName);
        ReleaseAssert(!linkedInFn->empty());

        if (shouldDeleteBody)
        {
            linkedInFn->deleteBody();
        }

        return linkedInFn;
    }
}

llvm::Function* WARN_UNUSED DeegenCreateFunctionDeclarationBasedOnSnippet(llvm::Module* module /*inout*/, const std::string& snippetName, const std::string& desiredFunctionName)
{
    using namespace llvm;
    LLVMContext& ctx = module->getContext();

    std::unique_ptr<Module> snippetModule = GetDeegenCommonSnippetLLVMIR(ctx, snippetName, 2 /*expectedKind = declTemplate*/);
    ReleaseAssert(snippetModule->functions().begin() != snippetModule->functions().end());
    ReleaseAssert(++snippetModule->functions().begin() == snippetModule->functions().end());
    Function* func = &(*snippetModule->functions().begin());
    std::string funcName = func->getName().str();
    ReleaseAssert(funcName != "");

    ReleaseAssert(funcName != desiredFunctionName);
    ReleaseAssert(module->getNamedValue(desiredFunctionName) == nullptr);
    ReleaseAssert(module->getNamedValue(funcName) == nullptr);

    // Create a fake body so the function can be linked into the module
    //
    {
        ReleaseAssert(func->empty());
        BasicBlock* bb = BasicBlock::Create(module->getContext(), "", func);
        std::ignore = new UnreachableInst(module->getContext(), bb);
        ReleaseAssert(!func->empty());
    }

    {
        Linker linker(*module);
        // linkInModule returns true on error
        //
        ReleaseAssert(linker.linkInModule(std::move(snippetModule)) == false);
    }

    Function* linkedInFn = module->getFunction(funcName);
    ReleaseAssert(linkedInFn != nullptr);
    ReleaseAssert(linkedInFn->getLinkage() == GlobalValue::ExternalLinkage);
    ReleaseAssert(linkedInFn->getName() == funcName);
    ReleaseAssert(!linkedInFn->empty());
    linkedInFn->deleteBody();

    ReleaseAssert(module->getNamedValue(desiredFunctionName) == nullptr);
    linkedInFn->setName(desiredFunctionName);
    ReleaseAssert(linkedInFn->getName() == desiredFunctionName);
    return linkedInFn;
}

}   // namespace dast
