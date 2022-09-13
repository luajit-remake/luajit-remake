#include "misc_llvm_helper.h"
#include "anonymous_file.h"
#include "read_file.h"

#include "llvm/Bitcode/BitcodeWriter.h"
#include "llvm/IRReader/IRReader.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/Pass.h"
#include "llvm/Transforms/IPO.h"

namespace dast {

std::unique_ptr<llvm::Module> WARN_UNUSED ExtractFunction(llvm::Module* module, std::string functionName, bool ignoreLinkageIssues)
{
    return ExtractFunctions(module, std::vector<std::string> { functionName }, ignoreLinkageIssues);
}

std::unique_ptr<llvm::Module> WARN_UNUSED ExtractFunctions(llvm::Module* moduleInput, const std::vector<std::string>& functionNameList, bool ignoreLinkageIssues)
{
    using namespace llvm;

    std::unique_ptr<Module> moduleHolder = CloneModule(*moduleInput);
    Module* module = moduleHolder.get();

    // For each global variable, if the global variable is not a constant,
    // the definition (address storing the value of the global variable) resides in the host process!
    // We must make this global variable a declaration (instead of a definition),
    // so it correctly resolves to the address in host process.
    //
    // For constants, if the address is significant, we also must make it a declaration,
    // otherwise we would get different addresses inside generated code and inside host process.
    //
    auto mustDropDefintion = [](const GlobalVariable& gv) {
        bool mustDrop = false;
        if (!gv.isConstant())
        {
            // For variables, we must always drop definition, so the generated code and host process
            // resolve the variable to the same address.
            //
            mustDrop = true;
        }
        else
        {
            // For constant, if it is at least local_unnamed_addr (global_unnamed_addr is not required),
            // its address is not significant, so it's OK to keep the definition,
            // and it allows optimizer to potentially work better.
            // Otherwise, we must make it a declaration to resolve it to the correct address.
            //
            if (!gv.hasAtLeastLocalUnnamedAddr())
            {
                mustDrop = true;
            }
        }
        return mustDrop;
    };

    for (GlobalVariable& gv : module->globals())
    {
        // We can only drop definition for globals with non-local linkage.
        // If there is a global with local linkage that is both needed by the function and
        // must drop defintion, it is an irrecoverable error.
        // We will check for that after we eliminate all the unneeded code, and fail it is the case.
        //
        if (!gv.hasLocalLinkage())
        {
            if (mustDropDefintion(gv))
            {
                // Drop the definition to make this global variable a declaration.
                //
                gv.setLinkage(GlobalValue::ExternalLinkage);
                gv.setInitializer(nullptr);
                gv.setComdat(nullptr);
            }
        }
    }

    std::map<Constant*, std::set<GlobalValue*>> constExprUsageGraph;
    std::map<Operator*, std::set<GlobalValue*>> operatorUsageGraph;

    std::function<void(Value*, std::set<GlobalValue*>&)> computeDependencies = [&constExprUsageGraph, &operatorUsageGraph, &computeDependencies](
                                                                                   Value* value, std::set<GlobalValue*>& allUsers /*out*/) {
        if (isa<Instruction>(value))
        {
            Instruction* inst = dyn_cast<Instruction>(value);
            Function* func = inst->getParent()->getParent();
            allUsers.insert(func);
        }
        else if (isa<GlobalValue>(value))
        {
            GlobalValue* gv = dyn_cast<GlobalValue>(value);
            allUsers.insert(gv);
        }
        else if (isa<Constant>(value))
        {
            Constant* cst = dyn_cast<Constant>(value);
            if (!constExprUsageGraph.count(cst))
            {
                // memorized search is needed to avoid exponential time
                //
                std::set<GlobalValue*>& cstUsers = constExprUsageGraph[cst];
                for (User* cstUser : cst->users())
                {
                    computeDependencies(cstUser, cstUsers /*out*/);
                }
            }
            std::set<GlobalValue*>& users = constExprUsageGraph[cst];
            allUsers.insert(users.begin(), users.end());
        }
        else if (isa<Operator>(value))
        {
            Operator* op = dyn_cast<Operator>(value);
            if (!operatorUsageGraph.count(op))
            {
                // memorized search is needed to avoid exponential time
                //
                std::set<GlobalValue*>& opUsers = operatorUsageGraph[op];
                for (User* opUser : op->users())
                {
                    computeDependencies(opUser, opUsers /*out*/);
                }
            }
            std::set<GlobalValue*>& users = operatorUsageGraph[op];
            allUsers.insert(users.begin(), users.end());
        }
        else
        {
            ReleaseAssert(false && "unhandled type");
        }
    };

    std::map<GlobalValue*, std::set<GlobalValue*>> usageGraph;

    auto addUserEdgesOfGlobalValue = [&](GlobalValue* gv) {
        std::set<GlobalValue*> allUsers;
        for (User* user : gv->users())
        {
            computeDependencies(user, allUsers);
        }
        for (GlobalValue* user : allUsers)
        {
            usageGraph[user].insert(gv);
        }
    };

    for (GlobalValue& gv : module->global_values())
    {
        addUserEdgesOfGlobalValue(&gv);
    }

    for (const std::string& functionName : functionNameList)
    {
        Function* functionTarget = module->getFunction(functionName);
        if (functionTarget == nullptr)
        {
            fprintf(stderr, "[ERROR] Failed to locate function '%s'.\n", functionName.c_str());
            abort();
        }

        ReleaseAssert(!functionTarget->empty());
    }

    std::set<GlobalValue*> isNeeded;
    std::set<Function*> callees;
    std::function<void(GlobalValue*, bool)> dfs = [&dfs, &isNeeded, &callees, &usageGraph](GlobalValue* cur, bool isRoot) {
        if (isNeeded.count(cur))
        {
            return;
        }
        isNeeded.insert(cur);
        if (isa<Function>(cur) && !isRoot)
        {
            // Stop at function. The function will be turned into an extern.
            //
            Function* callee = dyn_cast<Function>(cur);
            callees.insert(callee);
            return;
        }
        if (usageGraph.count(cur))
        {
            for (GlobalValue* dependent : usageGraph[cur])
            {
                dfs(dependent, false /*isRoot*/);
            }
        }
    };

    for (const std::string& functionName : functionNameList)
    {
        Function* functionTarget = module->getFunction(functionName);
        ReleaseAssert(functionTarget != nullptr);
        dfs(functionTarget, true /*isRoot*/);
    }

    for (Function* callee : callees)
    {
        // If the callee has non-public linkage, the runtime library will not be
        // able to find the definition at runtime! Lock it down now and ask the
        // user to remove the 'static' keyword.
        // We cannot fix the problem by somehow changing the linkage type by ourselves,
        // as that could cause name collisions.
        //
        if (callee->getLinkage() == GlobalValue::LinkageTypes::PrivateLinkage ||
            callee->getLinkage() == GlobalValue::LinkageTypes::InternalLinkage)
        {
            if (!ignoreLinkageIssues)
            {
                fprintf(stderr, "[ERROR] One of the function to be extracted called function '%s', which "
                                "has local linkage type. To include the function in runtime libary, "
                                "you have to make it have external linkage type (by removing the 'static' keyword etc).",
                        callee->getName().str().c_str());
                abort();
            }
            else
            {
                callee->setLinkage(GlobalValue::LinkageTypes::ExternalLinkage);
            }
        }
    }

    // Record all the global value identifiers for now,
    // the module and context will be modified in the next step
    // We later use these information to verify that the generated module matches our expectation
    // (that it keeps and only keeps the globals we expected).
    //
    std::vector<std::string> allGlobalValueIds;
    std::set<std::string> neededGlobalValueIds;
    for (GlobalValue& gv : module->global_values())
    {
        allGlobalValueIds.push_back(gv.getGlobalIdentifier());
    }
    for (GlobalValue* gv : isNeeded)
    {
        std::string s = gv->getGlobalIdentifier();
        ReleaseAssert(!neededGlobalValueIds.count(s));
        neededGlobalValueIds.insert(s);
    }

    // The logic below basically follows llvm_extract.cpp.
    // We first do a ExtractGV pass, which does not actually remove stuff here,
    // but only delete the body and make them declarations instead of definitions.
    // Then we call dead code elimination pass to actually delete declarations (which
    // contains some tricky logic that we probably should not try to write by ourselves).
    //
    // Below is basically the ExtractGV pass logic, with modifications to fit our purpose.
    //

    // Copied directly from ExtractGV.cpp, no idea if it fits us completely...
    // "Visit the global inline asm." <-- what is that?
    //
    module->setModuleInlineAsm("");

    // Delete bodies of unneeded global vars
    //
    for (GlobalVariable& gv : module->globals())
    {
        if (!isNeeded.count(&gv))
        {
            // a deleted symbol becomes an external declaration,
            // and since it is unused, it will be dropped by dead code elimination
            //
            bool isLocalLinkage = gv.hasLocalLinkage();
            gv.setLinkage(GlobalValue::ExternalLinkage);
            if (isLocalLinkage)
            {
                gv.setVisibility(GlobalValue::HiddenVisibility);
            }

            gv.setInitializer(nullptr);
            gv.setComdat(nullptr);
        }
    }

    std::unordered_set<std::string> extractedFnNameSet;
    for (const std::string& functionName : functionNameList)
    {
        ReleaseAssert(!extractedFnNameSet.count(functionName));
        extractedFnNameSet.insert(functionName);
    }

    for (Function& fn : module->functions())
    {
        // We should not need to keep any function body other than our target
        //
        if (!extractedFnNameSet.count(fn.getName().str()))
        {
            ReleaseAssert(!isNeeded.count(&fn) || callees.count(&fn));
            fn.deleteBody();
            fn.setComdat(nullptr);
        }
        else
        {
            ReleaseAssert(isNeeded.count(&fn));
            fn.setLinkage(GlobalValue::LinkageTypes::ExternalLinkage);
        }
    }

    // Copied directly from ExtractGV.cpp, no idea if it fits us completely...
    //
    for (Module::alias_iterator I = module->alias_begin(), E = module->alias_end(); I != E;)
    {
        Module::alias_iterator CurI = I;
        ++I;

        if (!isNeeded.count(&*CurI))
        {
            Type* Ty = CurI->getValueType();

            CurI->removeFromParent();
            Value* Declaration;
            if (FunctionType* FTy = dyn_cast<FunctionType>(Ty))
            {
                Declaration = Function::Create(FTy, GlobalValue::ExternalLinkage,
                                               CurI->getAddressSpace(),
                                               CurI->getName(), module);
            }
            else
            {
                Declaration = new GlobalVariable(*module, Ty, false, GlobalValue::ExternalLinkage,
                                                 nullptr, CurI->getName());
            }
            CurI->replaceAllUsesWith(Declaration);
            delete &*CurI;
        }
    }

    {
        // Copied from llvm_extract.cpp
        // Delete dead declarations
        // TODO: switch to new pass manager?
        //
        legacy::PassManager Passes;

        // Delete unreachable globals
        //
        Passes.add(createGlobalDCEPass());

        // Remove dead debug info
        //
        Passes.add(createStripDeadDebugInfoPass());

        // Remove dead func decls
        //
        Passes.add(createStripDeadPrototypesPass());

        Passes.run(*module);
    }

    // Change the linkage type to External
    //
    for (const std::string& functionName : functionNameList)
    {
        Function* functionTarget = module->getFunction(functionName);
        ReleaseAssert(functionTarget != nullptr);
        ReleaseAssert(!functionTarget->empty());
        functionTarget->setLinkage(GlobalValue::LinkageTypes::ExternalLinkage);

        // It should be fine that we simply drop the comdat, since C++ always follows ODR rule.
        //
        functionTarget->setComdat(nullptr);
    }

    ValidateLLVMModule(module);

    // Assert that the set of 'needed' symbols that we determined is exactly the set
    // of symbols kept by the dead code elimination pass
    //
    std::set<std::string> allGlobalIdsInNewModule;
    for (GlobalValue& gv : module->global_values())
    {
        std::string s = gv.getGlobalIdentifier();
        ReleaseAssert(!allGlobalIdsInNewModule.count(s));
        allGlobalIdsInNewModule.insert(s);
    }

    for (const std::string& gid : allGlobalValueIds)
    {
        bool expectExist = (neededGlobalValueIds.count(gid) > 0);
        bool exist = (allGlobalIdsInNewModule.count(gid) > 0);
        if (expectExist != exist)
        {
            fprintf(stderr, "[INTERNAL ERROR] We expected extracted IR to%s contain "
                            "global '%s', but in reality it does%s. Please report a bug.\n",
                    (!expectExist ? " not" : ""), gid.c_str(), (exist ? "" : " not"));
            abort();
        }
    }

    // These two checks should not really fail.. but just be paranoid
    //
    for (const std::string& gid : neededGlobalValueIds)
    {
        if (!allGlobalIdsInNewModule.count(gid))
        {
            fprintf(stderr, "[INTERNAL ERROR] We expected extracted IR to contain "
                            "global '%s', but in reality it does not [check2]. Please report a bug.\n",
                    gid.c_str());
            abort();
        }
    }

    for (const std::string& gid : allGlobalIdsInNewModule)
    {
        if (!neededGlobalValueIds.count(gid))
        {
            fprintf(stderr, "[INTERNAL ERROR] We expected extracted IR to not contain "
                            "global '%s', but in reality it does [check3]. Please report a bug.\n",
                    gid.c_str());
            abort();
        }
    }

    {
        for (const std::string& functionName : functionNameList)
        {
            Function* functionTarget = module->getFunction(functionName);
            ReleaseAssert(functionTarget != nullptr);
            ReleaseAssert(functionTarget->hasExternalLinkage());
            ReleaseAssert(!functionTarget->empty());
        }

        // Assert that for each global variable that is not constant, it must have non-local linkage,
        // since it should be resolved to an address in the host process.
        //
        for (GlobalVariable& gv : module->globals())
        {
            if (mustDropDefintion(gv))
            {
                if (gv.hasLocalLinkage())
                {
                    if (!ignoreLinkageIssues)
                    {
                        fprintf(stderr, "[ERROR] One of the functions to be extracted referenced global "
                                        "variable '%s', which has local linkage type. To include the function in "
                                        "runtime libary, you have to make global variable '%s' have external linkage type "
                                        "(by removing the 'static' keyword etc). If it is a static variable inside a "
                                        "function, try to move the function to a header file and add 'inline', which will "
                                        "give the static variable linkonce_odr linkage.\n",
                                gv.getGlobalIdentifier().c_str(),
                                gv.getGlobalIdentifier().c_str());
                        abort();
                    }
                    else
                    {
                        gv.setLinkage(GlobalValue::LinkageTypes::ExternalLinkage);
                    }
                }
                // We have dropped the definition earlier. Just sanity check again.
                //
                ReleaseAssert(gv.hasExternalLinkage());
                ReleaseAssert(!gv.hasInitializer());
                ReleaseAssert(!gv.hasComdat());
            }
        }

        // Assert that all functions, except our target, has become external declarations.
        //
        for (Function& fn : module->functions())
        {
            if (!extractedFnNameSet.count(fn.getName().str()))
            {
                ReleaseAssert(fn.empty() && fn.hasExternalLinkage());
            }
            else
            {
                ReleaseAssert(!fn.empty());
                ReleaseAssert(fn.hasExternalLinkage());
            }
        }
    }

    module->setModuleIdentifier("extracted_ir");

    ValidateLLVMModule(module);

    return moduleHolder;
}

}  // namespace dast
