#include "deegen_analyze_read_locations.h"
#include "deegen_simple_operand_expression.h"

namespace dast {

bool WARN_UNUSED TryAnalyzeBytecodeReadLocations(llvm::Argument* ptr,
                                                 std::function<BcOperand*(llvm::Argument*)> argMapper,
                                                 std::vector<SimpleOperandExprNode*>& result /*out*/)
{
    using namespace llvm;

    // Find all the Values based on ptr
    //
    std::unordered_set<Value*> basedOnSet;
    // This stores all the pointer based on 'ptr' that is loaded from
    //
    std::unordered_set<Value*> loadedFromSet;
    {
        std::queue<Value*> q;
        auto pushQueue = [&](Value* val)
        {
            if (basedOnSet.count(val))
            {
                return;
            }
            basedOnSet.insert(val);
            q.push(val);
        };

        pushQueue(ptr);

        while (!q.empty())
        {
            Value* val = q.front();
            q.pop();

            for (Use& u : val->uses())
            {
                User* usr = u.getUser();
                // If the value is being stored into memory, give up
                //
                if (isa<StoreInst>(usr) && val == cast<StoreInst>(usr)->getValueOperand())
                {
                    return false;
                }
                if (isa<AtomicCmpXchgInst>(usr) && val == cast<AtomicCmpXchgInst>(usr)->getNewValOperand())
                {
                    return false;
                }
                if (isa<AtomicRMWInst>(usr) && val == cast<AtomicRMWInst>(usr)->getValOperand())
                {
                    return false;
                }

                // If the value is being passed to a call, give up
                //
                if (isa<CallBase>(usr))
                {
                    return false;
                }

                // If we are the pointer operand that is being loaded from / stored to,
                // we need to add it to loadedFrom set if it's a load, but the result isn't based on the ptr any more
                //
                if (isa<StoreInst>(usr) && cast<StoreInst>(usr)->getPointerOperandIndex() == u.getOperandNo())
                {
                    continue;
                }
                if (isa<LoadInst>(usr) && cast<LoadInst>(usr)->getPointerOperandIndex() == u.getOperandNo())
                {
                    loadedFromSet.insert(val);
                    continue;
                }
                if (isa<AtomicCmpXchgInst>(usr) && cast<AtomicCmpXchgInst>(usr)->getPointerOperandIndex() == u.getOperandNo())
                {
                    loadedFromSet.insert(val);
                    continue;
                }
                if (isa<AtomicRMWInst>(usr) && cast<AtomicRMWInst>(usr)->getPointerOperandIndex() == u.getOperandNo())
                {
                    loadedFromSet.insert(val);
                    continue;
                }

                // Otherwise, treat the output as being dependent on this input operand
                //
                if (!llvm_type_has_type<void>(usr->getType()))
                {
                    pushQueue(usr);
                }
            }
        }
    }

    // Find all the Values that can be expressed as SimpleOperandExpr
    //
    LLVMValueToOperandExprMapper oem;
    oem.Run(ptr->getParent(), argMapper);
    if (oem.m_analysisFailed)
    {
        return false;
    }

    // If there exists a pointer based on 'ptr' that is loaded from but not expressible as SimpleOperandExpr, fail
    //
    std::unordered_set<SimpleOperandExprNode*> r;
    for (Value* loadPtrOperand : loadedFromSet)
    {
        if (!oem.m_map.count(loadPtrOperand))
        {
            return false;
        }
        ReleaseAssert(oem.m_map[loadPtrOperand].size() > 0);
        for (SimpleOperandExprNode* expr : oem.m_map[loadPtrOperand])
        {
            r.insert(expr);
        }
    }

    result.clear();
    for (SimpleOperandExprNode* expr : r)
    {
        result.push_back(expr);
    }
    return true;
}

}   // namespace dast
