#include "deegen_stencil_runtime_constant_insertion_pass.h"
#include "llvm/IR/GetElementPtrTypeIterator.h"

namespace dast {

std::string WARN_UNUSED CPRuntimeConstantNodeBase::PrintExpr(CpPlaceholderExprPrinter* printer)
{
    if (printer->m_alreadyPrintedMap.count(this))
    {
        return printer->m_alreadyPrintedMap[this];
    }

    std::string varName = PrintExprImpl(printer);
    printer->m_alreadyPrintedMap[this] = varName;
    return varName;
}

std::string WARN_UNUSED CpPlaceholderExprPrinter::Print(const std::vector<CPRuntimeConstantNodeBase*>& exprs)
{
    CpPlaceholderExprPrinter printer;
    for (size_t i = 0; i < exprs.size(); i++)
    {
        ReleaseAssert(exprs[i]->m_bitWidth == 64);
        std::string tmpVarName = exprs[i]->PrintExpr(&printer);
        fprintf(printer.GetFd(), "uint64_t %s = %s;\n", printer.GetResultVarName(i).c_str(), tmpVarName.c_str());
    }
    fclose(printer.GetFd());
    return printer.m_buf.GetFileContents();
}

std::string WARN_UNUSED CPRawRuntimeConstant::PrintExprImpl(CpPlaceholderExprPrinter* printer)
{
    std::string varName = printer->CreateTmpVar();
    fprintf(printer->GetFd(), "static_assert(std::is_same_v<decltype(%s), int64_t>);\n",
            printer->GetInputVarName(m_label).c_str());
    fprintf(printer->GetFd(), "%s %s = static_cast<%s>(static_cast<%s>(%s));\n",
            GetUnsignedCppType().c_str(), varName.c_str(), GetUnsignedCppType().c_str(), GetSignedCppType().c_str(), printer->GetInputVarName(m_label).c_str());
    return varName;
}

std::string WARN_UNUSED CPExprFixedConstant::PrintExprImpl(CpPlaceholderExprPrinter* printer)
{
    std::string varName = printer->CreateTmpVar();
    fprintf(printer->GetFd(), "%s %s = static_cast<%s>(static_cast<%s>(static_cast<int64_t>(%lld)));\n",
            GetUnsignedCppType().c_str(), varName.c_str(), GetUnsignedCppType().c_str(), GetSignedCppType().c_str(), static_cast<long long>(m_value));
    return varName;
}

CPExprBinaryOp::CPExprBinaryOp(CPExprBinaryOp::Kind kind, CPRuntimeConstantNodeBase* lhs, CPRuntimeConstantNodeBase* rhs)
    : CPRuntimeConstantNodeBase(lhs->m_bitWidth)
    , m_kind(kind)
    , m_lhs(lhs)
    , m_rhs(rhs)
{
    ReleaseAssert(m_lhs != nullptr && m_rhs != nullptr);
    ReleaseAssert(m_lhs->m_bitWidth == m_rhs->m_bitWidth);

    switch (m_kind)
    {
    case Kind::Add:
    {
        m_range = m_lhs->m_range.add(m_rhs->m_range);
        break;
    }
    case Kind::Sub:
    {
        m_range = m_lhs->m_range.sub(m_rhs->m_range);
        break;
    }
    case Kind::Mul:
    {
        m_range = m_lhs->m_range.multiply(m_rhs->m_range);
        break;
    }
    }   /* switch */
}

std::string WARN_UNUSED CPExprBinaryOp::PrintExprImpl(CpPlaceholderExprPrinter* printer)
{
    std::string lhsVarName = m_lhs->PrintExpr(printer);
    std::string rhsVarName = m_rhs->PrintExpr(printer);
    std::string varName = printer->CreateTmpVar();
    std::string opName;
    switch (m_kind)
    {
    case Kind::Add: { opName = "+"; break; }
    case Kind::Sub: { opName = "-"; break; }
    case Kind::Mul: { opName = "*"; break; }
    }   /* switch*/

    fprintf(printer->GetFd(), "%s %s = %s %s %s;\n",
            GetUnsignedCppType().c_str(), varName.c_str(), lhsVarName.c_str(), opName.c_str(), rhsVarName.c_str());
    return varName;
}

CPExprUnaryOp::CPExprUnaryOp(CPExprUnaryOp::Kind kind, CPRuntimeConstantNodeBase* operand, uint32_t newWidth)
    : CPRuntimeConstantNodeBase(newWidth)
    , m_kind(kind)
    , m_operand(operand)
{
    ReleaseAssert(m_operand != nullptr);

    switch (m_kind)
    {
    case Kind::SExt:
    {
        ReleaseAssert(m_operand->m_bitWidth < newWidth);
        m_range = m_operand->m_range.signExtend(newWidth);
        break;
    }
    case Kind::ZExt:
    {
        ReleaseAssert(m_operand->m_bitWidth < newWidth);
        m_range = m_operand->m_range.zeroExtend(newWidth);
        break;
    }
    case Kind::Trunc:
    {
        ReleaseAssert(m_operand->m_bitWidth > newWidth);
        m_range = m_operand->m_range.truncate(newWidth);
        break;
    }
    }   /* switch */
}

std::string WARN_UNUSED CPExprUnaryOp::PrintExprImpl(CpPlaceholderExprPrinter* printer)
{
    std::string operandVarName = m_operand->PrintExpr(printer);
    std::string varName = printer->CreateTmpVar();
    switch (m_kind)
    {
    case Kind::ZExt:
    {
        fprintf(printer->GetFd(), "%s %s = static_cast<%s>(%s);\n",
                GetUnsignedCppType().c_str(), varName.c_str(), GetUnsignedCppType().c_str(), operandVarName.c_str());
        break;
    }
    case Kind::SExt:
    {
        fprintf(printer->GetFd(), "%s %s = static_cast<%s>(static_cast<%s>(static_cast<%s>(%s)));\n",
                GetUnsignedCppType().c_str(), varName.c_str(), GetUnsignedCppType().c_str(), GetSignedCppType().c_str(),
                m_operand->GetSignedCppType().c_str(), operandVarName.c_str());
        break;
    }
    case Kind::Trunc:
    {
        fprintf(printer->GetFd(), "%s %s = static_cast<%s>(%s);\n",
                GetUnsignedCppType().c_str(), varName.c_str(), GetUnsignedCppType().c_str(), operandVarName.c_str());
        break;
    }
    }   /* switch */

    return varName;
}

class StencilRuntimeConstantInsertionPass
{
public:
    StencilRuntimeConstantInsertionPass(llvm::Function* f)
        : m_dataLayout(f->getParent())
    { }

    // The mapping from LLVM value to CPRuntimeConstant value
    // If the CPRuntimeConstant is nullptr, it means that it is not a runtime constant
    //
    std::unordered_map<llvm::Value*, CPRuntimeConstantNodeBase*> m_mapping;

    llvm::DataLayout m_dataLayout;

    std::unordered_map<uint64_t /*label*/, std::pair<int64_t, int64_t> /*range*/> m_rawRcRange;

    CPRuntimeConstantNodeBase* WARN_UNUSED TryMarkAsRuntimeConstant(llvm::Function* f, llvm::Value* val)
    {
        using namespace llvm;

        if (m_mapping.count(val))
        {
            return m_mapping[val];
        }

        CPRuntimeConstantNodeBase* res = nullptr;

        if (isa<Constant>(val))
        {
            if (!isa<ConstantInt>(val))
            {
                goto end;
            }

            ConstantInt* ci = cast<ConstantInt>(val);
            uint32_t bitWidth = ci->getBitWidth();
            if (bitWidth == 1)
            {
                goto end;
            }

            int64_t intv =  ci->getSExtValue();
            res = new CPExprFixedConstant(bitWidth, intv);
            goto end;
        }

        if (isa<Instruction>(val))
        {
            // Handle raw runtine constant
            //
            if (isa<CallInst>(val))
            {
                CallInst* ci = cast<CallInst>(val);
                Function* callee = ci->getCalledFunction();
                if (callee != nullptr && callee->getName().startswith("__deegen_constant_placeholder_bytecode_operand_"))
                {
                    ReleaseAssert(ci->arg_size() == 0);
                    std::string labelStr = callee->getName().substr(strlen("__deegen_constant_placeholder_bytecode_operand_")).str();
                    uint64_t label = static_cast<uint64_t>(std::stoi(labelStr));
                    ReleaseAssert(m_rawRcRange.count(label));
                    int64_t min = m_rawRcRange[label].first;
                    int64_t max = m_rawRcRange[label].second;
                    uint32_t bitWidth;
                    if (ci->getType()->isPointerTy())
                    {
                        bitWidth = 64;
                    }
                    else
                    {
                        ReleaseAssert(ci->getType()->isIntegerTy());
                        bitWidth = ci->getType()->getIntegerBitWidth();
                    }
                    res = new CPRawRuntimeConstant(label, bitWidth, min, max);
                }
                goto end;
            }

            // Handle binary op
            //
            if (isa<BinaryOperator>(val))
            {
                BinaryOperator* bo = cast<BinaryOperator>(val);
                Instruction::BinaryOps opcode = bo->getOpcode();
                Value* lhs = bo->getOperand(0);
                Value* rhs = bo->getOperand(1);

                CPRuntimeConstantNodeBase* rc_lhs = TryMarkAsRuntimeConstant(f, lhs);
                if (rc_lhs == nullptr)
                {
                    goto end;
                }

                CPRuntimeConstantNodeBase* rc_rhs = TryMarkAsRuntimeConstant(f, rhs);
                if (rc_rhs == nullptr)
                {
                    goto end;
                }

                switch (opcode)
                {
                case Instruction::BinaryOps::Add:
                {
                    res = new CPExprBinaryOp(CPExprBinaryOp::Add, rc_lhs, rc_rhs);
                    goto end;
                }
                case Instruction::BinaryOps::Sub:
                {
                    res = new CPExprBinaryOp(CPExprBinaryOp::Sub, rc_lhs, rc_rhs);
                    goto end;
                }
                case Instruction::BinaryOps::Mul:
                {
                    res = new CPExprBinaryOp(CPExprBinaryOp::Mul, rc_lhs, rc_rhs);
                    goto end;
                }
                case Instruction::BinaryOps::Shl:
                {
                    // For now, just handle the case where rhs is a fixed value for simplicity..
                    //
                    if (rc_rhs->m_range.isSingleElement())
                    {
                        const APInt* singleton = rc_rhs->m_range.getSingleElement();
                        ReleaseAssert(singleton != nullptr);
                        ReleaseAssert(!singleton->isNegative());
                        uint64_t shift = singleton->extractBitsAsZExtValue(64 /*numBits*/, 0 /*offset*/);
                        ReleaseAssert(shift < rc_lhs->m_bitWidth);
                        int64_t multiplier = static_cast<int64_t>(1) << static_cast<int64_t>(shift);
                        res = new CPExprBinaryOp(CPExprBinaryOp::Mul, rc_lhs, new CPExprFixedConstant(rc_lhs->m_bitWidth, multiplier));
                    }
                    goto end;
                }
                default:
                {
                    goto end;
                }
                }   /* switch */

                ReleaseAssert(false && "should never reach here");
            }

            // Handle trunc/zext/sext instruction
            //
            if (isa<TruncInst>(val) || isa<ZExtInst>(val) || isa<SExtInst>(val))
            {
                CastInst* ci = cast<CastInst>(val);
                Value* operand = ci->getOperand(0);
                CPRuntimeConstantNodeBase* rc = TryMarkAsRuntimeConstant(f, operand);
                if (rc == nullptr)
                {
                    goto end;
                }

                CPExprUnaryOp::Kind opcode;
                if (isa<TruncInst>(val))
                {
                    opcode = CPExprUnaryOp::Trunc;
                }
                else if (isa<ZExtInst>(val))
                {
                    opcode = CPExprUnaryOp::ZExt;
                }
                else
                {
                    ReleaseAssert(isa<SExtInst>(val));
                    opcode = CPExprUnaryOp::SExt;
                }

                res = new CPExprUnaryOp(opcode, rc, ci->getDestTy()->getIntegerBitWidth());
                goto end;
            }

            // Handle GEP instruction
            //
            if (isa<GetElementPtrInst>(val))
            {
                GetElementPtrInst* gep = cast<GetElementPtrInst>(val);
                MapVector<Value*, APInt> variableOffsets;

                // LLVM footgun warning: the 'constantOffset' passed to GEP::collectOffset must be initialized! Or we will silently get a 0 back!
                //
                APInt constantOffset = APInt(64, 0);
                if (!gep->collectOffset(m_dataLayout, 64 /*bitWidth*/, variableOffsets /*out*/, constantOffset /*out*/))
                {
                    // collectOffset failed for whatever reason, just skip
                    //
                    goto end;
                }

                ReleaseAssert(constantOffset.getBitWidth() == 64);
                for (auto& it : variableOffsets)
                {
                    ReleaseAssert(it.second.getBitWidth() == 64);
                }

                bool success = true;
                for (auto& it : variableOffsets)
                {
                    Value* variable = it.first;
                    if (TryMarkAsRuntimeConstant(f, variable) == nullptr)
                    {
                        success = false;
                        break;
                    }
                }

                // For now, let's ignore the case where only some of the offset parts are runtime constants
                //
                if (!success)
                {
                    goto end;
                }

                auto getValueOfApInt = [](APInt input) WARN_UNUSED -> int64_t
                {
                    uint64_t rawValue = input.extractBitsAsZExtValue(64 /*numBits*/, 0 /*bitPosition*/);
                    return static_cast<int64_t>(rawValue);
                };

                CPRuntimeConstantNodeBase* rc_ptr = TryMarkAsRuntimeConstant(f, gep->getPointerOperand());
                if (rc_ptr == nullptr)
                {
                    goto end;
                }

                // The whole GEP itself can be turned into a constant
                //
                CPRuntimeConstantNodeBase* cur = new CPExprBinaryOp(CPExprBinaryOp::Add, rc_ptr, new CPExprFixedConstant(rc_ptr->m_bitWidth, getValueOfApInt(constantOffset)));
                for (auto& it : variableOffsets)
                {
                    Value* variable = it.first;
                    APInt multiplier = it.second;
                    CPRuntimeConstantNodeBase* rc = TryMarkAsRuntimeConstant(f, variable);
                    ReleaseAssert(rc != nullptr);
                    CPExprBinaryOp* term = new CPExprBinaryOp(CPExprBinaryOp::Mul, rc, new CPExprFixedConstant(rc_ptr->m_bitWidth, getValueOfApInt(multiplier)));
                    cur = new CPExprBinaryOp(CPExprBinaryOp::Add, cur, term);
                }
                res = cur;
                goto end;
            }
        }

        // Other types of instructions are conservatively treated as not handlable for now
        //

end:
        m_mapping[val] = res;
        return res;
    }

    static llvm::GlobalVariable* WARN_UNUSED InsertOrGetCpPlaceholderGlobal(llvm::Module* module, uint64_t ord)
    {
        using namespace llvm;
        LLVMContext& ctx = module->getContext();
        std::string symName = "__deegen_cp_placeholder_" + std::to_string(ord);
        if (module->getNamedValue(symName) == nullptr)
        {
            GlobalVariable* gv = new GlobalVariable(*module,
                                                    llvm_type_of<uint8_t>(ctx) /*valueType*/,
                                                    true /*isConstant*/,
                                                    GlobalValue::ExternalLinkage,
                                                    nullptr /*initializer*/,
                                                    symName /*name*/);
            ReleaseAssert(gv->getName().str() == symName);
            gv->setAlignment(MaybeAlign(1));
            gv->setDSOLocal(true);
        }

        GlobalVariable* gv = module->getGlobalVariable(symName);
        ReleaseAssert(gv != nullptr);
        return gv;
    }

    std::vector<CPRuntimeConstantNodeBase*> m_externSymDefs;

    void DoRewrite(llvm::Function* f)
    {
        using namespace llvm;
        m_mapping.clear();

        Module* module = f->getParent();
        LLVMContext& ctx = module->getContext();

        // m_mapping is doing memorization on the instruction pointer.
        // However, this pass is both inserting and deleting instructions, so we can run into
        // ABA problem if no special care is taken!
        //
        // Therefore, whenever an instruction is deleted, m_mapping must be updated correspondingly!
        //
        // Honestly, this is really bad... If I had realized this issue from the beginning, I likely
        // would have switched to a less error-prone design.. but now let's just live with it..
        //
        auto deleteInst = [&](Instruction* inst)
        {
            ReleaseAssert(inst->use_empty());
            if (m_mapping.count(inst))
            {
                m_mapping.erase(m_mapping.find(inst));
            }
            inst->eraseFromParent();
        };

        struct CpPlaceholderInfo
        {
            uint64_t ord;
            CPRuntimeConstantNodeBase* symbol;
            Constant* subend;
        };

        std::unordered_map<CPRuntimeConstantNodeBase* /*origin*/, CpPlaceholderInfo> rcList;
        uint64_t rcCnt = 0;

        // Honestly, these are really bad.. I have no idea how to properly work with APInt and ConstantRange, but these should work...
        //
        // Return the # of values in the range
        // Note that if the range is full 64-bits, return maxUInt64 to avoid overflow
        //
        auto getNumElementsInRange = [&](ConstantRange cr) -> uint64_t
        {
            ReleaseAssert(!cr.isEmptySet());
            ReleaseAssert(8 <= cr.getBitWidth() && cr.getBitWidth() <= 64);
            if (cr.isFullSet())
            {
                if (cr.getBitWidth() == 64)
                {
                    return std::numeric_limits<uint64_t>::max();
                }
                else
                {
                    return static_cast<uint64_t>(1) << (cr.getBitWidth());
                }
            }
            else
            {
                uint64_t lb = cr.getLower().extractBitsAsZExtValue(64, 0);
                uint64_t ub = cr.getUpper().extractBitsAsZExtValue(64, 0);
                if (lb < ub)
                {
                    return ub - lb;
                }
                else
                {
                    ReleaseAssert(lb > ub);
                    if (cr.getBitWidth() == 64)
                    {
                        return ub - lb;
                    }
                    else
                    {
                        uint64_t msk = static_cast<uint64_t>(1) << cr.getBitWidth();
                        ReleaseAssert(lb < msk);
                        return msk - lb + ub;
                    }
                }
            }
        };

        // Properly inserts instructions before 'insertBefore', and returns a value that may be used to replace the value corresponding to 'rc'
        // Note that it always returns an integer value. Caller may need to cast it to the proper pointer type as necessary.
        //
        // Return nullptr if failure (because the possible range of the runtime constant is too large)
        //
        auto insertRc = [&](CPRuntimeConstantNodeBase* rc, Instruction* insertBefore) WARN_UNUSED -> Instruction*
        {
            // Under System V ABI, an external symbol has range [1, 2^31 - 16MB)
            // To safely represent a runtime constant using an external symbol, we must retrofit it into the range of the external symbol
            // For example, if the runtime constant is a i32 with range [-10, 100],
            // we must represent it by trunc<i32>(sym) - 11 with 'sym' defined as zext<i64>(original value + 11)
            //
            uint64_t rlim = (1ULL << 31) - (16ULL << 20) - 1024;
            if (getNumElementsInRange(rc->m_range) > rlim)
            {
                return nullptr;
            }

            auto isNoAdjustmentNeeded = [&](ConstantRange cr) WARN_UNUSED -> bool
            {
                if (cr.getBitWidth() <= 16)
                {
                    // [1, 2^31 - 16MB) clearly covers all bit patterns of [0, 65535], no adjustment needed
                    //
                    return true;
                }
                else if (cr.getBitWidth() == 32)
                {
                    ConstantRange rng(APInt(32, 1), APInt(32, (1ULL << 31) - (16ULL << 20)));
                    return rng.contains(cr);
                }
                else
                {
                    ReleaseAssert(cr.getBitWidth() == 64);
                    ConstantRange rng(APInt(64, 1), APInt(64, (1ULL << 31) - (16ULL << 20)));
                    return rng.contains(cr);
                }
            };

            if (!rcList.count(rc))
            {
                CPRuntimeConstantNodeBase* sym = rc;
                Constant* subend = nullptr;

                if (isNoAdjustmentNeeded(rc->m_range))
                {
                    subend = ConstantInt::get(ctx, APInt(rc->m_bitWidth, 0));
                }
                else
                {
                    // Need to adjust m_min to 1
                    //
                    APInt lb = rc->m_range.getLower();
                    APInt valToSubtract = APInt(rc->m_bitWidth, 1) - lb;
                    sym = new CPExprBinaryOp(CPExprBinaryOp::Add, sym, new CPExprFixedConstant(rc->m_bitWidth, valToSubtract.getSExtValue()));
                    subend = ConstantInt::get(ctx, valToSubtract);
                    ReleaseAssert(isNoAdjustmentNeeded(sym->m_range));
                }

                if (sym->m_bitWidth < 64)
                {
                    sym = new CPExprUnaryOp(CPExprUnaryOp::ZExt, sym, 64);
                }

                // After the adjustment, 'sym' should always meet the assumption of System V ABI external symbol
                //
                ReleaseAssert(sym->m_bitWidth == 64);
                ReleaseAssert(subend != nullptr);
                rcList[rc] = { .ord = rcCnt, .symbol = sym, .subend = subend };
                rcCnt++;
            }

            ReleaseAssert(rcList.count(rc));
            uint64_t ord = rcList[rc].ord;
            GlobalVariable* gv = InsertOrGetCpPlaceholderGlobal(module, ord);

            Constant* subend = rcList[rc].subend;

            // 'gv' is the adjusted symbol. Now in LLVM IR, we need to undo the adjustment to get the original value back
            //
            Instruction* res = new PtrToIntInst(gv, llvm_type_of<uint64_t>(ctx), "", insertBefore);

            // Undo the ZExt by a trunc if necessary
            //
            if (rc->m_bitWidth < 64)
            {
                res = new TruncInst(res, Type::getIntNTy(ctx, rc->m_bitWidth), "", insertBefore);
            }

            ReleaseAssert(subend->getType() == res->getType());
            res = CreateSub(res, rcList[rc].subend);
            res->insertBefore(insertBefore);

            return res;
        };

        // Do rewrite that helps LLVM generate better code:
        // 1. Rewrite GEP with non-constant pointer base but runtime constant offset to ugly gep
        //    e.g., a[x] where a is int64_t* and x is runtime constant is converted to an explicit uglygep
        //    expression reinterpret_cast<uint8_t*>(a) + x * 8.
        // 2. Rewrite comparison where one side is a non-64-bit runtime constant to 64-bit comparison
        //    e.g., %res = cmp slt i32 %cst, %rhs => %cst64 = sext i64 %cst, ...
        //
        {
            // Need to collect all instructions into a vector first to avoid iterator invalidation issues
            //
            std::vector<Instruction*> allInstrs;
            for (BasicBlock& bb : *f)
            {
                for (Instruction& inst : bb)
                {
                    allInstrs.push_back(&inst);
                }
            }

            for (Instruction* inst : allInstrs)
            {
                if (TryMarkAsRuntimeConstant(f, inst) != nullptr)
                {
                    // This instruction is a compile-time computable runtime constant, don't bother
                    //
                    continue;
                }

                if (isa<GetElementPtrInst>(inst))
                {
                    GetElementPtrInst* gep = cast<GetElementPtrInst>(inst);
                    MapVector<Value*, APInt> variableOffsets;

                    // LLVM footgun warning: the 'constantOffset' passed to GEP::collectOffset must be initialized! Or we will silently get a 0 back!
                    //
                    APInt constantOffset = APInt(64, 0);
                    if (!gep->collectOffset(m_dataLayout, 64 /*bitWidth*/, variableOffsets /*out*/, constantOffset /*out*/))
                    {
                        // collectOffset failed for whatever reason, just skip
                        //
                        continue;
                    }
                    ReleaseAssert(constantOffset.getBitWidth() == 64);

                    if (variableOffsets.size() == 0)
                    {
                        // This GEP doesn't contain any variable offset, skip
                        //
                        continue;
                    }

                    for (auto& it : variableOffsets)
                    {
                        ReleaseAssert(it.second.getBitWidth() == 64);
                    }

                    bool success = true;
                    for (auto& it : variableOffsets)
                    {
                        Value* variable = it.first;
                        if (TryMarkAsRuntimeConstant(f, variable) == nullptr)
                        {
                            success = false;
                            break;
                        }
                    }

                    // For now, let's ignore the case where only some of the offset parts are runtime constants
                    //
                    if (!success)
                    {
                        continue;
                    }

                    auto getValueOfApInt = [](APInt input) WARN_UNUSED -> int64_t
                    {
                        uint64_t rawValue = input.extractBitsAsZExtValue(64 /*numBits*/, 0 /*bitPosition*/);
                        return static_cast<int64_t>(rawValue);
                    };

                    // 'rc_ptr' must be nullptr, otherwise the GEP would have been a constant itself and would not reach here
                    //
                    Value* ptrOperand = gep->getPointerOperand();
                    CPRuntimeConstantNodeBase* rc_ptr = TryMarkAsRuntimeConstant(f, ptrOperand);
                    ReleaseAssert(rc_ptr == nullptr);

                    CPRuntimeConstantNodeBase* cur = new CPExprFixedConstant(64, getValueOfApInt(constantOffset));
                    for (auto& it : variableOffsets)
                    {
                        Value* variable = it.first;
                        APInt multiplier = it.second;
                        CPRuntimeConstantNodeBase* rc_offset = TryMarkAsRuntimeConstant(f, variable);
                        ReleaseAssert(rc_offset != nullptr);
                        CPExprBinaryOp* term = new CPExprBinaryOp(CPExprBinaryOp::Mul, rc_offset, new CPExprFixedConstant(64, getValueOfApInt(multiplier)));
                        cur = new CPExprBinaryOp(CPExprBinaryOp::Add, cur, term);
                    }

                    // Replace the GEP with an ugly GEP
                    //
                    Instruction* offset = insertRc(cur, gep /*insertBefore*/);
                    if (offset == nullptr)
                    {
                        continue;
                    }

                    ReleaseAssert(llvm_value_has_type<uint64_t>(offset));
                    GetElementPtrInst* replacement = GetElementPtrInst::CreateInBounds(llvm_type_of<uint8_t>(ctx), ptrOperand, { offset }, "", gep /*insertBefore*/);
                    gep->replaceAllUsesWith(replacement);
                    deleteInst(gep);
                    continue;
                }

                if (isa<ICmpInst>(inst))
                {
                    ICmpInst* cmp = cast<ICmpInst>(inst);
                    Type* cmpTy = cmp->getOperand(0)->getType();
                    if (cmpTy->isPointerTy()) { continue; }
                    ReleaseAssert(cmpTy->isIntegerTy());
                    if (cmpTy->getIntegerBitWidth() < 64)
                    {
                        auto doExtension = [&](Value* op, bool shouldSExt, Instruction* insertBefore) WARN_UNUSED -> Value*
                        {
                            CPRuntimeConstantNodeBase* rc_op = TryMarkAsRuntimeConstant(f, op);
                            if (isa<ConstantInt>(op))
                            {
                                ConstantInt* ci = cast<ConstantInt>(op);
                                if (shouldSExt)
                                {
                                    return CreateLLVMConstantInt<int64_t>(ctx, ci->getSExtValue());
                                }
                                else
                                {
                                    return CreateLLVMConstantInt<uint64_t>(ctx, ci->getZExtValue());
                                }
                            }
                            else if (rc_op == nullptr)
                            {
                                if (shouldSExt)
                                {
                                    return new SExtInst(op, llvm_type_of<uint64_t>(ctx), "", insertBefore);
                                }
                                else
                                {
                                    return new ZExtInst(op, llvm_type_of<uint64_t>(ctx), "", insertBefore);
                                }
                            }
                            else
                            {
                                return insertRc(new CPExprUnaryOp(shouldSExt ? CPExprUnaryOp::SExt : CPExprUnaryOp::ZExt, rc_op, 64 /*newWidth*/), insertBefore);
                            }
                        };

                        Value* lhs = cmp->getOperand(0);
                        Value* rhs = cmp->getOperand(1);
                        bool hasMeritToRewrite = false;
                        if (!isa<ConstantInt>(lhs) && TryMarkAsRuntimeConstant(f, lhs))
                        {
                            hasMeritToRewrite = true;
                        }
                        if (!isa<ConstantInt>(rhs) && TryMarkAsRuntimeConstant(f, rhs))
                        {
                            hasMeritToRewrite = true;
                        }
                        if (hasMeritToRewrite)
                        {
                            bool shouldSExt = cmp->isSigned();
                            Value* extended_lhs = doExtension(lhs, shouldSExt, inst /*insertBefore*/);
                            Value* extended_rhs = doExtension(rhs, shouldSExt, inst /*insertBefore*/);
                            if (extended_lhs != nullptr && extended_rhs != nullptr)
                            {
                                ReleaseAssert(extended_lhs->getType() == extended_rhs->getType());
                                cmp->setOperand(0, extended_lhs);
                                cmp->setOperand(1, extended_rhs);
                            }
                            else
                            {
                                // The failed rewrite might have inserted side-effect-free instructions with no users.
                                // They will be optimized away by LLVM later, so nothing to worry about.
                                //
                            }
                        }
                    }
                }
            }

            ValidateLLVMFunction(f);
        }

        // Now, change each instruction that is a runtime constant to an external symbol
        //
        std::unordered_set<Instruction*> rewriteFailedSet;

        while (true)
        {
            bool didSomething = false;

            std::vector<Instruction*> allInstrs;
            for (BasicBlock& bb : *f)
            {
                for (Instruction& inst : bb)
                {
                    allInstrs.push_back(&inst);
                }
            }

            std::vector<Instruction*> deleteList;
            std::vector<std::pair<Instruction*, Instruction*>> replacementList;
            for (Instruction* inst : allInstrs)
            {
                if (rewriteFailedSet.count(inst))
                {
                    continue;
                }

                CPRuntimeConstantNodeBase* rc = TryMarkAsRuntimeConstant(f, inst);
                if (rc == nullptr)
                {
                    continue;
                }

                bool hasNonConstantUser = false;
                for (Use& u : inst->uses())
                {
                    User* user = u.getUser();
                    ReleaseAssert(isa<Instruction>(user));
                    Instruction* userInst = cast<Instruction>(user);
                    if (TryMarkAsRuntimeConstant(f, userInst) == nullptr)
                    {
                        hasNonConstantUser = true;
                        break;
                    }
                    if (rewriteFailedSet.count(userInst))
                    {
                        hasNonConstantUser = true;
                        break;
                    }
                }

                if (hasNonConstantUser)
                {
                    // This instruction should be replaced by an external symbol
                    // But replace everything in the end, since replacing it now might break the analysis of it users
                    //
                    Instruction* replacement = insertRc(rc, inst /*insertBefore*/);
                    if (replacement == nullptr)
                    {
                        // The rewrite failed, this instruction should be treated as if it's not a constant
                        //
                        didSomething = true;
                        rewriteFailedSet.insert(inst);
                    }
                    else
                    {
                        if (inst->getType()->isPointerTy())
                        {
                            ReleaseAssert(llvm_value_has_type<uint64_t>(replacement));
                            replacement = new IntToPtrInst(replacement, inst->getType(), "", inst /*insertBefore*/);
                        }
                        else
                        {
                            ReleaseAssert(replacement->getType() == inst->getType());
                        }
                        replacementList.push_back(std::make_pair(inst, replacement));
                    }
                }
                else
                {
                    // This instruction is only used by other instructions that are runtime constants
                    // We can attempt to delete it in the end (the delete might not succeed though, because some rewrite might fail)
                    //
                    deleteList.push_back(inst);
                }
            }

            ValidateLLVMFunction(f);

            // Do all replacements
            //
            for (auto& it : replacementList)
            {
                Instruction* origin = it.first;
                Instruction* replacement = it.second;
                ReleaseAssert(origin->getParent() != nullptr && replacement->getParent() != nullptr);
                ReleaseAssert(origin->getType() == replacement->getType());
                origin->replaceAllUsesWith(replacement);
                deleteInst(origin);
                didSomething = true;
            }

            ValidateLLVMFunction(f);

            // Remove all the dead instructions: these are runtime constants that are only used to compute other runtime constants
            // We must remove in topological order to make LLVM happy. For simplicity, just use O(n^2) brute force..
            //
            while (!deleteList.empty())
            {
                std::vector<Instruction*> newList;
                for (Instruction* inst : deleteList)
                {
                    ReleaseAssert(inst->getParent() != nullptr);
                    if (inst->use_empty())
                    {
                        didSomething = true;
                        deleteInst(inst);
                    }
                    else
                    {
                        newList.push_back(inst);
                    }
                }
                ReleaseAssert(newList.size() <= deleteList.size());
                if (newList.size() == deleteList.size())
                {
                    break;
                }
                deleteList = newList;
            }

            ValidateLLVMFunction(f);

            if (!didSomething)
            {
                break;
            }
        }

        // Generate the definitions of all external symbols
        //
        m_externSymDefs.resize(rcList.size());
        for (auto& it : rcList)
        {
            uint64_t ord = it.second.ord;
            CPRuntimeConstantNodeBase* sym = it.second.symbol;
            ReleaseAssert(ord < m_externSymDefs.size());
            ReleaseAssert(m_externSymDefs[ord] == nullptr && sym != nullptr);
            m_externSymDefs[ord] = sym;
        }

        for (CPRuntimeConstantNodeBase* sym : m_externSymDefs)
        {
            ReleaseAssert(sym != nullptr);
        }

        // Assert that after the rewrite, there is no more calls to the constant placeholders
        //
        for (BasicBlock& bb : *f)
        {
            for (Instruction& inst : bb)
            {
                if (isa<CallInst>(&inst))
                {
                    CallInst* ci = cast<CallInst>(&inst);
                    Function* callee = ci->getCalledFunction();
                    if (callee != nullptr)
                    {
                        ReleaseAssert(!callee->getName().startswith("__deegen_constant_placeholder_bytecode_operand_"));
                    }
                }
            }
        }
    }
};

std::vector<CPRuntimeConstantNodeBase*> WARN_UNUSED StencilRuntimeConstantInserter::RunOnFunction(llvm::Function* func)
{
    StencilRuntimeConstantInsertionPass pass(func);
    pass.m_rawRcRange = m_rcRanges;
    pass.DoRewrite(func);
    return pass.m_externSymDefs;
}

}   // namespace dast
