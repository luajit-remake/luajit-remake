#include "deegen_dfg_jit_regalloc_rt_call_wrapper.h"
#include "dfg_reg_alloc_register_info.h"

namespace dast {

std::vector<X64Reg> WARN_UNUSED DfgCCallFuncInfo::GetGprArgShuffleList()
{
    uint64_t raGprMask = 0;
    ForEachDfgRegAllocGPR([&](X64Reg reg) { raGprMask |= static_cast<uint64_t>(1) << reg.MachineOrd(); });

    // Return the list in increasing order of machine register ordinal
    //
    std::vector<X64Reg> res;
    uint64_t overlapMask = raGprMask & m_argGprMask;
    while (overlapMask != 0)
    {
        size_t ord = CountTrailingZeros(overlapMask);
        overlapMask ^= static_cast<uint64_t>(1) << ord;
        res.push_back(X64Reg::GPR(ord));
    }
    return res;
}

json_t WARN_UNUSED DfgCCallFuncInfo::SaveToJSON()
{
    json_t r = json_t::object();
    r["fn_name"] = m_fnName;
    r["arg_gpr_mask"] = m_argGprMask;
    r["arg_fpr_mask"] = m_argFprMask;
    r["ret_gpr_mask"] = m_retGprMask;
    r["ret_fpr_mask"] = m_retFprMask;
    return r;
}

DfgCCallFuncInfo WARN_UNUSED DfgCCallFuncInfo::LoadFromJSON(json_t& j)
{
    DfgCCallFuncInfo info;
    JSONCheckedGet(j, "fn_name", info.m_fnName);
    JSONCheckedGet(j, "arg_gpr_mask", info.m_argGprMask);
    JSONCheckedGet(j, "arg_fpr_mask", info.m_argFprMask);
    JSONCheckedGet(j, "ret_gpr_mask", info.m_retGprMask);
    JSONCheckedGet(j, "ret_fpr_mask", info.m_retFprMask);
    return info;
}

// Helper class to get the X64Reg from a register name or a sub-register name
//
struct GetRegFromSubRegNameHelper
{
    GetRegFromSubRegNameHelper()
    {
        auto addEntry = [&](std::string key, X64Reg val)
        {
            key = ConvertStringToLowerCase(key);
            ReleaseAssert(!m_map.count(key));
            m_map[key] = val;
        };

        for (size_t mcOrd = 0; mcOrd < X64Reg::x_totalNumGprs; mcOrd++)
        {
            X64Reg reg = X64Reg::GPR(mcOrd);
            addEntry(reg.GetSubRegisterName<8>(), reg);
            addEntry(reg.GetSubRegisterName<16>(), reg);
            addEntry(reg.GetSubRegisterName<32>(), reg);
            addEntry(reg.GetName(), reg);
        }

        // Add the "H" registers as well for sanity
        //
        addEntry("ah", X64Reg::RAX);
        addEntry("bh", X64Reg::RBX);
        addEntry("ch", X64Reg::RCX);
        addEntry("dh", X64Reg::RDX);

        for (size_t mcOrd = 0; mcOrd < X64Reg::x_totalNumFprs; mcOrd++)
        {
            X64Reg reg = X64Reg::FPR(mcOrd);
            addEntry(reg.GetName(), reg);
        }
    }

    X64Reg WARN_UNUSED Get(std::string name)
    {
        name = ConvertStringToLowerCase(name);
        if (!m_map.count(name))
        {
            fprintf(stderr, "Unrecognized register name %s!\n", name.c_str());
            abort();
        }
        return m_map[name];
    }

private:
    std::unordered_map<std::string, X64Reg> m_map;
};

// Crash on failure!
//
// The annotation format looks like this: __deegen_asm_annotation_call{arg_regs[...]ret_regs[...]}
//
static DfgCCallFuncInfo WARN_UNUSED ParseDeegenCallArgAndRetRegistersAnnotation(GetRegFromSubRegNameHelper& nameToRegConverter,
                                                                                const std::string& funcName,
                                                                                const std::string& comments)
{
    std::string annotationMark = "__deegen_asm_annotation_call";
    size_t annotationLoc = comments.find(annotationMark);
    if (annotationLoc == std::string::npos)
    {
        fprintf(stderr, "Failed to find deegen ASM call annotation for function %s, did you turn on LLVM TargetOption EmitCallSiteInfo?\n",
                funcName.c_str());
        abort();
    }

    size_t contentStart = annotationLoc + annotationMark.length();
    ReleaseAssert(contentStart + 1 < comments.size());
    ReleaseAssert(comments[contentStart] == '{');
    size_t contentEnd = comments.find("}", contentStart);
    ReleaseAssert(contentEnd != std::string::npos);

    ReleaseAssert(contentEnd > contentStart);
    std::string content = comments.substr(contentStart + 1, contentEnd - contentStart - 1);

    auto getInfo = [&](const std::string& mark) WARN_UNUSED -> std::string
    {
        size_t loc = content.find(mark);
        if (loc == std::string::npos)
        {
            fprintf(stderr, "Deegen ASM call annotation for function %s does not contain expected component '%s'!\n",
                    funcName.c_str(), mark.c_str());
            abort();
        }
        size_t infoStart = loc + mark.size();
        size_t infoEnd = content.find("]", infoStart);
        ReleaseAssert(infoEnd != std::string::npos);
        ReleaseAssert(infoEnd >= infoStart);
        return content.substr(infoStart, infoEnd - infoStart);
    };

    auto parseInfo = [&](const std::string& info, uint64_t& gprMask /*out*/, uint64_t& fprMask /*out*/)
    {
        gprMask = 0;
        fprMask = 0;
        size_t curLoc = 0;
        while (curLoc < info.length())
        {
            size_t commaLoc = info.find(",", curLoc);
            ReleaseAssert(commaLoc != std::string::npos);
            ReleaseAssert(commaLoc > curLoc);
            X64Reg reg = nameToRegConverter.Get(info.substr(curLoc, commaLoc - curLoc));
            if (reg.IsGPR())
            {
                ReleaseAssert(reg.MachineOrd() < 64);
                gprMask |= static_cast<uint64_t>(1) << reg.MachineOrd();
            }
            else
            {
                ReleaseAssert(reg.MachineOrd() < 64);
                fprMask |= static_cast<uint64_t>(1) << reg.MachineOrd();
            }
            curLoc = commaLoc + 1;
        }
        ReleaseAssert(curLoc == info.length());
    };

    uint64_t argGprMask, argFprMask;
    parseInfo(getInfo("arg_regs["), argGprMask /*out*/, argFprMask /*out*/);

    uint64_t retGprMask, retFprMask;
    parseInfo(getInfo("ret_regs["), retGprMask /*out*/, retFprMask /*out*/);

    return {
        .m_fnName = funcName,
        .m_argGprMask = argGprMask,
        .m_argFprMask = argFprMask,
        .m_retGprMask = retGprMask,
        .m_retFprMask = retFprMask
    };
}

bool WARN_UNUSED DfgRegAllocCCallAsmTransformPass::RunPass(std::string wrapperPrefix)
{
    using namespace llvm;
    ReleaseAssert(!m_passExecuted);
    m_passExecuted = true;

    GetRegFromSubRegNameHelper nameToRegConverter;
    std::unordered_map<std::string, DfgCCallFuncInfo> alreadyRecordedFns;

    // The mask for all GPR registers that participates in DFG reg alloc
    //
    uint64_t raGprMask = 0;
    ForEachDfgRegAllocGPR([&](X64Reg reg) { raGprMask |= static_cast<uint64_t>(1) << reg.MachineOrd(); });

    // The mask for all FPR registers that participates in DFG reg alloc
    //
    uint64_t raFprMask = 0;
    ForEachDfgRegAllocFPR([&](X64Reg reg) { raFprMask |= static_cast<uint64_t>(1) << reg.MachineOrd(); });

    auto workOnBlocks = [&](std::vector<X64AsmBlock*>& blocks) WARN_UNUSED -> bool
    {
        for (X64AsmBlock* block : blocks)
        {
            std::unordered_map<size_t /*originalLine*/, std::vector<X64AsmLine>> replaceMap;
            for (size_t i = 0; i < block->m_lines.size(); i++)
            {
                if (block->m_lines[i].IsCallInstruction())
                {
                    ReleaseAssert(block->m_lines[i].NumWords() == 2);
                    std::string calleeName = block->m_lines[i].GetWord(1);
                    ReleaseAssert(!calleeName.starts_with("__deegen_cp_placeholder_"));

                    Function* calleeFn = m_module->getFunction(calleeName);
                    if (calleeFn == nullptr)
                    {
                        // This is either an indirect call, or something weird like memcpy@PLT,
                        // what we can be certain is that it's not a preserve_most call, so disable reg alloc
                        //
                        m_failReason = FailReason::HasIndirectCall;
                        return false;
                    }

                    // If the callee is no_return, no need to use the wrapper since the call doesn't return at all
                    //
                    if (calleeFn->hasFnAttribute(Attribute::NoReturn))
                    {
                        continue;
                    }

                    if (calleeFn->getCallingConv() != CallingConv::PreserveMost &&
                        calleeFn->getCallingConv() != CallingConv::PreserveAll)
                    {
                        m_failReason = FailReason::NotPreserveMostCC;
                        return false;
                    }

                    DfgCCallFuncInfo funcInfo = ParseDeegenCallArgAndRetRegistersAnnotation(nameToRegConverter,
                                                                                            calleeFn->getName().str(),
                                                                                            block->m_lines[i].m_trailingComments);

                    if ((funcInfo.m_retGprMask & raGprMask) != 0 || (funcInfo.m_retFprMask & raFprMask) != 0)
                    {
                        m_failReason = FailReason::ReturnRegConflict;
                        return false;
                    }

                    if ((funcInfo.m_argFprMask & raFprMask) != 0)
                    {
                        m_failReason = FailReason::FprArgumentRegConflict;
                        return false;
                    }

                    if (!alreadyRecordedFns.count(calleeName))
                    {
                        alreadyRecordedFns[calleeName] = funcInfo;
                        m_calledFns.push_back(funcInfo);
                    }
                    else
                    {
                        ReleaseAssert(alreadyRecordedFns[calleeName] == funcInfo);
                    }

                    std::vector<X64Reg> regsToPush = funcInfo.GetGprArgShuffleList();

                    std::vector<X64AsmLine> replacement;
                    for (X64Reg reg : regsToPush)
                    {
                        std::string inst = "\tpushq\t%" + ConvertStringToLowerCase(reg.GetName());
                        replacement.push_back(X64AsmLine::Parse(inst));
                    }

                    X64AsmLine nl = block->m_lines[i];
                    nl.GetWord(1) = GetWrappedName(wrapperPrefix, nl.GetWord(1));
                    replacement[0].m_prefixingText = nl.m_prefixingText;
                    nl.m_prefixingText = "";
                    replacement.push_back(nl);

                    std::vector<X64Reg> regsToPop = regsToPush;
                    std::reverse(regsToPop.begin(), regsToPop.end());

                    for (X64Reg reg : regsToPop)
                    {
                        std::string inst = "\tpopq\t%" + ConvertStringToLowerCase(reg.GetName());
                        replacement.push_back(X64AsmLine::Parse(inst));
                    }

                    ReleaseAssert(!replaceMap.count(i));
                    replaceMap[i] = std::move(replacement);
                }
            }

            if (!replaceMap.empty())
            {
                std::vector<X64AsmLine> newBlock;
                for (size_t i = 0; i < block->m_lines.size(); i++)
                {
                    if (replaceMap.count(i))
                    {
                        for (auto& line : replaceMap[i])
                        {
                            newBlock.push_back(line);
                        }
                    }
                    else
                    {
                        newBlock.push_back(block->m_lines[i]);
                    }
                }
                block->m_lines = std::move(newBlock);
            }
        }
        return true;
    };

    if (!workOnBlocks(m_file->m_blocks)) { return false; }
    if (!workOnBlocks(m_file->m_slowpath)) { return false; }
    if (!workOnBlocks(m_file->m_icPath)) { return false; }

    ReleaseAssert(alreadyRecordedFns.size() == m_calledFns.size());
    ReleaseAssert(m_failReason == FailReason::Unknown);
    return true;
}

bool WARN_UNUSED DfgRegAllocCCallAsmTransformPass::TryRewritePreserveMostToPreserveAll(llvm::Function* func, std::unordered_set<std::string>& rewrittenFnNames /*out*/)
{
    using namespace llvm;
    std::unordered_set<std::string> result;
    for (BasicBlock& bb : *func)
    {
        for (Instruction& inst : bb)
        {
            CallInst* callInst = dyn_cast<CallInst>(&inst);
            if (callInst != nullptr)
            {
                if (callInst->getCallingConv() == CallingConv::PreserveMost)
                {
                    Function* callee = callInst->getCalledFunction();
                    if (callee == nullptr)
                    {
                        // We cannot generate wrapper for indirect preserve_most call, disable reg alloc
                        //
                        return false;
                    }
                    else
                    {
                        // No need to do anything if callee is no_return, since the calling conv doesn't matter in such case
                        //
                        if (!callee->hasFnAttribute(Attribute::NoReturn))
                        {
                            callInst->setCallingConv(CallingConv::PreserveAll);
                            if (callee->getCallingConv() == CallingConv::PreserveMost)
                            {
                                callee->setCallingConv(CallingConv::PreserveAll);
                                // Must record all the function prototypes that we changed, and flip them back in the IC codegen module,
                                // since the IC codegen module needs to be linked back into the IC body module later and may reference these functions
                                //
                                ReleaseAssert(!result.count(callee->getName().str()));
                                result.insert(callee->getName().str());
                            }
                            else
                            {
                                ReleaseAssert(callee->getCallingConv() == CallingConv::PreserveAll);
                                ReleaseAssert(result.count(callee->getName().str()));
                            }
                        }
                    }
                }
            }
        }
    }
    rewrittenFnNames = std::move(result);
    return true;
}

uint64_t DfgRegAllocCCallWrapperRequest::GetExtraFprMaskToSave()
{
    uint64_t res = m_maskForAllUsedFprs;

    // The shared assembly stub will always restore all FPR that participates in DFG reg alloc,
    // so they can be excluded from the reg mask.
    //
    ForEachDfgRegAllocFPR(
        [&](X64Reg reg)
        {
            res &= ~(static_cast<uint64_t>(1) << reg.MachineOrd());
            // For sanity, assert again that no DFG FPR reg is used as argument / return value register,
            // as otherwise they could be renamed and it would be wrong to restore it by its direct name
            //
            ReleaseAssert((m_info.m_argFprMask & (static_cast<uint64_t>(1) << reg.MachineOrd())) == 0);
            ReleaseAssert((m_info.m_retFprMask & (static_cast<uint64_t>(1) << reg.MachineOrd())) == 0);
        });

    // And also for sanity, assert that the no DFG GPR reg is used as return value register (but arg register is fine)
    //
    ForEachDfgRegAllocGPR(
        [&](X64Reg reg)
        {
            ReleaseAssert((m_info.m_retGprMask & (static_cast<uint64_t>(1) << reg.MachineOrd())) == 0);
        });

    // Must not restore regs used to store return value
    //
    res &= ~(m_info.m_retFprMask);
    return res;
}

json_t WARN_UNUSED DfgRegAllocCCallWrapperRequest::SaveToJSON()
{
    json_t r = json_t::object();
    r["wrapper_prefix"] = m_wrapperPrefix;
    r["func_info"] = m_info.SaveToJSON();
    r["mask_for_all_used_fprs"] = m_maskForAllUsedFprs;
    return r;
}

DfgRegAllocCCallWrapperRequest WARN_UNUSED DfgRegAllocCCallWrapperRequest::LoadFromJSON(json_t& j)
{
    DfgRegAllocCCallWrapperRequest r;
    JSONCheckedGet(j, "wrapper_prefix", r.m_wrapperPrefix);
    ReleaseAssert(j.count("func_info") && j["func_info"].is_object());
    r.m_info = DfgCCallFuncInfo::LoadFromJSON(j["func_info"]);
    JSONCheckedGet(j, "mask_for_all_used_fprs", r.m_maskForAllUsedFprs);
    return r;
}

void DfgRegAllocCCallWrapperRequest::PrintAssemblyImpl(FILE* file)
{
    std::string fnName = GetFuncName();
    fprintf(file, "\t.globl\t%s\n", fnName.c_str());
    fprintf(file, "\t.p2align\t4, 0x90\n");
    fprintf(file, "\t.type\t%s,@function\n", fnName.c_str());
    fprintf(file, "%s:\n", fnName.c_str());

    std::vector<X64Reg> regInStackOrder = m_info.GetGprArgShuffleList();
    std::reverse(regInStackOrder.begin(), regInStackOrder.end());

    // The desired value of register regInStackOrder[i] is at stack offset (i+1)*8
    //
    for (size_t idx = 0; idx < regInStackOrder.size(); idx++)
    {
        X64Reg reg = regInStackOrder[idx];
        size_t offsetBytes = 8 * (idx + 1);
        fprintf(file, "\tmovq %d(%%rsp), %%%s\n", static_cast<int>(offsetBytes), ConvertStringToLowerCase(reg.GetName()).c_str());
    }

    uint64_t extraFprSaveMask = GetExtraFprMaskToSave();
    size_t numExtraFprRegsToSave = static_cast<size_t>(__builtin_popcountll(extraFprSaveMask));

    constexpr size_t x_sharedStubUsedBufferSize = x_dfg_reg_alloc_num_fprs * 16 + 8;

    size_t saveAreaLen = x_sharedStubUsedBufferSize + 16 * numExtraFprRegsToSave;
    if (regInStackOrder.size() % 2 == 1)
    {
        saveAreaLen += 8;
    }

    // Must satisfy x86-64 16-byte stack alignment ABI since we will call the original function using this rsp
    //
    ReleaseAssert((saveAreaLen + regInStackOrder.size() * 8) % 16 == 8);

    fprintf(file, "\tsubq $%d, %%rsp\n", static_cast<int>(saveAreaLen));

    fprintf(file, "\tcallq __deegen_dfg_preserve_most_c_wrapper_shared_prologue_stub\n");

    // Save extra FPR
    // Note that our buffer region is [rsp, rsp + saveAreaLen), and [rsp, rsp + x_sharedStubUsedBufferSize) are storing valid data now
    //
    std::map<size_t /*offset*/, size_t /*fprOrd*/> extraFprSaveLoc;
    {
        size_t curOffset = x_sharedStubUsedBufferSize;
        uint64_t mask = extraFprSaveMask;
        while (mask != 0)
        {
            size_t fprOrd = CountTrailingZeros(mask);
            ReleaseAssert(mask & (static_cast<uint64_t>(1) << fprOrd));
            mask ^= static_cast<uint64_t>(1) << fprOrd;

            ReleaseAssert(!extraFprSaveLoc.count(curOffset));
            extraFprSaveLoc[curOffset] = fprOrd;
            fprintf(file, "\tmovups %%xmm%d, %d(%%rsp)\n", static_cast<int>(fprOrd), static_cast<int>(curOffset));
            curOffset += 16;
        }
        ReleaseAssert(curOffset + ((regInStackOrder.size() % 2 == 0) ? 0 : 8) == saveAreaLen);
        ReleaseAssert(extraFprSaveLoc.size() == numExtraFprRegsToSave);
    }

    fprintf(file, "\tcallq %s\n", m_info.m_fnName.c_str());

    // Restore extra FPR
    //
    for (auto& it : extraFprSaveLoc)
    {
        size_t offset = it.first;
        size_t fprOrd = it.second;
        fprintf(file, "\tmovups %d(%%rsp), %%xmm%d\n", static_cast<int>(offset), static_cast<int>(fprOrd));
    }

    fprintf(file, "\tcallq __deegen_dfg_preserve_most_c_wrapper_shared_epilogue_stub\n");

    fprintf(file, "\taddq $%d, %%rsp\n", static_cast<int>(saveAreaLen));
    fprintf(file, "\tretq\n");

    fprintf(file, "\n");

    fprintf(file, ".Lfunc_end_%s:\n", fnName.c_str());
    fprintf(file, "\t.size\t%s, .Lfunc_end_%s-%s\n\n", fnName.c_str(), fnName.c_str(), fnName.c_str());
}

void DfgRegAllocCCallWrapperRequest::PrintAliasImpl(FILE* file, const std::string& aliasFnName)
{
    ReleaseAssert(aliasFnName != "");
    std::string fnName = GetFuncName();
    fprintf(file, "\t.globl\t%s\n", fnName.c_str());
    fprintf(file, "\t.type\t%s,@function\n", fnName.c_str());
    fprintf(file, "\t.set %s, %s\n\n", fnName.c_str(), aliasFnName.c_str());
}

}   // namespace dast
