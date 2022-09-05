#pragma once

#include "misc_llvm_helper.h"
#include "tvalue.h"

namespace dast {

struct TValueOperandWithProvenTypeDesc
{
    uint32_t m_ord;
    TypeSpeculationMask m_provenType;
};

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
// The input of this pass is the proven types of the operands.
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
        , m_typeInfo()
        , m_didAnalysis(false)
        , m_didOptimization(false)
        , m_provenPreconditionForTypeChecks()
        , m_provenConstants()
        , m_unreachableBBList()
        , m_isControlFlowEdgeFeasible()
    { }

    void SetTargetFunction(llvm::Function* func)
    {
        ReleaseAssert(m_targetFunction == nullptr && func != nullptr);
        m_targetFunction = func;
    }

    void AddOperandTypeInfo(uint32_t argOrd, TypeSpeculationMask info)
    {
        ReleaseAssert(m_targetFunction != nullptr);
        ReleaseAssert(m_targetFunction->arg_size() > argOrd);
        ReleaseAssert(llvm_type_has_type<uint64_t>(m_targetFunction->getArg(argOrd)->getType()));
        for (auto& item : m_typeInfo)
        {
            ReleaseAssert(item.first != argOrd);
        }
        if (info == x_typeSpeculationMaskFor<tTop>)
        {
            return;
        }
        m_typeInfo.push_back(std::make_pair(argOrd, info));
    }

    void DoAnalysis();
    void DoOptimization();

private:
    llvm::Function* m_targetFunction;
    std::vector<std::pair<uint32_t /*argOrd*/, TypeSpeculationMask /*provenType*/>> m_typeInfo;
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
