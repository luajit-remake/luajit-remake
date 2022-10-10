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

std::string GetApiCppTypeNameForDeegenBytecodeOperandType(DeegenBytecodeOperandType ty)
{
    switch (ty)
    {
    case DeegenBytecodeOperandType::INVALID_TYPE:
    {
        ReleaseAssert(false);
    }
    case DeegenBytecodeOperandType::BytecodeSlotOrConstant:
    {
        return "LocalOrCstWrapper";
    }
    case DeegenBytecodeOperandType::BytecodeSlot:
    {
        return "Local";
    }
    case DeegenBytecodeOperandType::Constant:
    {
        return "CstWrapper";
    }
    case DeegenBytecodeOperandType::BytecodeRangeRO:
    {
        return "Local";
    }
    case DeegenBytecodeOperandType::BytecodeRangeRW:
    {
        return "Local";
    }
    case DeegenBytecodeOperandType::Int8:
    {
        return "ForbidUninitialized<int8_t>";
    }
    case DeegenBytecodeOperandType::UInt8:
    {
        return "ForbidUninitialized<uint8_t>";
    }
    case DeegenBytecodeOperandType::Int16:
    {
        return "ForbidUninitialized<int16_t>";
    }
    case DeegenBytecodeOperandType::UInt16:
    {
        return "ForbidUninitialized<uint16_t>";
    }
    case DeegenBytecodeOperandType::Int32:
    {
        return "ForbidUninitialized<int32_t>";
    }
    case DeegenBytecodeOperandType::UInt32:
    {
        return "ForbidUninitialized<uint32_t>";
    }
    }   /* switch ty */
    ReleaseAssert(false);
}

std::string GetVariantCppTypeNameForDeegenBytecodeOperandType(DeegenBytecodeOperandType ty)
{
    switch (ty)
    {
    case DeegenBytecodeOperandType::INVALID_TYPE:
    {
        ReleaseAssert(false);
    }
    case DeegenBytecodeOperandType::BytecodeSlotOrConstant:
    {
        ReleaseAssert(false);
    }
    case DeegenBytecodeOperandType::BytecodeSlot:
    {
        return "Local";
    }
    case DeegenBytecodeOperandType::Constant:
    {
        return "TValue";
    }
    case DeegenBytecodeOperandType::BytecodeRangeRO:
    {
        return "Local";
    }
    case DeegenBytecodeOperandType::BytecodeRangeRW:
    {
        return "Local";
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

// Invariant:
// At the current point of the code, we know that for all variants in S, condition for operands [0, k) have been satisfied.
// We are responsible for generating the implementation that handles all variants in S.
// When our function returns, all variants in S have been handled, and control flow would reach the updated code cursor
// if and only if the specified operands cannot be handled by any variant in S.
//
void GenerateVariantSelectorImpl(FILE* fp,
                                 const std::vector<DeegenBytecodeOperandType>& opTypes,
                                 std::vector<BytecodeVariantDefinition*> S,
                                 std::vector<DeegenBytecodeOperandType>& selectedType,
                                 size_t k,
                                 bool isFirstCheckForK,
                                 size_t extraIndent)
{
    ReleaseAssert(S.size() > 0);
    size_t numOperands = S[0]->m_list.size();
    ReleaseAssert(k <= numOperands);
    ReleaseAssert(selectedType.size() == k);

    std::string extraIndentStr = std::string(extraIndent, ' ');
    if (k == numOperands)
    {
        // We have checked all operands. Now the variant set should have been filtered to exactly one variant
        //
        ReleaseAssert(S.size() == 1);
        ReleaseAssert(opTypes.size() == numOperands);
        BytecodeVariantDefinition* def = S[0];
        fprintf(fp, "%sreturn DeegenCreateImpl%u(", extraIndentStr.c_str(), SafeIntegerCast<unsigned int>(def->m_variantOrd));
        for (size_t i = 0; i < selectedType.size(); i++)
        {
            std::string operandName =  def->m_list[i]->OperandName();
            if (i > 0)
            {
                fprintf(fp, ", ");
            }
            if (opTypes[i] == DeegenBytecodeOperandType::BytecodeSlotOrConstant)
            {
                // LocalOrCstWrapper -> Local / TValue
                //
                if (selectedType[i] == DeegenBytecodeOperandType::BytecodeSlot)
                {
                    fprintf(fp, "ops.%s.AsLocal()", operandName.c_str());
                }
                else
                {
                    ReleaseAssert(selectedType[i] == DeegenBytecodeOperandType::Constant);
                    fprintf(fp, "ops.%s.AsConstant()", operandName.c_str());
                }
            }
            else if (opTypes[i] == DeegenBytecodeOperandType::Constant)
            {
                // CstWrapper -> TValue
                //
                fprintf(fp, "ops.%s.m_value", operandName.c_str());
            }
            else if (opTypes[i] == DeegenBytecodeOperandType::BytecodeSlot || opTypes[i] == DeegenBytecodeOperandType::BytecodeRangeRO || opTypes[i] == DeegenBytecodeOperandType::BytecodeRangeRW)
            {
                // Local -> Local, pass directly
                //
                fprintf(fp, "ops.%s", operandName.c_str());
            }
            else
            {
                // ForbidUninitialized<T> -> T
                //
                fprintf(fp, "ops.%s.m_value", operandName.c_str());
            }
        }
        if (def->m_hasOutputValue)
        {
            if (selectedType.size() > 0)
            {
                fprintf(fp, ", ");
            }
            fprintf(fp, "ops.output");
        }
        fprintf(fp, ");\n");

        return;
    }

    auto emitCheckSlot = [&]()
    {
        std::vector<BytecodeVariantDefinition*> selected;
        std::vector<BytecodeVariantDefinition*> remaining;
        for (BytecodeVariantDefinition* def : S)
        {
            if (def->m_list[k]->GetKind() == BcOperandKind::Slot)
            {
                selected.push_back(def);
            }
            else
            {
                remaining.push_back(def);
            }
        }

        std::string operandName =  S[0]->m_list[k]->OperandName();
        ReleaseAssert(opTypes[k] == DeegenBytecodeOperandType::BytecodeSlotOrConstant);
        fprintf(fp, "%sif (ops.%s.m_isLocal) {\n", extraIndentStr.c_str(), operandName.c_str());
        selectedType.push_back(DeegenBytecodeOperandType::BytecodeSlot);

        GenerateVariantSelectorImpl(fp, opTypes, selected, selectedType, k + 1, true /*isFirstForK*/, extraIndent + 4);

        fprintf(fp, "%s}\n", extraIndentStr.c_str());
        selectedType.pop_back();
        S = remaining;
    };

    auto emitCheckConstantType = [&](bool mustEmitTopCheck)
    {
        // Handle edge case: no constant is specialized
        //
        {
            bool isNoSpecializationEdgeCase = true;
            for (BytecodeVariantDefinition* def : S)
            {
                ReleaseAssert(def->m_list[k]->GetKind() == BcOperandKind::Constant);
                BcOpConstant* op = static_cast<BcOpConstant*>(def->m_list[k].get());
                TypeSpeculationMask mask = op->m_typeMask;
                ReleaseAssert(mask <= x_typeSpeculationMaskFor<tTop>);
                if (mask != x_typeSpeculationMaskFor<tTop>)
                {
                    isNoSpecializationEdgeCase = false;
                }
            }

            if (isNoSpecializationEdgeCase)
            {
                if (!mustEmitTopCheck)
                {
                    // I believe it's impossible to reach here for BytecodeSlotOrConstant: if we have already emitted
                    // at least one type check, we should never see any tTop specialization in S
                    //
                    ReleaseAssert(opTypes[k] == DeegenBytecodeOperandType::Constant);

                    // The check is tautologically true, no need to do anything
                    //
                    selectedType.push_back(DeegenBytecodeOperandType::Constant);
                    GenerateVariantSelectorImpl(fp, opTypes, S, selectedType, k + 1, true /*isFirstCheckForK*/, extraIndent);
                    selectedType.pop_back();
                }
                else
                {
                    ReleaseAssert(opTypes[k] == DeegenBytecodeOperandType::BytecodeSlotOrConstant);
                    selectedType.push_back(DeegenBytecodeOperandType::Constant);
                    std::string operandName =  S[0]->m_list[k]->OperandName();
                    fprintf(fp, "%sif (!ops.%s.m_isLocal) {\n", extraIndentStr.c_str(), operandName.c_str());

                    GenerateVariantSelectorImpl(fp, opTypes, S, selectedType, k + 1, true /*isFirstCheckForK*/, extraIndent + 4);

                    fprintf(fp, "%s}\n", extraIndentStr.c_str());
                    selectedType.pop_back();
                }
                S.clear();
                return;
            }
        }

        while (S.size() > 0)
        {
            // Find one 'maximal' (i.e. no other mask is a superset of it) type mask in the set
            //
            TypeSpeculationMask maximalMask = 0;
            for (BytecodeVariantDefinition* def : S)
            {
                ReleaseAssert(def->m_list[k]->GetKind() == BcOperandKind::Constant);
                BcOpConstant* op = static_cast<BcOpConstant*>(def->m_list[k].get());
                TypeSpeculationMask mask = op->m_typeMask;
                ReleaseAssert(mask > 0);
                if ((mask & maximalMask) == maximalMask)
                {
                    maximalMask = mask;
                }
            }

            // Emit check to the mask
            //
            std::string maskName = "";
            {
                for (auto& maskAndName : x_list_of_type_speculation_mask_and_name)
                {
                    TypeSpeculationMask mask = maskAndName.first;
                    if (mask == maximalMask)
                    {
                        maskName = maskAndName.second;
                    }
                }
            }
            ReleaseAssert(maskName != "");
            std::string checkFnName;
            if (opTypes[k] == DeegenBytecodeOperandType::BytecodeSlotOrConstant)
            {
                checkFnName = "IsConstantAndHasType";
            }
            else
            {
                ReleaseAssert(opTypes[k] == DeegenBytecodeOperandType::Constant);
                checkFnName = "HasType";
            }

            std::string operandName =  S[0]->m_list[k]->OperandName();
            fprintf(fp, "%sif (ops.%s.template %s<%s>()) {\n", extraIndentStr.c_str(), operandName.c_str(), checkFnName.c_str(), maskName.c_str());

            // Now, we need to first get all the variants which mask is the strict subset of 'maximalMask'
            // These more specialized variants must take precedence before we handle the more generalized case of specialization == maximalMask
            //
            {
                std::vector<BytecodeVariantDefinition*> selected;
                std::vector<BytecodeVariantDefinition*> remaining;
                for (BytecodeVariantDefinition* def : S)
                {
                    ReleaseAssert(def->m_list[k]->GetKind() == BcOperandKind::Constant);
                    BcOpConstant* op = static_cast<BcOpConstant*>(def->m_list[k].get());
                    TypeSpeculationMask mask = op->m_typeMask;
                    if ((mask & maximalMask) == mask && mask < maximalMask)
                    {
                        selected.push_back(def);
                    }
                    else
                    {
                        remaining.push_back(def);
                    }
                }

                if (selected.size() > 0)
                {
                    // These more specialized variants needs further checking, so we cannot advance to the next operand yet
                    //
                    GenerateVariantSelectorImpl(fp, opTypes, selected, selectedType, k, false /*isFirstCheckForK*/, extraIndent + 4);
                }

                S = remaining;
            }

            // Having handled all the more specialized variants, we can now handle the variants where specialization == maximalMask
            // In this case we can advance to the next operand, because all the variants here have the same mask
            //
            {
                std::vector<BytecodeVariantDefinition*> selected;
                std::vector<BytecodeVariantDefinition*> remaining;
                for (BytecodeVariantDefinition* def : S)
                {
                    ReleaseAssert(def->m_list[k]->GetKind() == BcOperandKind::Constant);
                    BcOpConstant* op = static_cast<BcOpConstant*>(def->m_list[k].get());
                    TypeSpeculationMask mask = op->m_typeMask;
                    if (mask == maximalMask)
                    {
                        selected.push_back(def);
                    }
                    else
                    {
                        remaining.push_back(def);
                    }
                }

                ReleaseAssert(selected.size() > 0);
                selectedType.push_back(DeegenBytecodeOperandType::Constant);

                GenerateVariantSelectorImpl(fp, opTypes, selected, selectedType, k + 1, true /*isFirstCheckForK*/, extraIndent + 4);

                selectedType.pop_back();
                S = remaining;
            }

            // In this iteration, we handled all the variants which specialization mask subseteq maximalMask, and removed them from S
            // If S is not empty yet, the next iterations will handle them.
            //
            fprintf(fp, "%s}\n", extraIndentStr.c_str());
        }
    };

    if (opTypes[k] == DeegenBytecodeOperandType::BytecodeSlotOrConstant)
    {
        bool hasSlotVariant = false;
        bool hasConstantVariant = false;
        for (BytecodeVariantDefinition* def : S)
        {
            BcOperandKind kind = def->m_list[k]->GetKind();
            if (kind == BcOperandKind::Slot)
            {
                hasSlotVariant = true;
            }
            else
            {
                ReleaseAssert(kind == BcOperandKind::Constant);
                hasConstantVariant = true;
            }
        }

        if (hasSlotVariant)
        {
            ReleaseAssert(isFirstCheckForK);
            emitCheckSlot();
        }

        if (hasConstantVariant)
        {
            emitCheckConstantType(isFirstCheckForK /*mustEmitTopCheck*/);
        }

        ReleaseAssert(S.size() == 0);
        return;
    }

    if (opTypes[k] == DeegenBytecodeOperandType::Constant)
    {
        emitCheckConstantType(false /*mustEmitTopCheck*/);

        ReleaseAssert(S.size() == 0);
        return;
    }

    if (opTypes[k] == DeegenBytecodeOperandType::BytecodeSlot ||
        opTypes[k] == DeegenBytecodeOperandType::BytecodeRangeRO ||
        opTypes[k] == DeegenBytecodeOperandType::BytecodeRangeRW)
    {
        // These kinds cannot be specialized, nothing to do
        //
        selectedType.push_back(opTypes[k]);
        GenerateVariantSelectorImpl(fp, opTypes, S, selectedType, k + 1, true /*isFirstCheckForK*/, extraIndent);
        selectedType.pop_back();
        return;
    }

    if (opTypes[k] == DeegenBytecodeOperandType::Int8 ||
        opTypes[k] == DeegenBytecodeOperandType::Int16 ||
        opTypes[k] == DeegenBytecodeOperandType::Int32 ||
        opTypes[k] == DeegenBytecodeOperandType::UInt8 ||
        opTypes[k] == DeegenBytecodeOperandType::UInt16 ||
        opTypes[k] == DeegenBytecodeOperandType::UInt32)
    {
        bool hasSpecializedLiteral = false;
        for (BytecodeVariantDefinition* def : S)
        {
            BcOperandKind kind = def->m_list[k]->GetKind();
            if (kind == BcOperandKind::SpecializedLiteral)
            {
                hasSpecializedLiteral = true;
            }
            else
            {
                ReleaseAssert(kind == BcOperandKind::Literal);
            }
        }
        if (hasSpecializedLiteral)
        {
            ReleaseAssert(false && "unimplemented");
        }
        else
        {
            // This literal operand has no specialized variants, nothing to do
            //
            selectedType.push_back(opTypes[k]);
            GenerateVariantSelectorImpl(fp, opTypes, S, selectedType, k + 1, true /*isFirstCheckForK*/, extraIndent);
            selectedType.pop_back();
            return;
        }
    }

    ReleaseAssert(false && "unhandled DeegenBytecodeOperandType");
}

std::string WARN_UNUSED DumpAuditFile(llvm::Module* module)
{
    ReleaseAssert(module != nullptr);
    std::unique_ptr<llvm::Module> clone = llvm::CloneModule(*module);
    std::string contents = CompileLLVMModuleToAssemblyFile(clone.get(), llvm::Reloc::Static, llvm::CodeModel::Small);
    return contents;
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
            fprintf(fp, "    struct Operands {\n");
            size_t numOperands = def->m_opNames.size();
            ReleaseAssert(numOperands == def->m_originalOperandTypes.size());
            bool hasAnyOperandsToProvide = false;
            for (size_t i = 0; i < numOperands; i++)
            {
                fprintf(fp, "        %s %s;\n", GetApiCppTypeNameForDeegenBytecodeOperandType(def->m_originalOperandTypes[i]).c_str(), def->m_opNames[i].c_str());
                hasAnyOperandsToProvide = true;
            }
            if (def->m_hasOutputValue)
            {
                fprintf(fp, "        Local output;\n");
                hasAnyOperandsToProvide = true;
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
            // Pass the operands with 'const&' since the Operands struct may contain sub-integer fields, which could require Clang to do weird ABI lowerings that hamper LLVM optimization
            //
            fprintf(fp, "    %s ALWAYS_INLINE Create%s(%s) {\n", bytecodeBuilderFunctionReturnType.c_str(), def->m_bytecodeName.c_str(), hasAnyOperandsToProvide ? "const Operands& ops" : "");

            {
                std::vector<DeegenBytecodeOperandType> selectedTypes;
                std::vector<BytecodeVariantDefinition*> S;
                for (auto& it : bytecodeDef)
                {
                    S.push_back(it.get());
                }
                GenerateVariantSelectorImpl(fp, def->m_originalOperandTypes, S, selectedTypes, 0 /*curOperandOrd*/, true /*isFirstCheckForK*/, 8 /*indent*/);
                ReleaseAssert(selectedTypes.size() == 0);
            }

            fprintf(fp, "        assert(false && \"Bad operand type/value combination! Did you forget to put it in Variant() list?\");\n");
            fprintf(fp, "        __builtin_unreachable();\n");
            fprintf(fp, "    }\n\n");

            fprintf(fp, "protected:\n");
            fprintf(fp, "    static constexpr size_t GetNumVariants() { return %d; }\n", SafeIntegerCast<int>(bytecodeDef.size()));
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

            std::string variantMainFunctionName = InterpreterBytecodeImplCreator::GetInterpreterBytecodeFunctionCName(bytecodeVariantDef.get());

            finalRes.m_auditFiles.push_back(std::make_pair(variantMainFunctionName + ".s", DumpAuditFile(bytecodeImpl.get())));

            allBytecodeFunctions.push_back(std::move(bytecodeImpl));
            cdeclNameForVariants.push_back(variantMainFunctionName);

            fprintf(fp, "    %s DeegenCreateImpl%u(", bytecodeBuilderFunctionReturnType.c_str(), SafeIntegerCast<unsigned int>(bytecodeVariantDef->m_variantOrd));
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
                        fprintf(fp, "TValue param%d", static_cast<int>(i));
                    }
                }
                else
                {
                    fprintf(fp, "%s param%d", GetVariantCppTypeNameForDeegenBytecodeOperandType(bytecodeVariantDef->m_originalOperandTypes[i]).c_str(), static_cast<int>(i));
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

            for (size_t i = 0; i < bytecodeVariantDef->m_list.size(); i++)
            {
                std::unique_ptr<BcOperand>& operand = bytecodeVariantDef->m_list[i];
                if (operand->IsElidedFromBytecodeStruct())
                {
                    continue;
                }
                if (operand->GetKind() == BcOperandKind::Constant)
                {
                    BcOpConstant* bcOpCst = static_cast<BcOpConstant*>(operand.get());
                    if (bcOpCst->m_typeMask != x_typeSpeculationMaskFor<tTop>)
                    {
                        std::string maskName = "";
                        {
                            for (auto& maskAndName : x_list_of_type_speculation_mask_and_name)
                            {
                                TypeSpeculationMask mask = maskAndName.first;
                                if (mask == bcOpCst->m_typeMask)
                                {
                                    maskName = maskAndName.second;
                                }
                            }
                        }
                        ReleaseAssert(maskName != "");
                        fprintf(fp, "        assert(param%d.Is<%s>());\n", static_cast<int>(i), maskName.c_str());
                    }
                }
                std::string tyName = std::string(operand->IsSignedValue() ? "" : "u") + "int" + std::to_string(operand->GetSizeInBytecodeStruct() * 8) + "_t";
                fprintf(fp, "        using StorageTypeForOperand%d = %s;\n", static_cast<int>(i), tyName.c_str());
                fprintf(fp, "        auto originalVal%d = ", static_cast<int>(i));
                if (operand->GetKind() == BcOperandKind::Slot)
                {
                    fprintf(fp, "param%d.m_localOrd", static_cast<int>(i));
                }
                else if (operand->GetKind() == BcOperandKind::Constant)
                {

                    fprintf(fp, "crtp->InsertConstantIntoTable(param%d)", static_cast<int>(i));
                }
                else if (operand->GetKind() == BcOperandKind::BytecodeRangeBase)
                {
                    fprintf(fp, "param%d.m_localOrd", static_cast<int>(i));
                }
                else
                {
                    ReleaseAssert(operand->GetKind() == BcOperandKind::Literal);
                    fprintf(fp, "param%d", static_cast<int>(i));
                }
                fprintf(fp, ";\n");
                fprintf(fp, "        static_assert(std::is_signed_v<StorageTypeForOperand%d> == std::is_signed_v<decltype(originalVal%d)>);\n", static_cast<int>(i), static_cast<int>(i));
            }

            fprintf(fp, "        uint8_t* base = crtp->Reserve(%d);\n", SafeIntegerCast<int>(bytecodeVariantDef->m_bytecodeStructLength));

            int numBitsInOpcodeField = static_cast<int>(BytecodeVariantDefinition::x_opcodeSizeBytes) * 8;
            if (BytecodeVariantDefinition::x_opcodeSizeBytes < 8) {
                fprintf(fp, "        static_assert(x_opcode <= (1ULL << %d) - 1);\n", numBitsInOpcodeField);
            }
            fprintf(fp, "        UnalignedStore<uint%d_t>(base, static_cast<uint%d_t>(x_opcode));\n", numBitsInOpcodeField, numBitsInOpcodeField);

            for (size_t i = 0; i < bytecodeVariantDef->m_list.size(); i++)
            {
                std::unique_ptr<BcOperand>& operand = bytecodeVariantDef->m_list[i];
                size_t offset = operand->GetOffsetInBytecodeStruct();
                fprintf(fp, "        UnalignedStore<StorageTypeForOperand%d>(base + %u, SafeIntegerCast<StorageTypeForOperand%d>(originalVal%d));\n", static_cast<int>(i), SafeIntegerCast<unsigned int>(offset), static_cast<int>(i), static_cast<int>(i));
            }

            if (bytecodeVariantDef->m_hasOutputValue)
            {
                size_t offset = bytecodeVariantDef->m_outputOperand->GetOffsetInBytecodeStruct();
                ReleaseAssert(bytecodeVariantDef->m_outputOperand->GetKind() == BcOperandKind::Slot);
                int numBitsInBytecodeStruct = static_cast<int>(bytecodeVariantDef->m_outputOperand->GetSizeInBytecodeStruct()) * 8;
                fprintf(fp, "        UnalignedStore<uint%d_t>(base + %u, SafeIntegerCast<uint%d_t>(paramOut.m_localOrd));\n", numBitsInBytecodeStruct, SafeIntegerCast<unsigned int>(offset), numBitsInBytecodeStruct);
            }

            if (bytecodeVariantDef->m_hasConditionalBranchTarget)
            {
                size_t offset = bytecodeVariantDef->m_condBrTarget->GetOffsetInBytecodeStruct();
                fprintf(fp, "        size_t curBytecodeOffset = crtp->GetCurLength();\n");
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
            if (gvInExtracted.hasLocalLinkage())
            {
                // If the global has local linkage, just link them in. There isn't any downside other than we potentially get one more copy
                // if the variable already exists in the main module. But that's fine, because LLVM optimizer should usually be able to merge
                // identical copies of internal globals into one. We should not try to manually merge them because it's very tricky and risky.
                //
                // TODO: after some more investigation, it seems like LLVM does not merge identical internal globals with unnamed_addr on the spot.
                // It seems like some optimization pass is needed to merge them. We don't want to run the whole optimization pipeline again
                // since LLVM doesn't recommend that (it could bloat the code), so we should identify what pass does that and run that pass alone.
                // But it's not very important anyway.
                //
                continue;
            }

            std::string gvName = gvInExtracted.getName().str();
            if (gvName == x_deegen_interpreter_dispatch_table_symbol_name)
            {
                // That's the only external global we added to the extracted module, it's fine that it's not
                // in the original module, and we don't have to do anything about it
                //
                continue;
            }

            GlobalVariable* gvInOriginal = module->getGlobalVariable(gvName);
            // Currently we simply fail if the processed module introduced new external global variables that we do not know.
            // We should be able to do better, but there is no use case for such scenario right now.
            //
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
