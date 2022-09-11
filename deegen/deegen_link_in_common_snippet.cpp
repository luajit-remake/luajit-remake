#include "annotated/deegen_common_snippets/deegen_common_snippet_ir_accessor.h"
#include "annotated/deegen_common_snippets/define_deegen_common_snippet.h"

#include "misc_llvm_helper.h"
#include "llvm/Linker/Linker.h"

namespace dast {

llvm::Function* LinkInDeegenCommonSnippet(llvm::Module* module /*inout*/, const std::string& snippetName)
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
    std::unique_ptr<Module> snippetModule = GetDeegenCommonSnippetLLVMIR(ctx, snippetName);

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

    if (func->hasFnAttribute(Attribute::AttrKind::NoInline))
    {
        func->removeFnAttr(Attribute::AttrKind::NoInline);
    }
    func->addFnAttr(Attribute::AttrKind::AlwaysInline);

    return func;
}

}   // namespace dast
