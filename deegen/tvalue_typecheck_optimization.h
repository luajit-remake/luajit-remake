#pragma once

#include "misc_llvm_helper.h"
#include "tvalue.h"

namespace dast {

class BytecodeVariantDefinition;

// The purpose of this pass is to optimize the function based on the *proven* type information of the operands.
// It does two optimizations:
// 1. Strength reduction on TValue::Is<XXX>() type checks through an accurate type precondition analysis.
// 2. Prove and replace constant-producing instructions and remove dead basic blocks.
//
// For example, for the following logic:
//   void f(TValue a) {
//     if (a.Is<tDouble>()) {
//        ....
//     } else if (a.Is<tNil>()) {
//        ....
//     } else {
//        ....
//     }
//   }
// If we have type proof that 'a' is either double or nil, we shall remove the Is<tNil>() typecheck,
// and completely remove the last 'else' branch.
//
// And for the following logic:
//   void f(TValue a, TValue b) {
//     if (a.Is<tDouble>() && b.Is<tDouble>()) {
//        ....
//     } else {
//        ....
//     }
//   }
// If we have type proof that 'a' and 'b' cannot be both double, we shall remove the if-check and the true-branch
// even though we cannot prove either 'a.Is<tDouble>()' or 'b.Is<tDouble>' produces a constant.
//
// The input of this pass is the proven type constraints of the operands.
//
// We do a brute-force (exponential) enumeration of all the valid type combinations of the operands,
// and do Sparse Conditional Constant Propagation (SCCP) to figure out if each typecheck call or boolean
// can be deduced to a constant under the given type combination.
// The results are then merged together to generate the analysis result for the function.
// Finally, user should call the 'DoOptimize()' method to optimize and transform the LLVM IR.
//
// Note that currently we do not reason about PHI nodes. User is expected to write the program in a way
// so that the interesting typechecks act directly on the operands (by rotating the loop, for example).
//
class TValueTypecheckOptimizationPass
{
public:
    TValueTypecheckOptimizationPass()
        : m_targetFunction(nullptr)
        , m_operandList()
        , m_constraint()
        , m_isOperandListSet(false)
        , m_didAnalysis(false)
        , m_didOptimization(false)
        , m_provenPreconditionForTypeChecks()
        , m_provenConstants()
        , m_unreachableBBList()
        , m_isControlFlowEdgeFeasible()
    { }

    class Constraint
    {
        MAKE_NONCOPYABLE(Constraint);
        MAKE_NONMOVABLE(Constraint);
    public:
        Constraint() { }
        virtual ~Constraint() { }

        virtual bool eval(const std::unordered_map<uint32_t /*operandOrd*/, TypeSpeculationMask /*singletonTypeMask*/>& comb) = 0;
    };

    class AndConstraint final : public Constraint
    {
    public:
        void AddClause(std::unique_ptr<Constraint> constraint)
        {
            ReleaseAssert(constraint.get() != nullptr);
            m_clauses.push_back(std::move(constraint));
        }

        virtual bool eval(const std::unordered_map<uint32_t /*operandOrd*/, TypeSpeculationMask /*singletonTypeMask*/>& comb) override
        {
            for (auto& it : m_clauses)
            {
                if (!it->eval(comb))
                {
                    return false;
                }
            }
            return true;
        }

    private:
        std::vector<std::unique_ptr<Constraint>> m_clauses;
    };

    class NotConstraint final : public Constraint
    {
    public:
        NotConstraint(std::unique_ptr<Constraint> clause)
            : m_clause(std::move(clause))
        {
            ReleaseAssert(m_clause.get() != nullptr);
        }

        virtual bool eval(const std::unordered_map<uint32_t /*operandOrd*/, TypeSpeculationMask /*singletonTypeMask*/>& comb) override
        {
            return !m_clause->eval(comb);
        }

    private:
        std::unique_ptr<Constraint> m_clause;
    };

    class LeafConstraint final : public Constraint
    {
    public:
        LeafConstraint(uint32_t operandOrd, TypeSpeculationMask allowedMask)
            : m_operandOrd(operandOrd), m_allowedMask(allowedMask)
        { }

        virtual bool eval(const std::unordered_map<uint32_t /*operandOrd*/, TypeSpeculationMask /*singletonTypeMask*/>& comb) override
        {
            auto it = comb.find(m_operandOrd);
            ReleaseAssert(it != comb.end());
            TypeSpeculationMask singletonMask = it->second;
            TypeSpeculationMask passed = m_allowedMask & singletonMask;
            ReleaseAssert(passed == 0 || passed == singletonMask);
            return passed == singletonMask;
        }

    private:
        uint32_t m_operandOrd;
        TypeSpeculationMask m_allowedMask;
    };

    void SetTargetFunction(llvm::Function* func)
    {
        ReleaseAssert(m_targetFunction == nullptr && func != nullptr);
        m_targetFunction = func;
    }

    void SetOperandList(const std::vector<uint32_t>& operandList)
    {
        ReleaseAssert(!m_isOperandListSet);
        m_isOperandListSet = true;
        m_operandList = operandList;
        for (size_t i = 0; i < m_operandList.size(); i++)
        {
            for (size_t j = i + 1; j < m_operandList.size(); j++)
            {
                ReleaseAssert(m_operandList[i] != m_operandList[j]);
            }
        }
    }

    void SetConstraint(std::unique_ptr<Constraint> constraint)
    {
        ReleaseAssert(constraint.get() != nullptr);
        ReleaseAssert(m_constraint.get() == nullptr);
        m_constraint = std::move(constraint);
    }

    void Run()
    {
        DoAnalysis();
        DoOptimization();
    }

    void DoAnalysis();
    void DoOptimization();

    static void DoOptimizationForBytecode(BytecodeVariantDefinition* bvd, llvm::Function* implFunction);

private:
    llvm::Function* m_targetFunction;
    std::vector<uint32_t /*operandOrd*/> m_operandList;
    std::unique_ptr<Constraint> m_constraint;
    bool m_isOperandListSet;
    bool m_didAnalysis;
    bool m_didOptimization;
    // Maps each TValue::Is<XXX>(arg) call to the proven type mask precondition of 'arg'
    //
    std::unordered_map<llvm::CallInst* /*typecheckInst*/, TypeSpeculationMask /*provenTypePrecondition*/> m_provenPreconditionForTypeChecks;
    // Map each instruction to the proven constant this instruction produces
    //
    std::unordered_map<llvm::Instruction*, llvm::Constant*> m_provenConstants;
    // The basic blocks proven to be unreachable
    //
    std::unordered_set<llvm::BasicBlock*> m_unreachableBBList;
    // The feasible control flow edges
    //
    std::unordered_map<llvm::BasicBlock*, std::unordered_map<llvm::BasicBlock*, bool>> m_isControlFlowEdgeFeasible;
};

bool IsTValueTypeCheckAPIFunction(llvm::Function* func, TypeSpeculationMask* typeMask /*out*/ = nullptr);
bool IsTValueTypeCheckStrengthReductionFunction(llvm::Function* func);
DesugarDecision ShouldDesugarTValueTypecheckAPI(llvm::Function* func, DesugaringLevel level);

struct TValueOperandSpecialization
{
    TValueOperandSpecialization()
        : m_provenType(x_typeSpeculationMaskFor<tTop>)
        , m_speculatedType(x_typeSpeculationMaskFor<tTop>)
    { }

    TValueOperandSpecialization(TypeSpeculationMask provenType)
        : m_provenType(provenType)
        , m_speculatedType(provenType)
    {
        ReleaseAssert(provenType != 0);
    }

    TValueOperandSpecialization(TypeSpeculationMask provenType, TypeSpeculationMask speculatedType)
        : m_provenType(provenType)
        , m_speculatedType(speculatedType)
    {
        ReleaseAssert((provenType & speculatedType) == speculatedType);
        ReleaseAssert(provenType != 0 && speculatedType != 0);
    }

    bool IsTrivial()
    {
        return m_provenType == x_typeSpeculationMaskFor<tTop> && m_speculatedType == x_typeSpeculationMaskFor<tTop>;
    }

    TypeSpeculationMask m_provenType;
    TypeSpeculationMask m_speculatedType;
};


}   // namespace dast
