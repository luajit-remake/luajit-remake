#include "deegen_desugaring_level.h"
#include "misc_llvm_helper.h"
#include "parse_bytecode_definition.h"

namespace dast
{

static DesugarDecision WARN_UNUSED AlwaysInlineAndNoInlineChecker(llvm::Function* func, DesugaringLevel /*level*/)
{
    using namespace llvm;
    if (func->hasFnAttribute(Attribute::AttrKind::AlwaysInline))
    {
        ReleaseAssert(!func->hasFnAttribute(Attribute::AttrKind::NoInline));
        return DesugarDecision::MustInline;
    }
    if (func->hasFnAttribute(Attribute::AttrKind::NoInline))
    {
        ReleaseAssert(!func->hasFnAttribute(Attribute::AttrKind::AlwaysInline));
        return DesugarDecision::MustNotInline;
    }
    return DesugarDecision::DontCare;
}

static DesugarDecision WARN_UNUSED GetDesugarDecisionForFunction(llvm::Function* func, DesugaringLevel level)
{
    constexpr DesugarDecisionFn funcs[] = {
        AlwaysInlineAndNoInlineChecker,
        ShouldDesugarTValueAPI,
    };
    constexpr size_t numFuncs = std::extent_v<decltype(funcs), 0 /*dim*/>;
    DesugarDecision decision = DesugarDecision::DontCare;
    for (size_t i = 0; i < numFuncs; i++)
    {
        DesugarDecision currentDecision = funcs[i](func, level);
        if (i == 0)
        {
            decision = currentDecision;
        }
        else
        {
            if (decision == DesugarDecision::MustInline)
            {
                ReleaseAssert(currentDecision != DesugarDecision::MustNotInline);
            }
            else if (decision == DesugarDecision::MustNotInline)
            {
                ReleaseAssert(currentDecision != DesugarDecision::MustInline);
            }
            else
            {
                ReleaseAssert(decision == DesugarDecision::DontCare);
                decision = currentDecision;
            }
        }
    }
    return decision;
}

void AddLLVMInliningAttributesForDesugaringLevel(llvm::Module* module, DesugaringLevel level)
{
    using namespace llvm;
    ReleaseAssert(level > DesugaringLevel::Bottom);
    for (Function& func : module->functions())
    {
        DesugarDecision decision = GetDesugarDecisionForFunction(&func, level);
        if (decision == DesugarDecision::MustInline)
        {
            ReleaseAssert(!func.hasFnAttribute(Attribute::AttrKind::NoInline));
            func.addFnAttr(Attribute::AttrKind::AlwaysInline);
        }
        else if (decision == DesugarDecision::MustNotInline)
        {
            ReleaseAssert(!func.hasFnAttribute(Attribute::AttrKind::AlwaysInline));
            func.addFnAttr(Attribute::AttrKind::NoInline);
        }
        else
        {
            ReleaseAssert(decision == DesugarDecision::DontCare);
        }
    }
}

}   // namespace dast
