#include "deegen_simple_operand_expression.h"

namespace dast {

void SimpleOperandExprNode::PrintImpl(FILE* fp, SimpleOperandExprCppPrinter* printer, size_t varOrd)
{
    if (m_op == Operator::Operand)
    {
        if (m_operand->GetKind() == BcOperandKind::BytecodeRangeBase)
        {
            ReleaseAssert(m_bitWidth == 64);
            fprintf(fp, "const uint64_t tmp_%llu = sizeof(TValue) * ops.%s.m_localOrd;\n", static_cast<unsigned long long>(varOrd), m_operand->OperandName().c_str());
        }
        else
        {
            ReleaseAssert(m_operand->GetKind() == BcOperandKind::Literal || m_operand->GetKind() == BcOperandKind::SpecializedLiteral);
            fprintf(fp, "const uint%d_t tmp_%llu = static_cast<uint%d_t>(ops.%s.m_value);\n",
                    static_cast<int>(m_bitWidth),
                    static_cast<unsigned long long>(varOrd),
                    static_cast<int>(m_bitWidth),
                    m_operand->OperandName().c_str());
        }
        return;
    }
    if (m_op == Operator::Literal)
    {
        fprintf(fp, "const uint%d_t tmp_%llu = static_cast<uint%d_t>(%llu);\n",
                static_cast<int>(m_bitWidth),
                static_cast<unsigned long long>(varOrd),
                static_cast<int>(m_bitWidth),
                static_cast<unsigned long long>(m_literalValue));
        return;
    }

    size_t numChildren = GetNumChildren();
    ReleaseAssert(numChildren > 0);

    if (numChildren == 1)
    {
        size_t chOrd = printer->Print(fp, m_child[0]);
        if (m_op == Operator::Trunc || m_op == Operator::ZExt)
        {
            fprintf(fp, "const uint%d_t tmp_%llu = static_cast<uint%d_t>(tmp_%llu);\n",
                    static_cast<int>(m_bitWidth),
                    static_cast<unsigned long long>(varOrd),
                    static_cast<int>(m_bitWidth),
                    static_cast<unsigned long long>(chOrd));
        }
        else
        {
            ReleaseAssert(m_op == Operator::SExt);
            fprintf(fp, "const uint%d_t tmp_%llu = SignExtendTo<uint%d_t>(tmp_%llu);\n",
                    static_cast<int>(m_bitWidth),
                    static_cast<unsigned long long>(varOrd),
                    static_cast<int>(m_bitWidth),
                    static_cast<unsigned long long>(chOrd));
        }
        return;
    }

    size_t chOrd0 = printer->Print(fp, m_child[0]);
    size_t chOrd1 = printer->Print(fp, m_child[1]);
    std::string operatorStr;
    if (m_op == Operator::Add)
    {
        operatorStr = "+";
    }
    else if (m_op == Operator::Sub)
    {
        operatorStr = "-";
    }
    else
    {
        ReleaseAssert(m_op == Operator::Mul);
        operatorStr = "*";
    }
    fprintf(fp, "const uint%d_t tmp_%llu = tmp_%llu %s tmp_%llu;\n",
            static_cast<int>(m_bitWidth),
            static_cast<unsigned long long>(varOrd),
            static_cast<unsigned long long>(chOrd0),
            operatorStr.c_str(),
            static_cast<unsigned long long>(chOrd1));
}

}   // namespace dast
