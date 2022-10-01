#include "deegen_process_bytecode_definition_for_interpreter.h"

#include "anonymous_file.h"
#include "misc_llvm_helper.h"
#include "deegen_ast_make_call.h"
#include "deegen_bytecode_operand.h"
#include "deegen_interpreter_bytecode_impl_creator.h"
#include "deegen_ast_return.h"

#include "llvm/Linker/Linker.h"

namespace dast {

namespace {

std::string GetGeneratedBuilderClassNameFromBytecodeName(const std::string& bytecodeName)
{
    return std::string("DeegenGenerated_BytecodeBuilder_" + bytecodeName);
}

std::string GetCppTypeNameForDeegenBytecodeOperandType(DeegenBytecodeOperandType ty)
{
    switch (ty)
    {
    case DeegenBytecodeOperandType::INVALID_TYPE:
    {
        ReleaseAssert(false);
    }
    case DeegenBytecodeOperandType::BytecodeSlotOrConstant:
    {
        return "LocalOrCsTab";
    }
    case DeegenBytecodeOperandType::BytecodeSlot:
    {
        return "Local";
    }
    case DeegenBytecodeOperandType::Constant:
    {
        return "CsTab";
    }
    case DeegenBytecodeOperandType::Int8:
    {
        return "int8_t";
    }
    case DeegenBytecodeOperandType::UInt8:
    {
        return "uint8_t";
    }
    case DeegenBytecodeOperandType::Int16:
    {
        return "int16_t";
    }
    case DeegenBytecodeOperandType::UInt16:
    {
        return "uint16_t";
    }
    case DeegenBytecodeOperandType::Int32:
    {
        return "int32_t";
    }
    case DeegenBytecodeOperandType::UInt32:
    {
        return "uint32_t";
    }
    }   /* switch ty */
    ReleaseAssert(false);
}

void GenerateVariantSelectorImpl(FILE* fp,
                                 const std::vector<DeegenBytecodeOperandType>& opTypes,
                                 const std::vector<std::string>& opNames,
                                 const std::function<bool(const std::vector<DeegenBytecodeOperandType>&)>& selectionOk,
                                 std::vector<DeegenBytecodeOperandType>& selectedType,
                                 bool hasOutputOperand,
                                 size_t extraIndent)
{
    ReleaseAssert(selectedType.size() <= opTypes.size());
    std::string extraIndentStr = std::string(extraIndent, ' ');
    if (selectedType.size() == opTypes.size())
    {
        if (selectionOk(selectedType))
        {
            fprintf(fp, "%sreturn DeegenCreateImpl(", extraIndentStr.c_str());
            for (size_t i = 0; i < selectedType.size(); i++)
            {
                if (i > 0)
                {
                    fprintf(fp, ", ");
                }
                if (opTypes[i] == DeegenBytecodeOperandType::BytecodeSlotOrConstant)
                {
                    if (selectedType[i] == DeegenBytecodeOperandType::BytecodeSlot)
                    {
                        fprintf(fp, "Local(inputDesc.%s.m_ord)", opNames[i].c_str());
                    }
                    else
                    {
                        ReleaseAssert(selectedType[i] == DeegenBytecodeOperandType::Constant);
                        fprintf(fp, "CsTab(inputDesc.%s.m_ord)", opNames[i].c_str());
                    }
                }
                else
                {
                    fprintf(fp, "inputDesc.%s", opNames[i].c_str());
                }
            }
            if (hasOutputOperand)
            {
                if (selectedType.size() > 0)
                {
                    fprintf(fp, ", ");
                }
                fprintf(fp, "inputDesc.output");
            }
            fprintf(fp, ");\n");
        }
        else
        {
            std::string errMsg = "Combination";
            for (size_t i = 0; i < selectedType.size(); i++)
            {
                if (opTypes[i] == DeegenBytecodeOperandType::BytecodeSlotOrConstant)
                {
                    if (selectedType[i] == DeegenBytecodeOperandType::BytecodeSlot)
                    {
                        errMsg += " " + opNames[i] + "=Local";
                    }
                    else
                    {
                        ReleaseAssert(selectedType[i] == DeegenBytecodeOperandType::Constant);
                        errMsg += " " + opNames[i] + "=CsTab";
                    }
                }
            }
            errMsg += " is not instantiated as a valid variant!";
            fprintf(fp, "%sassert(!\"%s\");\n", extraIndentStr.c_str(), errMsg.c_str());
            fprintf(fp, "%s__builtin_unreachable();\n", extraIndentStr.c_str());
        }
        return;
    }

    size_t curOrd = selectedType.size();
    if (opTypes[curOrd] != DeegenBytecodeOperandType::BytecodeSlotOrConstant)
    {
        selectedType.push_back(opTypes[curOrd]);
        GenerateVariantSelectorImpl(fp, opTypes, opNames, selectionOk, selectedType, hasOutputOperand, extraIndent);
        selectedType.pop_back();
    }
    else
    {
        fprintf(fp, "%sif (inputDesc.%s.m_isLocal) {\n", extraIndentStr.c_str(), opNames[curOrd].c_str());
        selectedType.push_back(DeegenBytecodeOperandType::BytecodeSlot);
        GenerateVariantSelectorImpl(fp, opTypes, opNames, selectionOk, selectedType, hasOutputOperand, extraIndent + 4);
        selectedType.pop_back();
        fprintf(fp, "%s} else {\n", extraIndentStr.c_str());
        selectedType.push_back(DeegenBytecodeOperandType::Constant);
        GenerateVariantSelectorImpl(fp, opTypes, opNames, selectionOk, selectedType, hasOutputOperand, extraIndent + 4);
        selectedType.pop_back();
        fprintf(fp, "%s}\n", extraIndentStr.c_str());
    }
}

}   // anonymous namespace

ProcessBytecodeDefinitionForInterpreterResult WARN_UNUSED ProcessBytecodeDefinitionForInterpreter(std::unique_ptr<llvm::Module> module)
{
    using namespace llvm;

    ProcessBytecodeDefinitionForInterpreterResult finalRes;

    AnonymousFile hdrOut;
    FILE* fp = hdrOut.GetFStream("w");

    DesugarAndSimplifyLLVMModule(module.get(), DesugaringLevel::PerFunctionSimplifyOnly);
    AstMakeCall::PreprocessModule(module.get());

    std::vector<std::vector<std::unique_ptr<BytecodeVariantDefinition>>> defs = BytecodeVariantDefinition::ParseAllFromModule(module.get());

    std::vector<std::unique_ptr<Module>> allBytecodeFunctions;

    for (auto& bytecodeDef : defs)
    {
        std::string bytecodeBuilderFunctionReturnType;
        std::string generatedClassName;

        finalRes.m_allExternCDeclarations.push_back({});
        std::vector<std::string>& cdeclNameForVariants = finalRes.m_allExternCDeclarations.back();

        {
            ReleaseAssert(bytecodeDef.size() > 0);
            std::unique_ptr<BytecodeVariantDefinition>& def = bytecodeDef[0];
            generatedClassName = GetGeneratedBuilderClassNameFromBytecodeName(def->m_bytecodeName);
            finalRes.m_generatedClassNames.push_back(generatedClassName);
            fprintf(fp, "template<typename CRTP>\nclass %s {\n", generatedClassName.c_str());
            fprintf(fp, "public:\n");
            fprintf(fp, "    static constexpr size_t GetNumVariants() { return %d; }\n", SafeIntegerCast<int>(bytecodeDef.size()));
            fprintf(fp, "    struct RobustInputDesc {\n");
            size_t numOperands = def->m_opNames.size();
            ReleaseAssert(numOperands == def->m_originalOperandTypes.size());
            for (size_t i = 0; i < numOperands; i++)
            {
                fprintf(fp, "        %s %s;\n", GetCppTypeNameForDeegenBytecodeOperandType(def->m_originalOperandTypes[i]).c_str(), def->m_opNames[i].c_str());
            }
            if (def->m_hasOutputValue)
            {
                fprintf(fp, "        Local output;\n");
            }
            fprintf(fp, "    };\n");

            if (def->m_hasConditionalBranchTarget)
            {
                bytecodeBuilderFunctionReturnType = "BranchTargetPopulator WARN_UNUSED";
            }
            else
            {
                bytecodeBuilderFunctionReturnType = "void";
            }
            fprintf(fp, "    %s ALWAYS_INLINE Create%s(RobustInputDesc inputDesc) {\n", bytecodeBuilderFunctionReturnType.c_str(), def->m_bytecodeName.c_str());

            {
                std::vector<DeegenBytecodeOperandType> selectedTypes;
                auto selectionValidChecker = [&](const std::vector<DeegenBytecodeOperandType>& selection) -> bool {
                    ReleaseAssert(selection.size() == def->m_originalOperandTypes.size());
                    for (auto& bytecodeVariantDef : bytecodeDef)
                    {
                        bool ok = true;
                        for (size_t i = 0; i < def->m_originalOperandTypes.size(); i++)
                        {
                            if (def->m_originalOperandTypes[i] == DeegenBytecodeOperandType::BytecodeSlotOrConstant)
                            {
                                if (selection[i] == DeegenBytecodeOperandType::BytecodeSlot)
                                {
                                    if (bytecodeVariantDef->m_list[i]->GetKind() != BcOperandKind::Slot)
                                    {
                                        ok = false;
                                        break;
                                    }
                                }
                                else
                                {
                                    ReleaseAssert(selection[i] == DeegenBytecodeOperandType::Constant);
                                    if (bytecodeVariantDef->m_list[i]->GetKind() != BcOperandKind::Constant)
                                    {
                                        ok = false;
                                        break;
                                    }
                                }
                            }
                        }
                        if (ok)
                        {
                            return true;
                        }
                    }
                    return false;
                };
                GenerateVariantSelectorImpl(fp, def->m_originalOperandTypes, def->m_opNames, selectionValidChecker, selectedTypes, def->m_hasOutputValue, 8 /*indent*/);
                ReleaseAssert(selectedTypes.size() == 0);
            }

            fprintf(fp, "    }\n\n");

            fprintf(fp, "private:\n");
        }

        int currentBytecodeVariantOrdinal = 0;
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
            cdeclNameForVariants.push_back(InterpreterBytecodeImplCreator::GetInterpreterBytecodeFunctionCName(bytecodeVariantDef.get()));

            fprintf(fp, "    %s DeegenCreateImpl(", bytecodeBuilderFunctionReturnType.c_str());
            for (size_t i = 0; i < bytecodeVariantDef->m_originalOperandTypes.size(); i++)
            {
                if (i > 0)
                {
                    fprintf(fp, ", ");
                }
                if (bytecodeVariantDef->m_originalOperandTypes[i] == DeegenBytecodeOperandType::BytecodeSlotOrConstant)
                {
                    if (bytecodeVariantDef->m_list[i]->GetKind() == BcOperandKind::Slot)
                    {
                        fprintf(fp, "Local param%d", static_cast<int>(i));
                    }
                    else
                    {
                        ReleaseAssert(bytecodeVariantDef->m_list[i]->GetKind() == BcOperandKind::Constant);
                        fprintf(fp, "CsTab param%d", static_cast<int>(i));
                    }
                }
                else
                {
                    fprintf(fp, "%s param%d", GetCppTypeNameForDeegenBytecodeOperandType(bytecodeVariantDef->m_originalOperandTypes[i]).c_str(), static_cast<int>(i));
                }
            }
            if (bytecodeVariantDef->m_hasOutputValue)
            {
                if (bytecodeVariantDef->m_originalOperandTypes.size() > 0)
                {
                    fprintf(fp, ", ");
                }
                fprintf(fp, "Local paramOut");
            }
            fprintf(fp, ") {\n");
            // I believe this is not strictly required, but if this assumption doesn't hold,
            // the 'variantOrd' part in the symbol name won't match the order here, which is going to be terrible for debugging
            //
            ReleaseAssert(static_cast<size_t>(currentBytecodeVariantOrdinal) == bytecodeVariantDef->m_variantOrd);
            fprintf(fp, "        static constexpr size_t x_opcode = CRTP::template GetBytecodeOpcodeBase<%s>() + %d;\n", generatedClassName.c_str(), currentBytecodeVariantOrdinal);
            fprintf(fp, "        CRTP* crtp = static_cast<CRTP*>(this);\n");
            fprintf(fp, "        uint8_t* base = crtp->Reserve(%d);\n", SafeIntegerCast<int>(bytecodeVariantDef->m_bytecodeStructLength));

            int numBitsInOpcodeField = static_cast<int>(BytecodeVariantDefinition::x_opcodeSizeBytes) * 8;
            if (BytecodeVariantDefinition::x_opcodeSizeBytes < 8) {
                fprintf(fp, "        static_assert(x_opcode <= (1ULL << %d) - 1);\n", numBitsInOpcodeField);
            }
            fprintf(fp, "        UnalignedStore<uint%d_t>(base, static_cast<uint%d_t>(x_opcode));\n", numBitsInOpcodeField, numBitsInOpcodeField);

            for (size_t i = 0; i < bytecodeVariantDef->m_list.size(); i++)
            {
                std::unique_ptr<BcOperand>& operand = bytecodeVariantDef->m_list[i];
                if (operand->IsElidedFromBytecodeStruct())
                {
                    continue;
                }
                size_t offset = operand->GetOffsetInBytecodeStruct();
                int numBitsInBytecodeStruct = static_cast<int>(operand->GetSizeInBytecodeStruct()) * 8;
                fprintf(fp, "        UnalignedStore<uint%d_t>(base + %u, BitwiseTruncateTo<uint%d_t>(", numBitsInBytecodeStruct, SafeIntegerCast<unsigned int>(offset), numBitsInBytecodeStruct);
                if (operand->GetKind() == BcOperandKind::Slot)
                {
                    fprintf(fp, "param%d.m_localOrd", static_cast<int>(i));
                }
                else if (operand->GetKind() == BcOperandKind::Constant)
                {
                    fprintf(fp, "param%d.m_csTableOrd", static_cast<int>(i));
                }
                else
                {
                    ReleaseAssert(operand->GetKind() == BcOperandKind::Literal);
                    fprintf(fp, "param%d", static_cast<int>(i));
                }
                fprintf(fp, "));\n");
            }

            if (bytecodeVariantDef->m_hasOutputValue)
            {
                size_t offset = bytecodeVariantDef->m_outputOperand->GetOffsetInBytecodeStruct();
                ReleaseAssert(bytecodeVariantDef->m_outputOperand->GetKind() == BcOperandKind::Slot);
                int numBitsInBytecodeStruct = static_cast<int>(bytecodeVariantDef->m_outputOperand->GetSizeInBytecodeStruct()) * 8;
                fprintf(fp, "        UnalignedStore<uint%d_t>(base + %u, BitwiseTruncateTo<uint%d_t>(paramOut.m_localOrd));\n", numBitsInBytecodeStruct, SafeIntegerCast<unsigned int>(offset), numBitsInBytecodeStruct);
            }

            if (bytecodeVariantDef->m_hasConditionalBranchTarget)
            {
                size_t offset = bytecodeVariantDef->m_condBrTarget->GetOffsetInBytecodeStruct();
                fprintf(fp, "        size_t curBytecodeOffset = crtp->GetCurBytecodeLength();\n");
                fprintf(fp, "        crtp->MarkWritten(%d);\n", SafeIntegerCast<int>(bytecodeVariantDef->m_bytecodeStructLength));
                fprintf(fp, "        return BranchTargetPopulator(curBytecodeOffset + %u /*fillOffset*/, curBytecodeOffset /*bytecodeBaseOffset*/);\n", SafeIntegerCast<unsigned int>(offset));
            }
            else
            {
                fprintf(fp, "        crtp->MarkWritten(%d);\n", SafeIntegerCast<int>(bytecodeVariantDef->m_bytecodeStructLength));
                fprintf(fp, "        return;\n");
            }

            fprintf(fp, "    }\n\n");

            currentBytecodeVariantOrdinal++;
        }
        fprintf(fp, "};\n\n");

        ReleaseAssert(currentBytecodeVariantOrdinal == static_cast<int>(bytecodeDef.size()));
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
    BytecodeVariantDefinition::AssertBytecodeDefinitionGlobalSymbolHasBeenRemoved(module.get());
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

    // Just for sanity, assert again after linkage, that the linkage didn't add back
    // any of the bytecode definition symbols or the implementation functions
    //
    BytecodeVariantDefinition::AssertBytecodeDefinitionGlobalSymbolHasBeenRemoved(module.get());
    for (auto& bytecodeDef : defs)
    {
        for (auto& bytecodeVariantDef : bytecodeDef)
        {
            ReleaseAssert(module->getNamedValue(bytecodeVariantDef->m_implFunctionName) == nullptr);
        }
    }

    finalRes.m_processedModule = std::move(module);

    fclose(fp);
    finalRes.m_generatedHeaderFile = hdrOut.GetFileContents();
    return finalRes;
}

}   // namespace dast
