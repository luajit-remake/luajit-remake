#include "tvalue_typecheck_optimization.h"
#include "llvm/Transforms/Utils/SCCPSolver.h"
#include "llvm/Transforms/Utils/Local.h"
#include "llvm/ADT/Triple.h"
#include "llvm/Analysis/ValueLattice.h"

#include "deegen_bytecode_operand.h"

namespace dast {

bool IsTValueTypeCheckAPIFunction(llvm::Function* func, TypeSpeculationMask* typeMask /*out*/)
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

            bool found = false;
            constexpr auto defs = detail::get_type_speculation_defs<TypeSpecializationList>::value;
            for (size_t i = 0; i < defs.size(); i++)
            {
                if (demangledName == defs[i].second)
                {
                    *typeMask = defs[i].first;
                    found = true;
                    break;
                }
            }
            ReleaseAssert(found);
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
    if (!IsTValueTypeCheckAPIFunction(func) && !IsTValueTypeCheckStrengthReductionFunction(func))
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
    TValueTypecheckInstData(llvm::CallInst* inst, TypeSpeculationMask mask)
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

    TypeSpeculationMask GetMask()
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

        BasicBlock::iterator bi(fromInst);
        BasicBlock::InstListType& bil = fromInst->getParent()->getInstList();
        bil.insert(bi, toInst);

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
    TypeSpeculationMask m_mask;
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

    constexpr size_t numTypes = x_numUsefulBitsInTypeSpeculationMask;
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
                TypeSpeculationMask mask;
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
    std::unordered_map<BasicBlock*, std::vector<TypeSpeculationMask>> possibleMasks;
    for (BasicBlock& bb : *m_targetFunction)
    {
        ReleaseAssert(!possibleMasks.count(&bb));
        std::vector<TypeSpeculationMask>& v = possibleMasks[&bb];
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
                                        ? solver.getConstant(V)
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

            result = LLVMValueLatticeElementIsConstant(IV) ? solver.getConstant(IV) : UndefValue::get(inst->getType());
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

    std::vector<TypeSpeculationMask> currentComb;
    currentComb.resize(m_operandList.size());
    for (size_t curCombVal = 0; curCombVal < enumMax; curCombVal++)
    {
        // Create the current type combination to test
        //
        {
            std::unordered_map<uint32_t, TypeSpeculationMask> val;
            size_t tmp = curCombVal;
            for (size_t i = 0; i < currentComb.size(); i++)
            {
                size_t curTypeOrd = tmp % numTypes;
                currentComb[i] = SingletonBitmask<TypeSpeculationMask>(curTypeOrd);
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
            TypeSpeculationMask currentTyMask = currentComb[listOrd];

            std::vector<TValueTypecheckInstData*>& q = targetedAPICalls[listOrd];
            for (TValueTypecheckInstData* item : q)
            {
                TypeSpeculationMask passed = item->GetMask() & currentTyMask;
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
                std::vector<TypeSpeculationMask>& dst = possibleMasks[&bb];
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
            TypeSpeculationMask possibleMaskForThisInst = possibleMasks[bb][listOrd];
            ReleaseAssert(0 <= possibleMaskForThisInst && possibleMaskForThisInst <= x_typeSpeculationMaskFor<tTop>);

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
        std::vector<TypeSpeculationMask>& maskList = it.second;
        bool foundNonZeroMask = false;
        bool foundZeroMask = false;
        for (TypeSpeculationMask mask : maskList)
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

struct TypecheckStrengthReductionCandidate
{
    TypeSpeculationMask m_checkedMask;
    TypeSpeculationMask m_precondMask;
    llvm::Function* m_func;
    size_t m_estimatedCost;

    bool WARN_UNUSED CanBeUsedToImplement(TypeSpeculationMask desiredCheckedMask, TypeSpeculationMask knownPreconditionMask)
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
        TypeSpeculationMask checkedMask = reader.GetValue<&tvalue_typecheck_strength_reduction_rule::m_typeToCheck>();
        TypeSpeculationMask precondMask = reader.GetValue<&tvalue_typecheck_strength_reduction_rule::m_typePrecondition>();
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

    std::vector<TypecheckStrengthReductionCandidate> allStrengthReductionCandidates = ParseTypecheckStrengthReductionCandidateList(m_targetFunction->getParent());

    // This function only handles the case where the check cannot be reduced to false to true
    //
    auto findBestStrengthReduction = [&](TypeSpeculationMask checkedMask, TypeSpeculationMask precondMask) -> std::pair<Function*, size_t /*cost*/>
    {
        ReleaseAssert((checkedMask & precondMask) == checkedMask);
        ReleaseAssert(0 < checkedMask && checkedMask < precondMask);
        size_t minimumCost = static_cast<size_t>(-1);
        Function* result = nullptr;
        for (TypecheckStrengthReductionCandidate& candidate : allStrengthReductionCandidates)
        {
            if (candidate.CanBeUsedToImplement(checkedMask, precondMask))
            {
                if (candidate.m_estimatedCost < minimumCost)
                {
                    minimumCost = candidate.m_estimatedCost;
                    result = candidate.m_func;
                }
            }
        }
        return std::make_pair(result, minimumCost);
    };

    // Do strength reduction on the TValue typechecks
    //
    for (auto& it : m_provenPreconditionForTypeChecks)
    {
        CallInst* callInst = it.first;
        TypeSpeculationMask provenPreconditionMask = it.second;

        ReleaseAssert(callInst->getCalledFunction() != nullptr);
        ReleaseAssert(llvm_type_has_type<bool>(callInst->getCalledFunction()->getReturnType()));
        ReleaseAssert(callInst->getCalledFunction()->arg_size() == 1);
        ReleaseAssert(llvm_type_has_type<uint64_t>(callInst->getCalledFunction()->getArg(0)->getType()));

        TypeSpeculationMask checkedMask;
        ReleaseAssert(IsTValueTypeCheckAPIFunction(callInst->getCalledFunction(), &checkedMask /*out*/));

        checkedMask &= provenPreconditionMask;
        if (checkedMask == 0)
        {
            // Replace to false
            //
            ReplaceInstructionWithValue(callInst, CreateLLVMConstantInt<bool>(ctx, false));
        }
        else if (checkedMask == provenPreconditionMask)
        {
            // Replace to true
            //
            ReplaceInstructionWithValue(callInst, CreateLLVMConstantInt<bool>(ctx, true));
        }
        else
        {
            // Do strength reduction
            // We have two strategies:
            // 1. Find the best candidate for (checkedMask, provenPreconditionMask)
            // 2. Find the best candidate for (checkedMask ^ provenPreconditionMask, provenPreconditionMask) and flip the result
            //
            std::pair<Function*, size_t> choice1 = findBestStrengthReduction(checkedMask, provenPreconditionMask);
            std::pair<Function*, size_t> choice2 = findBestStrengthReduction(checkedMask ^ provenPreconditionMask, provenPreconditionMask);
            if (choice1.second <= choice2.second)
            {
                Function* newCallee = choice1.first;
                ReleaseAssert(newCallee != nullptr);
                callInst->setCalledFunction(newCallee);
            }
            else
            {
                Function* newCallee = choice2.first;
                ReleaseAssert(newCallee != nullptr);
                callInst->setCalledFunction(newCallee);
                // We cannot directly insert %1 = not %callInst and RAUW since that would replace the operand of our 'not' instruction
                // So we create the 'not' instruction with a fake operand, then RAUW the callInst, then change the operand of 'not' to callInst
                //
                Instruction* xorInst = BinaryOperator::CreateXor(CreateLLVMConstantInt<bool>(ctx, true), CreateLLVMConstantInt<bool>(ctx, true));
                xorInst->insertAfter(callInst);
                callInst->replaceAllUsesWith(xorInst);
                xorInst->setOperand(0, callInst);
            }
        }
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
    BasicBlock* defaultUnreachableBB = nullptr;
    for (BasicBlock& bb : *m_targetFunction)
    {
        ReleaseAssert(m_isControlFlowEdgeFeasible.count(&bb));
        auto& info = m_isControlFlowEdgeFeasible[&bb];

        std::unordered_set<BasicBlock*> feasibleSuccs;
        bool hasNonFeasibleEdges = false;
        for (BasicBlock* succ : successors(&bb))
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
        Instruction* ti = bb.getTerminator();
        ReleaseAssert(isa<BranchInst>(ti) || isa<SwitchInst>(ti) || isa<IndirectBrInst>(ti));

        if (feasibleSuccs.size() == 0)
        {
            // Branch on undef/poison, replace with unreachable.
            //
            for (BasicBlock *Succ : successors(&bb))
            {
                Succ->removePredecessor(&bb);
            }
            ti->eraseFromParent();
            new UnreachableInst(ctx, &bb);
        }
        else if (feasibleSuccs.size() == 1)
        {
            // Replace with an unconditional branch to the only feasible successor.
            //
            BasicBlock* onlyFeasibleSuccessor = *feasibleSuccs.begin();
            bool haveSeenOnlyFeasibleSuccessor = false;
            for (BasicBlock* succ : successors(&bb))
            {
                if (succ == onlyFeasibleSuccessor && !haveSeenOnlyFeasibleSuccessor)
                {
                    haveSeenOnlyFeasibleSuccessor = true;
                    continue;
                }
                succ->removePredecessor(&bb);
            }
            BranchInst::Create(onlyFeasibleSuccessor, &bb);
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
                succ->removePredecessor(&bb);
                si.removeCase(ci);
                // Don't increment CI, as we removed a case.
            }
        }
    }

    // Make sure we didn't screw up anything
    //
    ValidateLLVMFunction(m_targetFunction);
}


void TValueTypecheckOptimizationPass::DoOptimizationForBytecode(BytecodeVariantDefinition* bvd, llvm::Function* implFunction)
{
    using namespace llvm;
    TValueTypecheckOptimizationPass pass;
    pass.SetTargetFunction(implFunction);

    // Even adding a constraint for 'tTop' can be helpful if the user code already contains redundant type checks
    // But that could further blow up the total combinations, so we only do it if there are few operands
    //
    bool addConstraintForTop = false;
    {
        size_t totalTValueOperands = 0;
        for (std::unique_ptr<BcOperand>& operand : bvd->m_list)
        {
            if (operand->GetKind() == BcOperandKind::Constant || operand->GetKind() == BcOperandKind::Slot)
            {
                totalTValueOperands++;
            }
        }
        addConstraintForTop = (totalTValueOperands <= 3);
    }

    {
        std::vector<uint32_t> operandList;
        std::unique_ptr<AndConstraint> constraint = std::make_unique<AndConstraint>();
        for (std::unique_ptr<BcOperand>& operand : bvd->m_list)
        {
            if (operand->GetKind() != BcOperandKind::Constant && operand->GetKind() != BcOperandKind::Slot)
            {
                continue;
            }

            if (operand->GetKind() == BcOperandKind::Constant)
            {
                BcOpConstant* op = assert_cast<BcOpConstant*>(operand.get());
                TypeSpeculationMask mask = op->m_typeMask;
                if (addConstraintForTop || mask != x_typeSpeculationMaskFor<tTop>)
                {
                    constraint->AddClause(std::make_unique<LeafConstraint>(op->OperandOrdinal(), mask /*allowedMask*/));
                    operandList.push_back(static_cast<uint32_t>(op->OperandOrdinal()));
                }
            }
            else
            {
                ReleaseAssert(operand->GetKind() == BcOperandKind::Slot);
                if (addConstraintForTop)
                {
                    constraint->AddClause(std::make_unique<LeafConstraint>(operand->OperandOrdinal(), x_typeSpeculationMaskFor<tTop> /*allowedMask*/));
                    operandList.push_back(static_cast<uint32_t>(operand->OperandOrdinal()));
                }
            }
        }
        pass.SetOperandList(operandList);
        pass.SetConstraint(std::move(constraint));
    }

    pass.Run();
}

}   // namespace dast
