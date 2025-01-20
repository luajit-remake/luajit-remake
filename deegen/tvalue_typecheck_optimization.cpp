#include "tvalue_typecheck_optimization.h"
#include "llvm/Transforms/Utils/SCCPSolver.h"
#include "llvm/Transforms/Utils/Local.h"
#include "llvm/TargetParser/Triple.h"
#include "llvm/Analysis/ValueLattice.h"

#include "deegen_bytecode_operand.h"
#include "typemask_overapprox_automata_generator.h"
#include "drt/dfg_edge_use_kind.h"

namespace dast {

static TypeMaskTy WARN_UNUSED ParseTypeMaskFromCppTypeName(std::string tplName)
{
    bool found = false;
    TypeMaskTy res = 0;
    const auto& defs = x_list_of_type_speculation_mask_and_name;
    for (size_t i = 0; i < defs.size(); i++)
    {
        if (tplName == defs[i].second)
        {
            ReleaseAssert(!found);
            found = true;
            res = defs[i].first;
        }
    }
    ReleaseAssert(found);
    return res;
}

bool IsTValueTypeCheckAPIFunction(llvm::Function* func, TypeMaskTy* typeMask /*out*/)
{
    std::string fnName = func->getName().str();
    ReleaseAssert(fnName != "");
    if (!IsCXXSymbol(fnName))
    {
        return false;
    }
    std::string demangledName = GetDemangledName(func);
    constexpr const char* expected_prefix = "bool DeegenImpl_TValueIs<";
    constexpr const char* expected_suffix = ">(TValue)";
    if (demangledName.starts_with(expected_prefix) && demangledName.ends_with(expected_suffix))
    {
        if (typeMask != nullptr)
        {
            demangledName = demangledName.substr(strlen(expected_prefix));
            demangledName = demangledName.substr(0, demangledName.length() - strlen(expected_suffix));
            *typeMask = ParseTypeMaskFromCppTypeName(demangledName);
        }
        return true;
    }
    else
    {
        return false;
    }
}

bool IsTValueDecodeAPIFunction(llvm::Function* func, TypeMaskTy* typeMask /*out*/)
{
    std::string fnName = func->getName().str();
    ReleaseAssert(fnName != "");
    if (!IsCXXSymbol(fnName))
    {
        return false;
    }
    std::string demangledName = GetDemangledName(func);
    constexpr const char* expected_prefix = "auto DeegenImpl_TValueAs<";
    constexpr const char* expected_suffix = ">(TValue)";
    if (demangledName.starts_with(expected_prefix) && demangledName.ends_with(expected_suffix))
    {
        if (typeMask != nullptr)
        {
            demangledName = demangledName.substr(strlen(expected_prefix));
            demangledName = demangledName.substr(0, demangledName.length() - strlen(expected_suffix));
            *typeMask = ParseTypeMaskFromCppTypeName(demangledName);
        }
        return true;
    }
    else
    {
        return false;
    }
}

bool IsTValueTypeCheckStrengthReductionFunction(llvm::Function* func)
{
    std::string fnName = func->getName().str();
    ReleaseAssert(fnName != "");
    if (!IsCXXSymbol(fnName))
    {
        return false;
    }
    std::string demangledName = GetDemangledName(func);
    constexpr const char* expected_prefix = "tvalue_typecheck_strength_reduction_impl_holder<";
    return demangledName.starts_with(expected_prefix);
}

DesugarDecision ShouldDesugarTValueTypecheckAPI(llvm::Function* func, DesugaringLevel level)
{
    // We preserve TValue::As<> functions as well, even though the TValueTypecheckOptimization pass does not use it.
    // This is mainly because it doesn't hurt to do so, and an optimization in the call IC relies on
    // identifying calls to TValue::As<>. I think putting TValue::Is<> and TValue::As<> at the same
    // desugaring level is the most reasonable thing to do.
    //
    if (!IsTValueTypeCheckAPIFunction(func) && !IsTValueTypeCheckStrengthReductionFunction(func) && !IsTValueDecodeAPIFunction(func))
    {
        return DesugarDecision::DontCare;
    }
    if (level >= DesugaringLevel::TypeSpecialization)
    {
        return DesugarDecision::MustInline;
    }
    else
    {
        return DesugarDecision::MustNotInline;
    }
}

struct RunSCCPAnalysisPassHelper
{
    RunSCCPAnalysisPassHelper(llvm::Function* func)
        : tlii(llvm::Triple(func->getParent()->getTargetTriple()))
        , tli(tlii, func)
        , dl(func->getParent())
        , tliGetter([this](llvm::Function& /*f*/) -> const llvm::TargetLibraryInfo& { return tli; })
        , solver(dl, tliGetter, func->getContext())
    {
        using namespace llvm;

        // Logic stolen from Scalar_SCCP.cpp: runSCCP
        //
        solver.markBlockExecutable(&func->front());
        for (Argument &AI : func->args())
        {
            solver.markOverdefined(&AI);
        }
        bool resolvedUndefs = true;
        while (resolvedUndefs)
        {
            solver.solve();
            resolvedUndefs = solver.resolvedUndefsIn(*func);
        }
    }

    llvm::TargetLibraryInfoImpl tlii;
    llvm::TargetLibraryInfo tli;
    llvm::DataLayout dl;
    std::function<const llvm::TargetLibraryInfo&(llvm::Function&)> tliGetter;
    llvm::SCCPSolver solver;
};

class TValueTypecheckInstData
{
    MAKE_NONCOPYABLE(TValueTypecheckInstData);
    MAKE_NONMOVABLE(TValueTypecheckInstData);
public:
    TValueTypecheckInstData(llvm::CallInst* inst, TypeMaskTy mask)
        : m_inst(inst)
        , m_trueReplacement(nullptr)
        , m_falseReplacement(nullptr)
        , m_isReplaced(false)
        , m_isReplacedByTrue(false)
        , m_mask(mask)
    {
        using namespace llvm;
        LLVMContext& ctx = inst->getContext();

        // We want to replace the Call to true/false for SCCP
        // but we also want to replace it back to the call after SCCP.
        //
        // Unfortunately, in LLVM Constants are hash cons'ed, so there's no way we can
        // replace the constant back to the instruction by RAUW.
        //
        // So instead, we create a Cast<bool>(1) and Cast<bool>(0) instruction to represent 'true' and 'false'.
        // SCCP can see through these instructions, so it would work normally.
        // But these instructions are not hash cons'ed, so we can replace them back afterwards.
        //
        m_trueReplacement = CastInst::CreateIntegerCast(CreateLLVMConstantInt<uint32_t>(ctx, 1), llvm_type_of<bool>(ctx), false /*isSigned*/);
        m_falseReplacement = CastInst::CreateIntegerCast(CreateLLVMConstantInt<uint32_t>(ctx, 0), llvm_type_of<bool>(ctx), false /*isSigned*/);
        ReleaseAssert(m_trueReplacement != nullptr && m_falseReplacement != nullptr && m_trueReplacement != m_falseReplacement);
        ReleaseAssert(m_inst != nullptr && m_inst->getParent() != nullptr);
    }

    ~TValueTypecheckInstData()
    {
        ReleaseAssert(!m_isReplaced);
        ReleaseAssert(m_trueReplacement != nullptr && m_falseReplacement != nullptr);
        ReleaseAssert(m_trueReplacement->users().begin() == m_trueReplacement->users().end());
        ReleaseAssert(m_falseReplacement->users().begin() == m_falseReplacement->users().end());
        ReleaseAssert(m_trueReplacement->getParent() == nullptr);
        ReleaseAssert(m_falseReplacement->getParent() == nullptr);
        delete m_trueReplacement;
        delete m_falseReplacement;
    }

    TypeMaskTy GetMask()
    {
        return m_mask;
    }

    llvm::CallInst* GetCallInst()
    {
        return m_inst;
    }

    // Put 'm_trueReplacement' and 'm_falseReplacement' to a set so we can identify them
    //
    void PopulateReplacementInstructionSet(std::unordered_set<llvm::Instruction*>& dst /*inout*/)
    {
        ReleaseAssert(!dst.count(m_trueReplacement));
        dst.insert(m_trueReplacement);
        ReleaseAssert(!dst.count(m_falseReplacement));
        dst.insert(m_falseReplacement);
    }

    void TemporarilyReplaceToBoolean(bool value)
    {
        ReleaseAssert(!m_isReplaced);
        m_isReplaced = true;
        m_isReplacedByTrue = value;
        if (value)
        {
            ReplaceImpl(m_inst, m_trueReplacement);
        }
        else
        {
            ReplaceImpl(m_inst, m_falseReplacement);
        }
    }

    void ReplaceBackToCall()
    {
        ReleaseAssert(m_isReplaced);
        m_isReplaced = false;
        if (m_isReplacedByTrue)
        {
            ReplaceImpl(m_trueReplacement, m_inst);
        }
        else
        {
            ReplaceImpl(m_falseReplacement, m_inst);
        }
    }

private:
    void ReplaceImpl(llvm::Instruction* fromInst, llvm::Instruction* toInst)
    {
        using namespace llvm;
        // We cannot use LLVM's ReplaceInstByInst utility because that deletes the replaced instruction
        //
        ReleaseAssert(fromInst->getParent() != nullptr);
        ReleaseAssert(toInst->getParent() == nullptr);

        toInst->insertBefore(fromInst);

        fromInst->replaceAllUsesWith(toInst);
        fromInst->removeFromParent();

        ReleaseAssert(fromInst->getParent() == nullptr);
        ReleaseAssert(toInst->getParent() != nullptr);
    }

    // The API call inst
    //
    llvm::CallInst* m_inst;
    // The true and false evaluation replacement for SCCP
    //
    llvm::CastInst* m_trueReplacement;
    llvm::CastInst* m_falseReplacement;
    // Whether m_inst has been replaced
    //
    bool m_isReplaced;
    // Whether m_inst is replaced by true or false
    //
    bool m_isReplacedByTrue;
    // The type mask being checked
    //
    TypeMaskTy m_mask;
};

// logic stolen from Scalar_SCCP.cpp: isConstant
//
// Helper to check if \p LV is either a constant or a constant
// range with a single element. This should cover exactly the same cases as the
// old ValueLatticeElement::isConstant() and is intended to be used in the
// transition to ValueLatticeElement.
//
static bool LLVMValueLatticeElementIsConstant(const llvm::ValueLatticeElement& LV)
{
    return LV.isConstant() || (LV.isConstantRange() && LV.getConstantRange().isSingleElement());
}

// logic stolen from Scalar_SCCP.cpp: isOverdefined
//
// Helper to check if \p LV is either overdefined or a constant range with more
// than a single element. This should cover exactly the same cases as the old
// ValueLatticeElement::isOverdefined() and is intended to be used in the
// transition to ValueLatticeElement.
//
static bool LLVMValueLatticeElementIsOverdefined(const llvm::ValueLatticeElement& LV)
{
    return !LV.isUnknownOrUndef() && !LLVMValueLatticeElementIsConstant(LV);
}

// logic stolen from Scalar_SCCP.cpp: canRemoveInstruction
//
static bool SCCPPassCanRemoveInstruction(llvm::Instruction* I)
{
    using namespace llvm;
    if (wouldInstructionBeTriviallyDead(I))
    {
        return true;
    }

    // Some instructions can be handled but are rejected above. Catch
    // those cases by falling through to here.
    return isa<LoadInst>(I);
}

void TValueTypecheckOptimizationPass::DoAnalysis()
{
    using namespace llvm;
    ReleaseAssert(m_targetFunction != nullptr && m_isOperandListSet && m_constraint.get() != nullptr);
    ReleaseAssert(!m_didAnalysis);
    m_didAnalysis = true;

    if (m_operandList.size() == 0)
    {
        return;
    }

    LLVMContext& ctx = m_targetFunction->getContext();

    constexpr size_t numTypes = x_numUsefulBitsInBytecodeTypeMask;
    size_t enumMax = 1;
    for (size_t i = 0; i < m_operandList.size(); i++)
    {
        enumMax *= numTypes;
        if (enumMax > 10000000)
        {
            fprintf(stderr, "%llu ^ %llu is too many type combinations.\n", static_cast<unsigned long long>(numTypes), static_cast<unsigned long long>(m_operandList.size()));
            abort();
        }
    }

    // Figure out all the TValue typecheck API calls on values with proven type
    //
    std::vector<std::vector<TValueTypecheckInstData*>> targetedAPICalls;
    targetedAPICalls.resize(m_operandList.size());
    for (size_t listOrd = 0; listOrd < m_operandList.size(); listOrd++)
    {
        std::vector<TValueTypecheckInstData*>& q = targetedAPICalls[listOrd];

        uint32_t operandOrd = m_operandList[listOrd];
        ReleaseAssert(operandOrd < m_targetFunction->arg_size());
        Argument* arg = m_targetFunction->getArg(operandOrd);

        // TODO: We currently don't do anything for PHI-Node-use of 'value', and rely on user to prevent this case by e.g., manually
        //       rotate the loops. In theory we probably could have done something automatically. for example, by pushing down the TypeCheck.
        //
        for (Use& u : arg->uses())
        {
            if (CallInst* callInst = dyn_cast<CallInst>(u.getUser()))
            {
                Function* callee = callInst->getCalledFunction();
                if (callee == nullptr)
                {
                    continue;
                }
                TypeMaskTy mask;
                if (IsTValueTypeCheckAPIFunction(callee, &mask /*out*/))
                {
                    ReleaseAssert(callee->arg_size() == 1);
                    ReleaseAssert(callInst->isArgOperand(&u));
                    ReleaseAssert(callInst->getArgOperandNo(&u) == 0);
                    ReleaseAssert(callInst->getType() == llvm_type_of<bool>(ctx));
                    q.push_back(new TValueTypecheckInstData {
                        callInst,
                        mask
                    });
                }
            }
        }
    }

    std::unordered_set<Instruction*> replacementInstructionSet;
    for (auto& dataList : targetedAPICalls)
    {
        for (TValueTypecheckInstData* data : dataList)
        {
            data->PopulateReplacementInstructionSet(replacementInstructionSet /*inout*/);
        }
    }

    // Whether each control flow edge is feasible
    //
    std::unordered_map<BasicBlock*, std::unordered_map<BasicBlock*, bool>> isControlFlowEdgeFeasible;
    for (BasicBlock& from : *m_targetFunction)
    {
        auto& mm = isControlFlowEdgeFeasible[&from];
        for (BasicBlock* succ: successors(&from))
        {
            mm[succ] = false;
        }
    }

    // Map from each BasicBlock to a vector of length m_typeInfo.size()
    // The i-th element denotes the possible type mask of m_typeInfo[i] at the start of the basic block
    //
    std::unordered_map<BasicBlock*, std::vector<TypeMaskTy>> possibleMasks;
    for (BasicBlock& bb : *m_targetFunction)
    {
        ReleaseAssert(!possibleMasks.count(&bb));
        std::vector<TypeMaskTy>& v = possibleMasks[&bb];
        v.resize(m_operandList.size());
        for (size_t i = 0; i < m_operandList.size(); i++)
        {
            v[i] = 0;
        }
    }

    // Returns nullptr if the instruction is not known to be a constant, otherwise return the proven constant
    //
    auto tryGetConstantValueOfInstruction = [&](SCCPSolver& solver, Instruction* inst) -> Constant*
    {
        ReleaseAssert(solver.isBlockExecutable(inst->getParent()));

        // logic stolen from Scalar_SCCP.cpp: tryToReplaceWithConstant
        //
        Constant* result = nullptr;
        if (inst->getType()->isStructTy())
        {
            std::vector<ValueLatticeElement> IVs = solver.getStructLatticeValueFor(inst);
            if (llvm::any_of(IVs, LLVMValueLatticeElementIsOverdefined))
            {
                return nullptr;
            }
            std::vector<Constant*> ConstVals;
            StructType* ST = dyn_cast<StructType>(inst->getType());
            ReleaseAssert(ST != nullptr);
            for (unsigned i = 0, e = ST->getNumElements(); i != e; ++i)
            {
                ValueLatticeElement V = IVs[i];
                ConstVals.push_back(LLVMValueLatticeElementIsConstant(V)
                                        ? solver.getConstant(V, ST->getElementType(i))
                                        : UndefValue::get(ST->getElementType(i)));
            }
            result = ConstantStruct::get(ST, ConstVals);
        }
        else
        {
            const ValueLatticeElement& IV = solver.getLatticeValueFor(inst);
            if (LLVMValueLatticeElementIsOverdefined(IV))
            {
                return nullptr;
            }

            result = LLVMValueLatticeElementIsConstant(IV) ? solver.getConstant(IV, inst->getType()) : UndefValue::get(inst->getType());
        }
        ReleaseAssert(result != nullptr);

        // LLVM needs some special handling when converting Calls to constant.
        // Fortunately we don't have to do that (since LLVM can take care of it for us later anyway), so just return nullptr in that case.
        //
        if (dyn_cast<CallBase>(inst) != nullptr)
        {
            return nullptr;
        }

        return result;
    };

    // A non-existent entry means the instruction is never reachable
    // An entry with nullptr value means the instruction is not a constant
    //
    std::unordered_map<Instruction*, Constant*> deducedConstants;
    auto updateDeducedConstants = [&](SCCPSolver& solver, Instruction* inst)
    {
        ReleaseAssert(solver.isBlockExecutable(inst->getParent()));

        if (inst->getType()->isVoidTy())
        {
            return;
        }

        Constant* knownConstantValue = nullptr;
        if (deducedConstants.count(inst))
        {
            Constant* val = deducedConstants.find(inst)->second;
            if (val == nullptr)
            {
                // It's already known to be not a constant, don't bother
                //
                return;
            }
            knownConstantValue = val;
        }

        // At this point, if 'knownConstantValue' is nullptr, it means that this is the first
        // time we know this instruction is reachable.
        // Otherwise, 'knownConstantValue' is the constant that this instruction must produce.
        //

        // Now, check the result from this run
        //
        Constant* deducedConstantForThisRun = tryGetConstantValueOfInstruction(solver, inst);

        Constant* valueToUpdate;
        if (deducedConstantForThisRun == nullptr)
        {
            // The instruction does not produce a constant because it isn't a constant in this run
            //
            valueToUpdate = nullptr;
        }
        else if (knownConstantValue == nullptr)
        {
            // This is the first time we have information on this instruction
            //
            valueToUpdate = deducedConstantForThisRun;
        }
        else if (knownConstantValue == deducedConstantForThisRun)
        {
            // This instruction is still proven to produce the same constant at this point
            //
            valueToUpdate = deducedConstantForThisRun;
        }
        else
        {
            // This instruction could produce two different constants, so it's no longer constant
            //
            valueToUpdate = nullptr;
        }

        deducedConstants[inst] = valueToUpdate;
    };

    std::unordered_map<BasicBlock*, bool> isBasicBlockReachable;
    for (BasicBlock& bb : *m_targetFunction)
    {
        ReleaseAssert(!isBasicBlockReachable.count(&bb));
        isBasicBlockReachable[&bb] = false;
    }

    std::vector<TypeMaskTy> currentComb;
    currentComb.resize(m_operandList.size());
    for (size_t curCombVal = 0; curCombVal < enumMax; curCombVal++)
    {
        // Create the current type combination to test
        //
        {
            std::unordered_map<uint32_t, TypeMaskTy> val;
            size_t tmp = curCombVal;
            for (size_t i = 0; i < currentComb.size(); i++)
            {
                size_t curTypeOrd = tmp % numTypes;
                currentComb[i] = SingletonBitmask<TypeMaskTy>(curTypeOrd);
                tmp /= numTypes;
                val[m_operandList[i]] = currentComb[i];
            }
            ReleaseAssert(tmp == 0);

            if (!m_constraint->eval(val))
            {
                continue;
            }
        }

        // Temporarily change Typecheck calls to 'false' or 'true' based on the current combination
        //
        for (size_t listOrd = 0; listOrd < m_operandList.size(); listOrd++)
        {
            TypeMaskTy currentTyMask = currentComb[listOrd];

            std::vector<TValueTypecheckInstData*>& q = targetedAPICalls[listOrd];
            for (TValueTypecheckInstData* item : q)
            {
                TypeMaskTy passed = item->GetMask() & currentTyMask;
                ReleaseAssert(passed == 0 || passed == currentTyMask);
                bool shouldReplaceToTrue = (passed == currentTyMask);
                item->TemporarilyReplaceToBoolean(shouldReplaceToTrue);
            }
        }

        // Run SCCP analysis pass
        //
        RunSCCPAnalysisPassHelper helper(m_targetFunction);
        SCCPSolver& solver = helper.solver;

        // Update the relavent information
        //
        for (BasicBlock& bb : *m_targetFunction)
        {
            ReleaseAssert(isBasicBlockReachable.count(&bb));
            if (solver.isBlockExecutable(&bb))
            {
                // Update reachable basic blocks
                //
                isBasicBlockReachable[&bb] = true;

                // Update instructions that can be deduced to have constant value
                //
                for (Instruction& inst : bb)
                {
                    if (replacementInstructionSet.count(&inst))
                    {
                        // This instruction is a fake instruction we used to replace the typecheck, don't bother
                        //
                        continue;
                    }

                    updateDeducedConstants(solver, &inst);
                }

                // Update 'possibleMasks'
                //
                ReleaseAssert(possibleMasks.count(&bb));
                std::vector<TypeMaskTy>& dst = possibleMasks[&bb];
                ReleaseAssert(dst.size() == currentComb.size());
                for (size_t i = 0; i < currentComb.size(); i++)
                {
                    dst[i] |= currentComb[i];
                }
            }
        }

        // Update feasible edges
        //
        for (BasicBlock& from : *m_targetFunction)
        {
            ReleaseAssert(isControlFlowEdgeFeasible.count(&from));
            auto& mm = isControlFlowEdgeFeasible[&from];
            for (BasicBlock* succ: successors(&from))
            {
                ReleaseAssert(mm.count(succ));
                if (solver.isEdgeFeasible(&from, succ))
                {
                    mm[succ] = true;
                }
            }
        }

        // Restore the Typecheck calls
        //
        for (auto& dataList : targetedAPICalls)
        {
            for (TValueTypecheckInstData* data : dataList)
            {
                data->ReplaceBackToCall();
            }
        }
    }

    for (Instruction* inst : replacementInstructionSet)
    {
        ReleaseAssert(inst->getParent() == nullptr);
    }

    // Record values that can be deduced to be constant
    //
    ReleaseAssert(m_provenConstants.empty());
    for (auto& it : deducedConstants)
    {
        Instruction* inst = it.first;
        ReleaseAssert(!inst->getType()->isVoidTy());
        ReleaseAssert(!replacementInstructionSet.count(inst));
        ReleaseAssert(inst->getParent() != nullptr);

        Constant* cst = it.second;
        if (cst == nullptr)
        {
            continue;
        }

        ReleaseAssert(!m_provenConstants.count(inst));
        m_provenConstants[inst] = cst;
    }

    // Record proven precondition for each type check
    //
    ReleaseAssert(m_provenPreconditionForTypeChecks.empty());
    for (size_t listOrd = 0; listOrd < m_operandList.size(); listOrd++)
    {
        std::vector<TValueTypecheckInstData*>& q = targetedAPICalls[listOrd];

        for (TValueTypecheckInstData* item : q)
        {
            CallInst* inst = item->GetCallInst();
            ReleaseAssert(!deducedConstants.count(inst));

            BasicBlock* bb = inst->getParent();
            ReleaseAssert(bb != nullptr);
            ReleaseAssert(possibleMasks.count(bb));
            ReleaseAssert(isBasicBlockReachable.count(bb));

            ReleaseAssert(listOrd < possibleMasks[bb].size());
            TypeMaskTy possibleMaskForThisInst = possibleMasks[bb][listOrd];
            ReleaseAssert(0 <= possibleMaskForThisInst && possibleMaskForThisInst <= x_typeMaskFor<tBoxedValueTop>);

            ReleaseAssertIff(possibleMaskForThisInst == 0, !isBasicBlockReachable[bb]);

            ReleaseAssert(!m_provenPreconditionForTypeChecks.count(inst));
            m_provenPreconditionForTypeChecks[inst] = possibleMaskForThisInst;
        }
    }

    for (auto& dataList : targetedAPICalls)
    {
        for (TValueTypecheckInstData* item : dataList)
        {
            delete item;
        }
    }
    targetedAPICalls.clear();

    // Record unreachable basic blocks
    //
    ReleaseAssert(m_unreachableBBList.empty());
    for (auto& it : possibleMasks)
    {
        BasicBlock* bb = it.first;
        std::vector<TypeMaskTy>& maskList = it.second;
        bool foundNonZeroMask = false;
        bool foundZeroMask = false;
        for (TypeMaskTy mask : maskList)
        {
            if (mask == 0)
            {
                foundZeroMask = true;
            }
            else
            {
                foundNonZeroMask = true;
            }
        }
        // Exactly one of 'foundNonZeroMask' and 'foundZeroMask' should be true
        //
        ReleaseAssert(foundNonZeroMask ^ foundZeroMask);

        ReleaseAssert(isBasicBlockReachable.count(bb));
        ReleaseAssert(isBasicBlockReachable[bb] == foundNonZeroMask);

        if (foundZeroMask)
        {
            ReleaseAssert(!m_unreachableBBList.count(bb));
            m_unreachableBBList.insert(bb);
        }
    }

    // Record all feasible edges
    //
    ReleaseAssert(m_isControlFlowEdgeFeasible.empty());
    m_isControlFlowEdgeFeasible = isControlFlowEdgeFeasible;
}

static std::vector<TypecheckStrengthReductionCandidate> WARN_UNUSED ParseTypecheckStrengthReductionCandidateList(llvm::Module* module)
{
    using namespace llvm;
    std::vector<TypecheckStrengthReductionCandidate> result;

    Constant* defList;
    {
        Constant* wrappedDefList = GetConstexprGlobalValue(module, "x_list_of_tvalue_typecheck_strength_reduction_rules");
        LLVMConstantStructReader reader(module, wrappedDefList);
        defList = reader.Dewrap();
    }

    LLVMConstantArrayReader listReader(module, defList);
    size_t totalElements = listReader.GetNumElements<tvalue_typecheck_strength_reduction_rule>();
    for (size_t i = 0; i < totalElements; i++)
    {
        LLVMConstantStructReader reader(module, listReader.Get<tvalue_typecheck_strength_reduction_rule>(i));
        TypeMaskTy checkedMask = reader.GetValue<&tvalue_typecheck_strength_reduction_rule::m_typeToCheck>();
        TypeMaskTy precondMask = reader.GetValue<&tvalue_typecheck_strength_reduction_rule::m_typePrecondition>();
        Constant* impl = reader.Get<&tvalue_typecheck_strength_reduction_rule::m_implementation>();
        Function* implFunc = dyn_cast<Function>(impl);
        ReleaseAssert(implFunc != nullptr);
        ReleaseAssert(IsTValueTypeCheckAPIFunction(implFunc) || IsTValueTypeCheckStrengthReductionFunction(implFunc));

        size_t estimatedCost = reader.GetValue<&tvalue_typecheck_strength_reduction_rule::m_estimatedCost>();
        ReleaseAssert((checkedMask & precondMask) == checkedMask);

        ReleaseAssert(llvm_type_has_type<bool>(implFunc->getReturnType()));
        ReleaseAssert(implFunc->arg_size() == 1);
        ReleaseAssert(llvm_type_has_type<uint64_t>(implFunc->getArg(0)->getType()));

        result.push_back({
            .m_checkedMask = checkedMask,
            .m_precondMask = precondMask,
            .m_func = implFunc,
            .m_estimatedCost = estimatedCost
        });
    }

    return result;
}

TypeCheckFunctionSelector::TypeCheckFunctionSelector(llvm::Module* module)
{
    m_candidateList = std::make_unique<std::vector<TypecheckStrengthReductionCandidate>>(ParseTypecheckStrengthReductionCandidateList(module));
}

TypeCheckFunctionSelector::TypeCheckFunctionSelector(const dfg::TypeCheckerMethodCostInfo* infoList, size_t numItems)
{
    m_candidateList = std::make_unique<std::vector<TypecheckStrengthReductionCandidate>>();
    m_candidateList->resize(numItems);
    for (size_t i = 0; i < numItems; i++)
    {
        (*m_candidateList)[i] = {
            .m_checkedMask = infoList[i].m_checkMask,
            .m_precondMask = infoList[i].m_precondMask,
            .m_func = nullptr,
            .m_estimatedCost = infoList[i].m_cost
        };
    }
}

TypeCheckFunctionSelector::~TypeCheckFunctionSelector() { }

// This function only handles the case where the check cannot be reduced to false to true
//
std::pair<TypecheckStrengthReductionCandidate*, size_t /*cost*/> TypeCheckFunctionSelector::FindBestStrengthReduction(TypeMaskTy checkedMask, TypeMaskTy precondMask)
{
    using namespace llvm;
    ReleaseAssert((checkedMask & precondMask) == checkedMask);
    ReleaseAssert(0 < checkedMask && checkedMask < precondMask);
    size_t minimumCost = static_cast<size_t>(-1);
    TypecheckStrengthReductionCandidate* result = nullptr;
    for (TypecheckStrengthReductionCandidate& candidate : *m_candidateList.get())
    {
        if (candidate.CanBeUsedToImplement(checkedMask, precondMask))
        {
            if (candidate.m_estimatedCost < minimumCost)
            {
                minimumCost = candidate.m_estimatedCost;
                result = &candidate;
            }
        }
    }
    return std::make_pair(result, minimumCost);
}

TypeCheckFunctionSelector::QueryResult WARN_UNUSED TypeCheckFunctionSelector::Query(TypeMaskTy maskToCheck, TypeMaskTy preconditionMask)
{
    using namespace llvm;
    maskToCheck &= preconditionMask;
    if (maskToCheck == 0)
    {
        return { .m_opKind = QueryResult::TriviallyFalse, .m_func = nullptr, .m_info = nullptr };
    }
    else if (maskToCheck == preconditionMask)
    {
        return { .m_opKind = QueryResult::TriviallyTrue, .m_func = nullptr, .m_info = nullptr };
    }
    else
    {
        // Do strength reduction
        // We have two strategies:
        // 1. Find the best candidate for (checkedMask, provenPreconditionMask)
        // 2. Find the best candidate for (checkedMask ^ provenPreconditionMask, provenPreconditionMask) and flip the result
        //
        std::pair<TypecheckStrengthReductionCandidate*, size_t> c1 = FindBestStrengthReduction(maskToCheck, preconditionMask);
        std::pair<TypecheckStrengthReductionCandidate*, size_t> c2 = FindBestStrengthReduction(maskToCheck ^ preconditionMask, preconditionMask);

        if (c1.second == static_cast<size_t>(-1) && c2.second == static_cast<size_t>(-1))
        {
            return { .m_opKind = QueryResult::NoSolutionFound, .m_func = nullptr, .m_info = nullptr };
        }

        if (c1.second <= c2.second)
        {
            ReleaseAssert(c1.first != nullptr);
            return { .m_opKind = QueryResult::CallFunction, .m_func = c1.first->m_func, .m_info = c1.first };
        }
        else
        {
            ReleaseAssert(c2.first != nullptr);
            return { .m_opKind = QueryResult::CallFunctionAndFlipResult, .m_func = c2.first->m_func, .m_info = c2.first };
        }
    }
}

void TValueTypecheckOptimizationPass::DoOptimization()
{
    using namespace llvm;
    ReleaseAssert(m_didAnalysis && !m_didOptimization);
    m_didOptimization = true;

    if (m_operandList.size() == 0)
    {
        return;
    }

    LLVMContext& ctx = m_targetFunction->getContext();

    TypeCheckFunctionSelector tcFnSelector(m_targetFunction->getParent());

    // Do strength reduction on the TValue typechecks
    //
    for (auto& it : m_provenPreconditionForTypeChecks)
    {
        CallInst* callInst = it.first;
        TypeMaskTy provenPreconditionMask = it.second;

        ReleaseAssert(callInst->getCalledFunction() != nullptr);
        ReleaseAssert(llvm_type_has_type<bool>(callInst->getCalledFunction()->getReturnType()));
        ReleaseAssert(callInst->getCalledFunction()->arg_size() == 1);
        ReleaseAssert(llvm_type_has_type<uint64_t>(callInst->getCalledFunction()->getArg(0)->getType()));

        TypeMaskTy checkedMask;
        ReleaseAssert(IsTValueTypeCheckAPIFunction(callInst->getCalledFunction(), &checkedMask /*out*/));

        TypeCheckFunctionSelector::QueryResult res = tcFnSelector.Query(checkedMask, provenPreconditionMask);

        switch (res.m_opKind)
        {
        case TypeCheckFunctionSelector::QueryResult::NoSolutionFound:
        {
            ReleaseAssert(false);
        }
        case TypeCheckFunctionSelector::QueryResult::TriviallyFalse:
        {
            // Replace to false
            //
            ReplaceInstructionWithValue(callInst, CreateLLVMConstantInt<bool>(ctx, false));
            break;
        }
        case TypeCheckFunctionSelector::QueryResult::TriviallyTrue:
        {
            // Replace to true
            //
            ReplaceInstructionWithValue(callInst, CreateLLVMConstantInt<bool>(ctx, true));
            break;
        }
        case TypeCheckFunctionSelector::QueryResult::CallFunction:
        {
            ReleaseAssert(res.m_func != nullptr);
            callInst->setCalledFunction(res.m_func);
            break;
        }
        case TypeCheckFunctionSelector::QueryResult::CallFunctionAndFlipResult:
        {
            ReleaseAssert(res.m_func != nullptr);
            callInst->setCalledFunction(res.m_func);
            // We cannot directly insert %1 = not %callInst and RAUW since that would replace the operand of our 'not' instruction
            // So we create the 'not' instruction with a fake operand, then RAUW the callInst, then change the operand of 'not' to callInst
            //
            Instruction* xorInst = BinaryOperator::CreateXor(CreateLLVMConstantInt<bool>(ctx, true), CreateLLVMConstantInt<bool>(ctx, true));
            xorInst->insertAfter(callInst);
            callInst->replaceAllUsesWith(xorInst);
            xorInst->setOperand(0, callInst);
            break;
        }
        } /*switch opKind*/
    }

    // Replace instruction to constants
    //
    for (auto& it : m_provenConstants)
    {
        Instruction* inst = it.first;
        Constant* cst = it.second;

        ReleaseAssert(!m_unreachableBBList.count(inst->getParent()));
        inst->replaceAllUsesWith(cst);

        if (SCCPPassCanRemoveInstruction(inst))
        {
            inst->eraseFromParent();
        }
    }

    // Remove dead basic blocks
    //
    for (BasicBlock* bb : m_unreachableBBList)
    {
        changeToUnreachable(bb->getFirstNonPHI());
    }


    // Remove infeasible control flow edges
    //
    // Collect all basic block beforehand, because we may create a new "default.unreachable" BB and invalidate iterators
    //
    BasicBlock* defaultUnreachableBB = nullptr;
    std::vector<BasicBlock*> bbWorkList;
    for (BasicBlock& bb : *m_targetFunction)
    {
        bbWorkList.push_back(&bb);
    }

    for (BasicBlock* bb : bbWorkList)
    {
        ReleaseAssert(m_isControlFlowEdgeFeasible.count(bb));
        auto& info = m_isControlFlowEdgeFeasible[bb];

        std::unordered_set<BasicBlock*> feasibleSuccs;
        bool hasNonFeasibleEdges = false;
        for (BasicBlock* succ : successors(bb))
        {
            ReleaseAssert(info.count(succ));
            if (info[succ])
            {
                feasibleSuccs.insert(succ);
            }
            else
            {
                hasNonFeasibleEdges = true;
            }
        }

        if (!hasNonFeasibleEdges)
        {
            continue;
        }

        // Logic stolen from removeNonFeasibleEdges
        //
        // SCCP can only determine non-feasible edges for br, switch and indirectbr.
        //
        Instruction* ti = bb->getTerminator();
        ReleaseAssert(isa<BranchInst>(ti) || isa<SwitchInst>(ti) || isa<IndirectBrInst>(ti));

        if (feasibleSuccs.size() == 0)
        {
            // Branch on undef/poison, replace with unreachable.
            //
            for (BasicBlock *Succ : successors(bb))
            {
                Succ->removePredecessor(bb);
            }
            ti->eraseFromParent();
            new UnreachableInst(ctx, bb);
        }
        else if (feasibleSuccs.size() == 1)
        {
            // Replace with an unconditional branch to the only feasible successor.
            //
            BasicBlock* onlyFeasibleSuccessor = *feasibleSuccs.begin();
            bool haveSeenOnlyFeasibleSuccessor = false;
            for (BasicBlock* succ : successors(bb))
            {
                if (succ == onlyFeasibleSuccessor && !haveSeenOnlyFeasibleSuccessor)
                {
                    haveSeenOnlyFeasibleSuccessor = true;
                    continue;
                }
                succ->removePredecessor(bb);
            }
            BranchInst::Create(onlyFeasibleSuccessor, bb);
            ti->eraseFromParent();
        }
        else if (feasibleSuccs.size() > 1)
        {
            ReleaseAssert(dyn_cast<SwitchInst>(ti) != nullptr);
            SwitchInstProfUpdateWrapper si(*cast<SwitchInst>(ti));

            BasicBlock* defaultDest = si->getDefaultDest();
            if (!feasibleSuccs.contains(defaultDest))
            {
                if (defaultUnreachableBB == nullptr)
                {
                    defaultUnreachableBB = BasicBlock::Create(ctx, "default.unreachable", m_targetFunction, defaultDest);
                    new UnreachableInst(ctx, defaultUnreachableBB);
                }

                // TODO: I'm a bit confused, why do we not need to do removePredecessor here?
                //
                si->setDefaultDest(defaultUnreachableBB);
            }

            for (auto ci = si->case_begin(); ci != si->case_end();)
            {
                if (feasibleSuccs.contains(ci->getCaseSuccessor()))
                {
                    ++ci;
                    continue;
                }

                BasicBlock* succ = ci->getCaseSuccessor();
                succ->removePredecessor(bb);
                si.removeCase(ci);
                // Don't increment CI, as we removed a case.
            }
        }
    }

    // Make sure we didn't screw up anything
    //
    ValidateLLVMFunction(m_targetFunction);
}

static bool ShouldAddConstraintForTop(BytecodeVariantDefinition* bvd)
{
    // Even adding a constraint for 'tBoxedValueTop' can be helpful if the user code already contains redundant type checks
    // But that could further blow up the total combinations, so we only do it if there are few operands
    //
    size_t totalTValueOperands = 0;
    for (std::unique_ptr<BcOperand>& operand : bvd->m_list)
    {
        if (operand->GetKind() == BcOperandKind::Constant || operand->GetKind() == BcOperandKind::Slot)
        {
            totalTValueOperands++;
        }
    }
    return totalTValueOperands <= 3;
}

struct ConstraintAndOperandList
{
    std::unique_ptr<TValueTypecheckOptimizationPass::Constraint> m_constraint;
    std::vector<uint32_t> m_operandList;
};

static ConstraintAndOperandList WARN_UNUSED CreateBaseConstraint(BytecodeVariantDefinition* bvd, bool forQuickeningFastPath)
{
    using AndConstraint = TValueTypecheckOptimizationPass::AndConstraint;
    using LeafConstraint = TValueTypecheckOptimizationPass::LeafConstraint;

    bool addConstraintForTop = ShouldAddConstraintForTop(bvd);

    if (forQuickeningFastPath)
    {
        ReleaseAssert(bvd->m_quickeningKind == BytecodeQuickeningKind::LockedQuickening || bvd->m_quickeningKind == BytecodeQuickeningKind::Quickened || bvd->m_quickeningKind == BytecodeQuickeningKind::QuickeningSelector);
    }

    std::vector<uint32_t> operandList;
    std::unique_ptr<AndConstraint> constraint = std::make_unique<AndConstraint>();
    for (std::unique_ptr<BcOperand>& operand : bvd->m_list)
    {
        if (operand->GetKind() != BcOperandKind::Constant && operand->GetKind() != BcOperandKind::Slot)
        {
            continue;
        }

        TypeMaskTy baseMask;
        if (operand->GetKind() == BcOperandKind::Constant)
        {
            ReleaseAssert(!bvd->m_isDfgVariant);

            BcOpConstant* op = assert_cast<BcOpConstant*>(operand.get());
            baseMask = op->m_typeMask;
        }
        else
        {
            ReleaseAssert(operand->GetKind() == BcOperandKind::Slot);
            baseMask = x_typeMaskFor<tBoxedValueTop>;

            BcOpSlot* op = assert_cast<BcOpSlot*>(operand.get());
            if (op->HasDfgSpeculation())
            {
                ReleaseAssert(bvd->m_isDfgVariant);
                baseMask = op->GetDfgSpecMask();
                ReleaseAssert(baseMask <= x_typeMaskFor<tBoxedValueTop>);
            }
        }

        TypeMaskTy quickeningMask;
        bool hasQuickeningData = false;

        if (forQuickeningFastPath)
        {
            for (auto& quickeningInfo : bvd->m_quickening)
            {
                if (quickeningInfo.m_operandOrd == operand->OperandOrdinal())
                {
                    ReleaseAssert(!hasQuickeningData);
                    hasQuickeningData = true;
                    quickeningMask = quickeningInfo.m_speculatedMask;
                    ReleaseAssert(0 < quickeningMask && quickeningMask < baseMask);
                    ReleaseAssert((quickeningMask & baseMask) == quickeningMask);
                }
            }
        }

        // If we are not emitting assumption for the quickening fast path, or we are emitting for fast path but
        // do not have a quickening for this operand, emit the base assumption
        //
        if (!hasQuickeningData)
        {
            if (addConstraintForTop || baseMask != x_typeMaskFor<tBoxedValueTop>)
            {
                constraint->AddClause(std::make_unique<LeafConstraint>(operand->OperandOrdinal(), baseMask /*allowedMask*/));
                operandList.push_back(static_cast<uint32_t>(operand->OperandOrdinal()));
            }
        }
        else
        {
            ReleaseAssert(forQuickeningFastPath);
            constraint->AddClause(std::make_unique<LeafConstraint>(operand->OperandOrdinal(), quickeningMask /*allowedMask*/));
            operandList.push_back(static_cast<uint32_t>(operand->OperandOrdinal()));
        }
    }

    ConstraintAndOperandList r;
    r.m_constraint = std::move(constraint);
    r.m_operandList = operandList;
    return r;
}

void TValueTypecheckOptimizationPass::DoOptimizationForBytecode(BytecodeVariantDefinition* bvd, llvm::Function* implFunction)
{
    using namespace llvm;
    TValueTypecheckOptimizationPass pass;
    pass.SetTargetFunction(implFunction);

    ConstraintAndOperandList r = CreateBaseConstraint(bvd, false /*forQuickeningFastPath*/);
    pass.SetOperandList(r.m_operandList);
    pass.SetConstraint(std::move(r.m_constraint));

    pass.Run();
}

void TValueTypecheckOptimizationPass::DoOptimizationForBytecodeQuickeningFastPath(BytecodeVariantDefinition* bvd, llvm::Function* implFunction)
{
    using namespace llvm;
    TValueTypecheckOptimizationPass pass;
    pass.SetTargetFunction(implFunction);

    ConstraintAndOperandList r = CreateBaseConstraint(bvd, true /*forQuickeningFastPath*/);
    pass.SetOperandList(r.m_operandList);
    pass.SetConstraint(std::move(r.m_constraint));

    pass.Run();
}

void TValueTypecheckOptimizationPass::DoOptimizationForBytecodeQuickeningSlowPath(BytecodeVariantDefinition* bvd, llvm::Function* implFunction)
{
    using namespace llvm;
    TValueTypecheckOptimizationPass pass;
    pass.SetTargetFunction(implFunction);

    ConstraintAndOperandList b = CreateBaseConstraint(bvd, false /*forQuickeningFastPath*/);
    ConstraintAndOperandList e = CreateBaseConstraint(bvd, true /*forQuickeningFastPath*/);

    // Create joint condition 'b & !e'
    //
    std::unique_ptr<AndConstraint> constraint = std::make_unique<AndConstraint>();
    constraint->AddClause(std::move(b.m_constraint));

    std::unique_ptr<NotConstraint> notE = std::make_unique<NotConstraint>(std::move(e.m_constraint));
    constraint->AddClause(std::move(notE));

    // The operand list of the joint condition should be the union of the two subconditions
    //
    std::set<uint32_t> allOperandOrdSet;
    for (uint32_t v : b.m_operandList)
    {
        allOperandOrdSet.insert(v);
    }
    for (uint32_t v : e.m_operandList)
    {
        allOperandOrdSet.insert(v);
    }
    std::vector<uint32_t> allOperandOrds;
    for (uint32_t v : allOperandOrdSet)
    {
        allOperandOrds.push_back(v);
    }

    pass.SetOperandList(allOperandOrds);
    pass.SetConstraint(std::move(constraint));

    pass.Run();
}

TypeMaskTy WARN_UNUSED GetCheckedMaskOfTValueTypecheckFunction(llvm::Function* func)
{
    using namespace llvm;
    TypeMaskTy res = 0;
    if (IsTValueTypeCheckAPIFunction(func, &res /*out*/))
    {
        return res;
    }

    ReleaseAssert(IsTValueTypeCheckStrengthReductionFunction(func));
    bool found = false;
    TypeCheckFunctionSelector strengthReductionList(func->getParent());
    for (const TypecheckStrengthReductionCandidate& candidate : strengthReductionList.GetStrengthReductionList())
    {
        if (candidate.m_func == func)
        {
            ReleaseAssert(!found);
            found = true;
            res = candidate.m_checkedMask;
        }
    }
    ReleaseAssert(found);
    return res;
}

static TypemaskOverapproxAutomataGenerator WARN_UNUSED BuildAutomataForSelectTypeCheckFn(const std::vector<TypecheckStrengthReductionCandidate>& list, TypeMask maskToCheck)
{
    ReleaseAssert(maskToCheck.SubsetOf(x_typeMaskFor<tBoxedValueTop>));

    // Edge case: maskToCheck = tBoxedValueTop. If precond is tBottom, should return UseKind_Unreachable, otherwise UseKind_Untyped
    //
    if (maskToCheck == x_typeMaskFor<tBoxedValueTop>)
    {
        TypemaskOverapproxAutomataGenerator gen;
        gen.AddItem(x_typeMaskFor<tBottom>, dfg::UseKind_Unreachable);
        gen.AddItem(x_typeMaskFor<tBoxedValueTop>, dfg::UseKind_Untyped);
        return gen;
    }

    // Edge case: maskToCheck = tBottom. If precond is tBottom, should return UseKind_Unreachable, otherwise UseKind_AlwaysOsrExit
    //
    if (maskToCheck == x_typeMaskFor<tBottom>)
    {
        TypemaskOverapproxAutomataGenerator gen;
        gen.AddItem(x_typeMaskFor<tBottom>, dfg::UseKind_Unreachable);
        gen.AddItem(x_typeMaskFor<tBoxedValueTop>, dfg::UseKind_AlwaysOsrExit);
        return gen;
    }

    MinCostTypemaskOverapproxAutomataGenerator mcaGen;

    uint16_t firstUnprovenUseKind = static_cast<uint16_t>(dfg::UseKind_FirstUnprovenUseKind);

    for (size_t ruleIdx = 0; ruleIdx < list.size(); ruleIdx++)
    {
        const TypecheckStrengthReductionCandidate& rule = list[ruleIdx];

        TypeMask S = maskToCheck;
        TypeMask P = rule.m_precondMask;
        TypeMask Q = rule.m_checkedMask;
        Q = Q.Cap(P);

        //       P-----------------------P
        //       |                       |
        //       |     Q-----------Q     |
        //       |     |           |     |
        //   S---+-----+-----S     |     |
        //   |   |     |     |     |     |
        //   |   |  A  |  B  |  C  |  D  |
        //   |   |     |     |     |     |
        //   S---+-----+-----S     |     |
        //       |     |           |     |
        //       |     Q-----------Q     |
        //       |                       |
        //       P-----------------------P
        //
        // Given precondition mask M and that we want to check S under the precondition M,
        // 'rule' can be used if M \subset P, and, Q \cap M = S \cap M
        // This translates to M \subset B \cup D in the figure above, or (Q \cap S) \cup (P - (Q \cup S)).
        //
        // 'not rule' can be used if M \subset P, and, (P - Q) \cap M = S \cap M
        // This translates to M \subset A \cup C in the figure above, or (S \cap (P - Q)) \cup (Q - S)
        //

        ReleaseAssert(ruleIdx <= 10000);
        ReleaseAssert(rule.m_estimatedCost < (1ULL << 60));

        // Use same rule to break ties in m_cost as in TypeCheckFunctionSelector::Query,
        // that is, the "rule" version with cost c better than "not rule" version with cost c better than "rule" version with cost c+1
        //
        // We add 2 to all costs, since cost 0 and 1 are reserved for trivial results
        //
        mcaGen.AddItem(
            Q.Cap(S).Cup(P.Subtract(Q.Cup(S))),
            SafeIntegerCast<uint16_t>(firstUnprovenUseKind + ruleIdx * 2) /*identValue*/,
            rule.m_estimatedCost * 2 + 2 /*cost*/);

        mcaGen.AddItem(
            S.Cap(P.Subtract(Q)).Cup(Q.Subtract(S)),
            SafeIntegerCast<uint16_t>(firstUnprovenUseKind + ruleIdx * 2 + 1) /*identValue*/,
            rule.m_estimatedCost * 2 + 3 /*cost*/);
    }

    uint16_t triviallyTrueUseKind = static_cast<uint16_t>(-1);
    ReleaseAssert(x_list_of_type_speculation_masks.size() > 2);
    ReleaseAssert(x_list_of_type_speculation_masks[0] == x_typeMaskFor<tBoxedValueTop>);
    ReleaseAssert(x_list_of_type_speculation_masks.back() == x_typeMaskFor<tBottom>);
    for (size_t idx = 0; idx < x_list_of_type_speculation_masks.size(); idx++)
    {
        TypeMask item = x_list_of_type_speculation_masks[idx];
        if (item == maskToCheck)
        {
            ReleaseAssert(triviallyTrueUseKind == static_cast<uint16_t>(-1));
            ReleaseAssert(0 < idx && idx + 1 < x_list_of_type_speculation_masks.size());
            triviallyTrueUseKind = SafeIntegerCast<uint16_t>(dfg::UseKind_FirstProvenUseKind + idx - 1);
        }
    }
    ReleaseAssert(triviallyTrueUseKind != static_cast<uint16_t>(-1));
    ReleaseAssert(dfg::UseKind_FirstProvenUseKind <= triviallyTrueUseKind && triviallyTrueUseKind < dfg::UseKind_FirstUnprovenUseKind);

    // If the precondition is a subset of maskToCheck, result is trivially true
    //
    mcaGen.AddItem(
        maskToCheck,
        triviallyTrueUseKind /*identValue*/,
        1 /*cost*/);

    // If the precondition is disjoint with maskToCheck, result is trivially false
    //
    mcaGen.AddItem(
        x_typeMaskFor<tBoxedValueTop> ^ maskToCheck.m_mask /*mask*/,
        dfg::UseKind_AlwaysOsrExit /*identValue*/,
        1 /*cost*/);

    // Special case: if precondition is tBottom, should return UseKind_Unreachable
    //
    mcaGen.AddItem(
        x_typeMaskFor<tBottom> /*mask*/,
        dfg::UseKind_Unreachable /*identValue*/,
        0 /*cost*/);

    return mcaGen.GetAutomata();
}

std::vector<uint8_t> WARN_UNUSED DfgSelectTypeCheckFnAutomataGenerator::BuildAutomata(TypeMask maskToCheck, size_t* depthOfGeneratedAutomata /*out*/)
{
    TypemaskOverapproxAutomataGenerator gen = BuildAutomataForSelectTypeCheckFn(m_infoList, maskToCheck);
    return gen.GenerateAutomata(depthOfGeneratedAutomata /*out*/);
}

}   // namespace dast
