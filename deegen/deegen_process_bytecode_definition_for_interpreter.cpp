#include "deegen_process_bytecode_definition_for_interpreter.h"

#include "misc_llvm_helper.h"
#include "deegen_ast_make_call.h"
#include "deegen_bytecode_operand.h"
#include "deegen_interpreter_bytecode_impl_creator.h"
#include "deegen_ast_return.h"

#include "llvm/Linker/Linker.h"

namespace dast {

std::unique_ptr<llvm::Module> WARN_UNUSED ProcessBytecodeDefinitionForInterpreter(std::unique_ptr<llvm::Module> module)
{
    using namespace llvm;

    DesugarAndSimplifyLLVMModule(module.get(), DesugaringLevel::PerFunctionSimplifyOnly);
    AstMakeCall::PreprocessModule(module.get());

    std::vector<std::vector<std::unique_ptr<BytecodeVariantDefinition>>> defs = BytecodeVariantDefinition::ParseAllFromModule(module.get());

    std::vector<std::unique_ptr<Module>> allBytecodeFunctions;

    for (auto& bytecodeDef : defs)
    {
        for (auto& bytecodeVariantDef : bytecodeDef)
        {
            // For now we just stay simple and unconditionally let all operands have 2 bytes, which is already stronger than LuaJIT's assumption
            // Let's think about saving memory by providing 1-byte variants, or further removing limitations by allowing 4-byte operands later
            //
            bytecodeVariantDef->SetMaxOperandWidthBytes(2);
            Function* implFunc = module->getFunction(bytecodeVariantDef->m_implFunctionName);
            ReleaseAssert(implFunc != nullptr);
            std::unique_ptr<Module> bytecodeImpl = InterpreterBytecodeImplCreator::ProcessBytecode(bytecodeVariantDef.get(), implFunc);
            allBytecodeFunctions.push_back(std::move(bytecodeImpl));
        }
    }

    // We need to take care when putting everything together, so that we do not introduce linkage problems
    // (for example, we need to make sure all the symbols are referenced correctly, all the necessary symbols are existent,
    // all the droppable symbols are dropped, and all the internal linkage symbols are kept internal)
    //
    // The modules in 'allBytecodeFunctions' should only contain the generated functions, and all other functions
    // have their bodies dropped and linkage changed to external.
    //
    // We will do the following:
    // (1) Iterate all the extracted modules 'E' to figure out the set of all needed functions 'D'.
    // (2) Mark all functions in 'D' in our original module M as '__used__', change their linkage to external if they
    //     have internal linkage, then run optimization pass on M
    // (3) Link the extracted modules into our original module. The linking must succeed because all needed functions
    //     have visible linkage and must exist in our original module (since they are marked used).
    // (4) Restore the linkage of all functions in 'D' to their original linkage, and remove the 'used' annotations.
    //     This allows our final produced module to be correctly linked against other modules.
    //
    struct RestoreAction
    {
        bool m_isFunction;
        bool m_shallRemoveUseAttribute;
        bool m_shouldRestoreLinkage;
        GlobalValue::LinkageTypes m_originalLinkage;
    };

    std::unordered_map<std::string /*funcOrVarName*/, RestoreAction> changeMap;

    for (std::unique_ptr<Module>& m : allBytecodeFunctions)
    {
        for (Function& funcInExtracted : *m.get())
        {
            ReleaseAssert(funcInExtracted.hasExternalLinkage());
            std::string fnName = funcInExtracted.getName().str();
            if (funcInExtracted.empty())
            {
                // Ignore intrinsic functions
                //
                if (funcInExtracted.isIntrinsic())
                {
                    continue;
                }
                Function* funcInOriginal = module->getFunction(fnName);
                // It's possible that the lowering introduced new function declarations that we are not aware of.
                // We don't need to do anything for those functions.
                //
                if (funcInOriginal != nullptr)
                {
                    if (changeMap.count(fnName))
                    {
                        continue;
                    }
                    bool shouldRestoreLinkage = false;
                    GlobalValue::LinkageTypes originalLinkage = funcInOriginal->getLinkage();
                    if (funcInOriginal->hasLocalLinkage())
                    {
                        shouldRestoreLinkage = true;
                        funcInOriginal->setLinkage(GlobalValue::ExternalLinkage);
                    }
                    bool justAddedToUsed = AddUsedAttributeToGlobalValue(funcInOriginal);
                    changeMap[fnName] = RestoreAction {
                        .m_isFunction = true,
                        .m_shallRemoveUseAttribute = justAddedToUsed,
                        .m_shouldRestoreLinkage = shouldRestoreLinkage,
                        .m_originalLinkage = originalLinkage
                    };
                }
                else
                {
                    ReleaseAssert(module->getNamedValue(fnName) == nullptr);
                }
            }
            else
            {
                ReleaseAssert(module->getNamedValue(fnName) == nullptr);
            }
        }

        for (GlobalVariable& gvInExtracted : m->globals())
        {
            std::string gvName = gvInExtracted.getName().str();
            if (gvName == x_deegen_interpreter_dispatch_table_symbol_name)
            {
                // That's the only global we added to the extracted module, it's fine that it's not
                // in the original module, and we don't have to do anything about it
                //
                continue;
            }

            GlobalVariable* gvInOriginal = module->getGlobalVariable(gvName, true /*allowInternal*/);
            ReleaseAssert(gvInOriginal != nullptr);
            if (changeMap.count(gvName))
            {
                continue;
            }

            bool shouldRestoreLinkage = false;
            GlobalValue::LinkageTypes originalLinkage = gvInOriginal->getLinkage();
            if (gvInOriginal->hasLocalLinkage())
            {
                shouldRestoreLinkage = true;
                if (gvInOriginal->isConstant() && gvInOriginal->hasInitializer())
                {
                    ReleaseAssert(gvInExtracted.getLinkage() == GlobalValue::LinkOnceODRLinkage || gvInExtracted.getLinkage() == GlobalValue::ExternalLinkage);
                    gvInOriginal->setLinkage(GlobalValue::LinkOnceODRLinkage);
                }
                else
                {
                    ReleaseAssert(!gvInExtracted.hasInitializer());
                    gvInOriginal->setLinkage(GlobalValue::ExternalLinkage);
                }
            }
            bool justAddedToUsed = AddUsedAttributeToGlobalValue(gvInOriginal);
            changeMap[gvName] = RestoreAction {
                .m_isFunction = false,
                .m_shallRemoveUseAttribute = justAddedToUsed,
                .m_shouldRestoreLinkage = shouldRestoreLinkage,
                .m_originalLinkage = originalLinkage
            };
        }
    }

    // Remove the 'used' attribute of the bytecode definition globals, this should make all the implementation functions dead
    //
    BytecodeVariantDefinition::RemoveUsedAttributeOfBytecodeDefinitionGlobalSymbol(module.get());

    RunLLVMOptimizePass(module.get());

    // Check that the module is as expected after the optimization pass
    //
    // Assert that all the functions we expected to survive the optimization are still there
    //
    for (auto& it : changeMap)
    {
        if (it.second.m_isFunction)
        {
            ReleaseAssert(module->getFunction(it.first) != nullptr);
        }
        else
        {
            ReleaseAssert(module->getGlobalVariable(it.first) != nullptr);
        }
    }

    // Assert that the bytecode definition symbols, and all the implementation functions are gone
    //
    ReleaseAssert(module->getNamedValue(BytecodeVariantDefinition::x_defListSymbolName) == nullptr);
    ReleaseAssert(module->getNamedValue(BytecodeVariantDefinition::x_nameListSymbolName) == nullptr);
    for (auto& bytecodeDef : defs)
    {
        for (auto& bytecodeVariantDef : bytecodeDef)
        {
            ReleaseAssert(module->getNamedValue(bytecodeVariantDef->m_implFunctionName) == nullptr);
        }
    }

    // Link in all the generated bytecode interpreter functions
    //
    for (size_t i = 0; i < allBytecodeFunctions.size(); i++)
    {
        Linker linker(*module);
        // linkInModule returns true on error
        //
        ReleaseAssert(linker.linkInModule(std::move(allBytecodeFunctions[i])) == false);
    }

    // Now, revert all the changes we did to the linkage, so that this module can be linked properly with other modules
    //
    for (auto& it : changeMap)
    {
        RestoreAction ra = it.second;
        if (ra.m_isFunction)
        {
            Function* func = module->getFunction(it.first);
            ReleaseAssert(func != nullptr);
            if (ra.m_shallRemoveUseAttribute)
            {
                RemoveGlobalValueUsedAttributeAnnotation(func);
            }
            if (ra.m_shouldRestoreLinkage)
            {
                func->setLinkage(ra.m_originalLinkage);
            }
        }
        else
        {
            GlobalVariable* gv = module->getGlobalVariable(it.first);
            ReleaseAssert(gv != nullptr);
            if (ra.m_shallRemoveUseAttribute)
            {
                RemoveGlobalValueUsedAttributeAnnotation(gv);
            }
            if (ra.m_shouldRestoreLinkage)
            {
                gv->setLinkage(ra.m_originalLinkage);
            }
        }
    }

    ValidateLLVMModule(module.get());
    return module;
}

}   // namespace dast
