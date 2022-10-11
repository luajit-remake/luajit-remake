#include "misc_llvm_helper.h"

#include "deegen_bytecode_operand.h"
#include "deegen_interpreter_bytecode_impl_creator.h"
#include "bytecode_definition_utils.h"
#include "runtime_utils.h"

namespace dast {

llvm::Value* WARN_UNUSED BcOperand::GetOperandValueFromBytecodeStruct(InterpreterBytecodeImplCreator* ifi, llvm::BasicBlock* targetBB)
{
    using namespace llvm;
    LLVMContext& ctx = ifi->GetModule()->getContext();
    llvm::Value* bytecodeStruct = ifi->GetCurBytecode();
    ReleaseAssert(llvm_value_has_type<void*>(bytecodeStruct));
    if (IsElidedFromBytecodeStruct())
    {
        return nullptr;
    }

    size_t offsetInBytecodeStruct = GetOffsetInBytecodeStruct();
    size_t numBytesInBytecodeStruct = GetSizeInBytecodeStruct();
    GetElementPtrInst* gep = GetElementPtrInst::CreateInBounds(llvm_type_of<uint8_t>(ctx), bytecodeStruct, { CreateLLVMConstantInt<uint64_t>(ctx, offsetInBytecodeStruct) }, "", targetBB);
    ReleaseAssert(llvm_value_has_type<uint8_t*>(gep));
    Type* storageTypeInBytecodeStruct = Type::getIntNTy(ctx, static_cast<uint32_t>(numBytesInBytecodeStruct * 8));
    LoadInst* storageValue = new LoadInst(storageTypeInBytecodeStruct, gep, "", false /*isVolatile*/, Align(1), targetBB);

    Type* dstType = GetSourceValueFullRepresentationType(ctx);
    Value* result;
    if (IsSignedValue())
    {
        result = CastInst::CreateSExtOrBitCast(storageValue, dstType, "", targetBB);
    }
    else
    {
        result = CastInst::CreateZExtOrBitCast(storageValue, dstType, "", targetBB);
    }
    ReleaseAssert(result != nullptr);
    return result;
}

llvm::Value* WARN_UNUSED BcOpSlot::EmitUsageValueFromBytecodeValue(InterpreterBytecodeImplCreator* ifi, llvm::BasicBlock* targetBB /*out*/, llvm::Value* bytecodeValue)
{
    using namespace llvm;
    LLVMContext& ctx = ifi->GetModule()->getContext();
    Value* stackBase = ifi->GetStackBase();
    ReleaseAssert(bytecodeValue->getType() == GetSourceValueFullRepresentationType(ctx));
    Value* bvPtr = GetElementPtrInst::CreateInBounds(llvm_type_of<uint64_t>(ctx), stackBase, { bytecodeValue }, "", targetBB);
    LoadInst* bv = new LoadInst(llvm_type_of<uint64_t>(ctx), bvPtr, "", targetBB);
    bv->setAlignment(Align(8));
    ReleaseAssert(bv->getType() == GetUsageType(ctx));
    return bv;
}

llvm::Value* WARN_UNUSED BcOpConstant::EmitUsageValueFromBytecodeValue(InterpreterBytecodeImplCreator* ifi, llvm::BasicBlock* targetBB /*out*/, llvm::Value* bytecodeValue)
{
    using namespace llvm;
    LLVMContext& ctx = ifi->GetModule()->getContext();
    Value* codeBlock = ifi->GetCodeBlock();
    if (m_typeMask == x_typeSpeculationMaskFor<tNil>)
    {
        ReleaseAssert(bytecodeValue == nullptr);
        Value* res = CreateLLVMConstantInt(ctx, TValue::Nil().m_value);
        ReleaseAssert(res->getType() == GetUsageType(ctx));
        return res;
    }
    else
    {
        ReleaseAssert(bytecodeValue != nullptr);
        ReleaseAssert(bytecodeValue->getType() == GetSourceValueFullRepresentationType(ctx));
        Value* bvPtr = GetElementPtrInst::CreateInBounds(llvm_type_of<uint64_t>(ctx), codeBlock, { bytecodeValue }, "", targetBB);
        LoadInst* bv = new LoadInst(llvm_type_of<uint64_t>(ctx), bvPtr, "", targetBB);
        bv->setAlignment(Align(8));
        ReleaseAssert(bv->getType() == GetUsageType(ctx));
        return bv;
    }
}

llvm::Value* WARN_UNUSED BcOpLiteral::EmitUsageValueFromBytecodeValue(InterpreterBytecodeImplCreator* /*ifi*/, llvm::BasicBlock* /*targetBB*/ /*out*/, llvm::Value* bytecodeValue)
{
    ReleaseAssert(bytecodeValue != nullptr);
    ReleaseAssert(bytecodeValue->getType() == GetSourceValueFullRepresentationType(bytecodeValue->getContext()));
    ReleaseAssert(bytecodeValue->getType() == GetUsageType(bytecodeValue->getContext()));
    return bytecodeValue;
}

llvm::Value* WARN_UNUSED BcOpSpecializedLiteral::EmitUsageValueFromBytecodeValue(InterpreterBytecodeImplCreator* /*ifi*/, llvm::BasicBlock* targetBB /*out*/, llvm::Value* bytecodeValue)
{
    using namespace llvm;
    ReleaseAssert(bytecodeValue == nullptr);
    LLVMContext& ctx = targetBB->getContext();
    Value* res = ConstantInt::get(ctx, APInt(static_cast<uint32_t>(m_numBytes * 8) /*bitWidth*/, m_concreteValue, m_isSigned));
    ReleaseAssert(res->getType() == GetUsageType(ctx));
    return res;
}

llvm::Value* WARN_UNUSED BcOpBytecodeRangeBase::EmitUsageValueFromBytecodeValue(InterpreterBytecodeImplCreator* ifi, llvm::BasicBlock* targetBB /*out*/, llvm::Value* bytecodeValue)
{
    using namespace llvm;
    LLVMContext& ctx = ifi->GetModule()->getContext();
    ReleaseAssert(bytecodeValue->getType() == GetSourceValueFullRepresentationType(bytecodeValue->getContext()));
    Value* res = GetElementPtrInst::CreateInBounds(llvm_type_of<uint64_t>(ctx), ifi->GetStackBase(), { bytecodeValue }, "", targetBB);
    ReleaseAssert(res->getType() == GetUsageType(ctx));
    return res;
}

llvm::Value* WARN_UNUSED BytecodeVariantDefinition::DecodeBytecodeOpcode(llvm::Value* bytecode, llvm::Instruction* insertBefore)
{
    using namespace llvm;
    static_assert(x_opcodeSizeBytes == 2);
    LLVMContext& ctx = bytecode->getContext();
    ReleaseAssert(llvm_value_has_type<void*>(bytecode));
    Value* opcodeShort = new LoadInst(llvm_type_of<uint16_t>(ctx), bytecode, "", false /*isVolatile*/, Align(1), insertBefore);
    return new ZExtInst(opcodeShort, llvm_type_of<uint64_t>(ctx), "", insertBefore);
}

std::vector<std::vector<std::unique_ptr<BytecodeVariantDefinition>>> WARN_UNUSED BytecodeVariantDefinition::ParseAllFromModule(llvm::Module* module)
{
    using namespace llvm;
    using Desc = DeegenFrontendBytecodeDefinitionDescriptor;
    using SpecializedOperand = DeegenFrontendBytecodeDefinitionDescriptor::SpecializedOperand;

    ReleaseAssert(module->getGlobalVariable(x_defListSymbolName) != nullptr);
    ReleaseAssert(module->getGlobalVariable(x_nameListSymbolName) != nullptr);

    Constant* defList;
    {
        Constant* wrappedDefList = GetConstexprGlobalValue(module, x_defListSymbolName);
        LLVMConstantStructReader reader(module, wrappedDefList);
        defList = reader.Dewrap();
    }

    LLVMConstantArrayReader defListReader(module, defList);
    uint64_t numBytecodesInThisTU = defListReader.GetNumElements<Desc>();

    std::vector<std::string> bytecodeNamesInThisTU;
    {
        Constant* wrappedNameList = GetConstexprGlobalValue(module, x_nameListSymbolName);
        LLVMConstantStructReader readerTmp(module, wrappedNameList);
        Constant* nameList = readerTmp.Dewrap();
        LLVMConstantArrayReader reader(module, nameList);
        ReleaseAssert(reader.GetNumElements<uint8_t*>() == numBytecodesInThisTU);
        for (size_t i = 0; i < numBytecodesInThisTU; i++)
        {
            Constant* cst = reader.Get<uint8_t*>(i);
            bytecodeNamesInThisTU.push_back(GetValueFromLLVMConstantCString(cst));
        }
    }

    auto readSpecializedOperand = [&](Constant* cst) -> SpecializedOperand {
        LLVMConstantStructReader spOperandReader(module, cst);
        auto kind = spOperandReader.GetValue<&SpecializedOperand::m_kind>();
        auto value = spOperandReader.GetValue<&SpecializedOperand::m_value>();
        return SpecializedOperand { kind, value };
    };

    std::vector<std::vector<std::unique_ptr<BytecodeVariantDefinition>>> result;
    for (size_t curBytecodeOrd = 0; curBytecodeOrd < numBytecodesInThisTU; curBytecodeOrd++)
    {
        result.push_back({});
        std::vector<std::unique_ptr<BytecodeVariantDefinition>>& listForCurrentBytecode = result.back();

        LLVMConstantStructReader curDefReader(module, defListReader.Get<Desc>(curBytecodeOrd));
        ReleaseAssert(curDefReader.GetValue<&Desc::m_operandTypeListInitialized>() == true);
        ReleaseAssert(curDefReader.GetValue<&Desc::m_implementationInitialized>() == true);
        ReleaseAssert(curDefReader.GetValue<&Desc::m_resultKindInitialized>() == true);
        size_t numVariants = curDefReader.GetValue<&Desc::m_numVariants>();
        size_t numOperands = curDefReader.GetValue<&Desc::m_numOperands>();
        LLVMConstantArrayReader operandListReader(module, curDefReader.Get<&Desc::m_operandTypes>());
        ReleaseAssert(operandListReader.GetNumElements<Desc::Operand>() == Desc::x_maxOperands);
        bool hasTValueOutput = curDefReader.GetValue<&Desc::m_hasTValueOutput>();
        bool canPerformBranch = curDefReader.GetValue<&Desc::m_canPerformBranch>();

        std::vector<std::string> operandNames;
        std::vector<DeegenBytecodeOperandType> operandTypes;
        for (size_t i = 0; i < numOperands; i++)
        {
            LLVMConstantStructReader operandReader(module, operandListReader.Get<Desc::Operand>(i));
            std::string operandName = GetValueFromLLVMConstantCString(operandReader.Get<&Desc::Operand::m_name>());
            DeegenBytecodeOperandType opType = operandReader.GetValue<&Desc::Operand::m_type>();
            operandNames.push_back(operandName);
            operandTypes.push_back(opType);
        }

        std::string implFuncName;
        {
            Constant* cst = curDefReader.Get<&Desc::m_implementationFn>();
            Function* fnc = dyn_cast<Function>(cst);
            ReleaseAssert(fnc != nullptr);
            implFuncName = fnc->getName().str();
            if (fnc->getLinkage() != GlobalValue::InternalLinkage)
            {
                // We require the implementation function to be marked 'static', so they can be automatically dropped
                // after we finished the transformation and made them dead
                //
                fprintf(stderr, "The implementation function of the bytecode must be marked 'static'!\n");
                abort();
            }
        }

        LLVMConstantArrayReader variantListReader(module, curDefReader.Get<&Desc::m_variants>());
        for (size_t variantOrd = 0; variantOrd < numVariants; variantOrd++)
        {
            std::unique_ptr<BytecodeVariantDefinition> def = std::make_unique<BytecodeVariantDefinition>();
            def->m_bytecodeOrdInTU = curBytecodeOrd;
            def->m_variantOrd = variantOrd;
            def->m_bytecodeName = bytecodeNamesInThisTU[curBytecodeOrd];
            def->m_opNames = operandNames;
            def->m_originalOperandTypes = operandTypes;
            def->m_bytecodeStructLength = static_cast<size_t>(-1);
            def->m_implFunctionName = implFuncName;
            def->m_hasOutputValue = hasTValueOutput;
            def->m_hasConditionalBranchTarget = canPerformBranch;
            if (hasTValueOutput)
            {
                def->m_outputOperand = std::make_unique<BcOpSlot>("output");
            }
            if (canPerformBranch)
            {
                def->m_condBrTarget = std::make_unique<BcOpLiteral>("condBrTarget", true /*isSigned*/, 4 /*numBytes*/);
            }

            LLVMConstantStructReader variantReader(module, variantListReader.Get<Desc::SpecializedVariant>(variantOrd));
            LLVMConstantArrayReader baseReader(module, variantReader.Get<&Desc::SpecializedVariant::m_base>());
            for (size_t opOrd = 0; opOrd < numOperands; opOrd++)
            {
                SpecializedOperand spOp = readSpecializedOperand(baseReader.Get<SpecializedOperand>(opOrd));
                DeegenBytecodeOperandType opType = operandTypes[opOrd];
                std::string operandName = operandNames[opOrd];
                if (opType == DeegenBytecodeOperandType::BytecodeSlotOrConstant)
                {
                    if (spOp.m_kind == DeegenSpecializationKind::BytecodeSlot)
                    {
                        def->m_list.push_back(std::make_unique<BcOpSlot>(operandName));
                    }
                    else
                    {
                        ReleaseAssert(spOp.m_kind == DeegenSpecializationKind::BytecodeConstantWithType && "unexpected specialization");
                        def->m_list.push_back(std::make_unique<BcOpConstant>(operandName, SafeIntegerCast<TypeSpeculationMask>(spOp.m_value)));
                    }
                }
                else if (opType == DeegenBytecodeOperandType::BytecodeSlot)
                {
                    ReleaseAssert(spOp.m_kind == DeegenSpecializationKind::NotSpecialized && "unexpected specialization");
                    def->m_list.push_back(std::make_unique<BcOpSlot>(operandName));
                }
                else if (opType == DeegenBytecodeOperandType::Constant)
                {
                    TypeSpeculationMask specMask;
                    if (spOp.m_kind == DeegenSpecializationKind::NotSpecialized)
                    {
                        specMask = x_typeSpeculationMaskFor<tTop>;
                    }
                    else if (spOp.m_kind == DeegenSpecializationKind::BytecodeConstantWithType)
                    {
                        specMask = SafeIntegerCast<TypeSpeculationMask>(spOp.m_value);
                    }
                    else
                    {
                        ReleaseAssert(false && "unexpected specialization");
                    }
                    def->m_list.push_back(std::make_unique<BcOpConstant>(operandName, specMask));
                }
                else if (opType == DeegenBytecodeOperandType::BytecodeRangeRO || opType == DeegenBytecodeOperandType::BytecodeRangeRW)
                {
                    ReleaseAssert(spOp.m_kind == DeegenSpecializationKind::NotSpecialized);
                    bool isReadOnly = (opType == DeegenBytecodeOperandType::BytecodeRangeRO);
                    def->m_list.push_back(std::make_unique<BcOpBytecodeRangeBase>(operandName, isReadOnly));
                }
                else
                {
                    bool isSigned;
                    size_t numBytes;
                    switch (opType)
                    {
                    case DeegenBytecodeOperandType::Int8:
                    {
                        isSigned = true;
                        numBytes = 1;
                        break;
                    }
                    case DeegenBytecodeOperandType::UInt8:
                    {
                        isSigned = false;
                        numBytes = 1;
                        break;
                    }
                    case DeegenBytecodeOperandType::Int16:
                    {
                        isSigned = true;
                        numBytes = 2;
                        break;
                    }
                    case DeegenBytecodeOperandType::UInt16:
                    {
                        isSigned = false;
                        numBytes = 2;
                        break;
                    }
                    case DeegenBytecodeOperandType::Int32:
                    {
                        isSigned = true;
                        numBytes = 4;
                        break;
                    }
                    case DeegenBytecodeOperandType::UInt32:
                    {
                        isSigned = false;
                        numBytes = 4;
                        break;
                    }
                    default:
                    {
                        ReleaseAssert(false && "unhandled enum");
                    }
                    }   /*switch opType*/

                    if (spOp.m_kind == DeegenSpecializationKind::NotSpecialized)
                    {
                        def->m_list.push_back(std::make_unique<BcOpLiteral>(operandName, isSigned, numBytes));
                    }
                    else
                    {
                        ReleaseAssert(spOp.m_kind == DeegenSpecializationKind::Literal);
                        def->m_list.push_back(std::make_unique<BcOpSpecializedLiteral>(operandName, isSigned, numBytes, spOp.m_value));
                    }
                }
            }
            ReleaseAssert(def->m_list.size() == def->m_opNames.size());

            for (size_t i = 0; i < def->m_list.size(); i++)
            {
                def->m_list[i]->SetOperandOrdinal(i);
            }

            listForCurrentBytecode.push_back(std::move(def));
        }
    }
    return result;
}

void BytecodeVariantDefinition::RemoveUsedAttributeOfBytecodeDefinitionGlobalSymbol(llvm::Module* module)
{
    using namespace llvm;
    GlobalVariable* gv = module->getGlobalVariable(x_defListSymbolName);
    ReleaseAssert(gv != nullptr);
    RemoveGlobalValueUsedAttributeAnnotation(gv);

    gv = module->getGlobalVariable(x_nameListSymbolName);
    ReleaseAssert(gv != nullptr);
    RemoveGlobalValueUsedAttributeAnnotation(gv);
}

void BytecodeVariantDefinition::AssertBytecodeDefinitionGlobalSymbolHasBeenRemoved(llvm::Module* module)
{
    ReleaseAssert(module->getNamedValue(BytecodeVariantDefinition::x_defListSymbolName) == nullptr);
    ReleaseAssert(module->getNamedValue(BytecodeVariantDefinition::x_nameListSymbolName) == nullptr);
}

}   // namespace dast
