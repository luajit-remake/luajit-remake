#include "misc_llvm_helper.h"

#include "deegen_bytecode_operand.h"
#include "deegen_interpreter_bytecode_impl_creator.h"
#include "deegen_baseline_jit_impl_creator.h"
#include "deegen_options.h"
#include "api_define_bytecode.h"
#include "runtime_utils.h"
#include "deegen_jit_slow_path_data.h"
#include "base64_util.h"

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

llvm::Value* WARN_UNUSED BcOperand::GetOperandValueFromStorage(llvm::Value* storagePtr, size_t offsetInStorage, size_t storageSize, llvm::Instruction* insertBefore)
{
    using namespace llvm;
    LLVMContext& ctx = insertBefore->getContext();
    ReleaseAssert(llvm_value_has_type<void*>(storagePtr) || llvm_value_has_type<HeapPtr<void>>(storagePtr));
    ReleaseAssert(SupportsGetOperandValueFromBytecodeStruct());

    Type* storageTypeInBytecodeStruct = Type::getIntNTy(ctx, static_cast<uint32_t>(storageSize * 8));
    Value* storageValue = nullptr;

    GetElementPtrInst* gep = GetElementPtrInst::CreateInBounds(llvm_type_of<uint8_t>(ctx), storagePtr, { CreateLLVMConstantInt<uint64_t>(ctx, offsetInStorage) }, "", insertBefore);
    storageValue = new LoadInst(storageTypeInBytecodeStruct, gep, "", false /*isVolatile*/, Align(1), insertBefore);

    ReleaseAssert(storageValue != nullptr && storageValue->getType() == storageTypeInBytecodeStruct);

    Type* dstType = GetSourceValueFullRepresentationType(ctx);
    Value* result;
    if (IsSignedValue())
    {
        result = CastInst::CreateSExtOrBitCast(storageValue, dstType, "", insertBefore);
    }
    else
    {
        result = CastInst::CreateZExtOrBitCast(storageValue, dstType, "", insertBefore);
    }
    ReleaseAssert(result != nullptr);
    return result;
}

llvm::Value* WARN_UNUSED BcOperand::GetOperandValueFromStorage(llvm::Value* storagePtr, size_t offsetInStorage, size_t storageSize, llvm::BasicBlock* insertAtEnd)
{
    using namespace llvm;
    UnreachableInst* dummy = new UnreachableInst(storagePtr->getContext(), insertAtEnd);
    Value* res = GetOperandValueFromStorage(storagePtr, offsetInStorage, storageSize, dummy);
    dummy->eraseFromParent();
    return res;
}

llvm::Value* WARN_UNUSED BcOperand::GetOperandValueFromBytecodeStruct(llvm::Value* bytecodePtr, llvm::BasicBlock* targetBB)
{
    using namespace llvm;
    size_t offsetInBytecodeStruct = GetOffsetInBytecodeStruct();
    size_t numBytesInBytecodeStruct = GetSizeInBytecodeStruct();
    UnreachableInst* dummy = new UnreachableInst(bytecodePtr->getContext(), targetBB);
    Value* res = GetOperandValueFromStorage(bytecodePtr, offsetInBytecodeStruct, numBytesInBytecodeStruct, dummy);
    dummy->eraseFromParent();
    return res;
}

llvm::Value* WARN_UNUSED BcOperand::GetOperandValueFromBytecodeStruct(InterpreterBytecodeImplCreator* ifi, llvm::BasicBlock* targetBB)
{
    return GetOperandValueFromBytecodeStruct(ifi->GetCurBytecode(), targetBB);
}

json_t WARN_UNUSED BcOperand::SaveBaseToJSON()
{
    json_t j;
    j["kind"] = StringifyBcOperandKind(GetKind());
    j["name"] = m_name;
    j["operand_ordinal"] = m_operandOrdinal;
    j["offset_in_bcstruct"] = m_offsetInBytecodeStruct;
    j["size_in_bcstruct"] = m_sizeInBytecodeStruct;
    return j;
}

BcOperand::BcOperand(json_t& j)
{
    JSONCheckedGet(j, "name", m_name /*out*/);
    JSONCheckedGet(j, "operand_ordinal", m_operandOrdinal /*out*/);
    JSONCheckedGet(j, "offset_in_bcstruct", m_offsetInBytecodeStruct /*out*/);
    JSONCheckedGet(j, "size_in_bcstruct", m_sizeInBytecodeStruct /*out*/);
}

std::unique_ptr<BcOperand> WARN_UNUSED BcOperand::LoadFromJSON(json_t& j)
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

BcOpSlot::BcOpSlot(json_t& j)
    : BcOperand(j)
    , m_hasSpeculation(false)
    , m_specMask(0)
{
    ReleaseAssert(GetBcOperandKindFromString(JSONCheckedGet<std::string>(j, "kind")) == BcOperandKind::Slot);
    JSONCheckedGet(j, "has_dfg_speculation", m_hasSpeculation /*out*/);
    JSONCheckedGet(j, "dfg_speculation_mask", m_specMask /*out*/);
}

json_t WARN_UNUSED BcOpSlot::SaveToJSON()
{
    json_t j = SaveBaseToJSON();
    j["has_dfg_speculation"] = m_hasSpeculation;
    j["dfg_speculation_mask"] = m_specMask;
    ReleaseAssertImp(m_hasSpeculation, m_specMask <= x_typeMaskFor<tTop>);
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

BcOpConstant::BcOpConstant(json_t& j)
    : BcOperand(j)
{
    ReleaseAssert(GetBcOperandKindFromString(JSONCheckedGet<std::string>(j, "kind")) == BcOperandKind::Constant);
    JSONCheckedGet(j, "typeMask", m_typeMask /*out*/);
}

json_t WARN_UNUSED BcOpConstant::SaveToJSON()
{
    json_t j = SaveBaseToJSON();
    j["typeMask"] = m_typeMask;
    return j;
}

llvm::Value* WARN_UNUSED BcOpConstant::EmitUsageValueFromBytecodeValue(DeegenBytecodeImplCreatorBase* ifi, llvm::BasicBlock* targetBB /*out*/, llvm::Value* bytecodeValue)
{
    using namespace llvm;
    LLVMContext& ctx = ifi->GetModule()->getContext();
    if (m_typeMask == x_typeMaskFor<tNil>)
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
        // CodeBlock, BaselineCodeBlock and DfgCodeBlock all share the same layout for constant table: the constant table
        // just sits right before the CodeBlock struct. So we don't need to distinguish them here
        //
        Value* cb;
        if (ifi->IsInterpreter())
        {
            InterpreterBytecodeImplCreator* ibc = assert_cast<InterpreterBytecodeImplCreator*>(ifi);
            cb = ibc->GetInterpreterCodeBlock();
        }
        else
        {
            JitImplCreatorBase* jbc = assert_cast<JitImplCreatorBase*>(ifi);
            cb = jbc->GetJitCodeBlock();
        }
        Value* bvPtr = GetElementPtrInst::CreateInBounds(llvm_type_of<uint64_t>(ctx), cb, { bytecodeValue }, "", targetBB);
        LoadInst* bv = new LoadInst(llvm_type_of<uint64_t>(ctx), bvPtr, "", targetBB);
        bv->setAlignment(Align(8));
        ReleaseAssert(bv->getType() == GetUsageType(ctx));
        return bv;
    }
}

BcOpLiteral::BcOpLiteral(json_t& j)
    : BcOperand(j)
{
    // This class is not final and is inherited by SpecializedLiteral, so both class could be calling us
    //
    BcOperandKind opKind = GetBcOperandKindFromString(JSONCheckedGet<std::string>(j, "kind"));
    ReleaseAssert(opKind == BcOperandKind::Literal || opKind == BcOperandKind::SpecializedLiteral);
    JSONCheckedGet(j, "lit_is_signed", m_isSigned /*out*/);
    JSONCheckedGet(j, "lit_num_bytes", m_numBytes /*out*/);
}

json_t WARN_UNUSED BcOpLiteral::SaveToJSON()
{
    json_t j = SaveBaseToJSON();
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

BcOpSpecializedLiteral::BcOpSpecializedLiteral(json_t& j)
    : BcOpLiteral(j)
{
    ReleaseAssert(GetBcOperandKindFromString(JSONCheckedGet<std::string>(j, "kind")) == BcOperandKind::SpecializedLiteral);
    JSONCheckedGet(j, "lit_concrete_value", m_concreteValue /*out*/);
}

json_t WARN_UNUSED BcOpSpecializedLiteral::SaveToJSON()
{
    json_t j = BcOpLiteral::SaveToJSON();
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

BcOpBytecodeRangeBase::BcOpBytecodeRangeBase(json_t& j)
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

json_t WARN_UNUSED BcOpBytecodeRangeBase::SaveToJSON()
{
    json_t j = SaveBaseToJSON();
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

BcOpInlinedMetadata::BcOpInlinedMetadata(json_t& j)
    : BcOperand(j)
{
    ReleaseAssert(GetBcOperandKindFromString(JSONCheckedGet<std::string>(j, "kind")) == BcOperandKind::InlinedMetadata);
    JSONCheckedGet(j, "inline_md_size", m_size /*out*/);
}

json_t WARN_UNUSED BcOpInlinedMetadata::SaveToJSON()
{
    json_t j = SaveBaseToJSON();
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

json_t WARN_UNUSED OperandRegPreferenceInfo::SaveToJSON()
{
    json_t j = json_t::object();
    j["is_initialized"] = m_isInitialized;
    j["is_initialized_by_user"] = m_isInitializedByUser;
    j["is_gpr_allowed"] = m_isGprAllowed;
    j["is_fpr_allowed"] = m_isFprAllowed;
    j["is_gpr_preferred"] = m_isGprPreferred;
    return j;
}

void OperandRegPreferenceInfo::LoadFromJSON(json_t& j)
{
    ReleaseAssert(j.is_object());
    m_isInitialized = JSONCheckedGet<bool>(j, "is_initialized");
    m_isInitializedByUser = JSONCheckedGet<bool>(j, "is_initialized_by_user");
    m_isGprAllowed = JSONCheckedGet<bool>(j, "is_gpr_allowed");
    m_isFprAllowed = JSONCheckedGet<bool>(j, "is_fpr_allowed");
    m_isGprPreferred = JSONCheckedGet<bool>(j, "is_gpr_preferred");
}

OperandRegPreferenceInfo WARN_UNUSED OperandRegPreferenceInfo::ParseFromLLVM(llvm::Module* module, llvm::Constant* cst)
{
    using namespace llvm;
    using OperandRegPriorityInfo = DeegenFrontendBytecodeDefinitionDescriptor::OperandRegPriorityInfo;
    using RegHintEnum = DeegenFrontendBytecodeDefinitionDescriptor::RegHint;

    OperandRegPreferenceInfo r;

    LLVMConstantStructReader reader(module, cst);
    auto numTerms = reader.GetValue<&DeegenFrontendBytecodeDefinitionDescriptor::OperandRegPriorityInfo::m_numTerms>();
    static_assert(std::is_same_v<decltype(numTerms), uint8_t>);
    if (numTerms == static_cast<uint8_t>(-1))
    {
        return r;
    }

    ReleaseAssert(numTerms <= OperandRegPriorityInfo::x_maxTerms);
    LLVMConstantArrayReader arrReader(module, reader.Get<&OperandRegPriorityInfo::m_priorityList>());
    ReleaseAssert(arrReader.GetNumElements<RegHintEnum>() == OperandRegPriorityInfo::x_maxTerms);

    RegHintEnum data[OperandRegPriorityInfo::x_maxTerms];
    for (size_t i = 0; i < numTerms; i++)
    {
        RegHintEnum val = static_cast<RegHintEnum>(arrReader.GetValue<std::underlying_type_t<RegHintEnum>>(i));
        data[i] = val;
    }

    r.m_isInitialized = true;
    r.m_isInitializedByUser = true;

    for (size_t i = 0; i < numTerms; i++)
    {
        if (data[i] == RegHintEnum::GPR)
        {
            r.m_isGprAllowed = true;
        }
        else
        {
            ReleaseAssert(data[i] == RegHintEnum::FPR);
            r.m_isFprAllowed = true;
        }
    }

    ReleaseAssert(numTerms > 0 && "empty reg candidate is not allowed!");
    if (data[0] == RegHintEnum::GPR)
    {
        r.m_isGprPreferred = true;
    }
    return r;
}

OperandRegPreferenceInfo WARN_UNUSED OperandRegPreferenceInfo::GetDefaultRegPreferenceFromTypeMask(TypeMaskTy mask)
{
    OperandRegPreferenceInfo r;
    r.m_isInitialized = true;
    if ((mask & x_typeMaskFor<tDouble>) == mask)
    {
        // Subset of tDouble, FPR only
        //
        r.m_isFprAllowed = true;
        return r;
    }

    if ((mask & x_typeMaskFor<tDouble>) == 0)
    {
        // Disjoint with tDouble, GPR only
        //
        r.m_isGprAllowed = true;
        return r;
    }

    // GPR & FPR
    //
    r.m_isGprAllowed = true;
    r.m_isFprAllowed = true;
    r.m_isGprPreferred = true;
    return r;
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

std::vector<BytecodeVariantCollection> WARN_UNUSED BytecodeVariantDefinition::ParseAllFromModule(llvm::Module* module)
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

    struct ParsedSpecializedOperand
    {
        DeegenSpecializationKind m_kind;
        uint64_t m_value;
        OperandRegPreferenceInfo m_regInfo;
    };

    auto readSpecializedOperand = [&](Constant* cst) -> ParsedSpecializedOperand {
        LLVMConstantStructReader spOperandReader(module, cst);
        auto kind = spOperandReader.GetValue<&SpecializedOperand::m_kind>();
        auto value = spOperandReader.GetValue<&SpecializedOperand::m_value>();
        OperandRegPreferenceInfo info = OperandRegPreferenceInfo::ParseFromLLVM(module, spOperandReader.Get<&SpecializedOperand::m_regHint>());

        return ParsedSpecializedOperand {
            .m_kind = kind,
            .m_value = value,
            .m_regInfo = info
        };
    };

    // Returns the C++ code that constructs the BytecodeRWCInfo as a string
    //
    struct ReadRCWExprResult
    {
        bool isApiCalled;
        std::vector<std::string> preheaderLines;
        std::vector<std::string> ctorItems;
        std::vector<RangeRcwInfoItem> allRawRangeItems;
    };

    auto readRCWExprList = [&](Constant* cst) -> ReadRCWExprResult {
        using RCWDesc = Desc::DeclareRWCInfo;
        using RangeDesc = Desc::Range;
        using OperandExpr = Desc::OperandExpr;
        using OperandExprNode = Desc::OperandExprNode;

        auto readOperandExpr = [&](Constant* expr) -> OperandExpr
        {
            OperandExpr ex;
            LLVMConstantStructReader r(module, expr);
            size_t numNodes = r.GetValue<&OperandExpr::m_numNodes>();
            ex.m_numNodes = numNodes;
            ReleaseAssert(numNodes <= OperandExpr::x_maxNodes);
            LLVMConstantArrayReader rd(module, r.Get<&OperandExpr::m_nodes>());
            ReleaseAssert(rd.GetNumElements<OperandExprNode>() == OperandExpr::x_maxNodes);
            for (size_t i = 0; i < numNodes; i++)
            {
                LLVMConstantStructReader rds(module, rd.Get<OperandExprNode>(i));
                ex.m_nodes[i].m_kind = rds.GetValue<&OperandExprNode::m_kind>();
                ex.m_nodes[i].m_left = rds.GetValue<&OperandExprNode::m_left>();
                ex.m_nodes[i].m_right = rds.GetValue<&OperandExprNode::m_right>();
                if (ex.m_nodes[i].m_kind == OperandExprNode::Kind::Operand)
                {
                    LLVMConstantArrayReader strReader(module, rds.Get<&OperandExprNode::m_operandName>());
                    ReleaseAssert(strReader.GetNumElements<char>() == OperandExprNode::x_maxOperandNameLength);
                    bool hasZero = false;
                    for (size_t idx = 0; idx < OperandExprNode::x_maxOperandNameLength; idx++)
                    {
                        char ch = GetValueOfLLVMConstantInt<char>(strReader.Get<char>(idx));
                        hasZero |= (ch == '\0');
                        ex.m_nodes[i].m_operandName[idx] = ch;
                    }
                    ReleaseAssert(hasZero);
                }
                ex.m_nodes[i].m_number = rds.GetValue<&OperandExprNode::m_number>();
            }
            return ex;
        };

        // Takes advantage of the fact that the OperandExpr contains no pointers inside
        //
        auto stringifyOperandExprBase64 = [&](const OperandExpr& expr) -> std::string
        {
            const unsigned char* data = reinterpret_cast<const unsigned char*>(&expr);
            return base64_encode(data, sizeof(OperandExpr));
        };

        LLVMConstantStructReader reader(module, cst);
        if (!reader.GetValue<&RCWDesc::m_apiCalled>())
        {
            ReadRCWExprResult res;
            res.isApiCalled = false;
            return res;
        }

        std::vector<std::string> ctorItems, preheaderItems;
        std::vector<RangeRcwInfoItem> allRawRangeItems;

        if (reader.GetValue<&RCWDesc::m_accessesVariadicArgs>())
        {
            ctorItems.push_back("BytecodeRWCDesc::CreateVarArgs()");
        }
        if (reader.GetValue<&RCWDesc::m_accessesVariadicResults>())
        {
            ctorItems.push_back("BytecodeRWCDesc::CreateVarRets()");
        }

        size_t numRanges = reader.GetValue<&RCWDesc::m_numRanges>();

        LLVMConstantArrayReader exprArrayReader(module, reader.Get<&RCWDesc::m_ranges>());
        ReleaseAssert(exprArrayReader.GetNumElements<RangeDesc>() == RCWDesc::x_maxRangeAnnotations);
        ReleaseAssert(numRanges <= RCWDesc::x_maxRangeAnnotations);
        for (size_t i = 0; i < numRanges; i++)
        {
            LLVMConstantStructReader rangeDescReader(module, exprArrayReader.Get<RangeDesc>(i));
            OperandExpr start = readOperandExpr(rangeDescReader.Get<&RangeDesc::m_start>());
            OperandExpr len = readOperandExpr(rangeDescReader.Get<&RangeDesc::m_len>());

            bool shouldValueProfileRange = rangeDescReader.GetValue<&RangeDesc::m_shouldValueProfileRange>();
            bool shouldExplicitNoProfileRange = rangeDescReader.GetValue<&RangeDesc::m_isExplicitlyNoProfile>();
            bool isFixedOutputTypeMaskRange = rangeDescReader.GetValue<&RangeDesc::m_isFixedOutputTypeMask>();
            TypeMaskTy fixedOutputTypeMask = rangeDescReader.GetValue<&RangeDesc::m_fixedOutputTypeMask>();
            Constant* typeDeductionRuleFnCst = rangeDescReader.Get<&RangeDesc::m_typeDeductionRuleFn>();
            Function* typeDeductionRuleFn = nullptr;
            bool hasTypeDeductionRuleFn;
            if (isa<ConstantPointerNull>(typeDeductionRuleFnCst))
            {
                hasTypeDeductionRuleFn = false;
            }
            else
            {
                ReleaseAssert(isa<Function>(typeDeductionRuleFnCst));
                hasTypeDeductionRuleFn = true;
                typeDeductionRuleFn = cast<Function>(typeDeductionRuleFnCst);
                if (!typeDeductionRuleFn->hasInternalLinkage())
                {
                    fprintf(stderr, "Type deduction function should be marked 'static'!\n");
                    abort();
                }
            }

            RangeRcwInfoItem item;
            item.m_startExpr = stringifyOperandExprBase64(start);
            item.m_lenExpr = stringifyOperandExprBase64(len);
            item.m_fixedMask = 0;
            item.m_typeDeductionFnName = "";
            if (shouldValueProfileRange)
            {
                ReleaseAssert(!shouldExplicitNoProfileRange && !isFixedOutputTypeMaskRange);
                if (hasTypeDeductionRuleFn)
                {
                    item.m_typeDeductionKind = TypeDeductionKind::ValueProfileWithFunction;
                    item.m_typeDeductionFnName = typeDeductionRuleFn->getName().str();
                }
                else
                {
                    item.m_typeDeductionKind = TypeDeductionKind::ValueProfile;
                }
            }
            else if (shouldExplicitNoProfileRange)
            {
                ReleaseAssert(!shouldValueProfileRange && !isFixedOutputTypeMaskRange && !hasTypeDeductionRuleFn);
                item.m_typeDeductionKind = TypeDeductionKind::NeverProfile;
            }
            else if (isFixedOutputTypeMaskRange)
            {
                ReleaseAssert(!shouldValueProfileRange && !shouldExplicitNoProfileRange && !hasTypeDeductionRuleFn);
                ReleaseAssert(fixedOutputTypeMask != 0);
                ReleaseAssert(fixedOutputTypeMask <= x_typeMaskFor<tTop>);
                item.m_typeDeductionKind = TypeDeductionKind::Constant;
                item.m_fixedMask = fixedOutputTypeMask;
            }
            else if (hasTypeDeductionRuleFn)
            {
                ReleaseAssert(!shouldValueProfileRange && !shouldExplicitNoProfileRange && !isFixedOutputTypeMaskRange);
                item.m_typeDeductionKind = TypeDeductionKind::Function;
                item.m_typeDeductionFnName = typeDeductionRuleFn->getName().str();
            }
            else
            {
                item.m_typeDeductionKind = TypeDeductionKind::Invalid;
            }
            allRawRangeItems.push_back(item);
            ReleaseAssert(!start.IsInfinity());
            preheaderItems.push_back("int64_t range_start_" + std::to_string(i) + " = " + start.PrintCppExpression("ops") + ";");
            preheaderItems.push_back("TestAssert(range_start_" + std::to_string(i) + " >= 0);");
            preheaderItems.push_back("int64_t range_len_" + std::to_string(i) + " = " + len.PrintCppExpression("ops") + ";");
            if (!len.IsInfinity())
            {
                preheaderItems.push_back("TestAssert(range_len_" + std::to_string(i) + " >= 0);");
            }
            ctorItems.push_back("BytecodeRWCDesc::CreateRange(SafeIntegerCast<size_t>(range_start_" + std::to_string(i) + "), range_len_" + std::to_string(i) + ")");
        }

        return ReadRCWExprResult {
            .isApiCalled = true,
            .preheaderLines = preheaderItems,
            .ctorItems = ctorItems,
            .allRawRangeItems = allRawRangeItems
        };
    };

    std::vector<BytecodeVariantCollection> result;
    for (size_t curBytecodeOrd = 0; curBytecodeOrd < numBytecodesInThisTU; curBytecodeOrd++)
    {
        result.push_back({});
        BytecodeVariantCollection& curBytecodeInfo = result.back();

        LLVMConstantStructReader curDefReader(module, defListReader.Get<Desc>(curBytecodeOrd));
        ReleaseAssert(curDefReader.GetValue<&Desc::m_operandTypeListInitialized>() == true);
        ReleaseAssert(curDefReader.GetValue<&Desc::m_implementationInitialized>() == true);
        ReleaseAssert(curDefReader.GetValue<&Desc::m_resultKindInitialized>() == true);
        size_t numVariants = curDefReader.GetValue<&Desc::m_numVariants>();
        size_t numDfgVariants = curDefReader.GetValue<&Desc::m_numDfgVariants>();
        size_t numOperands = curDefReader.GetValue<&Desc::m_numOperands>();
        LLVMConstantArrayReader operandListReader(module, curDefReader.Get<&Desc::m_operandTypes>());
        ReleaseAssert(operandListReader.GetNumElements<Desc::Operand>() == Desc::x_maxOperands);
        bool hasTValueOutput = curDefReader.GetValue<&Desc::m_hasTValueOutput>();
        bool canPerformBranch = curDefReader.GetValue<&Desc::m_canPerformBranch>();
        bool isInterpreterToBaselineJitOsrEntryPoint = curDefReader.GetValue<&Desc::m_isInterpreterTierUpPoint>();

        size_t intrinsicOrd = curDefReader.GetValue<&Desc::m_intrinsicOrd>();

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

        std::vector<OperandRegPreferenceInfo> bcLevelOperandRegPrefInfo;
        {
            LLVMConstantStructReader tmpReader(module, curDefReader.Get<&Desc::m_bcRegPriorityInfo>());
            LLVMConstantArrayReader regPrefInfoList(module, tmpReader.Dewrap());
            ReleaseAssert(regPrefInfoList.GetNumElements<Desc::OperandRegPriorityInfo>() == Desc::x_maxOperands);
            for (size_t i = 0; i < numOperands; i++)
            {
                Constant* regPrefInfo = regPrefInfoList.Get<Desc::OperandRegPriorityInfo>(i);
                bcLevelOperandRegPrefInfo.push_back(OperandRegPreferenceInfo::ParseFromLLVM(module, regPrefInfo));
            }
        }

        OperandRegPreferenceInfo bcLevelOutputRegPrefInfo = OperandRegPreferenceInfo::ParseFromLLVM(
            module, curDefReader.Get<&Desc::m_outputRegPriorityInfo>());

        bool shouldDisableRegAllocEnabledAssertEvenIfRegHintGiven = curDefReader.GetValue<&Desc::m_disableRegAllocMustBeEnabledAssert>();

        ReadRCWExprResult bcwiseRCWReadInfo = readRCWExprList(curDefReader.Get<&Desc::m_bcDeclareReadsInfo>());
        ReadRCWExprResult bcwiseRCWWriteInfo = readRCWExprList(curDefReader.Get<&Desc::m_bcDeclareWritesInfo>());
        ReadRCWExprResult bcwiseRCWClobberInfo = readRCWExprList(curDefReader.Get<&Desc::m_bcDeclareClobbersInfo>());

        auto readVariant = [&](bool isDfgVariant, size_t variantOrd, LLVMConstantStructReader& variantReader) -> std::unique_ptr<BytecodeVariantDefinition>
        {
            std::unique_ptr<BytecodeVariantDefinition> def = std::make_unique<BytecodeVariantDefinition>();
            def->m_bytecodeOrdInTU = curBytecodeOrd;
            def->m_variantOrd = variantOrd;
            def->m_bytecodeName = bytecodeNamesInThisTU[curBytecodeOrd];
            def->m_opNames = operandNames;
            def->m_originalOperandTypes = operandTypes;
            def->m_isDfgVariant = isDfgVariant;
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
            def->m_bytecodeMayFallthroughToNextBytecodeDetermined = false;
            def->m_bytecodeMayMakeTailCallDetermined = false;
            def->m_isInterpreterToBaselineJitOsrEntryPoint = isInterpreterToBaselineJitOsrEntryPoint;
            def->m_disableRegAllocEnabledAssertEvenIfRegHintGiven = shouldDisableRegAllocEnabledAssertEvenIfRegHintGiven;
            if (hasTValueOutput)
            {
                def->m_outputOperand = std::make_unique<BcOpSlot>("output");
            }
            if (canPerformBranch)
            {
                def->m_condBrTarget = std::make_unique<BcOpLiteral>("condBrTarget", true /*isSigned*/, 4 /*numBytes*/);
            }

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

            def->m_operandRegPrefInfo = bcLevelOperandRegPrefInfo;
            def->m_outputRegPrefInfo = bcLevelOutputRegPrefInfo;

            {
                OperandRegPreferenceInfo regPrefInfo = OperandRegPreferenceInfo::ParseFromLLVM(
                    module, variantReader.Get<&Desc::SpecializedVariant::m_baseOutputRegPriority>());

                if (regPrefInfo.m_isInitialized)
                {
                    def->m_outputRegPrefInfo = regPrefInfo;
                }
            }

            ReleaseAssertImp(def->m_outputRegPrefInfo.m_isInitialized, def->m_hasOutputValue);

            LLVMConstantArrayReader baseReader(module, variantReader.Get<&Desc::SpecializedVariant::m_base>());
            for (size_t opOrd = 0; opOrd < numOperands; opOrd++)
            {
                // Type specialization/speculation information overrides bytecode-level reg hint
                // Helper function that updates reg hint based on the type info
                //
                auto updateRegPrefBasedOnTypeMask = [&](uint64_t typeMask)
                {
                    ReleaseAssert(typeMask <= x_typeMaskFor<tTop>);
                    ReleaseAssert(opOrd < def->m_operandRegPrefInfo.size());
                    if (typeMask != x_typeMaskFor<tTop>)
                    {
                        def->m_operandRegPrefInfo[opOrd] = OperandRegPreferenceInfo::GetDefaultRegPreferenceFromTypeMask(SafeIntegerCast<TypeMaskTy>(typeMask));
                    }
                };

                ParsedSpecializedOperand spOp = readSpecializedOperand(baseReader.Get<SpecializedOperand>(opOrd));
                DeegenBytecodeOperandType opType = operandTypes[opOrd];
                std::string operandName = operandNames[opOrd];
                if (opType == DeegenBytecodeOperandType::BytecodeSlotOrConstant)
                {
                    // For DFG variant, BytecodeSlotOrConstant is always a bytecode slot, since the input is either reg-alloc'ed or spilled
                    //
                    if (spOp.m_kind == DeegenSpecializationKind::BytecodeSlot || isDfgVariant)
                    {
                        std::unique_ptr<BcOpSlot> op = std::make_unique<BcOpSlot>(operandName);
                        // If this is a DFG variant, we also need to know the speculations
                        //
                        if (isDfgVariant)
                        {
                            ReleaseAssert(spOp.m_kind == DeegenSpecializationKind::SpeculatedTypeForOptimizer || spOp.m_kind == DeegenSpecializationKind::NotSpecialized);
                            if (spOp.m_kind == DeegenSpecializationKind::SpeculatedTypeForOptimizer)
                            {
                                op->SetDfgSpeculation(spOp.m_value);
                                updateRegPrefBasedOnTypeMask(spOp.m_value);
                            }
                        }
                        def->m_list.push_back(std::move(op));
                    }
                    else
                    {
                        ReleaseAssert(spOp.m_kind == DeegenSpecializationKind::BytecodeConstantWithType && "unexpected specialization");
                        TypeMaskTy typeMask = SafeIntegerCast<TypeMaskTy>(spOp.m_value);
                        def->m_list.push_back(std::make_unique<BcOpConstant>(operandName, typeMask));
                        updateRegPrefBasedOnTypeMask(typeMask);
                    }
                }
                else if (opType == DeegenBytecodeOperandType::BytecodeSlot)
                {
                    std::unique_ptr<BcOpSlot> op = std::make_unique<BcOpSlot>(operandName);
                    if (isDfgVariant)
                    {
                        ReleaseAssert(spOp.m_kind == DeegenSpecializationKind::SpeculatedTypeForOptimizer || spOp.m_kind == DeegenSpecializationKind::NotSpecialized);
                        if (spOp.m_kind == DeegenSpecializationKind::SpeculatedTypeForOptimizer)
                        {
                            op->SetDfgSpeculation(spOp.m_value);
                            updateRegPrefBasedOnTypeMask(spOp.m_value);
                        }
                    }
                    else
                    {
                        ReleaseAssert(spOp.m_kind == DeegenSpecializationKind::NotSpecialized && "unexpected specialization");
                    }
                    def->m_list.push_back(std::move(op));
                }
                else if (opType == DeegenBytecodeOperandType::Constant)
                {
                    // DFG variant does not have the concept of constant, constant is implemented as bytecode slot
                    //
                    if (isDfgVariant)
                    {
                        ReleaseAssert(spOp.m_kind == DeegenSpecializationKind::SpeculatedTypeForOptimizer || spOp.m_kind == DeegenSpecializationKind::NotSpecialized);
                        std::unique_ptr<BcOpSlot> op = std::make_unique<BcOpSlot>(operandName);
                        if (spOp.m_kind == DeegenSpecializationKind::SpeculatedTypeForOptimizer)
                        {
                            op->SetDfgSpeculation(spOp.m_value);
                            updateRegPrefBasedOnTypeMask(spOp.m_value);
                        }
                        def->m_list.push_back(std::move(op));
                    }
                    else
                    {
                        TypeMaskTy specMask;
                        if (spOp.m_kind == DeegenSpecializationKind::NotSpecialized)
                        {
                            specMask = x_typeMaskFor<tTop>;
                        }
                        else if (spOp.m_kind == DeegenSpecializationKind::BytecodeConstantWithType)
                        {
                            specMask = SafeIntegerCast<TypeMaskTy>(spOp.m_value);
                        }
                        else
                        {
                            ReleaseAssert(false && "unexpected specialization");
                        }
                        def->m_list.push_back(std::make_unique<BcOpConstant>(operandName, specMask));
                        updateRegPrefBasedOnTypeMask(specMask);
                    }
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
                // RegHint directly specified at the variant level can override all earlier decisions
                //
                if (spOp.m_regInfo.m_isInitialized)
                {
                    ReleaseAssert(opOrd < def->m_operandRegPrefInfo.size());
                    def->m_operandRegPrefInfo[opOrd] = spOp.m_regInfo;
                }
            }
            ReleaseAssert(def->m_list.size() == def->m_opNames.size());
            ReleaseAssert(def->m_list.size() == def->m_operandRegPrefInfo.size());

            for (size_t i = 0; i < def->m_list.size(); i++)
            {
                if (def->m_operandRegPrefInfo[i].m_isInitialized)
                {
                    ReleaseAssert(def->m_list[i]->GetKind() == BcOperandKind::Slot || def->m_list[i]->GetKind() == BcOperandKind::Constant);
                }
            }

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
                    ParsedSpecializedOperand spOp = readSpecializedOperand(quickeningReader.Get<SpecializedOperand>(opOrd));
                    ReleaseAssert(!spOp.m_regInfo.m_isInitialized && "Specifying a RegHint in EnableHotColdSplitting has no effect!");
                    if (spOp.m_kind == DeegenSpecializationKind::NotSpecialized)
                    {
                        continue;
                    }
                    ReleaseAssert(spOp.m_kind == DeegenSpecializationKind::SpeculatedTypeForOptimizer);
                    ReleaseAssert(def->m_list[opOrd]->GetKind() == BcOperandKind::Slot || def->m_list[opOrd]->GetKind() == BcOperandKind::Constant);
                    TypeMaskTy specMask = SafeIntegerCast<TypeMaskTy>(spOp.m_value);
                    def->m_quickening.push_back({ .m_operandOrd = opOrd, .m_speculatedMask = specMask });
                }
                ReleaseAssert(def->m_quickening.size() > 0);
            }

            enum class GenerateRCWKind
            {
                Read,
                Write,
                Clobber
            };

            auto generateRCWGetterFuncImpl = [&](ReadRCWExprResult& rcwInfo, GenerateRCWKind rcwKind) -> std::string
            {
                std::vector<std::string> ctorItems;
                if (rcwKind == GenerateRCWKind::Write)
                {
                    // The explicit output must come first, if it exists
                    //
                    if (hasTValueOutput)
                    {
                        ctorItems.push_back("BytecodeRWCDesc::CreateLocal(SafeIntegerCast<size_t>(ops.output.AsRawValue()))");
                    }
                }
                if (rcwKind == GenerateRCWKind::Read)
                {
                    // Append all the direct inputs and constants to the read list
                    //
                    for (size_t i = 0; i < def->m_list.size(); i++)
                    {
                        bool isSpecialized = (operandTypes[i] == DeegenBytecodeOperandType::BytecodeSlotOrConstant);
                        BcOperand* operand = def->m_list[i].get();
                        if (operand->GetKind() == BcOperandKind::Slot)
                        {
                            ctorItems.push_back("BytecodeRWCDesc::CreateLocal(SafeIntegerCast<size_t>(ops." + operand->OperandName() + (isSpecialized ? ".AsLocal()" : "") + ".AsRawValue()))");
                        }
                        else if (operand->GetKind() == BcOperandKind::Constant)
                        {
                            ctorItems.push_back("BytecodeRWCDesc::CreateConstant(ops." + operand->OperandName() + (isSpecialized ? ".AsConstant()" : ".m_value") + ")");
                        }
                    }
                }
                ctorItems.insert(ctorItems.end(), rcwInfo.ctorItems.begin(), rcwInfo.ctorItems.end());

                std::string res = std::string("    BytecodeRWCInfo DeegenGetBytecode")
                    + (rcwKind == GenerateRCWKind::Read ? "Read" : (rcwKind == GenerateRCWKind::Write ? "Write" : "Clobber"))
                    + "DeclarationsImpl" + std::to_string(variantOrd) + "(size_t bcPos)\n";

                res += "    {\n";
                res += "        [[maybe_unused]] Operands ops = DeegenDecodeImpl" + std::to_string(variantOrd) + "(bcPos);\n";
                for (const std::string& s : rcwInfo.preheaderLines)
                {
                    res += "        " + s + "\n";
                }

                res += "        return BytecodeRWCInfo (\n";
                bool isFirstItem = true;
                for (const std::string& s : ctorItems)
                {
                    if (!isFirstItem)
                    {
                        res += ",\n";
                    }
                    isFirstItem = false;
                    res += "            " + s;
                }
                res += "\n        );\n";
                res += "    }\n\n";

                return res;
            };

            auto generateRCWGetterFunc = [&](ReadRCWExprResult& bcwiseInfo, GenerateRCWKind rcwKind) -> std::string
            {
                if (!bcwiseInfo.isApiCalled)
                {
                    for (size_t i = 0; i < def->m_list.size(); i++)
                    {
                        if (def->m_list[i]->GetKind() == BcOperandKind::BytecodeRangeBase)
                        {
                            if (rcwKind == GenerateRCWKind::Read)
                            {
                                fprintf(stderr, "You should declare reads if you used a BytecodeRange!\n");
                                abort();
                            }
                            if (rcwKind == GenerateRCWKind::Write && !assert_cast<BcOpBytecodeRangeBase*>(def->m_list[i].get())->m_isReadOnly)
                            {
                                fprintf(stderr, "You should declare writes if you used a writable BytecodeRange!\n");
                                abort();
                            }
                        }
                    }
                }

                return generateRCWGetterFuncImpl(bcwiseInfo, rcwKind);
            };

            def->m_rcwInfoFuncs =
                generateRCWGetterFunc(bcwiseRCWReadInfo, GenerateRCWKind::Read)
                + generateRCWGetterFunc(bcwiseRCWWriteInfo, GenerateRCWKind::Write)
                + generateRCWGetterFunc(bcwiseRCWClobberInfo, GenerateRCWKind::Clobber);

            auto getRCWRawRangeExprs = [&](ReadRCWExprResult& bcwiseInfo) -> std::vector<RangeRcwInfoItem>
            {
                if (bcwiseInfo.isApiCalled)
                {
                    return bcwiseInfo.allRawRangeItems;
                }
                else
                {
                    ReleaseAssert(bcwiseInfo.allRawRangeItems.empty());
                    return {};
                }
            };

            def->m_rawReadRangeExprs = getRCWRawRangeExprs(bcwiseRCWReadInfo);
            def->m_rawWriteRangeExprs = getRCWRawRangeExprs(bcwiseRCWWriteInfo);
            def->m_rawClobberRangeExprs = getRCWRawRangeExprs(bcwiseRCWClobberInfo);

            for (RangeRcwInfoItem& item : def->m_rawReadRangeExprs)
            {
                if (item.m_typeDeductionKind != TypeDeductionKind::Invalid)
                {
                    fprintf(stderr, "Read ranges must NOT have a type deduction rule!\n");
                    abort();
                }
            }
            for (RangeRcwInfoItem& item : def->m_rawWriteRangeExprs)
            {
                if (item.m_typeDeductionKind == TypeDeductionKind::Invalid)
                {
                    fprintf(stderr, "Write ranges must provide a valid type deduction rule!\n");
                    abort();
                }
            }
            for (RangeRcwInfoItem& item : def->m_rawClobberRangeExprs)
            {
                if (item.m_typeDeductionKind != TypeDeductionKind::Invalid)
                {
                    fprintf(stderr, "Clobber ranges must NOT have a type deduction rule!\n");
                    abort();
                }
            }

            // Parse and populate output type deduction info
            //
            {
                bool shouldValueProfileRange = curDefReader.GetValue<&Desc::m_outputShouldBeValueProfiled>();
                bool shouldExplicitNoProfileRange = curDefReader.GetValue<&Desc::m_outputShouldExplicitlyNotProfiled>();
                bool isFixedOutputTypeMaskRange = curDefReader.GetValue<&Desc::m_outputHasFixedTypeMask>();
                TypeMaskTy fixedOutputTypeMask = curDefReader.GetValue<&Desc::m_outputFixedTypeMask>();
                Constant* typeDeductionRuleFnCst = curDefReader.Get<&Desc::m_outputTypeDeductionRule>();
                Function* typeDeductionRuleFn = nullptr;
                bool hasTypeDeductionRuleFn;
                if (isa<ConstantPointerNull>(typeDeductionRuleFnCst))
                {
                    hasTypeDeductionRuleFn = false;
                }
                else
                {
                    ReleaseAssert(isa<Function>(typeDeductionRuleFnCst));
                    hasTypeDeductionRuleFn = true;
                    typeDeductionRuleFn = cast<Function>(typeDeductionRuleFnCst);
                    if (!typeDeductionRuleFn->hasInternalLinkage())
                    {
                        fprintf(stderr, "Type deduction function should be marked 'static'!\n");
                        abort();
                    }
                }

                def->m_outputTypeDeductionInfo.m_fixedMask = 0;
                def->m_outputTypeDeductionInfo.m_typeDeductionFnName = "";

                if (hasTValueOutput)
                {
                    if (shouldValueProfileRange)
                    {
                        ReleaseAssert(!shouldExplicitNoProfileRange && !isFixedOutputTypeMaskRange);
                        if (hasTypeDeductionRuleFn)
                        {
                            def->m_outputTypeDeductionInfo.m_typeDeductionKind = TypeDeductionKind::ValueProfileWithFunction;
                            def->m_outputTypeDeductionInfo.m_typeDeductionFnName = typeDeductionRuleFn->getName().str();
                        }
                        else
                        {
                            def->m_outputTypeDeductionInfo.m_typeDeductionKind = TypeDeductionKind::ValueProfile;
                        }
                    }
                    else if (shouldExplicitNoProfileRange)
                    {
                        ReleaseAssert(!shouldValueProfileRange && !isFixedOutputTypeMaskRange && !hasTypeDeductionRuleFn);
                        def->m_outputTypeDeductionInfo.m_typeDeductionKind = TypeDeductionKind::NeverProfile;
                    }
                    else if (isFixedOutputTypeMaskRange)
                    {
                        ReleaseAssert(!shouldValueProfileRange && !shouldExplicitNoProfileRange && !hasTypeDeductionRuleFn);
                        ReleaseAssert(fixedOutputTypeMask != 0);
                        ReleaseAssert(fixedOutputTypeMask <= x_typeMaskFor<tTop>);
                        def->m_outputTypeDeductionInfo.m_typeDeductionKind = TypeDeductionKind::Constant;
                        def->m_outputTypeDeductionInfo.m_fixedMask = fixedOutputTypeMask;
                    }
                    else if (hasTypeDeductionRuleFn)
                    {
                        ReleaseAssert(!shouldValueProfileRange && !shouldExplicitNoProfileRange && !isFixedOutputTypeMaskRange);
                        def->m_outputTypeDeductionInfo.m_typeDeductionKind = TypeDeductionKind::Function;
                        def->m_outputTypeDeductionInfo.m_typeDeductionFnName = typeDeductionRuleFn->getName().str();
                    }
                    else
                    {
                        fprintf(stderr, "No output type deduction rule specified for node with an output value!\n");
                        abort();
                    }
                }
                else
                {
                    def->m_outputTypeDeductionInfo.m_typeDeductionKind = TypeDeductionKind::Invalid;
                    if (shouldValueProfileRange || shouldExplicitNoProfileRange || isFixedOutputTypeMaskRange || hasTypeDeductionRuleFn)
                    {
                        fprintf(stderr, "Output type deduction rule should only be specified for nodes with an output value!\n");
                        abort();
                    }
                }
            }

            def->m_bcIntrinsicOrd = intrinsicOrd;

#define macro2(intrinsicName, ...) PP_STRINGIFY(intrinsicName),
#define macro(item) macro2 item
            constexpr const char* bcIntrinsicNames[] = { PP_FOR_EACH(macro, DEEGEN_BYTECODE_INTRINSIC_LIST) nullptr };
#undef macro
#undef macro2
            constexpr size_t numBcIntrinsics = std::extent_v<decltype(bcIntrinsicNames)> - 1;

            if (intrinsicOrd != static_cast<size_t>(-1))
            {
                ReleaseAssert(intrinsicOrd < numBcIntrinsics);
                def->m_bcIntrinsicName = bcIntrinsicNames[intrinsicOrd];

                auto generateIntrinsicInfoGetter = [&]() -> std::string
                {
                    size_t numIntrinsicArgs = curDefReader.GetValue<&Desc::m_numIntrinsicArgs>();
                    ReleaseAssert(numIntrinsicArgs <= Desc::x_maxIntrinsicArgCount);
                    std::vector<size_t> intrinsicArgOrd;
                    LLVMConstantArrayReader rds(module, curDefReader.Get<&Desc::m_intrinsicArgOperandOrd>());
                    ReleaseAssert(rds.GetNumElements<size_t>() == Desc::x_maxIntrinsicArgCount);
                    for (size_t i = 0; i < numIntrinsicArgs; i++)
                    {
                        size_t argOrd = rds.GetValue<size_t>(i);
                        ReleaseAssert(argOrd < def->m_list.size());
                        intrinsicArgOrd.push_back(argOrd);
                    }

                    std::string res = "    BytecodeIntrinsicInfo::" + std::string(bcIntrinsicNames[intrinsicOrd])
                        + " GetIntrinsicInfoImpl" + std::to_string(variantOrd) + "(size_t bcPos)\n";

                    res += "    {\n";
                    res += "        [[maybe_unused]] Operands ops = DeegenDecodeImpl" + std::to_string(variantOrd) + "(bcPos);\n";
                    res += "        return BytecodeIntrinsicInfo::" + std::string(bcIntrinsicNames[intrinsicOrd]) + " {\n";
                    bool isFirst = true;
                    for (size_t argOrd : intrinsicArgOrd)
                    {
                        if (!isFirst)
                        {
                            res += ",\n";
                        }
                        isFirst = false;
                        BcOperand* op = def->m_list[argOrd].get();
                        bool isSpecialized = (operandTypes[argOrd] == DeegenBytecodeOperandType::BytecodeSlotOrConstant);
                        switch (op->GetKind())
                        {
                        case BcOperandKind::Slot:
                        {
                            res += "            LocalOrConstantOrNumber::CreateLocal(SafeIntegerCast<size_t>(ops."
                                + op->OperandName() + (isSpecialized ? ".AsLocal()" : "") + ".AsRawValue()))";
                            break;
                        }
                        case BcOperandKind::Constant:
                        {
                            res += "            LocalOrConstantOrNumber::CreateConstant(ops."
                                + op->OperandName() + (isSpecialized ? ".AsConstant()" : ".m_value") + ")";
                            break;
                        }
                        case BcOperandKind::Literal: [[fallthrough]];
                        case BcOperandKind::SpecializedLiteral:
                        {
                            res += "            LocalOrConstantOrNumber::CreateNumber(ops."
                                + op->OperandName() + ".AsRawValue())";
                            break;
                        }
                        case BcOperandKind::BytecodeRangeBase:
                        {
                            res += "            LocalOrConstantOrNumber::CreateLocal(SafeIntegerCast<size_t>(ops."
                                + op->OperandName() + ".AsRawValue()))";
                            break;
                        }
                        case BcOperandKind::InlinedMetadata:
                        {
                            ReleaseAssert(false);
                        }
                        }
                    }
                    res += "\n        };\n";
                    res += "    }\n\n";
                    return res;
                };

                def->m_bcIntrinsicInfoGetterFunc = generateIntrinsicInfoGetter();
            }

            return def;
        };

        {
            ReleaseAssert(numVariants > 0);
            LLVMConstantArrayReader variantListReader(module, curDefReader.Get<&Desc::m_variants>());
            for (size_t variantOrd = 0; variantOrd < numVariants; variantOrd++)
            {
                LLVMConstantStructReader variantReader(module, variantListReader.Get<Desc::SpecializedVariant>(variantOrd));
                curBytecodeInfo.m_variants.push_back(readVariant(
                    false /*isDfgVariant*/,
                    variantOrd,
                    variantReader));
            }
        }

        {
            LLVMConstantArrayReader variantListReader(module, curDefReader.Get<&Desc::m_dfgVariants>());
            for (size_t variantOrd = 0; variantOrd < numDfgVariants; variantOrd++)
            {
                LLVMConstantStructReader variantReader(module, variantListReader.Get<Desc::SpecializedVariant>(variantOrd));
                curBytecodeInfo.m_dfgVariants.push_back(readVariant(
                    true /*isDfgVariant*/,
                    variantOrd,
                    variantReader));
            }
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
            for (std::unique_ptr<BytecodeVariantDefinition>& it : result[ord].m_variants)
            {
                list.push_back(it.get());
            }
        }
        for (std::unique_ptr<BytecodeVariantDefinition>& it : result[curBytecodeOrd].m_variants)
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

BytecodeVariantDefinition::BytecodeVariantDefinition(json_t& j)
    : BytecodeVariantDefinition()
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
        std::vector<json_t> operand_list = j["operand_list"];
        for (json_t& op_json : operand_list)
        {
            std::unique_ptr<BcOperand> op = BcOperand::LoadFromJSON(op_json);
            m_list.push_back(std::move(op));
        }
    }
    m_hasDecidedOperandWidth = true;
    m_bytecodeStructLengthTentativelyFinalized = true;
    m_bytecodeStructLengthFinalized = true;

    JSONCheckedGet(j, "is_dfg_variant", m_isDfgVariant);

    JSONCheckedGet(j, "has_output_value", m_hasOutputValue);
    if (m_hasOutputValue)
    {
        ReleaseAssert(j.count("output_operand"));
        json_t op_json = j["output_operand"];
        std::unique_ptr<BcOperand> op = BcOperand::LoadFromJSON(op_json);
        ReleaseAssert(op->GetKind() == BcOperandKind::Slot);
        m_outputOperand.reset(assert_cast<BcOpSlot*>(op.release()));
    }

    JSONCheckedGet(j, "has_cond_br_target", m_hasConditionalBranchTarget);
    if (m_hasConditionalBranchTarget)
    {
        ReleaseAssert(j.count("cond_br_target_operand"));
        json_t op_json = j["cond_br_target_operand"];
        std::unique_ptr<BcOperand> op = BcOperand::LoadFromJSON(op_json);
        ReleaseAssert(op->GetKind() == BcOperandKind::Literal);
        m_condBrTarget.reset(assert_cast<BcOpLiteral*>(op.release()));
    }

    // The bytecode metadata info is currently not serialized since the JIT doesn't need it
    //
    m_metadataStructInfoAssigned = false;

    JSONCheckedGet(j, "is_interpreter_call_ic_explicitly_disabled", m_isInterpreterCallIcExplicitlyDisabled);
    JSONCheckedGet(j, "is_interpreter_call_ic_ever_used", m_isInterpreterCallIcEverUsed);
    JSONCheckedGet(j, "num_jit_call_ic", m_numJitCallICs);
    JSONCheckedGet(j, "num_jit_generic_ic", m_numJitGenericICs);
    JSONCheckedGet(j, "num_total_generic_ic_effect_kinds", m_totalGenericIcEffectKinds);

    JSONCheckedGet(j, "is_interpreter_to_baseline_jit_osr_entry_point", m_isInterpreterToBaselineJitOsrEntryPoint);
    JSONCheckedGet(j, "disable_reg_alloc_enabled_assert_even_if_reg_hint_given", m_disableRegAllocEnabledAssertEvenIfRegHintGiven);
    JSONCheckedGet(j, "rcw_info_funcs", m_rcwInfoFuncs);

    auto checkTdkValid = [&](uint32_t val) -> TypeDeductionKind
    {
        ReleaseAssert(val == static_cast<uint32_t>(TypeDeductionKind::Invalid) ||
                      val == static_cast<uint32_t>(TypeDeductionKind::ValueProfile) ||
                      val == static_cast<uint32_t>(TypeDeductionKind::NeverProfile) ||
                      val == static_cast<uint32_t>(TypeDeductionKind::Constant) ||
                      val == static_cast<uint32_t>(TypeDeductionKind::Function) ||
                      val == static_cast<uint32_t>(TypeDeductionKind::ValueProfileWithFunction));
        return static_cast<TypeDeductionKind>(val);
    };

    auto getRcwRangeInfo = [&](json_t& data) -> std::vector<RangeRcwInfoItem>
    {
        ReleaseAssert(data.is_array());
        std::vector<RangeRcwInfoItem> resList;
        for (json_t& jdata : data)
        {
            RangeRcwInfoItem item;
            JSONCheckedGet(jdata, "start", item.m_startExpr);
            JSONCheckedGet(jdata, "length", item.m_lenExpr);
            item.m_typeDeductionKind = checkTdkValid(JSONCheckedGet<uint32_t>(jdata, "type_deduction_kind"));
            JSONCheckedGet(jdata, "type_deduction_fixed_mask", item.m_fixedMask);
            JSONCheckedGet(jdata, "type_deduction_fn_name", item.m_typeDeductionFnName);
            resList.push_back(item);
        }
        return resList;
    };

    ReleaseAssert(j.count("rcw_range_operand_reads"));
    m_rawReadRangeExprs = getRcwRangeInfo(j["rcw_range_operand_reads"]);

    ReleaseAssert(j.count("rcw_range_operand_writes"));
    m_rawWriteRangeExprs = getRcwRangeInfo(j["rcw_range_operand_writes"]);

    ReleaseAssert(j.count("rcw_range_operand_clobbers"));
    m_rawClobberRangeExprs = getRcwRangeInfo(j["rcw_range_operand_clobbers"]);

    m_outputTypeDeductionInfo.m_typeDeductionKind = checkTdkValid(JSONCheckedGet<uint32_t>(j, "output_type_deduction_kind"));
    JSONCheckedGet(j, "output_type_deduction_fixed_mask", m_outputTypeDeductionInfo.m_fixedMask);
    JSONCheckedGet(j, "output_type_deduction_fn_name", m_outputTypeDeductionInfo.m_typeDeductionFnName);

    {
        ReleaseAssert(j.count("operand_reg_pref_info_list"));
        json_t& arr = j["operand_reg_pref_info_list"];
        ReleaseAssert(arr.is_array());
        for (size_t idx = 0; idx < arr.size(); idx++)
        {
            OperandRegPreferenceInfo info;
            info.LoadFromJSON(arr[idx]);
            m_operandRegPrefInfo.push_back(std::move(info));
        }
        ReleaseAssert(m_operandRegPrefInfo.size() == m_list.size());
    }

    ReleaseAssert(j.count("output_reg_pref_info"));
    m_outputRegPrefInfo.LoadFromJSON(j["output_reg_pref_info"]);

    JSONCheckedGet(j, "bc_intrinsic_ord", m_bcIntrinsicOrd);
    JSONCheckedGet(j, "bc_intrinsic_info_getter", m_bcIntrinsicInfoGetterFunc);
    JSONCheckedGet(j, "bc_intrinsic_name", m_bcIntrinsicName);

    m_bytecodeMayFallthroughToNextBytecodeDetermined = false;
    m_bytecodeMayMakeTailCallDetermined = false;

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
            desc.m_speculatedMask = SafeIntegerCast<TypeMaskTy>(item[1]);
            m_quickening.push_back(desc);
        }
    }

    JSONCheckedGet(j, "bytecode_struct_length", m_bytecodeStructLength);

    JSONCheckedGet(j, "bytecode_may_fallthrough_to_next_bytecode", m_bytecodeMayFallthroughToNextBytecode);
    m_bytecodeMayFallthroughToNextBytecodeDetermined = true;

    JSONCheckedGet(j, "bytecode_may_make_tail_call", m_bytecodeMayMakeTailCall);
    m_bytecodeMayMakeTailCallDetermined = true;
}

json_t WARN_UNUSED BytecodeVariantDefinition::SaveToJSON()
{
    json_t j;
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
        std::vector<json_t> operand_list;
        for (std::unique_ptr<BcOperand>& op : m_list)
        {
            operand_list.push_back(op->SaveToJSON());
        }
        j["operand_list"] = operand_list;
    }

    ReleaseAssert(m_hasDecidedOperandWidth);
    ReleaseAssert(m_bytecodeStructLengthTentativelyFinalized);
    ReleaseAssert(m_bytecodeStructLengthFinalized);

    j["is_dfg_variant"] = m_isDfgVariant;

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
    j["num_jit_call_ic"] = GetNumCallICsInBaselineJitTier();
    j["num_jit_generic_ic"] = GetNumGenericICsInJitTier();
    j["num_total_generic_ic_effect_kinds"] = GetTotalGenericIcEffectKinds();

    j["is_interpreter_to_baseline_jit_osr_entry_point"] = m_isInterpreterToBaselineJitOsrEntryPoint;
    j["disable_reg_alloc_enabled_assert_even_if_reg_hint_given"] = m_disableRegAllocEnabledAssertEvenIfRegHintGiven;
    j["rcw_info_funcs"] = m_rcwInfoFuncs;

    auto saveRcwRangeInfo = [&](const std::vector<RangeRcwInfoItem>& data) -> json_t
    {
        json_t r = json_t::array();
        for (const RangeRcwInfoItem& it : data)
        {
            json_t item = json_t::object();
            item["start"] = it.m_startExpr;
            item["length"] = it.m_lenExpr;
            item["type_deduction_kind"] = static_cast<uint32_t>(it.m_typeDeductionKind);
            item["type_deduction_fixed_mask"] = it.m_fixedMask;
            item["type_deduction_fn_name"] = it.m_typeDeductionFnName;
            r.push_back(std::move(item));
        }
        return r;
    };

    j["rcw_range_operand_reads"] = saveRcwRangeInfo(m_rawReadRangeExprs);
    j["rcw_range_operand_writes"] = saveRcwRangeInfo(m_rawWriteRangeExprs);
    j["rcw_range_operand_clobbers"] = saveRcwRangeInfo(m_rawClobberRangeExprs);

    j["output_type_deduction_kind"] = static_cast<uint32_t>(m_outputTypeDeductionInfo.m_typeDeductionKind);
    j["output_type_deduction_fixed_mask"] = m_outputTypeDeductionInfo.m_fixedMask;
    j["output_type_deduction_fn_name"] = m_outputTypeDeductionInfo.m_typeDeductionFnName;

    {
        json_t arr = json_t::array();
        for (auto& item : m_operandRegPrefInfo)
        {
            arr.push_back(item.SaveToJSON());
        }
        j["operand_reg_pref_info_list"] = std::move(arr);
    }

    j["output_reg_pref_info"] = m_outputRegPrefInfo.SaveToJSON();

    j["bc_intrinsic_ord"] = m_bcIntrinsicOrd;
    j["bc_intrinsic_info_getter"] = m_bcIntrinsicInfoGetterFunc;
    j["bc_intrinsic_name"] = m_bcIntrinsicName;

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

    j["bytecode_may_fallthrough_to_next_bytecode"] = BytecodeMayFallthroughToNextBytecode();
    j["bytecode_may_make_tail_call"] = BytecodeMayMakeTailCall();
    return j;
}

size_t WARN_UNUSED BytecodeVariantDefinition::GetBaselineJitSlowPathDataLength()
{
    ReleaseAssert(IsBaselineJitSlowPathDataLayoutDetermined());
    return m_baselineJitSlowPathData->GetTotalLength();
}

void BytecodeVariantDefinition::ComputeBaselineJitSlowPathDataLayout()
{
    ReleaseAssert(IsBytecodeStructLengthFinalized());
    ReleaseAssert(!IsBaselineJitSlowPathDataLayoutDetermined());

    m_baselineJitSlowPathData = new BaselineJitSlowPathDataLayout();
    m_baselineJitSlowPathData->ComputeLayout(this);
}

BytecodeOpcodeRawValueMap WARN_UNUSED BytecodeOpcodeRawValueMap::ParseFromJSON(json_t j)
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
