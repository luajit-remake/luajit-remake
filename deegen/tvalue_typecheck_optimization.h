#pragma once

#include "misc_llvm_helper.h"
#include "tvalue.h"
#include "drt/dfg_edge_use_kind.h"

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

        virtual bool eval(const std::unordered_map<uint32_t /*operandOrd*/, TypeMaskTy /*singletonTypeMask*/>& comb) = 0;
    };

    class AndConstraint final : public Constraint
    {
    public:
        void AddClause(std::unique_ptr<Constraint> constraint)
        {
            ReleaseAssert(constraint.get() != nullptr);
            m_clauses.push_back(std::move(constraint));
        }

        virtual bool eval(const std::unordered_map<uint32_t /*operandOrd*/, TypeMaskTy /*singletonTypeMask*/>& comb) override
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

        virtual bool eval(const std::unordered_map<uint32_t /*operandOrd*/, TypeMaskTy /*singletonTypeMask*/>& comb) override
        {
            return !m_clause->eval(comb);
        }

    private:
        std::unique_ptr<Constraint> m_clause;
    };

    class LeafConstraint final : public Constraint
    {
    public:
        LeafConstraint(uint32_t operandOrd, TypeMaskTy allowedMask)
            : m_operandOrd(operandOrd), m_allowedMask(allowedMask)
        { }

        virtual bool eval(const std::unordered_map<uint32_t /*operandOrd*/, TypeMaskTy /*singletonTypeMask*/>& comb) override
        {
            auto it = comb.find(m_operandOrd);
            ReleaseAssert(it != comb.end());
            TypeMaskTy singletonMask = it->second;
            TypeMaskTy passed = m_allowedMask & singletonMask;
            ReleaseAssert(passed == 0 || passed == singletonMask);
            return passed == singletonMask;
        }

    private:
        uint32_t m_operandOrd;
        TypeMaskTy m_allowedMask;
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
    static void DoOptimizationForBytecodeQuickeningFastPath(BytecodeVariantDefinition* bvd, llvm::Function* implFunction);
    static void DoOptimizationForBytecodeQuickeningSlowPath(BytecodeVariantDefinition* bvd, llvm::Function* implFunction);

private:
    llvm::Function* m_targetFunction;
    std::vector<uint32_t /*operandOrd*/> m_operandList;
    std::unique_ptr<Constraint> m_constraint;
    bool m_isOperandListSet;
    bool m_didAnalysis;
    bool m_didOptimization;
    // Maps each TValue::Is<XXX>(arg) call to the proven type mask precondition of 'arg'
    //
    std::unordered_map<llvm::CallInst* /*typecheckInst*/, TypeMaskTy /*provenTypePrecondition*/> m_provenPreconditionForTypeChecks;
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

// Returns true if the function is a TValue typecheck API function (TValue::Is<>(...))
//
bool IsTValueTypeCheckAPIFunction(llvm::Function* func, TypeMaskTy* typeMask /*out*/ = nullptr);

// Returns true if the function is a TValue decode API function (TValue::As<>(...))
//
bool IsTValueDecodeAPIFunction(llvm::Function* func, TypeMaskTy* typeMask /*out*/ = nullptr);

// Returns true if the function is a TValue typecheck strength reduction function (created by optimization)
//
bool IsTValueTypeCheckStrengthReductionFunction(llvm::Function* func);

DesugarDecision ShouldDesugarTValueTypecheckAPI(llvm::Function* func, DesugaringLevel level);

// 'func' must be a TValue typecheck API or typecheck strength reduction function
// Returns the mask being checked
//
TypeMaskTy WARN_UNUSED GetCheckedMaskOfTValueTypecheckFunction(llvm::Function* func);

struct TypecheckStrengthReductionCandidate
{
    TypeMaskTy m_checkedMask;
    TypeMaskTy m_precondMask;
    llvm::Function* m_func;
    size_t m_estimatedCost;

    bool WARN_UNUSED CanBeUsedToImplement(TypeMaskTy desiredCheckedMask, TypeMaskTy knownPreconditionMask)
    {
        ReleaseAssert((desiredCheckedMask & knownPreconditionMask) == desiredCheckedMask);
        ReleaseAssert(desiredCheckedMask > 0);
        ReleaseAssert(desiredCheckedMask < knownPreconditionMask);
        if ((knownPreconditionMask & m_precondMask) != knownPreconditionMask)
        {
            // Our precondition is not a superset of the knownPrecondition
            // We cannot be used, as there exists a type where our check has undefined behavior where desired check has defined behavior
            //
            return false;
        }
        if ((m_checkedMask & knownPreconditionMask) != desiredCheckedMask)
        {
            // We cannot be used, as there exists a type where our check will return a different value from the desired check
            //
            return false;
        }
        return true;
    }
};

struct TypeCheckFunctionSelector
{
    TypeCheckFunctionSelector(llvm::Module* module);

    // This version is only used for unit test. Since the llvm::Function* is not available, it will always be nullptr in QueryResult
    //
    TypeCheckFunctionSelector(const dfg::TypeCheckerMethodCostInfo* infoList, size_t numItems);

    ~TypeCheckFunctionSelector();

    struct QueryResult
    {
        enum Kind
        {
            NoSolutionFound,
            TriviallyTrue,
            TriviallyFalse,
            CallFunction,
            CallFunctionAndFlipResult
        };

        Kind m_opKind;
        llvm::Function* m_func;
        const TypecheckStrengthReductionCandidate* m_info;
    };

    QueryResult WARN_UNUSED Query(TypeMaskTy maskToCheck, TypeMaskTy preconditionMask);

    const std::vector<TypecheckStrengthReductionCandidate>& GetStrengthReductionList()
    {
        return *m_candidateList.get();
    }

private:
    std::pair<TypecheckStrengthReductionCandidate*, size_t /*cost*/> FindBestStrengthReduction(TypeMaskTy checkedMask, TypeMaskTy precondMask);

    std::unique_ptr<std::vector<TypecheckStrengthReductionCandidate>> m_candidateList;
};

struct DfgSelectTypeCheckFnAutomataGenerator
{
    DfgSelectTypeCheckFnAutomataGenerator(TypeCheckFunctionSelector& tcfInfo)
        : m_info(tcfInfo)
    { }

    // Build an automata for checking 'maskToCheck'.
    // The automata takes in 'precondMask', and output the UseKind to check 'maskToCheck' given precondition 'precondMask'.
    //
    // Precisely, the output is as follow:
    //   If 'precondMask' is 0, the output is UseKind_Unreachable
    //   If the condition is trivially true, the output is the ProvenUseKind that corresponds to maskToCheck
    //   If the condition is trivially false, the output is UseKind_AlwaysOsrExit
    //   Otherwise, the output is UseKind_FirstUnprovenUseKind + N where:
    //     N is even: use function N/2 in tcfInfo
    //     N is odd: use function N/2 in tcfInfo and flip result
    //
    std::vector<uint8_t> WARN_UNUSED BuildAutomata(TypeMask maskToCheck, size_t* depthOfGeneratedAutomata = nullptr /*out*/);

private:
    TypeCheckFunctionSelector& m_info;
};

}   // namespace dast
