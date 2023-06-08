#include "misc_llvm_helper.h"

#include "deegen_bytecode_operand.h"
#include "deegen_interpreter_bytecode_impl_creator.h"
#include "deegen_baseline_jit_impl_creator.h"
#include "deegen_options.h"
#include "api_define_bytecode.h"
#include "runtime_utils.h"

namespace dast {

bool WARN_UNUSED BcOperand::SupportsGetOperandValueFromBytecodeStruct()
{
    if (IsElidedFromBytecodeStruct())
    {
        return false;
    }
    if (IsNotTriviallyDecodeableFromBytecodeStruct())
    {
        return false;
    }
    return true;
}

llvm::Value* WARN_UNUSED BcOperand::EmitGetOperandValueImpl(llvm::Value* structPtr, llvm::BasicBlock* targetBB, bool isBytecodeStruct)
{
    using namespace llvm;
    LLVMContext& ctx = targetBB->getContext();
    ReleaseAssert(llvm_value_has_type<void*>(structPtr) || llvm_value_has_type<HeapPtr<void>>(structPtr));
    ReleaseAssert(SupportsGetOperandValueFromBytecodeStruct());

    size_t offsetInBytecodeStruct;
    size_t numBytesInBytecodeStruct;
    if (isBytecodeStruct)
    {
        offsetInBytecodeStruct = GetOffsetInBytecodeStruct();
        numBytesInBytecodeStruct = GetSizeInBytecodeStruct();
    }
    else
    {
        offsetInBytecodeStruct = GetOffsetInBaselineJitSlowPathDataStruct();
        numBytesInBytecodeStruct = GetSizeInBaselineJitSlowPathDataStruct();
    }

    Type* storageTypeInBytecodeStruct = Type::getIntNTy(ctx, static_cast<uint32_t>(numBytesInBytecodeStruct * 8));
    Value* storageValue = nullptr;

    GetElementPtrInst* gep = GetElementPtrInst::CreateInBounds(llvm_type_of<uint8_t>(ctx), structPtr, { CreateLLVMConstantInt<uint64_t>(ctx, offsetInBytecodeStruct) }, "", targetBB);
    storageValue = new LoadInst(storageTypeInBytecodeStruct, gep, "", false /*isVolatile*/, Align(1), targetBB);

    ReleaseAssert(storageValue != nullptr && storageValue->getType() == storageTypeInBytecodeStruct);

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

llvm::Value* WARN_UNUSED BcOperand::GetOperandValueFromBytecodeStruct(llvm::Value* bytecodePtr, llvm::BasicBlock* targetBB)
{
    return EmitGetOperandValueImpl(bytecodePtr, targetBB, true /*isBytecodeStruct*/);
}

llvm::Value* WARN_UNUSED BcOperand::GetOperandValueFromBytecodeStruct(InterpreterBytecodeImplCreator* ifi, llvm::BasicBlock* targetBB)
{
    return EmitGetOperandValueImpl(ifi->GetCurBytecode(), targetBB, true /*isBytecodeStruct*/);
}

llvm::Value* WARN_UNUSED BcOperand::GetOperandValueFromBaselineJitSlowPathData(llvm::Value* slowPathDataPtr, llvm::BasicBlock* targetBB)
{
    return EmitGetOperandValueImpl(slowPathDataPtr, targetBB, false /*isBytecodeStruct*/);
}

llvm::Value* WARN_UNUSED BcOperand::GetOperandValueFromBaselineJitSlowPathData(BaselineJitImplCreator* ifi, llvm::BasicBlock* targetBB)
{
    return EmitGetOperandValueImpl(ifi->GetJitSlowPathData(), targetBB, false /*isBytecodeStruct*/);
}

json WARN_UNUSED BcOperand::SaveBaseToJSON()
{
    json j;
    j["kind"] = StringifyBcOperandKind(GetKind());
    j["name"] = m_name;
    j["operand_ordinal"] = m_operandOrdinal;
    j["offset_in_bcstruct"] = m_offsetInBytecodeStruct;
    j["size_in_bcstruct"] = m_sizeInBytecodeStruct;
    return j;
}

BcOperand::BcOperand(json& j)
{
    JSONCheckedGet(j, "name", m_name /*out*/);
    JSONCheckedGet(j, "operand_ordinal", m_operandOrdinal /*out*/);
    JSONCheckedGet(j, "offset_in_bcstruct", m_offsetInBytecodeStruct /*out*/);
    JSONCheckedGet(j, "size_in_bcstruct", m_sizeInBytecodeStruct /*out*/);
    m_offsetInBaselineJitSlowPathDataStruct = static_cast<size_t>(-1);
    m_sizeInBaselineJitSlowPathDataStruct = 0;
}

std::unique_ptr<BcOperand> WARN_UNUSED BcOperand::LoadFromJSON(json& j)
{
    BcOperandKind opKind = GetBcOperandKindFromString(JSONCheckedGet<std::string>(j, "kind"));
    switch (opKind)
    {
#define macro(e) case BcOperandKind::e: return std::make_unique< BcOp ## e >(j);
        PP_FOR_EACH(macro, BC_OPERAND_KIND_LIST)
#undef macro
    }
    ReleaseAssert(false);
}

BcOpSlot::BcOpSlot(json& j)
    : BcOperand(j)
{
    ReleaseAssert(GetBcOperandKindFromString(JSONCheckedGet<std::string>(j, "kind")) == BcOperandKind::Slot);
}

json WARN_UNUSED BcOpSlot::SaveToJSON()
{
    json j = SaveBaseToJSON();
    return j;
}

llvm::Value* WARN_UNUSED BcOpSlot::EmitUsageValueFromBytecodeValue(DeegenBytecodeImplCreatorBase* ifi, llvm::BasicBlock* targetBB /*out*/, llvm::Value* bytecodeValue)
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

BcOpConstant::BcOpConstant(json& j)
    : BcOperand(j)
{
    ReleaseAssert(GetBcOperandKindFromString(JSONCheckedGet<std::string>(j, "kind")) == BcOperandKind::Constant);
    JSONCheckedGet(j, "typeMask", m_typeMask /*out*/);
}

json WARN_UNUSED BcOpConstant::SaveToJSON()
{
    json j = SaveBaseToJSON();
    j["typeMask"] = m_typeMask;
    return j;
}

llvm::Value* WARN_UNUSED BcOpConstant::EmitUsageValueFromBytecodeValue(DeegenBytecodeImplCreatorBase* ifi, llvm::BasicBlock* targetBB /*out*/, llvm::Value* bytecodeValue)
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

BcOpLiteral::BcOpLiteral(json& j)
    : BcOperand(j)
{
    // This class is not final and is inherited by SpecializedLiteral, so both class could be calling us
    //
    BcOperandKind opKind = GetBcOperandKindFromString(JSONCheckedGet<std::string>(j, "kind"));
    ReleaseAssert(opKind == BcOperandKind::Literal || opKind == BcOperandKind::SpecializedLiteral);
    JSONCheckedGet(j, "lit_is_signed", m_isSigned /*out*/);
    JSONCheckedGet(j, "lit_num_bytes", m_numBytes /*out*/);
}

json WARN_UNUSED BcOpLiteral::SaveToJSON()
{
    json j = SaveBaseToJSON();
    j["lit_is_signed"] = m_isSigned;
    j["lit_num_bytes"] = m_numBytes;
    return j;
}

llvm::Value* WARN_UNUSED BcOpLiteral::EmitUsageValueFromBytecodeValue(DeegenBytecodeImplCreatorBase* /*ifi*/, llvm::BasicBlock* /*targetBB*/ /*out*/, llvm::Value* bytecodeValue)
{
    ReleaseAssert(bytecodeValue != nullptr);
    ReleaseAssert(bytecodeValue->getType() == GetSourceValueFullRepresentationType(bytecodeValue->getContext()));
    ReleaseAssert(bytecodeValue->getType() == GetUsageType(bytecodeValue->getContext()));
    return bytecodeValue;
}

BcOpSpecializedLiteral::BcOpSpecializedLiteral(json& j)
    : BcOpLiteral(j)
{
    ReleaseAssert(GetBcOperandKindFromString(JSONCheckedGet<std::string>(j, "kind")) == BcOperandKind::SpecializedLiteral);
    JSONCheckedGet(j, "lit_concrete_value", m_concreteValue /*out*/);
}

json WARN_UNUSED BcOpSpecializedLiteral::SaveToJSON()
{
    json j = BcOpLiteral::SaveToJSON();
    j["lit_concrete_value"] = m_concreteValue;
    return j;
}

llvm::Value* WARN_UNUSED BcOpSpecializedLiteral::EmitUsageValueFromBytecodeValue(DeegenBytecodeImplCreatorBase* /*ifi*/, llvm::BasicBlock* targetBB /*out*/, llvm::Value* bytecodeValue)
{
    using namespace llvm;
    ReleaseAssert(bytecodeValue == nullptr);
    LLVMContext& ctx = targetBB->getContext();
    Value* res = ConstantInt::get(ctx, APInt(static_cast<uint32_t>(m_numBytes * 8) /*bitWidth*/, m_concreteValue, m_isSigned));
    ReleaseAssert(res->getType() == GetUsageType(ctx));
    return res;
}

BcOpBytecodeRangeBase::BcOpBytecodeRangeBase(json& j)
    : BcOperand(j)
{
    ReleaseAssert(GetBcOperandKindFromString(JSONCheckedGet<std::string>(j, "kind")) == BcOperandKind::BytecodeRangeBase);
    JSONCheckedGet(j, "range_is_readonly", m_isReadOnly /*out*/);
    JSONCheckedGet(j, "range_has_limit", m_hasRangeLimit /*out*/);
    if (m_hasRangeLimit)
    {
        JSONCheckedGet(j, "range_limit_is_constant", m_isRangeLimitConstant /*out*/);
        if (m_isRangeLimitConstant)
        {
            JSONCheckedGet(j, "range_constant_limit", m_constantRangeLimit /*out*/);
        }
        else
        {
            ReleaseAssert(false && "operand range limit is unimplemented yet!");
        }
    }
}

json WARN_UNUSED BcOpBytecodeRangeBase::SaveToJSON()
{
    json j = SaveBaseToJSON();
    j["range_is_readonly"] = m_isReadOnly;
    j["range_has_limit"] = m_hasRangeLimit;
    if (m_hasRangeLimit)
    {
        j["range_limit_is_constant"] = m_isRangeLimitConstant;
        if (m_isRangeLimitConstant)
        {
            j["range_constant_limit"] = m_constantRangeLimit;
        }
        else
        {
            ReleaseAssert(false && "operand range limit is unimplemented yet!");
        }
    }
    return j;
}

llvm::Value* WARN_UNUSED BcOpBytecodeRangeBase::EmitUsageValueFromBytecodeValue(DeegenBytecodeImplCreatorBase* ifi, llvm::BasicBlock* targetBB /*out*/, llvm::Value* bytecodeValue)
{
    using namespace llvm;
    LLVMContext& ctx = ifi->GetModule()->getContext();
    ReleaseAssert(bytecodeValue->getType() == GetSourceValueFullRepresentationType(bytecodeValue->getContext()));
    Value* res = GetElementPtrInst::CreateInBounds(llvm_type_of<uint64_t>(ctx), ifi->GetStackBase(), { bytecodeValue }, "", targetBB);
    ReleaseAssert(res->getType() == GetUsageType(ctx));
    return res;
}

BcOpInlinedMetadata::BcOpInlinedMetadata(json& j)
    : BcOperand(j)
{
    ReleaseAssert(GetBcOperandKindFromString(JSONCheckedGet<std::string>(j, "kind")) == BcOperandKind::InlinedMetadata);
    JSONCheckedGet(j, "inline_md_size", m_size /*out*/);
}

json WARN_UNUSED BcOpInlinedMetadata::SaveToJSON()
{
    json j = SaveBaseToJSON();
    j["inline_md_size"] = m_size;
    return j;
}

llvm::Value* WARN_UNUSED BcOpInlinedMetadata::EmitUsageValueFromBytecodeValue(DeegenBytecodeImplCreatorBase* ifi, llvm::BasicBlock* targetBB /*out*/, llvm::Value* bytecodeValue)
{
    using namespace llvm;
    LLVMContext& ctx = ifi->GetModule()->getContext();

    ReleaseAssert(bytecodeValue == nullptr);
    Value* res = GetElementPtrInst::CreateInBounds(llvm_type_of<uint8_t>(ctx), ifi->GetCurBytecode(), { CreateLLVMConstantInt<uint64_t>(ctx, GetOffsetInBytecodeStruct()) }, "", targetBB);
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

llvm::Value* WARN_UNUSED BytecodeVariantDefinition::DecodeBytecodeOpcode(llvm::Value* bytecode, llvm::BasicBlock* insertAtEnd)
{
    using namespace llvm;
    UnreachableInst* dummy = new UnreachableInst(bytecode->getContext(), insertAtEnd);
    Value* res = DecodeBytecodeOpcode(bytecode, dummy);
    dummy->eraseFromParent();
    return res;
}

std::vector<std::vector<std::unique_ptr<BytecodeVariantDefinition>>> WARN_UNUSED BytecodeVariantDefinition::ParseAllFromModule(llvm::Module* module)
{
    using namespace llvm;
    using Desc = DeegenFrontendBytecodeDefinitionDescriptor;
    using SpecializedOperand = DeegenFrontendBytecodeDefinitionDescriptor::SpecializedOperand;

    ReleaseAssert(module->getGlobalVariable(x_defListSymbolName) != nullptr);
    ReleaseAssert(module->getGlobalVariable(x_nameListSymbolName) != nullptr);
    ReleaseAssert(module->getGlobalVariable(x_sameLengthConstraintListSymbolName) != nullptr);

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

    std::unordered_map<std::string, size_t> bytecodeNameToBytecodeNameOrdInList;
    for (size_t i = 0; i < bytecodeNamesInThisTU.size(); i++)
    {
        ReleaseAssert(!bytecodeNameToBytecodeNameOrdInList.count(bytecodeNamesInThisTU[i]));
        bytecodeNameToBytecodeNameOrdInList[bytecodeNamesInThisTU[i]] = i;
    }

    std::vector<std::pair<std::string, std::string>> bytecodeSameLengthConstraints;
    {
        Constant* wrappedNameList = GetConstexprGlobalValue(module, x_sameLengthConstraintListSymbolName);
        LLVMConstantStructReader readerTmp(module, wrappedNameList);
        Constant* nameList = readerTmp.Dewrap();
        LLVMConstantArrayReader reader(module, nameList);
        size_t listLen = reader.GetNumElements<uint8_t*>();
        ReleaseAssert(listLen % 2 == 0);
        for (size_t i = 0; i < listLen; i += 2)
        {
            Constant* cst1 = reader.Get<uint8_t*>(i);
            Constant* cst2 = reader.Get<uint8_t*>(i + 1);
            bytecodeSameLengthConstraints.push_back(std::make_pair(GetValueFromLLVMConstantCString(cst1), GetValueFromLLVMConstantCString(cst2)));
        }
    }

    for (auto& it : bytecodeSameLengthConstraints)
    {
        auto checkExists = [&](const std::string& s)
        {
            for (const std::string& t : bytecodeNamesInThisTU)
            {
                if (t == s)
                {
                    return;
                }
            }
            fprintf(stderr, "Bytecode name '%s' specified in BytecodeSameLengthConstraint is not defined in this translation unit!\n", s.c_str());
            abort();
        };
        checkExists(it.first);
        checkExists(it.second);
    }

    std::unordered_map<std::string, std::string> dsu;
    for (auto& it : bytecodeSameLengthConstraints)
    {
        dsu[it.first] = it.first;
        dsu[it.second] = it.second;
    }

    std::function<std::string(const std::string&)> dsuFind = [&](const std::string& val) {
        ReleaseAssert(dsu.count(val));
        if (dsu[val] == val)
        {
            return val;
        }
        dsu[val] = dsuFind(dsu[val]);
        return dsu[val];
    };

    auto dsuUnion = [&](const std::string& val1, const std::string& val2) {
        std::string p1 = dsuFind(val1);
        std::string p2 = dsuFind(val2);
        ReleaseAssert(dsu.count(p1) && dsu[p1] == p1 && dsu.count(p2) && dsu[p2] == p2);
        dsu[p1] = p2;
    };

    for (auto& it : bytecodeSameLengthConstraints)
    {
        dsuUnion(it.first, it.second);
    }

    std::unordered_map<std::string, std::vector<size_t /*bytecodeNameOrd*/>> bytecodeSameLengthConstraintGroup;
    for (auto& it : dsu)
    {
        std::string bytecodeName = it.first;
        std::string p = dsuFind(bytecodeName);
        ReleaseAssert(bytecodeNameToBytecodeNameOrdInList.count(bytecodeName));
        bytecodeSameLengthConstraintGroup[p].push_back(bytecodeNameToBytecodeNameOrdInList[bytecodeName]);
    }

    auto getBytecodeSameLengthConstraintGroup = [&](const std::string& bytecodeName) WARN_UNUSED -> std::vector<size_t>
    {
        if (!dsu.count(bytecodeName))
        {
            // No constraint is associated with this bytecode
            //
            return {};
        }
        std::string p = dsuFind(bytecodeName);
        ReleaseAssert(bytecodeSameLengthConstraintGroup.count(p));
        return bytecodeSameLengthConstraintGroup[p];
    };

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
        bool isInterpreterToBaselineJitOsrEntryPoint = curDefReader.GetValue<&Desc::m_isInterpreterTierUpPoint>();

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
            def->m_hasDecidedOperandWidth = false;
            def->m_bytecodeStructLengthTentativelyFinalized = false;
            def->m_bytecodeStructLengthFinalized = false;
            def->m_metadataStructInfoAssigned = false;
            def->m_isInterpreterCallIcEverUsed = false;
            def->m_isInterpreterCallIcExplicitlyDisabled = false;
            def->m_numJitCallICs = static_cast<size_t>(-1);
            def->m_numJitGenericICs = static_cast<size_t>(-1);
            def->m_totalGenericIcEffectKinds = static_cast<size_t>(-1);
            def->m_implFunctionName = implFuncName;
            def->m_hasOutputValue = hasTValueOutput;
            def->m_hasConditionalBranchTarget = canPerformBranch;
            def->m_baselineJitSlowPathDataLength = static_cast<size_t>(-1);
            def->m_isInterpreterToBaselineJitOsrEntryPoint = isInterpreterToBaselineJitOsrEntryPoint;
            if (hasTValueOutput)
            {
                def->m_outputOperand = std::make_unique<BcOpSlot>("output");
            }
            if (canPerformBranch)
            {
                def->m_condBrTarget = std::make_unique<BcOpLiteral>("condBrTarget", true /*isSigned*/, 4 /*numBytes*/);
            }

            LLVMConstantStructReader variantReader(module, variantListReader.Get<Desc::SpecializedVariant>(variantOrd));
            bool enableHCS = variantReader.GetValue<&Desc::SpecializedVariant::m_enableHCS>();
            size_t numQuickenings = variantReader.GetValue<&Desc::SpecializedVariant::m_numQuickenings>();
            if (enableHCS)
            {
                def->m_quickeningKind = BytecodeQuickeningKind::LockedQuickening;
                ReleaseAssert(numQuickenings == 1);
            }
            else
            {
                def->m_quickeningKind = BytecodeQuickeningKind::NoQuickening;
                ReleaseAssert(numQuickenings == 0);
            }

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

            if (def->m_quickeningKind == BytecodeQuickeningKind::LockedQuickening)
            {
                LLVMConstantArrayReader quickeningListReader(module, variantReader.Get<&Desc::SpecializedVariant::m_quickenings>());
                LLVMConstantStructReader quickeningReaderTmp(module, quickeningListReader.Get<Desc::SpecializedVariant::Quickening>(0 /*ord*/));
                LLVMConstantArrayReader quickeningReader(module, quickeningReaderTmp.Get<&Desc::SpecializedVariant::Quickening::value>());
                for (size_t opOrd = 0; opOrd < numOperands; opOrd++)
                {
                    SpecializedOperand spOp = readSpecializedOperand(quickeningReader.Get<SpecializedOperand>(opOrd));
                    if (spOp.m_kind == DeegenSpecializationKind::NotSpecialized)
                    {
                        continue;
                    }
                    ReleaseAssert(spOp.m_kind == DeegenSpecializationKind::SpeculatedTypeForOptimizer);
                    ReleaseAssert(def->m_list[opOrd]->GetKind() == BcOperandKind::Slot || def->m_list[opOrd]->GetKind() == BcOperandKind::Constant);
                    TypeSpeculationMask specMask = SafeIntegerCast<TypeSpeculationMask>(spOp.m_value);
                    def->m_quickening.push_back({ .m_operandOrd = opOrd, .m_speculatedMask = specMask });
                }
                ReleaseAssert(def->m_quickening.size() > 0);
            }

            listForCurrentBytecode.push_back(std::move(def));
        }
    }

    ReleaseAssert(result.size() == numBytecodesInThisTU);

    for (size_t curBytecodeOrd = 0; curBytecodeOrd < numBytecodesInThisTU; curBytecodeOrd++)
    {
        std::string bytecodeName = bytecodeNamesInThisTU[curBytecodeOrd];
        std::vector<size_t> sameLengthConstraintGroup = getBytecodeSameLengthConstraintGroup(bytecodeName);
        std::vector<BytecodeVariantDefinition*> list;
        for (size_t ord : sameLengthConstraintGroup)
        {
            ReleaseAssert(ord < numBytecodesInThisTU);
            for (std::unique_ptr<BytecodeVariantDefinition>& it : result[ord])
            {
                list.push_back(it.get());
            }
        }
        for (std::unique_ptr<BytecodeVariantDefinition>& it : result[curBytecodeOrd])
        {
            it->m_sameLengthConstraintList = list;
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

    gv = module->getGlobalVariable(x_sameLengthConstraintListSymbolName);
    ReleaseAssert(gv != nullptr);
    RemoveGlobalValueUsedAttributeAnnotation(gv);
}

void BytecodeVariantDefinition::AssertBytecodeDefinitionGlobalSymbolHasBeenRemoved(llvm::Module* module)
{
    ReleaseAssert(module->getNamedValue(BytecodeVariantDefinition::x_defListSymbolName) == nullptr);
    ReleaseAssert(module->getNamedValue(BytecodeVariantDefinition::x_nameListSymbolName) == nullptr);
    ReleaseAssert(module->getNamedValue(BytecodeVariantDefinition::x_sameLengthConstraintListSymbolName) == nullptr);
}

BytecodeVariantDefinition::BytecodeVariantDefinition(json& j)
{
    JSONCheckedGet(j, "bytecode_ord_in_tu", m_bytecodeOrdInTU);
    JSONCheckedGet(j, "bytecode_variant_ord", m_variantOrd);
    JSONCheckedGet(j, "bytecode_name", m_bytecodeName);
    JSONCheckedGet(j, "impl_function_name", m_implFunctionName);
    m_opNames = j["operand_name_list"];
    {
        ReleaseAssert(j.count("original_operand_type_list") && j["original_operand_type_list"].is_array());
        std::vector<int> originalOperandTypes = j["original_operand_type_list"];
        for (int val : originalOperandTypes) { m_originalOperandTypes.push_back(static_cast<DeegenBytecodeOperandType>(val)); }
    }
    {
        ReleaseAssert(j.count("operand_list") && j["operand_list"].is_array());
        std::vector<json> operand_list = j["operand_list"];
        for (json& op_json : operand_list)
        {
            std::unique_ptr<BcOperand> op = BcOperand::LoadFromJSON(op_json);
            m_list.push_back(std::move(op));
        }
    }
    m_hasDecidedOperandWidth = true;
    m_bytecodeStructLengthTentativelyFinalized = true;
    m_bytecodeStructLengthFinalized = true;

    JSONCheckedGet(j, "has_output_value", m_hasOutputValue);
    if (m_hasOutputValue)
    {
        ReleaseAssert(j.count("output_operand"));
        json op_json = j["output_operand"];
        std::unique_ptr<BcOperand> op = BcOperand::LoadFromJSON(op_json);
        ReleaseAssert(op->GetKind() == BcOperandKind::Slot);
        m_outputOperand.reset(assert_cast<BcOpSlot*>(op.release()));
    }

    JSONCheckedGet(j, "has_cond_br_target", m_hasConditionalBranchTarget);
    if (m_hasConditionalBranchTarget)
    {
        ReleaseAssert(j.count("cond_br_target_operand"));
        json op_json = j["cond_br_target_operand"];
        std::unique_ptr<BcOperand> op = BcOperand::LoadFromJSON(op_json);
        ReleaseAssert(op->GetKind() == BcOperandKind::Literal);
        m_condBrTarget.reset(assert_cast<BcOpLiteral*>(op.release()));
    }

    // The bytecode metadata info is currently not serialized since the JIT doesn't need it
    //
    m_metadataStructInfoAssigned = false;

    m_baselineJitSlowPathDataLength = static_cast<size_t>(-1);

    JSONCheckedGet(j, "is_interpreter_call_ic_explicitly_disabled", m_isInterpreterCallIcExplicitlyDisabled);
    JSONCheckedGet(j, "is_interpreter_call_ic_ever_used", m_isInterpreterCallIcEverUsed);
    JSONCheckedGet(j, "num_jit_call_ic", m_numJitCallICs);
    JSONCheckedGet(j, "num_jit_generic_ic", m_numJitGenericICs);
    JSONCheckedGet(j, "num_total_generic_ic_effect_kinds", m_totalGenericIcEffectKinds);

    JSONCheckedGet(j, "is_interpreter_to_baseline_jit_osr_entry_point", m_isInterpreterToBaselineJitOsrEntryPoint);

    {
        int quickeningKindInt = JSONCheckedGet<int>(j, "quickening_kind");
        m_quickeningKind = static_cast<BytecodeQuickeningKind>(quickeningKindInt);
    }

    {
        ReleaseAssert(j.count("quickening_descriptor") && j["quickening_descriptor"].is_array());
        std::vector<std::vector<size_t>> serializedQuickeningDescList = j["quickening_descriptor"];
        for (auto& item : serializedQuickeningDescList)
        {
            ReleaseAssert(item.size() == 2);
            BytecodeOperandQuickeningDescriptor desc;
            desc.m_operandOrd = item[0];
            desc.m_speculatedMask = SafeIntegerCast<TypeSpeculationMask>(item[1]);
            m_quickening.push_back(desc);
        }
    }

    JSONCheckedGet(j, "bytecode_struct_length", m_bytecodeStructLength);
}

json WARN_UNUSED BytecodeVariantDefinition::SaveToJSON()
{
    json j;
    j["bytecode_ord_in_tu"] = m_bytecodeOrdInTU;
    j["bytecode_variant_ord"] = m_variantOrd;
    j["bytecode_name"] = m_bytecodeName;
    j["impl_function_name"] = m_implFunctionName;
    j["operand_name_list"] = m_opNames;
    {
        std::vector<int> originalOperandTypes;
        for (DeegenBytecodeOperandType val : m_originalOperandTypes) { originalOperandTypes.push_back(static_cast<int>(val)); }
        j["original_operand_type_list"] = originalOperandTypes;
    }
    {
        std::vector<json> operand_list;
        for (std::unique_ptr<BcOperand>& op : m_list)
        {
            operand_list.push_back(op->SaveToJSON());
        }
        j["operand_list"] = operand_list;
    }

    ReleaseAssert(m_hasDecidedOperandWidth);
    ReleaseAssert(m_bytecodeStructLengthTentativelyFinalized);
    ReleaseAssert(m_bytecodeStructLengthFinalized);

    j["has_output_value"] = m_hasOutputValue;
    if (m_hasOutputValue)
    {
        j["output_operand"] = m_outputOperand->SaveToJSON();
    }

    j["has_cond_br_target"] = m_hasConditionalBranchTarget;
    if (m_hasConditionalBranchTarget)
    {
        j["cond_br_target_operand"] = m_condBrTarget->SaveToJSON();
    }

    // The bytecode metadata info is currently not serialized since the JIT doesn't need it
    //

    j["is_interpreter_call_ic_explicitly_disabled"] = m_isInterpreterCallIcExplicitlyDisabled;
    j["is_interpreter_call_ic_ever_used"] = m_isInterpreterCallIcEverUsed;
    j["num_jit_call_ic"] = GetNumCallICsInJitTier();
    j["num_jit_generic_ic"] = GetNumGenericICsInJitTier();
    j["num_total_generic_ic_effect_kinds"] = GetTotalGenericIcEffectKinds();

    j["is_interpreter_to_baseline_jit_osr_entry_point"] = m_isInterpreterToBaselineJitOsrEntryPoint;

    j["quickening_kind"] = static_cast<int>(m_quickeningKind);

    {
        std::vector<std::vector<size_t>> serializedQuickeningDescList;
        for (auto& it : m_quickening)
        {
            serializedQuickeningDescList.push_back(std::vector<size_t> { it.m_operandOrd, it.m_speculatedMask });
        }
        j["quickening_descriptor"] = serializedQuickeningDescList;
    }

    // m_allOtherQuickenings not serialized since it's not used right now.
    // m_sameLengthConstraintList not serialized since JIT doesn't care about it.
    // m_metadataStructInfo and m_interpreterCallIcMetadata not serialized since JIT doesn't need it.
    //

    j["bytecode_struct_length"] = m_bytecodeStructLength;

    return j;
}

// Currently the baseline JIT slow path data is layouted as follow:
//     2-byte opcode
//     4-byte jitAddr -- the JIT'ed fast path address for this bytecode
//     4-byte condBrJitAddr -- exists if this bytecode can branch, the JIT'ed address to branch to
//     4-byte condBrBytecodeIndex -- exists if this bytecode can branch, the index of the bytecode target
//     All the bytecode operands, except condBr and metadata
//
void BytecodeVariantDefinition::ComputeBaselineJitSlowPathDataLayout()
{
    ReleaseAssert(IsBytecodeStructLengthFinalized());
    ReleaseAssert(!IsBaselineJitSlowPathDataLayoutDetermined());

    size_t currentOffset = x_opcodeSizeBytes;

    // Reserve space for jitAddr
    //
    currentOffset += 4;

    // Reserve space for condBrJitAddr and condBrBytecodeIndex, if needed
    //
    if (m_hasConditionalBranchTarget)
    {
        currentOffset += 8;
    }

    auto update = [&](BcOperand* operand, size_t maxWidthBytes)
    {
        if (operand->IsElidedFromBytecodeStruct())
        {
            operand->AssignOrChangeBaselineJitSlowPathDataOffsetAndSize(static_cast<size_t>(-1) /*offset*/, 0 /*size*/);
        }
        else
        {
            size_t operandMaxWidth = operand->ValueFullByteLength();
            size_t width = std::min(operandMaxWidth, maxWidthBytes);
            operand->AssignOrChangeBaselineJitSlowPathDataOffsetAndSize(currentOffset, width);
            currentOffset += width;
        }
    };

    // For now for simplicity, just hardcode 2-byte operands similar to what we have assumed for the bytecode structs.
    //
    for (auto& operand : m_list)
    {
        update(operand.get(), 2 /*maxWidthBytes*/);
    }
    if (m_hasOutputValue)
    {
        update(m_outputOperand.get(), 2 /*maxWidthBytes*/);
    }

    // Reserve space for each Call IC site
    //
    m_baselineJitCallIcBaseOffset = currentOffset;
    currentOffset += GetNumCallICsInJitTier() * sizeof(JitCallInlineCacheSite);

    if (GetNumGenericICsInJitTier() > 0)
    {
        m_baselineJitSlowPathAndDataSecInfoOffset = currentOffset;
        currentOffset += 8;

        m_baselineJitGenericIcBaseOffset = currentOffset;
        currentOffset += GetNumGenericICsInJitTier() * sizeof(JitGenericInlineCacheSite);
    }

    m_baselineJitSlowPathDataLength = currentOffset;
}

// Currently the jitAddr is simply stored as a int32_t value, because all the code resides in the first 2GB address space.
// We might want to change this assumption to support PIC/PIE in the future, but for now let's stay simple.
//
static llvm::Value* DecodeJitAddrFromSlowPathDataImpl(llvm::Value* storageAddr, llvm::Instruction* insertBefore)
{
    using namespace llvm;
    LLVMContext& ctx = storageAddr->getContext();
    ReleaseAssert(llvm_value_has_type<void*>(storageAddr));

    Value* addr32 = new LoadInst(llvm_type_of<int32_t>(ctx), storageAddr, "", false /*isVolatile*/, Align(1), insertBefore);

    // ZExt/SExt doesn't matter because the address is < 2GB
    //
    Value* addr64 = new ZExtInst(addr32, llvm_type_of<uint64_t>(ctx), "", insertBefore);

    Value* ptr = new IntToPtrInst(addr64, llvm_type_of<void*>(ctx), "", insertBefore);
    return ptr;
}

llvm::Value* WARN_UNUSED BytecodeVariantDefinition::GetCodePtrOfCurrentBytecodeSlowPathForBaselineJit(llvm::Value* slowPathDataAddr, llvm::Instruction* insertBefore)
{
    using namespace llvm;
    LLVMContext& ctx = slowPathDataAddr->getContext();
    size_t offset = GetJitSlowPathOffsetInSlowPathData();
    ReleaseAssert(llvm_value_has_type<void*>(slowPathDataAddr));
    GetElementPtrInst* gep = GetElementPtrInst::CreateInBounds(llvm_type_of<uint8_t>(ctx), slowPathDataAddr,
                                                               { CreateLLVMConstantInt<uint64_t>(ctx, offset) }, "", insertBefore);

    return DecodeJitAddrFromSlowPathDataImpl(gep, insertBefore);
}

llvm::Value* WARN_UNUSED BytecodeVariantDefinition::GetCodePtrOfCurrentBytecodeSlowPathForBaselineJit(llvm::Value* slowPathDataAddr, llvm::BasicBlock* insertAtEnd)
{
    using namespace llvm;
    UnreachableInst* dummy = new UnreachableInst(slowPathDataAddr->getContext(), insertAtEnd);
    Value* res = GetCodePtrOfCurrentBytecodeSlowPathForBaselineJit(slowPathDataAddr, dummy);
    dummy->eraseFromParent();
    return res;
}

llvm::Value* WARN_UNUSED BytecodeVariantDefinition::GetPtrOfCurrentBytecodeDataSectionForBaselineJit(llvm::Value* slowPathDataAddr, llvm::Instruction* insertBefore)
{
    using namespace llvm;
    LLVMContext& ctx = slowPathDataAddr->getContext();
    size_t offset = GetJitDataSectionOffsetInSlowPathData();
    ReleaseAssert(llvm_value_has_type<void*>(slowPathDataAddr));
    GetElementPtrInst* gep = GetElementPtrInst::CreateInBounds(llvm_type_of<uint8_t>(ctx), slowPathDataAddr,
                                                               { CreateLLVMConstantInt<uint64_t>(ctx, offset) }, "", insertBefore);

    return DecodeJitAddrFromSlowPathDataImpl(gep, insertBefore);
}

llvm::Value* WARN_UNUSED BytecodeVariantDefinition::GetPtrOfCurrentBytecodeDataSectionForBaselineJit(llvm::Value* slowPathDataAddr, llvm::BasicBlock* insertAtEnd)
{
    using namespace llvm;
    UnreachableInst* dummy = new UnreachableInst(slowPathDataAddr->getContext(), insertAtEnd);
    Value* res = GetPtrOfCurrentBytecodeDataSectionForBaselineJit(slowPathDataAddr, dummy);
    dummy->eraseFromParent();
    return res;
}

llvm::Value* WARN_UNUSED BytecodeVariantDefinition::GetCodePtrOfCurrentBytecodeForBaselineJit(llvm::Value* slowPathDataAddr, llvm::Instruction* insertBefore)
{
    using namespace llvm;
    ReleaseAssert(IsBaselineJitSlowPathDataLayoutDetermined());

    LLVMContext& ctx = slowPathDataAddr->getContext();

    // The jitAddr of every SlowPathData is always at offset x_opcodeSizeBytes
    //
    size_t offset = x_opcodeSizeBytes;

    ReleaseAssert(llvm_value_has_type<void*>(slowPathDataAddr));
    GetElementPtrInst* gep = GetElementPtrInst::CreateInBounds(llvm_type_of<uint8_t>(ctx), slowPathDataAddr,
                                                               { CreateLLVMConstantInt<uint64_t>(ctx, offset) }, "", insertBefore);

    return DecodeJitAddrFromSlowPathDataImpl(gep, insertBefore);
}

llvm::Value* WARN_UNUSED BytecodeVariantDefinition::GetCodePtrOfCurrentBytecodeForBaselineJit(llvm::Value* slowPathDataAddr, llvm::BasicBlock* insertAtEnd)
{
    using namespace llvm;
    UnreachableInst* dummy = new UnreachableInst(slowPathDataAddr->getContext(), insertAtEnd);
    Value* res = GetCodePtrOfCurrentBytecodeForBaselineJit(slowPathDataAddr, dummy);
    dummy->eraseFromParent();
    return res;
}

llvm::Value* WARN_UNUSED BytecodeVariantDefinition::GetFallthroughCodePtrForBaselineJit(llvm::Value* slowPathDataAddr, llvm::Instruction* insertBefore)
{
    using namespace llvm;
    ReleaseAssert(IsBaselineJitSlowPathDataLayoutDetermined());

    LLVMContext& ctx = slowPathDataAddr->getContext();

    // Note that the jitAddr of every SlowPathData is always at offset x_opcodeSizeBytes,
    // so we can decode without knowing the bytecode type of the next SlowPathData
    //
    size_t offset = GetBaselineJitSlowPathDataLength() + x_opcodeSizeBytes;

    ReleaseAssert(llvm_value_has_type<void*>(slowPathDataAddr));
    GetElementPtrInst* gep = GetElementPtrInst::CreateInBounds(llvm_type_of<uint8_t>(ctx), slowPathDataAddr,
                                                               { CreateLLVMConstantInt<uint64_t>(ctx, offset) }, "", insertBefore);

    return DecodeJitAddrFromSlowPathDataImpl(gep, insertBefore);
}

llvm::Value* WARN_UNUSED BytecodeVariantDefinition::GetFallthroughCodePtrForBaselineJit(llvm::Value* slowPathDataAddr, llvm::BasicBlock* insertAtEnd)
{
    using namespace llvm;
    UnreachableInst* dummy = new UnreachableInst(slowPathDataAddr->getContext(), insertAtEnd);
    Value* res = GetFallthroughCodePtrForBaselineJit(slowPathDataAddr, dummy);
    dummy->eraseFromParent();
    return res;
}

llvm::Value* WARN_UNUSED BytecodeVariantDefinition::GetAddressOfCondBrOperandForBaselineJit(llvm::Value* slowPathDataAddr, llvm::Instruction* insertBefore)
{
    using namespace llvm;
    ReleaseAssert(IsBaselineJitSlowPathDataLayoutDetermined());
    ReleaseAssert(m_hasConditionalBranchTarget);

    LLVMContext& ctx = slowPathDataAddr->getContext();

    size_t offset = x_opcodeSizeBytes + 4;
    GetElementPtrInst* gep = GetElementPtrInst::CreateInBounds(llvm_type_of<uint8_t>(ctx), slowPathDataAddr,
                                                               { CreateLLVMConstantInt<uint64_t>(ctx, offset) }, "", insertBefore);
    return gep;
}

llvm::Value* WARN_UNUSED BytecodeVariantDefinition::GetAddressOfCondBrOperandForBaselineJit(llvm::Value* slowPathDataAddr, llvm::BasicBlock* insertAtEnd)
{
    using namespace llvm;
    UnreachableInst* dummy = new UnreachableInst(slowPathDataAddr->getContext(), insertAtEnd);
    Value* res = GetAddressOfCondBrOperandForBaselineJit(slowPathDataAddr, dummy);
    dummy->eraseFromParent();
    return res;
}

llvm::Value* WARN_UNUSED BytecodeVariantDefinition::GetCondBrTargetCodePtrForBaselineJit(llvm::Value* slowPathDataAddr, llvm::Instruction* insertBefore)
{
    using namespace llvm;
    Value* addr = GetAddressOfCondBrOperandForBaselineJit(slowPathDataAddr, insertBefore);
    return DecodeJitAddrFromSlowPathDataImpl(addr, insertBefore);
}

llvm::Value* WARN_UNUSED BytecodeVariantDefinition::GetCondBrTargetCodePtrForBaselineJit(llvm::Value* slowPathDataAddr, llvm::BasicBlock* insertAtEnd)
{
    using namespace llvm;
    UnreachableInst* dummy = new UnreachableInst(slowPathDataAddr->getContext(), insertAtEnd);
    Value* res = GetCondBrTargetCodePtrForBaselineJit(slowPathDataAddr, dummy);
    dummy->eraseFromParent();
    return res;
}

BytecodeOpcodeRawValueMap WARN_UNUSED BytecodeOpcodeRawValueMap::ParseFromJSON(json j)
{
    ReleaseAssert(j.is_array());
    size_t n = j.size();
    std::vector<std::string> lis;
    for (size_t i = 0; i < n; i++)
    {
        ReleaseAssert(j[i].is_string());
        std::string s = j[i];
        lis.push_back(s);
    }
    ReleaseAssert(lis.size() == n);

    std::unordered_map<std::string, size_t> revMap;
    for (size_t i = 0; i < n; i++)
    {
        ReleaseAssert(!revMap.count(lis[i]));
        revMap[lis[i]] = i;
    }

    BytecodeOpcodeRawValueMap r;
    r.m_list = std::move(lis);
    r.m_map = std::move(revMap);
    return r;
}

}   // namespace dast
