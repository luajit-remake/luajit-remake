#pragma once

#include "common_utils.h"
#include "deegen_bytecode_operand.h"
#include "misc_llvm_helper.h"

namespace dast {

struct SimpleOperandExprCppPrinter;

// Expresses a simple expression consisting of only bytecode operands, literal values, and basic arithmetics
//
struct SimpleOperandExprNode
{
    enum Operator
    {
        ZExt,
        SExt,
        Trunc,
        Add,
        Sub,
        Mul,
        Operand,
        Literal
    };

    static size_t GetNumChildren(Operator op)
    {
        switch (op)
        {
        case Operator::ZExt:
        case Operator::SExt:
        case Operator::Trunc:
        {
            return 1;
        }
        case Operator::Add:
        case Operator::Sub:
        case Operator::Mul:
        {
            return 2;
        }
        case Operator::Operand:
        case Operator::Literal:
        {
            return 0;
        }
        }   /*switch*/
    }

    size_t GetNumChildren() { return GetNumChildren(m_op); }

    static SimpleOperandExprNode* WARN_UNUSED CreateLiteral(uint32_t bitWidth, uint64_t value)
    {
        SimpleOperandExprNode* node = new SimpleOperandExprNode();
        node->m_op = Operator::Literal;
        node->m_bitWidth = bitWidth;
        node->m_literalValue = value;
        return node;
    }

    static SimpleOperandExprNode* WARN_UNUSED CreateOperand(BcOperand* op)
    {
        SimpleOperandExprNode* node = new SimpleOperandExprNode();
        node->m_op = Operator::Operand;
        ReleaseAssert(op->GetKind() == BcOperandKind::BytecodeRangeBase ||
                      op->GetKind() == BcOperandKind::Literal ||
                      op->GetKind() == BcOperandKind::SpecializedLiteral);
        if (op->GetKind() == BcOperandKind::BytecodeRangeBase)
        {
            node->m_bitWidth = 64;
        }
        else
        {
            BcOpLiteral* lit = assert_cast<BcOpLiteral*>(op);
            ReleaseAssert(lit != nullptr);
            node->m_bitWidth = static_cast<uint32_t>(lit->m_numBytes) * 8;
        }
        node->m_operand = op;
        return node;
    }

    static SimpleOperandExprNode* WARN_UNUSED CreateCastExpr(Operator op, SimpleOperandExprNode* ch, uint32_t bitWidth)
    {
        ReleaseAssert(GetNumChildren(op) == 1);
        SimpleOperandExprNode* node = new SimpleOperandExprNode();
        node->m_op = op;
        node->m_bitWidth = bitWidth;
        node->m_child[0] = ch;
        ReleaseAssertImp(op == Operator::Trunc, bitWidth < ch->m_bitWidth);
        ReleaseAssertImp(op == Operator::ZExt || op == Operator::SExt, bitWidth > ch->m_bitWidth);
        return node;
    }

    static SimpleOperandExprNode* WARN_UNUSED CreateBinaryExpr(Operator op, SimpleOperandExprNode* lhs, SimpleOperandExprNode* rhs)
    {
        ReleaseAssert(GetNumChildren(op) == 2);
        SimpleOperandExprNode* node = new SimpleOperandExprNode();
        node->m_op = op;
        ReleaseAssert(lhs->m_bitWidth == rhs->m_bitWidth);
        node->m_bitWidth = lhs->m_bitWidth;
        node->m_child[0] = lhs;
        node->m_child[1] = rhs;
        return node;
    }

    void PrintImpl(FILE* fp, SimpleOperandExprCppPrinter* printer, size_t varOrd);

    SimpleOperandExprNode* m_child[2];
    Operator m_op;
    uint32_t m_bitWidth;
    BcOperand* m_operand;
    uint64_t m_literalValue;
};

struct SimpleOperandExprCppPrinter
{
    SimpleOperandExprCppPrinter()
        : m_curVarOrd(0)
    { }

    size_t Print(FILE* fp, SimpleOperandExprNode* expr)
    {
        if (m_cache.count(expr))
        {
            return m_cache[expr];
        }
        size_t tmp = m_curVarOrd;
        m_curVarOrd++;
        m_cache[expr] = tmp;
        expr->PrintImpl(fp, this, tmp);
        return tmp;
    }

    std::unordered_map<SimpleOperandExprNode*, size_t /*varOrd*/> m_cache;
    size_t m_curVarOrd;
};

// Map each LLVM Value to (potentially a list of) SimpleOperandExpr if possible
//
// If there is a record for a Value, the runtime value must be within the list of results.
//
// TODO: currently this function does not consider calls
//
struct LLVMValueToOperandExprMapper
{
    // Map arguments of the target function to BcOperand
    //
    std::function<BcOperand*(llvm::Argument*)> m_argMapper;

    // Map from Value to a vector of SimpleOperandExpr
    // We need a vector due to PHI nodes
    //
    std::unordered_map<llvm::Value*, std::vector<SimpleOperandExprNode*>> m_map;

    // We give up if there are too many items in the map
    //
    static constexpr size_t x_itemThreshold = 10000;
    size_t m_itemEstimate;
    bool m_analysisFailed;

    // Return nullptr on failure!
    //
    SimpleOperandExprNode* WARN_UNUSED TryGetUniqueExpr(llvm::Value* value)
    {
        if (m_analysisFailed)
        {
            return nullptr;
        }
        if (!m_map.count(value))
        {
            return nullptr;
        }
        const std::vector<SimpleOperandExprNode*>& list = m_map[value];
        ReleaseAssert(list.size() > 0);
        if (list.size() != 1)
        {
            return nullptr;
        }
        return list[0];
    }

    void Run(llvm::Function* func, std::function<BcOperand*(llvm::Argument*)> mapper)
    {
        using namespace llvm;

        m_argMapper = mapper;
        m_map.clear();

        m_analysisFailed = false;
        m_itemEstimate = 0;

        for (uint32_t i = 0; i < func->arg_size(); i++)
        {
            Argument* arg = func->getArg(i);
            BcOperand* bcOp = m_argMapper(arg);
            if (bcOp != nullptr)
            {
                ReleaseAssert(!m_map.count(arg));
                m_map[arg].push_back(SimpleOperandExprNode::CreateOperand(bcOp));
            }
        }

        DataLayout dataLayout(func->getParent());

        auto queryMap = [&](Value* val) WARN_UNUSED -> const std::vector<SimpleOperandExprNode*>&
        {
            if (m_map.count(val))
            {
                return m_map[val];
            }
            if (isa<ConstantPointerNull>(val))
            {
                m_map[val].push_back(SimpleOperandExprNode::CreateLiteral(64 /*bitWidth*/, 0 /*value*/));
                return m_map[val];
            }

            ReleaseAssert(isa<ConstantInt>(val));
            ConstantInt* ci = cast<ConstantInt>(val);
            uint32_t bitWidth = ci->getBitWidth();
            ReleaseAssert(bitWidth == 8 || bitWidth == 16 || bitWidth == 32 || bitWidth == 64);
            uint64_t ciVal = ci->getValue().extractBitsAsZExtValue(64 /*numBits*/, 0 /*bitPosition*/);
            m_map[val].push_back(SimpleOperandExprNode::CreateLiteral(bitWidth, ciVal));
            return m_map[val];
        };

        auto handleBinaryInst = [&](Instruction* inst, SimpleOperandExprNode::Operator op, Value* lhs, Value* rhs)
        {
            ReleaseAssert(!m_map.count(inst));
            std::vector<SimpleOperandExprNode*>& res = m_map[inst];
            const std::vector<SimpleOperandExprNode*>& lhsList = queryMap(lhs);
            const std::vector<SimpleOperandExprNode*>& rhsList = queryMap(rhs);
            m_itemEstimate += lhsList.size() * rhsList.size();
            for (SimpleOperandExprNode* lhsExpr : lhsList)
            {
                for (SimpleOperandExprNode* rhsExpr : rhsList)
                {
                    res.push_back(SimpleOperandExprNode::CreateBinaryExpr(op, lhsExpr, rhsExpr));
                }
            }
        };

        auto handleCastInst = [&](Instruction* inst, Value* operand)
        {
            ReleaseAssert(inst->getType()->isIntegerTy());
            ReleaseAssert(!m_map.count(inst));
            std::vector<SimpleOperandExprNode*>& res = m_map[inst];
            const std::vector<SimpleOperandExprNode*>& operandList = queryMap(operand);
            m_itemEstimate += operandList.size();
            for (SimpleOperandExprNode* operandExpr : operandList)
            {
                if (isa<TruncInst>(inst))
                {
                    res.push_back(SimpleOperandExprNode::CreateCastExpr(SimpleOperandExprNode::Trunc, operandExpr, inst->getType()->getIntegerBitWidth()));
                }
                else if (isa<ZExtInst>(inst))
                {
                    res.push_back(SimpleOperandExprNode::CreateCastExpr(SimpleOperandExprNode::ZExt, operandExpr, inst->getType()->getIntegerBitWidth()));
                }
                else
                {
                    ReleaseAssert(isa<SExtInst>(inst));
                    res.push_back(SimpleOperandExprNode::CreateCastExpr(SimpleOperandExprNode::SExt, operandExpr, inst->getType()->getIntegerBitWidth()));
                }
            }
        };

        while (true)
        {
            bool didSomething = false;
            for (BasicBlock& bb : *func)
            {
                for (Instruction& inst : bb)
                {
                    if (m_map.count(&inst))
                    {
                        continue;
                    }
                    if (m_itemEstimate > x_itemThreshold)
                    {
                        m_analysisFailed = true;
                        return;
                    }
                    if (isa<BinaryOperator>(&inst))
                    {
                        BinaryOperator* bo = cast<BinaryOperator>(&inst);
                        Instruction::BinaryOps opcode = bo->getOpcode();
                        Value* lhs = bo->getOperand(0);
                        Value* rhs = bo->getOperand(1);

                        if (!isa<ConstantInt>(lhs) && !m_map.count(lhs))
                        {
                            continue;
                        }
                        if (!isa<ConstantInt>(rhs) && !m_map.count(rhs))
                        {
                            continue;
                        }

                        switch (opcode)
                        {
                        case Instruction::BinaryOps::Add:
                        {
                            handleBinaryInst(&inst, SimpleOperandExprNode::Add, lhs, rhs);
                            didSomething = true;
                            break;
                        }
                        case Instruction::BinaryOps::Sub:
                        {
                            handleBinaryInst(&inst, SimpleOperandExprNode::Sub, lhs, rhs);
                            didSomething = true;
                            break;
                        }
                        case Instruction::BinaryOps::Mul:
                        {
                            handleBinaryInst(&inst, SimpleOperandExprNode::Mul, lhs, rhs);
                            didSomething = true;
                            break;
                        }
                        case Instruction::BinaryOps::Shl:
                        {
                            if (isa<ConstantInt>(rhs))
                            {
                                uint64_t rhsVal = GetValueOfLLVMConstantInt<uint64_t>(rhs);
                                ReleaseAssert(rhsVal < 64);
                                handleBinaryInst(&inst,
                                                 SimpleOperandExprNode::Mul,
                                                 lhs,
                                                 ConstantInt::get(rhs->getType(), static_cast<uint64_t>(1) << rhsVal));
                                didSomething = true;
                            }
                            break;
                        }
                        default:
                        {
                            break;
                        }
                        }   /* switch */
                    }
                    else if (isa<TruncInst>(&inst) || isa<ZExtInst>(&inst) || isa<SExtInst>(&inst))
                    {
                        Value* operand = inst.getOperand(0);
                        if (!isa<ConstantInt>(operand) && !m_map.count(operand))
                        {
                            continue;
                        }
                        handleCastInst(&inst, operand);
                        didSomething = true;
                    }
                    else if (isa<GetElementPtrInst>(&inst))
                    {
                        GetElementPtrInst* gep = cast<GetElementPtrInst>(&inst);
                        MapVector<Value*, APInt> variableOffsets;

                        // LLVM requires 'constantOffset' to be initialized!
                        //
                        APInt constantOffset = APInt(64, 0);
                        if (!gep->collectOffset(dataLayout, 64 /*bitWidth*/, variableOffsets /*out*/, constantOffset /*out*/))
                        {
                            continue;
                        }

                        ReleaseAssert(constantOffset.getBitWidth() == 64);
                        for (auto& it : variableOffsets)
                        {
                            ReleaseAssert(it.second.getBitWidth() == 64);
                        }

                        bool ready = true;
                        for (auto& it : variableOffsets)
                        {
                            Value* variable = it.first;
                            if (!m_map.count(variable))
                            {
                                ready = false;
                                break;
                            }
                        }
                        if (!ready)
                        {
                            continue;
                        }

                        if (!m_map.count(gep->getPointerOperand()))
                        {
                            continue;
                        }

                        std::vector<const std::vector<SimpleOperandExprNode*>*> list;
                        std::vector<uint64_t> coeffs;
                        list.push_back(&queryMap(gep->getPointerOperand()));
                        coeffs.push_back(1);
                        for (auto& it : variableOffsets)
                        {
                            Value* variable = it.first;
                            list.push_back(&queryMap(variable));
                            coeffs.push_back(it.second.extractBitsAsZExtValue(64 /*numBits*/, 0 /*bitPosition*/));
                        }
                        std::vector<SimpleOperandExprNode*> constantOffsetTermHolder;
                        {
                            uint64_t constantOffsetU64 = constantOffset.extractBitsAsZExtValue(64 /*numBits*/, 0 /*bitPosition*/);
                            if (constantOffsetU64 != 0)
                            {
                                coeffs.push_back(1);
                                constantOffsetTermHolder.push_back(SimpleOperandExprNode::CreateLiteral(64, constantOffsetU64));
                                list.push_back(&constantOffsetTermHolder);
                            }
                        }

                        ReleaseAssert(list.size() == coeffs.size());
                        std::vector<SimpleOperandExprNode*> tmpRes;
                        size_t totalItems = 1;
                        {
                            for (auto& it : list)
                            {
                                size_t num = it->size();
                                ReleaseAssert(num > 0);
                                if (totalItems > x_itemThreshold / num)
                                {
                                    m_analysisFailed = true;
                                    return;
                                }
                                totalItems *= num;
                            }
                            m_itemEstimate += totalItems;
                            if (m_itemEstimate > x_itemThreshold)
                            {
                                m_analysisFailed = true;
                                return;
                            }
                        }

                        std::vector<size_t> ords;
                        std::function<void(SimpleOperandExprNode*)> dfs = [&](SimpleOperandExprNode* curVal)
                        {
                            if (ords.size() == list.size())
                            {
                                tmpRes.push_back(curVal);
                                return;
                            }

                            size_t curOrd = ords.size();
                            ReleaseAssert(curOrd < list.size());
                            for (size_t k = 0; k < list[curOrd]->size(); k++)
                            {
                                ords.push_back(k);
                                SimpleOperandExprNode* term = SimpleOperandExprNode::CreateBinaryExpr(
                                    SimpleOperandExprNode::Mul,
                                    list[curOrd]->at(k),
                                    SimpleOperandExprNode::CreateLiteral(64, coeffs[curOrd]));
                                ReleaseAssertIff(curOrd > 0, curVal != nullptr);
                                if (curOrd > 0)
                                {
                                    term = SimpleOperandExprNode::CreateBinaryExpr(
                                        SimpleOperandExprNode::Add, curVal, term);
                                }
                                dfs(term);
                                ords.pop_back();
                            }
                        };
                        dfs(nullptr);
                        ReleaseAssert(tmpRes.size() == totalItems);

                        ReleaseAssert(!m_map.count(&inst));
                        m_map[&inst] = std::move(tmpRes);
                        didSomething = true;
                    }
                    else if (isa<PHINode>(&inst))
                    {
                        PHINode* phi = cast<PHINode>(&inst);
                        uint32_t numVals = phi->getNumIncomingValues();
                        bool ready = true;
                        for (uint32_t i = 0; i < numVals; i++)
                        {
                            Value* val = phi->getIncomingValue(i);
                            if (!isa<ConstantInt>(val) && !m_map.count(val))
                            {
                                ready = false;
                                break;
                            }
                        }
                        if (ready)
                        {
                            std::vector<SimpleOperandExprNode*>& res = m_map[&inst];
                            for (uint32_t i = 0; i < numVals; i++)
                            {
                                Value* val = phi->getIncomingValue(i);
                                const std::vector<SimpleOperandExprNode*>& list = queryMap(val);
                                res.insert(res.end(), list.begin(), list.end());
                            }
                            m_itemEstimate += res.size();
                            if (m_itemEstimate > x_itemThreshold)
                            {
                                m_analysisFailed = true;
                                return;
                            }
                            didSomething = true;
                        }
                    }
                }
            }
            if (!didSomething)
            {
                break;
            }
        }
    }
};

}   // namespace dast
