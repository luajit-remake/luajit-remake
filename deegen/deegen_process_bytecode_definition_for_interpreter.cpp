#include "deegen_process_bytecode_definition_for_interpreter.h"

#include "anonymous_file.h"
#include "misc_llvm_helper.h"
#include "deegen_ast_make_call.h"
#include "deegen_bytecode_operand.h"
#include "deegen_interpreter_bytecode_impl_creator.h"
#include "deegen_ast_return.h"
#include "deegen_analyze_lambda_capture_pass.h"
#include "deegen_ast_inline_cache.h"
#include "deegen_ast_slow_path.h"
#include "deegen_postprocess_module_linker.h"
#include "llvm_identical_function_merger.h"

#include "llvm/Transforms/Utils/FunctionComparator.h"
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

template<typename T>
std::string GetSpecializedValueAsStringImpl(uint64_t spVal64)
{
    static_assert(std::is_integral_v<T> && !std::is_same_v<T, bool>);
    using ST = std::conditional_t<std::is_signed_v<T>, int64_t, uint64_t>;
    ST st = static_cast<ST>(spVal64);
    ReleaseAssert(IntegerCanBeRepresentedIn<T>(st));
    T val = SafeIntegerCast<T>(st);
    return std::to_string(val);
}

std::string GetSpecializedValueAsString(DeegenBytecodeOperandType ty, uint64_t spVal64)
{
    switch (ty)
    {
    case DeegenBytecodeOperandType::Int8:
    {
        return GetSpecializedValueAsStringImpl<int8_t>(spVal64);
    }
    case DeegenBytecodeOperandType::UInt8:
    {
        return GetSpecializedValueAsStringImpl<uint8_t>(spVal64);
    }
    case DeegenBytecodeOperandType::Int16:
    {
        return GetSpecializedValueAsStringImpl<int16_t>(spVal64);
    }
    case DeegenBytecodeOperandType::UInt16:
    {
        return GetSpecializedValueAsStringImpl<uint16_t>(spVal64);
    }
    case DeegenBytecodeOperandType::Int32:
    {
        return GetSpecializedValueAsStringImpl<int32_t>(spVal64);
    }
    case DeegenBytecodeOperandType::UInt32:
    {
        return GetSpecializedValueAsStringImpl<uint32_t>(spVal64);
    }
    default:
        ReleaseAssert(false && "unexpected type");
    }   /* switch ty */
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
        // Use std::map because we want 'specializedVal' to be sorted
        // Also we cast it to int64_t so that negative values show up first
        //
        std::map<int64_t /*specializedVal*/, std::vector<BytecodeVariantDefinition*>> spValMap;
        std::vector<BytecodeVariantDefinition*> unspecializedVariantList;
        for (BytecodeVariantDefinition* def : S)
        {
            BcOperandKind kind = def->m_list[k]->GetKind();
            if (kind == BcOperandKind::SpecializedLiteral)
            {
                hasSpecializedLiteral = true;
                BcOpSpecializedLiteral* sl = assert_cast<BcOpSpecializedLiteral*>(def->m_list[k].get());
                ReleaseAssert(sl->GetLiteralType() == opTypes[k]);
                uint64_t specializedVal = sl->m_concreteValue;
                spValMap[static_cast<int64_t>(specializedVal)].push_back(def);
            }
            else
            {
                ReleaseAssert(kind == BcOperandKind::Literal);
                ReleaseAssert(assert_cast<BcOpLiteral*>(def->m_list[k].get())->GetLiteralType() == opTypes[k]);
                unspecializedVariantList.push_back(def);
            }
        }
        if (hasSpecializedLiteral)
        {
            // This literal operand has specialized variants
            // We should enumerate through each specialized variant and recurse,
            // then finally fallback to the default variant (if any)
            //
            for (auto& it : spValMap)
            {
                uint64_t spVal64 = static_cast<uint64_t>(it.first);
                auto& spVarList = it.second;
                ReleaseAssert(spVarList.size() > 0);

                std::string operandName =  S[0]->m_list[k]->OperandName();
                std::string spVal = GetSpecializedValueAsString(opTypes[k], spVal64);
                fprintf(fp, "%sif (ops.%s.m_value == %s) {\n", extraIndentStr.c_str(), operandName.c_str(), spVal.c_str());
                selectedType.push_back(opTypes[k]);

                GenerateVariantSelectorImpl(fp, opTypes, spVarList, selectedType, k + 1, true /*isFirstCheckForK*/, extraIndent + 4);

                selectedType.pop_back();
                fprintf(fp, "%s}\n", extraIndentStr.c_str());
            }

            if (unspecializedVariantList.size() > 0)
            {
                selectedType.push_back(opTypes[k]);
                GenerateVariantSelectorImpl(fp, opTypes, unspecializedVariantList, selectedType, k + 1, true /*isFirstCheckForK*/, extraIndent);
                selectedType.pop_back();
            }
            return;
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

std::string WARN_UNUSED DumpAuditFileAsm(llvm::Module* module)
{
    ReleaseAssert(module != nullptr);
    std::unique_ptr<llvm::Module> clone = llvm::CloneModule(*module);
    return CompileLLVMModuleToAssemblyFile(clone.get(), llvm::Reloc::Static, llvm::CodeModel::Small);
}

std::string WARN_UNUSED DumpAuditFileIR(llvm::Module* module)
{
    ReleaseAssert(module != nullptr);
    std::unique_ptr<llvm::Module> clone = llvm::CloneModule(*module);
    return DumpLLVMModuleAsString(clone.get());
}

}   // anonymous namespace

ProcessBytecodeDefinitionForInterpreterResult WARN_UNUSED ProcessBytecodeDefinitionForInterpreter(std::unique_ptr<llvm::Module> module)
{
    using namespace llvm;

    ProcessBytecodeDefinitionForInterpreterResult finalRes;

    AnonymousFile hdrOut;
    FILE* fp = hdrOut.GetFStream("w");

    AnonymousFile hdrPreheader;
    FILE* preFp = hdrPreheader.GetFStream("w");

    // The lambda capture analyze pass must be called first before ANY transformation is performed
    //
    DeegenAnalyzeLambdaCapturePass::AddAnnotations(module.get());
    DesugarAndSimplifyLLVMModule(module.get(), DesugaringLevel::PerFunctionSimplifyOnly);
    AstInlineCache::PreprocessModule(module.get());
    AstMakeCall::PreprocessModule(module.get());
    AstSlowPath::PreprocessModule(module.get());

    // After all the preprocessing stage, the lambda capture analyze results are no longer useful. Remove them now.
    //
    DeegenAnalyzeLambdaCapturePass::RemoveAnnotations(module.get());
    DesugarAndSimplifyLLVMModule(module.get(), DesugaringLevel::PerFunctionSimplifyOnlyAggresive);

    std::vector<std::vector<std::unique_ptr<BytecodeVariantDefinition>>> defs = BytecodeVariantDefinition::ParseAllFromModule(module.get());

    std::vector<std::unique_ptr<Module>> allBytecodeFunctions;
    std::vector<std::string> allReturnContinuationNames;

    std::unordered_map<BytecodeVariantDefinition*, std::unique_ptr<BytecodeIrInfo>> bvdImplMap;
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

            ReleaseAssert(!bvdImplMap.count(bytecodeVariantDef.get()));
            bvdImplMap[bytecodeVariantDef.get()] = std::make_unique<BytecodeIrInfo>(BytecodeIrInfo::Create(bytecodeVariantDef.get(), implFunc));
        }
    }

    for (auto& bytecodeDef : defs)
    {
        std::string generatedClassName;

        size_t totalCreatedBytecodeFunctionsInThisBytecode = 0;
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

            // Pass the operands with 'const&' since the Operands struct may contain sub-integer fields, which could require Clang to do weird ABI lowerings that hamper LLVM optimization
            //
            fprintf(fp, "    void ALWAYS_INLINE Create%s(%s) {\n", def->m_bytecodeName.c_str(), hasAnyOperandsToProvide ? "const Operands& ops" : "");

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

            fprintf(fp, "private:\n");
        }

        std::vector<std::string> allBytecodeMetadataDefinitionNames;

        struct BcTraitInfo
        {
            BcTraitInfo()
                : m_length(static_cast<size_t>(-1))
                , m_outputOperandOffset(static_cast<size_t>(-1))
                , m_branchOperandOffset(static_cast<size_t>(-1))
                , m_variantOrd(static_cast<size_t>(-1))
                , m_isBarrier(false)
                , m_mayMakeTailCall(false)
            { }

            size_t m_length;
            size_t m_outputOperandOffset;
            size_t m_branchOperandOffset;
            size_t m_variantOrd;
            bool m_isBarrier;
            bool m_mayMakeTailCall;
        };

        // Only contains the primitive (non-quickening) variants
        //
        std::unordered_map<size_t /*opcodeVariantOrdinal*/, BcTraitInfo> bytecodeTraitInfoMap;

        std::unordered_map<size_t /*variantOrd*/, size_t /*opcodeVariantOrdinal*/> opcodeVariantOrdinalMap;

        size_t bytecodeDfgNsdLength = static_cast<size_t>(-1);
        size_t currentBytecodeVariantOrdinal = 0;
        for (auto& bytecodeVariantDef : bytecodeDef)
        {
            BcTraitInfo traitInfo;

            ReleaseAssert(bvdImplMap.count(bytecodeVariantDef.get()));
            BytecodeIrInfo* bii = bvdImplMap[bytecodeVariantDef.get()].get();
            std::unique_ptr<Module> resultModule = InterpreterBytecodeImplCreator::DoLoweringForAll(*bii);
            std::vector<std::string> affliatedFunctionNameList = bii->m_affliatedBytecodeFnNames;

            traitInfo.m_isBarrier = !bytecodeVariantDef->BytecodeMayFallthroughToNextBytecode();
            traitInfo.m_mayMakeTailCall = bytecodeVariantDef->BytecodeMayMakeTailCall();

            size_t totalSubVariantsInThisVariant = 1 + affliatedFunctionNameList.size();
            totalCreatedBytecodeFunctionsInThisBytecode += totalSubVariantsInThisVariant;
            std::string variantMainFunctionName = BytecodeIrInfo::ToInterpreterName(bii->m_interpreterMainComponent->m_identFuncName);
            for (auto& it : bii->m_allRetConts)
            {
                std::string fnName = BytecodeIrInfo::ToInterpreterName(it->m_identFuncName);
                Function* func = resultModule->getFunction(fnName);
                ReleaseAssert(func != nullptr);
                ReleaseAssert(func->hasExternalLinkage());
                ReleaseAssert(!func->empty());
                allReturnContinuationNames.push_back(func->getName().str());
            }

            finalRes.m_auditFiles.push_back(std::make_pair(variantMainFunctionName + ".s", DumpAuditFileAsm(resultModule.get())));
            finalRes.m_auditFiles.push_back(std::make_pair(variantMainFunctionName + ".ll", DumpAuditFileIR(resultModule.get())));
            allBytecodeFunctions.push_back(std::move(resultModule));
            cdeclNameForVariants.push_back(variantMainFunctionName);
            for (std::string& fnName : affliatedFunctionNameList)
            {
                cdeclNameForVariants.push_back(BytecodeIrInfo::ToInterpreterName(fnName));
            }

            // If this variant has metadata, it's time to generate the corresponding definition now
            //
            bool hasOutlinedMetadata = bytecodeVariantDef->HasBytecodeMetadata() && !bytecodeVariantDef->IsBytecodeMetadataInlined();
            std::string bmsClassName;   // only populated if hasOutlinedMetadata
            if (hasOutlinedMetadata)
            {
                BytecodeMetadataStruct* bms = bytecodeVariantDef->m_bytecodeMetadataMaybeNull.get();

                BytecodeMetadataStructBase::StructInfo bmsInfo = bytecodeVariantDef->GetMetadataStructInfo();

                bmsClassName = "GeneratedDeegenBytecodeMetadata_" + bytecodeVariantDef->m_bytecodeName + "_" + std::to_string(bytecodeVariantDef->m_variantOrd);
                allBytecodeMetadataDefinitionNames.push_back(bmsClassName);
                fprintf(preFp, "using %s = DeegenMetadata<\n", bmsClassName.c_str());
                fprintf(preFp, "    %d /*alignment*/,\n", SafeIntegerCast<int>(bmsInfo.alignment));
                fprintf(preFp, "    %d /*size*/,\n", SafeIntegerCast<int>(bmsInfo.allocSize));

                std::vector<BytecodeMetadataElement*> initInfo = bms->CollectInitializationInfo();
                fprintf(preFp, "    MakeNTTPTuple(");
                std::vector<std::pair<size_t /*offset*/, std::vector<uint8_t> /*initVal*/>> initVals;
                for (BytecodeMetadataElement* e : initInfo)
                {
                    ReleaseAssert(e->HasInitValue());
                    initVals.push_back(std::make_pair(e->GetStructOffset(), e->GetInitValue()));
                }
                std::sort(initVals.begin(), initVals.end());
                for (size_t i = 0; i < initVals.size(); i++)
                {
                    size_t offset = initVals[i].first;
                    std::vector<uint8_t>& val = initVals[i].second;
                    if (i > 0) { fprintf(preFp, ","); }
                    fprintf(preFp, "\n        DeegenMetadataInitRecord<%d> { .offset = %d, .initData = { ", SafeIntegerCast<int>(val.size()), SafeIntegerCast<int>(offset));
                    for (size_t k = 0; k < val.size(); k++)
                    {
                        uint8_t v = val[k];
                        if (k > 0) { fprintf(preFp, ", "); }
                        fprintf(preFp, "%d", static_cast<int>(v));
                    }
                    fprintf(preFp, " } }");
                }
                fprintf(preFp, ")\n");
                fprintf(preFp, ">;\n\n");
            }

            // Generate the implementation that emits the bytecode
            //
            fprintf(fp, "    void DeegenCreateImpl%u(", SafeIntegerCast<unsigned int>(bytecodeVariantDef->m_variantOrd));
            for (size_t i = 0; i < bytecodeVariantDef->m_originalOperandTypes.size(); i++)
            {
                if (i > 0)
                {
                    fprintf(fp, ", ");
                }
                std::string attr = "";
                if (bytecodeVariantDef->m_list[i]->IsElidedFromBytecodeStruct())
                {
                    attr = "[[maybe_unused]] ";
                }
                if (bytecodeVariantDef->m_originalOperandTypes[i] == DeegenBytecodeOperandType::BytecodeSlotOrConstant)
                {
                    if (bytecodeVariantDef->m_list[i]->GetKind() == BcOperandKind::Slot)
                    {
                        fprintf(fp, "%sLocal param%d", attr.c_str(), static_cast<int>(i));
                    }
                    else
                    {
                        ReleaseAssert(bytecodeVariantDef->m_list[i]->GetKind() == BcOperandKind::Constant);
                        fprintf(fp, "%sTValue param%d", attr.c_str(), static_cast<int>(i));
                    }
                }
                else
                {
                    fprintf(fp, "%s%s param%d", attr.c_str(), GetVariantCppTypeNameForDeegenBytecodeOperandType(bytecodeVariantDef->m_originalOperandTypes[i]).c_str(), static_cast<int>(i));
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
            fprintf(fp, "        static constexpr size_t x_opcode = CRTP::template GetBytecodeOpcodeBase<%s>() + %d;\n", generatedClassName.c_str(), SafeIntegerCast<int>(currentBytecodeVariantOrdinal));
            fprintf(fp, "        CRTP* crtp = static_cast<CRTP*>(this);\n");

            for (size_t i = 0; i < bytecodeVariantDef->m_list.size(); i++)
            {
                std::unique_ptr<BcOperand>& operand = bytecodeVariantDef->m_list[i];

                if (operand->GetKind() == BcOperandKind::Constant)
                {
                    BcOpConstant* bcOpCst = assert_cast<BcOpConstant*>(operand.get());
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

                if (operand->GetKind() == BcOperandKind::SpecializedLiteral)
                {
                    BcOpSpecializedLiteral* spLit = assert_cast<BcOpSpecializedLiteral*>(operand.get());
                    uint64_t spVal64 = spLit->m_concreteValue;
                    std::string spVal = GetSpecializedValueAsString(spLit->GetLiteralType(), spVal64);
                    fprintf(fp, "        assert(param%d == %s);\n", static_cast<int>(i), spVal.c_str());
                    ReleaseAssert(operand->IsElidedFromBytecodeStruct());
                }

                if (operand->IsElidedFromBytecodeStruct())
                {
                    continue;
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

            fprintf(fp, "        uint8_t* base = crtp->Reserve(%d);\n", SafeIntegerCast<int>(bytecodeVariantDef->GetBytecodeStructLength()));

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
                fprintf(fp, "        UnalignedStore<StorageTypeForOperand%d>(base + %u, SafeIntegerCast<StorageTypeForOperand%d>(originalVal%d));\n", static_cast<int>(i), SafeIntegerCast<unsigned int>(offset), static_cast<int>(i), static_cast<int>(i));
            }

            if (bytecodeVariantDef->m_hasOutputValue)
            {
                size_t offset = bytecodeVariantDef->m_outputOperand->GetOffsetInBytecodeStruct();
                ReleaseAssert(bytecodeVariantDef->m_outputOperand->GetKind() == BcOperandKind::Slot);
                int numBitsInBytecodeStruct = static_cast<int>(bytecodeVariantDef->m_outputOperand->GetSizeInBytecodeStruct()) * 8;
                fprintf(fp, "        UnalignedStore<uint%d_t>(base + %u, SafeIntegerCast<uint%d_t>(paramOut.m_localOrd));\n", numBitsInBytecodeStruct, SafeIntegerCast<unsigned int>(offset), numBitsInBytecodeStruct);
                traitInfo.m_outputOperandOffset = offset;
            }

            ReleaseAssertIff(hasOutlinedMetadata, bytecodeVariantDef->m_metadataPtrOffset.get() != nullptr);
            if (hasOutlinedMetadata)
            {
                size_t offset = bytecodeVariantDef->m_metadataPtrOffset->GetOffsetInBytecodeStruct();
                ReleaseAssert(bytecodeVariantDef->m_metadataPtrOffset->GetKind() == BcOperandKind::Literal);
                ReleaseAssert(bytecodeVariantDef->m_metadataPtrOffset->GetSizeInBytecodeStruct() == 4);
                ReleaseAssert(bmsClassName != "");
                fprintf(fp, "        crtp->template RegisterMetadataField<%s>(base + %u);\n", bmsClassName.c_str(), SafeIntegerCast<unsigned int>(offset));
            }

            bool hasInlinedMetadata = bytecodeVariantDef->HasBytecodeMetadata() && bytecodeVariantDef->IsBytecodeMetadataInlined();
            ReleaseAssertIff(hasInlinedMetadata || hasOutlinedMetadata, bytecodeVariantDef->HasBytecodeMetadata());
            ReleaseAssertIff(hasInlinedMetadata, bytecodeVariantDef->m_inlinedMetadata.get() != nullptr);
            ReleaseAssert(!(hasInlinedMetadata && hasOutlinedMetadata));
            if (hasInlinedMetadata)
            {
                size_t inlinedMetadataOffset = bytecodeVariantDef->m_inlinedMetadata->GetOffsetInBytecodeStruct();
                std::vector<BytecodeMetadataElement*> initInfo = bytecodeVariantDef->m_bytecodeMetadataMaybeNull->CollectInitializationInfo();
                std::vector<std::pair<size_t /*offset*/, uint8_t /*initVal*/>> initVals;
                for (BytecodeMetadataElement* e : initInfo)
                {
                    ReleaseAssert(e->HasInitValue());
                    std::vector<uint8_t> initVal = e->GetInitValue();
                    size_t offsetBegin = e->GetStructOffset();
                    for (size_t i = 0; i < initVal.size(); i++)
                    {
                        initVals.push_back(std::make_pair(offsetBegin + i, initVal[i]));
                    }
                }
                std::sort(initVals.begin(), initVals.end());
                {
                    std::unordered_set<size_t> checkUnique;
                    for (auto& it : initVals)
                    {
                        ReleaseAssert(!checkUnique.count(it.first));
                        checkUnique.insert(it.first);
                        ReleaseAssert(it.first < bytecodeVariantDef->m_inlinedMetadata->m_size);
                    }
                }
                for (size_t i = 0; i < initVals.size(); i++)
                {
                    size_t offset = inlinedMetadataOffset + initVals[i].first;
                    uint8_t val = initVals[i].second;
                    fprintf(fp, "        UnalignedStore<uint8_t>(base + %u, %d);\n", SafeIntegerCast<unsigned int>(offset), static_cast<int>(val));
                }
            }

            if (bytecodeVariantDef->m_hasConditionalBranchTarget)
            {
                size_t offset = bytecodeVariantDef->m_condBrTarget->GetOffsetInBytecodeStruct();
                // Initialize the unconditional jump to jump to itself, just so that we have no uninitialized value
                //
                assert(bytecodeVariantDef->m_condBrTarget->GetSizeInBytecodeStruct() == 2);
                fprintf(fp, "        UnalignedStore<int16_t>(base + %u, 0);\n", SafeIntegerCast<unsigned int>(offset));
                traitInfo.m_branchOperandOffset = offset;
            }

            fprintf(fp, "        crtp->MarkWritten(%d);\n", SafeIntegerCast<int>(bytecodeVariantDef->GetBytecodeStructLength()));
            fprintf(fp, "        return;\n");

            fprintf(fp, "    }\n\n");

            // Generate the implementation that decodes the bytecode
            //
            fprintf(fp, "    Operands ALWAYS_INLINE DeegenDecodeImpl%u(size_t bcPos)\n", SafeIntegerCast<unsigned int>(bytecodeVariantDef->m_variantOrd));
            fprintf(fp, "    {\n");

            fprintf(fp, "        CRTP* crtp = static_cast<CRTP*>(this);\n");
            fprintf(fp, "        [[maybe_unused]] uint8_t* base = crtp->GetBytecodeStart() + bcPos;\n");
            fprintf(fp, "        assert(crtp->GetCanonicalizedOpcodeFromOpcode(UnalignedLoad<uint16_t>(base)) == CRTP::template GetBytecodeOpcodeBase<%s>() + %d);\n", generatedClassName.c_str(), SafeIntegerCast<int>(currentBytecodeVariantOrdinal));

            for (size_t i = 0; i < bytecodeVariantDef->m_list.size(); i++)
            {
                std::unique_ptr<BcOperand>& operand = bytecodeVariantDef->m_list[i];

                if (operand->IsElidedFromBytecodeStruct())
                {
                    if (operand->GetKind() == BcOperandKind::SpecializedLiteral)
                    {
                        BcOpSpecializedLiteral* spLit = assert_cast<BcOpSpecializedLiteral*>(operand.get());
                        uint64_t spVal64 = spLit->m_concreteValue;
                        std::string spVal = GetSpecializedValueAsString(spLit->GetLiteralType(), spVal64);
                        fprintf(fp, "        %s param%d = %s;\n",
                                GetVariantCppTypeNameForDeegenBytecodeOperandType(bytecodeVariantDef->m_originalOperandTypes[i]).c_str(),
                                static_cast<int>(i),
                                spVal.c_str());
                    }
                    else
                    {
                        ReleaseAssert(operand->GetKind() == BcOperandKind::Constant);
                        BcOpConstant* cst = assert_cast<BcOpConstant*>(operand.get());
                        ReleaseAssert(cst->m_typeMask == x_typeSpeculationMaskFor<tNil>);
                        fprintf(fp, "        TValue param%d = TValue::Create<tNil>();\n",static_cast<int>(i));
                    }
                    continue;
                }

                std::string tyName = std::string(operand->IsSignedValue() ? "" : "u") + "int" + std::to_string(operand->GetSizeInBytecodeStruct() * 8) + "_t";
                fprintf(fp, "        using StorageTypeForOperand%d = %s;\n", static_cast<int>(i), tyName.c_str());
                fprintf(fp, "        StorageTypeForOperand%d storageVal%d = UnalignedLoad<StorageTypeForOperand%d>(base + %u);\n",
                        static_cast<int>(i), static_cast<int>(i),
                        static_cast<int>(i), static_cast<unsigned int>(operand->GetOffsetInBytecodeStruct()));

                if (bytecodeVariantDef->m_originalOperandTypes[i] == DeegenBytecodeOperandType::BytecodeSlotOrConstant)
                {
                    if (operand->GetKind() == BcOperandKind::Slot)
                    {
                        fprintf(fp, "        Local param%d = Local { storageVal%d };\n", static_cast<int>(i), static_cast<int>(i));
                    }
                    else
                    {
                        ReleaseAssert(operand->GetKind() == BcOperandKind::Constant);
                        fprintf(fp, "        TValue param%d = crtp->GetConstantFromConstantTable(storageVal%d);\n", static_cast<int>(i), static_cast<int>(i));
                    }
                }
                else if (operand->GetKind() == BcOperandKind::Constant)
                {
                    fprintf(fp, "        TValue param%d = crtp->GetConstantFromConstantTable(storageVal%d);\n", static_cast<int>(i), static_cast<int>(i));
                }
                else
                {
                    fprintf(fp, "        %s param%d = %s { storageVal%d };\n",
                            GetVariantCppTypeNameForDeegenBytecodeOperandType(bytecodeVariantDef->m_originalOperandTypes[i]).c_str(),
                            static_cast<int>(i),
                            GetVariantCppTypeNameForDeegenBytecodeOperandType(bytecodeVariantDef->m_originalOperandTypes[i]).c_str(),
                            static_cast<int>(i));
                }
            }

            if (bytecodeVariantDef->m_hasOutputValue)
            {
                std::unique_ptr<BcOpSlot>& operand = bytecodeVariantDef->m_outputOperand;
                std::string tyName = std::string(operand->IsSignedValue() ? "" : "u") + "int" + std::to_string(operand->GetSizeInBytecodeStruct() * 8) + "_t";
                fprintf(fp, "        Local param_output = Local { UnalignedLoad<%s>(base + %u) };\n",
                        tyName.c_str(), static_cast<unsigned int>(operand->GetOffsetInBytecodeStruct()));
            }

            fprintf(fp, "        return Operands {\n");
            {
                bool isFirstTerm = true;
                for (size_t i = 0; i < bytecodeVariantDef->m_list.size(); i++)
                {
                    if (!isFirstTerm) { fprintf(fp, ",\n"); }
                    isFirstTerm = false;
                    fprintf(fp, "            .%s = param%d", bytecodeVariantDef->m_opNames[i].c_str(), static_cast<int>(i));
                }

                if (bytecodeVariantDef->m_hasOutputValue)
                {
                    if (!isFirstTerm) { fprintf(fp, ",\n"); }
                    isFirstTerm = false;
                    fprintf(fp, "            .output = param_output");
                }
            }

            fprintf(fp, "\n        };\n");

            fprintf(fp, "    }\n\n");

            // Generate the Read/Write/Clobber information getters
            //
            fprintf(fp, "%s\n", bytecodeVariantDef->m_rcwInfoFuncs.c_str());

            fprintf(fp, "%s\n", bytecodeVariantDef->m_bcIntrinsicInfoGetterFunc.c_str());

            // Generate the DFG node-specific-data emitters, which should simply carry all the literals in this bytecode
            //
            size_t dfgNsdLength = 0;
            fprintf(fp, "    void PopulateDfgNodeSpecificDataImpl%u([[maybe_unused]] uint8_t* nsdPtr, size_t bcPos)\n", SafeIntegerCast<unsigned int>(bytecodeVariantDef->m_variantOrd));
            fprintf(fp, "    {\n");
            fprintf(fp, "        [[maybe_unused]] Operands ops = DeegenDecodeImpl%u(bcPos);\n", SafeIntegerCast<unsigned int>(bytecodeVariantDef->m_variantOrd));
            for (size_t i = 0; i < bytecodeVariantDef->m_list.size(); i++)
            {
                BcOperand* operand = bytecodeVariantDef->m_list[i].get();
                if (operand->GetKind() == BcOperandKind::Literal || operand->GetKind() == BcOperandKind::SpecializedLiteral)
                {
                    BcOpLiteral* lit = assert_cast<BcOpLiteral*>(operand);
                    size_t numBytes = lit->m_numBytes;
                    bool isSigned = lit->m_isSigned;
                    fprintf(fp, "        UnalignedStore<%sint%d_t>(nsdPtr + %d, ops.%s.m_value);\n",
                            (isSigned ? "" : "u"), static_cast<int>(numBytes * 8),
                            static_cast<int>(dfgNsdLength),
                            lit->OperandName().c_str());
                    dfgNsdLength += numBytes;
                }
            }
            fprintf(fp, "    }\n\n");

            if (bytecodeDfgNsdLength == static_cast<size_t>(-1))
            {
                bytecodeDfgNsdLength = dfgNsdLength;
            }
            else
            {
                // All the variants' node-specific-data must have the same length, as they all just consist of the literal fields in the bytecode
                //
                ReleaseAssert(bytecodeDfgNsdLength == dfgNsdLength);
            }

            ReleaseAssert(!opcodeVariantOrdinalMap.count(bytecodeVariantDef->m_variantOrd));
            opcodeVariantOrdinalMap[bytecodeVariantDef->m_variantOrd] = currentBytecodeVariantOrdinal;

            traitInfo.m_length = bytecodeVariantDef->GetBytecodeStructLength();
            traitInfo.m_variantOrd = bytecodeVariantDef->m_variantOrd;
            ReleaseAssert(!bytecodeTraitInfoMap.count(currentBytecodeVariantOrdinal));
            bytecodeTraitInfoMap[currentBytecodeVariantOrdinal] = traitInfo;
            currentBytecodeVariantOrdinal += totalSubVariantsInThisVariant;
        }

        ReleaseAssert(currentBytecodeVariantOrdinal == totalCreatedBytecodeFunctionsInThisBytecode);
        ReleaseAssert(cdeclNameForVariants.size() == totalCreatedBytecodeFunctionsInThisBytecode);

        fprintf(fp, "public:\n");
        fprintf(fp, "    Operands WARN_UNUSED Decode%s(size_t bcPos)\n", bytecodeDef[0]->m_bytecodeName.c_str());
        fprintf(fp, "    {\n");
        fprintf(fp, "        CRTP* crtp = static_cast<CRTP*>(this);\n");
        fprintf(fp, "        uint8_t* base = crtp->GetBytecodeStart() + bcPos;\n");
        fprintf(fp, "        size_t opcode = UnalignedLoad<uint16_t>(base);\n");
        fprintf(fp, "        assert(opcode >= CRTP::template GetBytecodeOpcodeBase<%s>());\n", generatedClassName.c_str());
        fprintf(fp, "        opcode -= CRTP::template GetBytecodeOpcodeBase<%s>();\n", generatedClassName.c_str());
        fprintf(fp, "        assert(opcode < %d);\n", static_cast<int>(currentBytecodeVariantOrdinal));
        fprintf(fp, "        assert(x_isVariantEmittable[opcode]);\n");
        fprintf(fp, "        switch (opcode)\n");
        fprintf(fp, "        {\n");

        for (auto& bytecodeVariantDef : bytecodeDef)
        {
            ReleaseAssert(opcodeVariantOrdinalMap.count(bytecodeVariantDef->m_variantOrd));
            fprintf(fp, "        case %d:\n", static_cast<int>(opcodeVariantOrdinalMap[bytecodeVariantDef->m_variantOrd]));
            fprintf(fp, "            return DeegenDecodeImpl%d(bcPos);\n", static_cast<int>(bytecodeVariantDef->m_variantOrd));
        }
        fprintf(fp, "        default:\n");
        fprintf(fp, "        assert(false && \"unexpected opcode\");\n");
        fprintf(fp, "        __builtin_unreachable();\n");
        fprintf(fp, "        } /*switch opcode*/\n");
        fprintf(fp, "    }\n\n");

        fprintf(fp, "    template<size_t variantOrd>\n");
        fprintf(fp, "    Operands WARN_UNUSED ALWAYS_INLINE Decode%s_Variant(size_t bcPos)\n", bytecodeDef[0]->m_bytecodeName.c_str());
        fprintf(fp, "    {\n        ");
        for (auto& bytecodeVariantDef : bytecodeDef)
        {
            ReleaseAssert(opcodeVariantOrdinalMap.count(bytecodeVariantDef->m_variantOrd));
            fprintf(fp, "if constexpr(variantOrd == %d) {\n", static_cast<int>(bytecodeVariantDef->m_variantOrd));
            fprintf(fp, "            return DeegenDecodeImpl%d(bcPos);\n", static_cast<int>(bytecodeVariantDef->m_variantOrd));
            fprintf(fp, "        } else ");
        }
        fprintf(fp, "{\n            static_assert(type_dependent_false<decltype(variantOrd)>::value);\n        }\n    }\n");

        fprintf(fp, "protected:\n");
        fprintf(fp, "    static constexpr size_t GetNumVariants() { return %d; }\n", SafeIntegerCast<int>(currentBytecodeVariantOrdinal));

        // Emit code that tells whether a variant is a primitive variant or a quickening variant
        //
        fprintf(fp, "    static constexpr std::array<bool, %d> x_isVariantEmittable = { ", SafeIntegerCast<int>(currentBytecodeVariantOrdinal));
        for (size_t i = 0; i < currentBytecodeVariantOrdinal; i++)
        {
            if (i > 0) { fprintf(fp, ", "); }
            fprintf(fp, "%s", bytecodeTraitInfoMap.count(i) ? "true" : "false");
        }
        fprintf(fp, " };\n");

        // Emit code that tells the length, output operand offset and branch operand offset of each primitive variant.
        // We could have emitted info about the quickening variant as well, but since it is not needed, we just stay simple.
        //
        fprintf(fp, "    static constexpr std::array<uint8_t, %d> x_bytecodeLength = { ", SafeIntegerCast<int>(currentBytecodeVariantOrdinal));
        for (size_t i = 0; i < currentBytecodeVariantOrdinal; i++)
        {
            if (i > 0) { fprintf(fp, ", "); }
            uint8_t len = 255;
            if (bytecodeTraitInfoMap.count(i))
            {
                ReleaseAssert(bytecodeTraitInfoMap[i].m_length < 255);
                len = static_cast<uint8_t>(bytecodeTraitInfoMap[i].m_length);
            }
            fprintf(fp, "%d", static_cast<int>(len));
        }
        fprintf(fp, " };\n");

        fprintf(fp, "    static constexpr std::array<uint8_t, %d> x_bytecodeOutputOperandOffset = { ", SafeIntegerCast<int>(currentBytecodeVariantOrdinal));
        for (size_t i = 0; i < currentBytecodeVariantOrdinal; i++)
        {
            if (i > 0) { fprintf(fp, ", "); }
            uint8_t val = 255;
            if (bytecodeTraitInfoMap.count(i) && bytecodeTraitInfoMap[i].m_outputOperandOffset != static_cast<size_t>(-1))
            {
                ReleaseAssert(bytecodeTraitInfoMap[i].m_outputOperandOffset < 255);
                val = static_cast<uint8_t>(bytecodeTraitInfoMap[i].m_outputOperandOffset);
            }
            fprintf(fp, "%d", static_cast<int>(val));
        }
        fprintf(fp, " };\n");

        fprintf(fp, "    static constexpr std::array<uint8_t, %d> x_bytecodeBranchOperandOffset = { ", SafeIntegerCast<int>(currentBytecodeVariantOrdinal));
        for (size_t i = 0; i < currentBytecodeVariantOrdinal; i++)
        {
            if (i > 0) { fprintf(fp, ", "); }
            uint8_t val = 255;
            if (bytecodeTraitInfoMap.count(i) && bytecodeTraitInfoMap[i].m_branchOperandOffset != static_cast<size_t>(-1))
            {
                ReleaseAssert(bytecodeTraitInfoMap[i].m_branchOperandOffset < 255);
                val = static_cast<uint8_t>(bytecodeTraitInfoMap[i].m_branchOperandOffset);
            }
            fprintf(fp, "%d", static_cast<int>(val));
        }
        fprintf(fp, " };\n");

        fprintf(fp, "    static constexpr std::array<uint8_t, %d> x_isBytecodeBarrier = { ", SafeIntegerCast<int>(currentBytecodeVariantOrdinal));
        for (size_t i = 0; i < currentBytecodeVariantOrdinal; i++)
        {
            if (i > 0) { fprintf(fp, ", "); }
            uint8_t value = 255;
            if (bytecodeTraitInfoMap.count(i))
            {
                value = (bytecodeTraitInfoMap[i].m_isBarrier ? 1 : 0);
            }
            fprintf(fp, "%d", static_cast<int>(value));
        }
        fprintf(fp, " };\n");

        fprintf(fp, "    static constexpr std::array<uint8_t, %d> x_bytecodeMayMakeTailCall = { ", SafeIntegerCast<int>(currentBytecodeVariantOrdinal));
        for (size_t i = 0; i < currentBytecodeVariantOrdinal; i++)
        {
            if (i > 0) { fprintf(fp, ", "); }
            uint8_t value = 255;
            if (bytecodeTraitInfoMap.count(i))
            {
                value = (bytecodeTraitInfoMap[i].m_mayMakeTailCall ? 1 : 0);
            }
            fprintf(fp, "%d", static_cast<int>(value));
        }
        fprintf(fp, " };\n");

        // If the bytecode is not in a 'SameLengthConstraint' set, it is definitely not allowed to replace it.
        // We will assert at runtime that the replacing bytecode has the same length as the replaced bytecode as well,
        // but providing this information statically allows us to catch more errors statically.
        //
        fprintf(fp, "    static constexpr bool x_isPotentiallyReplaceable = %s;\n",
                (bytecodeDef[0]->m_sameLengthConstraintList.size() > 0) ? "true" : "false");

        // Emit the arrays of RWC information getters
        //
        fprintf(fp, "    using RWCInfoGetterFn = BytecodeRWCInfo(%s<CRTP>::*)(size_t);\n", generatedClassName.c_str());

        auto printRWCGetterArray = [&](std::string rwcName)
        {
            fprintf(fp, "    static constexpr std::array<RWCInfoGetterFn, %d> x_bytecode%sDeclGetters = { ",
                    SafeIntegerCast<int>(currentBytecodeVariantOrdinal),
                    rwcName.c_str());
            for (size_t i = 0; i < currentBytecodeVariantOrdinal; i++)
            {
                if (i > 0) { fprintf(fp, ", "); }
                if (bytecodeTraitInfoMap.count(i))
                {
                    fprintf(fp, "&%s<CRTP>::DeegenGetBytecode%sDeclarationsImpl%d",
                            generatedClassName.c_str(),
                            rwcName.c_str(),
                            static_cast<int>(bytecodeTraitInfoMap[i].m_variantOrd));
                }
                else
                {
                    fprintf(fp, "nullptr");
                }
            }
            fprintf(fp, " };\n");
        };

        printRWCGetterArray("Read");
        printRWCGetterArray("Write");
        printRWCGetterArray("Clobber");

        if (bytecodeDef[0]->m_bcIntrinsicOrd != static_cast<size_t>(-1))
        {
            ReleaseAssert(bytecodeDef[0]->m_bcIntrinsicOrd < 255);
            fprintf(fp, "    static constexpr uint8_t x_bytecodeIntrinsicOrdinal = %d;\n", static_cast<int>(bytecodeDef[0]->m_bcIntrinsicOrd));
            fprintf(fp, "    using BytecodeIntrinsicDefTy = BytecodeIntrinsicInfo::%s;\n", bytecodeDef[0]->m_bcIntrinsicName.c_str());
            fprintf(fp, "    using BytecodeIntrinsicInfoGetterFn = BytecodeIntrinsicDefTy(%s<CRTP>::*)(size_t);\n",
                    generatedClassName.c_str());
            fprintf(fp, "    static constexpr std::array<BytecodeIntrinsicInfoGetterFn, %d> x_bytecodeIntrinsicInfoGetters = { ",
                    SafeIntegerCast<int>(currentBytecodeVariantOrdinal));
            for (size_t i = 0; i < currentBytecodeVariantOrdinal; i++)
            {
                if (i > 0) { fprintf(fp, ", "); }
                if (bytecodeTraitInfoMap.count(i))
                {
                    fprintf(fp, "&%s<CRTP>::GetIntrinsicInfoImpl%d",
                            generatedClassName.c_str(),
                            static_cast<int>(bytecodeTraitInfoMap[i].m_variantOrd));
                }
                else
                {
                    fprintf(fp, "nullptr");
                }
            }
            fprintf(fp, " };\n");
        }
        else
        {
            fprintf(fp, "    static constexpr uint8_t x_bytecodeIntrinsicOrdinal = 255;\n");
            fprintf(fp, "    using BytecodeIntrinsicDefTy = void;\n");
        }

        ReleaseAssert(bytecodeDfgNsdLength != static_cast<size_t>(-1));
        fprintf(fp, "    static constexpr size_t x_dfgNodeSpecificDataLength = %u;\n", static_cast<unsigned int>(bytecodeDfgNsdLength));
        fprintf(fp, "    using DfgNsdInfoWriterFn = void(%s<CRTP>::*)(uint8_t*, size_t);\n", generatedClassName.c_str());
        fprintf(fp, "    static constexpr std::array<DfgNsdInfoWriterFn, %d> x_dfgNsdInfoWriterFns = { ",
                SafeIntegerCast<int>(currentBytecodeVariantOrdinal));
        for (size_t i = 0; i < currentBytecodeVariantOrdinal; i++)
        {
            if (i > 0) { fprintf(fp, ", "); }
            if (bytecodeTraitInfoMap.count(i))
            {
                fprintf(fp, "&%s<CRTP>::PopulateDfgNodeSpecificDataImpl%d",
                        generatedClassName.c_str(),
                        static_cast<int>(bytecodeTraitInfoMap[i].m_variantOrd));
            }
            else
            {
                fprintf(fp, "nullptr");
            }
        }
        fprintf(fp, " };\n");

        // Emit the DFG node-specific-data dump function
        //
        {
            size_t curNsdOffset = 0;
            bool isFirstItem = true;
            fprintf(fp, "\n    static void DumpDfgNodeSpecificDataImpl([[maybe_unused]] FILE* file, [[maybe_unused]] uint8_t* nsdPtr, [[maybe_unused]] bool& shouldPrefixCommaOnFirstItem)\n");
            fprintf(fp, "    {\n");
            for (size_t i = 0; i < bytecodeDef[0]->m_list.size(); i++)
            {
                BcOperand* operand = bytecodeDef[0]->m_list[i].get();
                if (operand->GetKind() == BcOperandKind::Literal || operand->GetKind() == BcOperandKind::SpecializedLiteral)
                {
                    BcOpLiteral* lit = assert_cast<BcOpLiteral*>(operand);
                    size_t numBytes = lit->m_numBytes;
                    bool isSigned = lit->m_isSigned;
                    fprintf(fp, "        {\n");
                    fprintf(fp, "            %sint%d_t value = UnalignedLoad<%sint%d_t>(nsdPtr + %u);\n",
                            (isSigned ? "" : "u"), static_cast<int>(numBytes * 8),
                            (isSigned ? "" : "u"), static_cast<int>(numBytes * 8),
                            static_cast<unsigned int>(curNsdOffset));
                    curNsdOffset += numBytes;
                    if (isFirstItem)
                    {
                        fprintf(fp, "            if (shouldPrefixCommaOnFirstItem) { fprintf(file, \", \"); }\n");
                    }
                    fprintf(fp, "            fprintf(file, \"%s%s=%%%s\", static_cast<%s>(value));\n",
                            (isFirstItem ? "" : ", "),
                            lit->OperandName().c_str(),
                            (isSigned ? "lld" : "llu"),
                            (isSigned ? "long long" : "unsigned long long"));
                    fprintf(fp, "            shouldPrefixCommaOnFirstItem = true;\n");
                    fprintf(fp, "        }\n");
                    isFirstItem = false;
                }
            }
            fprintf(fp, "    }\n\n");
            ReleaseAssert(curNsdOffset == bytecodeDfgNsdLength);
        }

        // Emit out the bytecode metadata type list for this bytecode
        //
        fprintf(preFp, "using %s_BytecodeMetadataInfo = std::tuple<", generatedClassName.c_str());
        for (size_t i = 0; i < allBytecodeMetadataDefinitionNames.size(); i++)
        {
            if (i > 0) { fprintf(preFp, ", "); }
            fprintf(preFp, "%s", allBytecodeMetadataDefinitionNames[i].c_str());
        }
        fprintf(preFp, ">;\n\n");

        fprintf(fp, "};\n\n");
    }

    fclose(preFp);
    fclose(fp);
    finalRes.m_generatedHeaderFile = hdrPreheader.GetFileContents() + hdrOut.GetFileContents();

    finalRes.m_referenceModule = std::move(module);
    for (size_t i = 0; i < allBytecodeFunctions.size(); i++)
    {
        finalRes.m_bytecodeModules.push_back(std::move(allBytecodeFunctions[i]));
    }

    finalRes.m_returnContinuationNameList = allReturnContinuationNames;

    // Important to generate the JSON file after all processing, since the interpreter processor
    // also removes unused slowpaths and return continuations from the bytecode definition,
    // and renames the slow paths to unique names
    //
    {
        std::vector<nlohmann::json> allJsonList;
        for (auto& bytecodeDef : defs)
        {
            for (auto& bytecodeVariantDef : bytecodeDef)
            {
                ReleaseAssert(bvdImplMap.count(bytecodeVariantDef.get()));
                std::unique_ptr<BytecodeIrInfo>& info = bvdImplMap[bytecodeVariantDef.get()];
                allJsonList.push_back(info->SaveToJSON());
            }
        }
        ReleaseAssert(allJsonList.size() == bvdImplMap.size());
        finalRes.m_bytecodeInfoJson = std::move(allJsonList);
    }

    return finalRes;
}

}   // namespace dast
