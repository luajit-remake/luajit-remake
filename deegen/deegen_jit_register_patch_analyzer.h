#pragma once

#include "common_utils.h"
#include "drt/x64_register_info.h"
#include "misc_llvm_helper.h"
#include "deegen_dfg_register_ident_class.h"

namespace dast {

// To support register allocation in DFG, we need to figure out how to generate code with different register configurations.
// Pre-generating all combinations is not an option, since there are too many combinations.
//
// The key observation is the following:
// In X86-64 the 16 registers are encoded by 3+1 bits in instructions. The lower 3 bits can show up anywhere in an instruction
// (depending on instruction encoding), and the highest bit sits in an instruction prefix (or *absense* of a prefix).
//
// The consequence is the following:
// 1. As long as the *highest bit* of the register ordinal is unchanged, changing an register in an instruction is simply changing
//    3 bits in the instruction to the wanted register ordinal.
// 2. Furthermore, doing so will not change the length of the instruction. This is extremely important, as changing the instruction
//    length can invalid all the jump targets and is very hard to fix (e.g., it may require changing an 8-bit offset to 32-bit).
//
// So we can divide the registers into two groups: 0-7 and 8-15, and for each register that needs to be passed through a snippet,
// we only need to care whether it comes from the first group or the second group. Now changing register name within the same
// group is guaranteed to not change instruction length. This is enough to reduce the number of different register configurations
// to a few dozens, since we only care about whether each operand comes from the first or second group, and how many pass-through
// registers come from the first group.
//
// Of course, there are all sorts of architecture idiosyncrasies:
//
// 1. Instructions that have implicit (hardcoded) register operands
//    Fortunately, all such instructions (with the exception of SYSCALL and REP) only hardcode AX,BX,CX,DX,
//    and under our current scheme we never use them to pass values across stencils, so we should never need to modify them
//    (we can also validate this at build time thanks to ZyDis, so we should never hit such a case and generate bad code at
//    runtime). REP instructions are problematic as they are in theory useful, but it seems like compilers won't generate them
//    in practice, and this issue can also be workarounded by rewriting the assembly if really needed.
//
// 2. AH/BH/CH/DH
//    The "H" subregister only exist for AX,BX,CX,DX.
//    Fortunately, we always use AX/BX/CX/DX as scratch so we don't need to rename them.
//
// 3. SIB addressing mode idiosyncrasy with R13/RBP/RSP
//    When ModRM.Mod=00, SIB.base=RBP/R13 has special meaning.
//    Specifically, if SIB.base=RBP/R13, the addressing mode is (index*scale+disp32), but otherwise it is (base+index*scale)
//    That is, [rbp/r13+index*scale] is not a legal addressing mode! Only [rbp/r13+index*scale+disp8/disp32] is legal.
//    Fortunately, we currently always use RBP and R13 as context registers, so we are good.. But this can also be workarounded
//    by rewriting the assembly to force a disp8 in such case (it's only adding one extra byte) if really needed.
//
// 4. ModRM addressing mode idiosyncrasy with R12/R13/RBP/RSP
//    R12/R13/RBP/RSP cannot be used in ModRM r/m addressing.
//    R12/RSP cannot be used in ModRM r/m+disp addressing.
//
// Side note: I've also considered unconditionally prefixing the instructions (e.g., adding a REX prefix even if not needed)
// so we can always change a register to anything else. However, there are edge case bugs, due to the rule that use of AH/BH/CH/DH
// must not come with a REX prefix (e.g., movzx ESI, CH is legal but movzx R8d, CH is not, so we cannot rename ESI to R8d).
//

// What kind of role a register plays in a stencil
//
enum class PhyRegUseKind : uint8_t
{
   // It is an input or output operand. Note that it may still have garbage value at start (for output-only reg) or
   // end (for input reg which is the last use for this reg) of the stencil, but this information is part of the
   // stencil so we do not care about this here
   //
    OperandUse,
    // It is a pass-through register but unused before this snippet, so it can be used as a scratch register in this snippet
    // it has garbage value at the start and end of the stencil
    //
    ScratchUse,
    // It is a pass-through register, it must retain its start value at end of the stencil
    //
    PassThruUse,
    // We will never need to rename (and sometimes cannot due to x64 idiosyncrasy) this register to another name
    // It could be a context register (e.g., the tag registers, the register holding current CodeBlock, etc.)
    // or a scratch register that is never used to pass values across stencils.
    //
    NoRenaming,
    // Must come last
    //
    X_END_OF_ENUM
};

// What role a register plays in a stencil
//
struct PhyRegPurpose
{
    static PhyRegPurpose WARN_UNUSED Operand(uint8_t raOpIdx)
    {
        ReleaseAssert(raOpIdx != static_cast<uint8_t>(-1));
        return PhyRegPurpose(PhyRegUseKind::OperandUse, raOpIdx);
    }

    static PhyRegPurpose WARN_UNUSED Scratch() { return PhyRegPurpose(PhyRegUseKind::ScratchUse, static_cast<uint8_t>(-1)); }
    static PhyRegPurpose WARN_UNUSED PassThru() { return PhyRegPurpose(PhyRegUseKind::PassThruUse, static_cast<uint8_t>(-1)); }
    static PhyRegPurpose WARN_UNUSED NoRenaming() { return PhyRegPurpose(PhyRegUseKind::NoRenaming, static_cast<uint8_t>(-1)); }

    void SetOrd(uint8_t ord)
    {
        ReleaseAssert(m_kindOrd == static_cast<uint8_t>(-1) && ord != static_cast<uint8_t>(-1));
        m_kindOrd = ord;
    }

    uint8_t WARN_UNUSED Ord() { ReleaseAssert(IsValid() && m_kindOrd != static_cast<uint8_t>(-1)); return m_kindOrd; }
    PhyRegUseKind WARN_UNUSED Kind() { ReleaseAssert(IsValid()); return m_kind; }

    bool WARN_UNUSED IsValid() { return m_kind != PhyRegUseKind::X_END_OF_ENUM; }
    bool WARN_UNUSED HasOrd() { ReleaseAssert(IsValid()); return m_kindOrd != static_cast<uint8_t>(-1); }

    PhyRegPurpose() : m_kind(PhyRegUseKind::X_END_OF_ENUM), m_kindOrd(static_cast<uint8_t>(-1)) { }

private:
    PhyRegPurpose(PhyRegUseKind kind, uint8_t kindOrd) : m_kind(kind), m_kindOrd(kindOrd) { }

    PhyRegUseKind m_kind;
    uint8_t m_kindOrd;
};

struct StencilRegIdent
{
    StencilRegIdentClass m_class;
    uint8_t m_ord;  // always < 8

    StencilRegIdent() : m_class(StencilRegIdentClass::X_END_OF_ENUM), m_ord(static_cast<uint8_t>(-1)) { }
    StencilRegIdent(StencilRegIdentClass cl, uint8_t ord) : m_class(cl), m_ord(ord) { ReleaseAssert(m_ord != static_cast<uint8_t>(-1)); }
    bool IsValid() { return m_ord != static_cast<uint8_t>(-1); }

    // Print human-readable string for audit purpose
    //
    // Operands: %operand<ord>
    // Scratch/Passthru: %<scratch/passthru>.<eg/g/f><ord>
    //
    // where g = GPR0~7, eg = GPR8~15, f = FPR
    //
    std::string WARN_UNUSED ToPrettyString();
};

// Information about a single use of a register in a stencil
//
struct PhyRegUseInfo
{
    // This register can only be renamed if m_kind is not NoRenaming
    //
    PhyRegUseKind m_kind;

    // If true, this is one of the 8 registers added by 64-bit x86 (i.e., its highest bit is 1)
    // For FPR, we currently only use XMM0-7 so this is always false.
    //
    bool m_isExtReg;

    // Whether this is a GPR or FPR register
    //
    bool m_isGPR;

    // A logical ordinal that identifies the register within its own class, for renaming
    //
    // Operand is its own class: for operand, this is the operand ordinal
    // <Scratch, PassThru> * <NonExtGPR, ExtGPR, FPR> creates 6 classes, each class has its own counter for ordinals.
    //
    // If m_kind is NoRenaming, this value doesn't make sense
    //
    uint8_t m_kindOrd;

    // PhyRegUseKind must not be NoRenaming
    //
    static StencilRegIdentClass GetIdentClassFromUseAndRegInfo(PhyRegUseKind kind, bool isGpr, bool isExtReg)
    {
        if (kind == PhyRegUseKind::OperandUse)
        {
            return StencilRegIdentClass::Operand;
        }
        if (kind == PhyRegUseKind::PassThruUse)
        {
            if (isGpr)
            {
                return (isExtReg ? StencilRegIdentClass::PtExtG : StencilRegIdentClass::PtNonExtG);
            }
            else
            {
                ReleaseAssert(!isExtReg);
                return StencilRegIdentClass::PtF;
            }
        }
        else if (kind == PhyRegUseKind::ScratchUse)
        {
            if (isGpr)
            {
                return (isExtReg ? StencilRegIdentClass::ScExtG : StencilRegIdentClass::ScNonExtG);
            }
            else
            {
                ReleaseAssert(!isExtReg);
                return StencilRegIdentClass::ScF;
            }
        }
        else
        {
            ReleaseAssert(false);
        }
    }

    static StencilRegIdentClass GetIdentClassFromUseAndReg(PhyRegUseKind kind, X64Reg reg)
    {
        return GetIdentClassFromUseAndRegInfo(kind, reg.IsGPR(), reg.MachineOrd() >= 8 /*isExtReg*/);
    }

    StencilRegIdentClass GetIdentClass()
    {
        return GetIdentClassFromUseAndRegInfo(m_kind, m_isGPR, m_isExtReg);
    }

    StencilRegIdent GetIdent()
    {
        return StencilRegIdent(GetIdentClass(), m_kindOrd);
    }
};

// Describes a single use of a register that can be renamed
//
struct PhyRegRenameRecord
{
    // Kind must not be NoRenaming
    //
    PhyRegUseInfo m_regInfo;

    // Where the register ordinal value is encoded in the instruction stream
    // The value is encoded in bits [m_bitOffset, m_bitOffset+3) of byte m_byteOffset
    //
    uint16_t m_byteOffset;
    uint8_t m_bitOffset;

    // Some X86-64 FPU instructions encode the flipped 3-bit value instead of the original value
    //
    bool m_isFlipped;
};

struct StencilRegisterFileContext
{
    static constexpr size_t x_numGprRegs = 16;
    static constexpr size_t x_numFprRegs = 8;

    StencilRegisterFileContext()
    {
        m_finalized = false;
        for (size_t i = 0; i < 2; i++)
        {
            for (size_t j = 0; j < 3; j++)
            {
                m_ordCounter[i][j] = 0;
            }
        }
    }

    void SetGprPurpose(size_t mcRegOrd, PhyRegPurpose purpose)
    {
        ReleaseAssert(!m_finalized && mcRegOrd < x_numGprRegs);
        ReleaseAssert(!m_purpose[mcRegOrd].IsValid() && purpose.IsValid());
        m_purpose[mcRegOrd] = purpose;
        UpdateOperandRegUseInfo(X64Reg::GPR(mcRegOrd), purpose);
    }

    void SetFprPurpose(size_t mcRegOrd, PhyRegPurpose purpose)
    {
        ReleaseAssert(!m_finalized && mcRegOrd < x_numFprRegs);
        ReleaseAssert(!m_purpose[mcRegOrd + x_numGprRegs].IsValid() && purpose.IsValid());
        m_purpose[mcRegOrd + x_numGprRegs] = purpose;
        UpdateOperandRegUseInfo(X64Reg::FPR(mcRegOrd), purpose);
    }

    void SetRegPurpose(X64Reg reg, PhyRegPurpose purpose)
    {
        if (reg.IsGPR())
        {
            SetGprPurpose(reg.MachineOrd(), purpose);
        }
        else
        {
            SetFprPurpose(reg.MachineOrd(), purpose);
        }
    }

    bool IsFinalized() { return m_finalized; }

    void Finalize()
    {
        ReleaseAssert(!m_finalized);
        m_finalized = true;
        for (size_t i = 0; i < x_numGprRegs + x_numFprRegs; i++)
        {
            ReleaseAssert(m_purpose[i].IsValid());
        }
        {
            std::unordered_set<size_t> checkUnique;
            for (auto& it : m_operandRegMap)
            {
                X64Reg reg = it.second;
                size_t val = (reg.IsGPR() ? reg.MachineOrd() : reg.MachineOrd() + 10000);
                ReleaseAssert(!checkUnique.count(val));
                checkUnique.insert(val);
            }
        }
    }

    bool IsGprExtendedReg(size_t mcRegOrd)
    {
        ReleaseAssert(mcRegOrd < x_numGprRegs);
        return mcRegOrd >= 8;
    }

    bool IsGprNoRenaming(size_t mcRegOrd)
    {
        ReleaseAssert(mcRegOrd < x_numGprRegs);
        PhyRegPurpose& reg = GetPhyRegRawRef(true /*isGPR*/, mcRegOrd);
        ReleaseAssert(reg.IsValid());
        return reg.Kind() == PhyRegUseKind::NoRenaming;
    }

    bool IsFprNoRenaming(size_t mcRegOrd)
    {
        // We only care about XMM0-7 but machine may have more, for those registers we will never rename them
        //
        if (mcRegOrd >= x_numFprRegs)
        {
            ReleaseAssert(mcRegOrd < 32);
            return true;
        }
        PhyRegPurpose& reg = GetPhyRegRawRef(false /*isGPR*/, mcRegOrd);
        ReleaseAssert(reg.IsValid());
        return reg.Kind() == PhyRegUseKind::NoRenaming;
    }

    bool IsRegNoRenaming(X64Reg reg)
    {
        if (reg.IsGPR())
        {
            return IsGprNoRenaming(reg.MachineOrd());
        }
        else
        {
            return IsFprNoRenaming(reg.MachineOrd());
        }
    }

    // Ordinals for PassThru and Scratch registers are assigned on demand.
    // This function assigns the ordinal if it hasn't and returns the register info.
    // This is only supposed to be used by the StencilRegParser logic when it actually sees a reg in the machine code.
    //
    PhyRegPurpose WARN_UNUSED GetPhyRegPurposeAndMarkUsedByMachineCode(bool isGPR, size_t mcRegOrd)
    {
        ReleaseAssert(m_finalized);
        PhyRegPurpose& reg = GetPhyRegRawRef(isGPR, mcRegOrd);
        if (reg.Kind() != PhyRegUseKind::ScratchUse && reg.Kind() != PhyRegUseKind::PassThruUse)
        {
            return reg;
        }

        if (reg.HasOrd())
        {
            // Already assigned an ordinal
            //
            return reg;
        }

        uint8_t& ord = GetRegClassCounterRef(
            reg.Kind() == PhyRegUseKind::ScratchUse /*isScratch*/,
            isGPR,
            isGPR ? IsGprExtendedReg(mcRegOrd) : false /*isExt*/);

        ReleaseAssert(ord < 255);
        reg.SetOrd(ord);
        ord++;

        return reg;
    }

    // Unlike GetPhyRegPurposeAndMarkUsedByMachineCode, this function does not assign an ordinal if the register is unused yet.
    // Thus, this is a query-only function that does not modify the state of this class (and m_ord in the returned PhyRegPurpose might not be set).
    //
    PhyRegPurpose WARN_UNUSED GetPhyRegPurpose(X64Reg reg)
    {
        ReleaseAssert(m_finalized);
        // gracefully handle larger FPR case for sanity
        //
        if (IsRegNoRenaming(reg)) { return PhyRegPurpose::NoRenaming(); }
        PhyRegPurpose purpose = GetPhyRegRawRef(reg.IsGPR(), reg.MachineOrd());
        ReleaseAssert(purpose.IsValid());
        return purpose;
    }

    PhyRegUseInfo WARN_UNUSED GetPhyRegInfoAndMarkUsedByMachineCode(bool isGPR, size_t mcRegOrd)
    {
        PhyRegPurpose purpose = GetPhyRegPurposeAndMarkUsedByMachineCode(isGPR, mcRegOrd);

        PhyRegUseInfo r;
        r.m_kind = purpose.Kind();
        if (isGPR)
        {
            r.m_isExtReg = IsGprExtendedReg(mcRegOrd);
        }
        else
        {
            r.m_isExtReg = false;
        }
        r.m_isGPR = isGPR;

        if (r.m_kind == PhyRegUseKind::NoRenaming)
        {
            ReleaseAssert(!purpose.HasOrd());
            r.m_kindOrd = 0;
        }
        else
        {
            r.m_kindOrd = purpose.Ord();
        }
        return r;
    }

    size_t GetRegClassUsedSize(bool isScratch, bool isGPR, bool isExt)
    {
        return GetRegClassCounterRef(isScratch, isGPR, isExt);
    }

    X64Reg GetOperandReg(uint8_t operandOrd)
    {
        ReleaseAssert(m_operandRegMap.count(operandOrd));
        return m_operandRegMap[operandOrd];
    }

    // If a reg did not show up in the code at all, it will not show up in the returned map
    //
    std::unordered_map<X64Reg, StencilRegIdent> WARN_UNUSED GetRegPurposeForAllDfgRegisters();

private:
    PhyRegPurpose& GetPhyRegRawRef(bool isGPR, size_t mcRegOrd)
    {
        ReleaseAssertImp(isGPR, mcRegOrd < x_numGprRegs);
        ReleaseAssertImp(!isGPR, mcRegOrd < x_numFprRegs);
        return m_purpose[isGPR ? mcRegOrd : (x_numGprRegs + mcRegOrd)];
    }

    uint8_t& GetRegClassCounterRef(bool isScratch, bool isGPR, bool isExt)
    {
        size_t idx1 = (isScratch ? 0 : 1);
        size_t idx2;
        if (isGPR)
        {
            idx2 = (isExt ? 1 : 0);
        }
        else
        {
            ReleaseAssert(!isExt);
            idx2 = 2;
        }

        return m_ordCounter[idx1][idx2];
    }

    void UpdateOperandRegUseInfo(X64Reg reg, PhyRegPurpose purpose)
    {
        if (purpose.Kind() == PhyRegUseKind::OperandUse)
        {
            ReleaseAssert(purpose.HasOrd());
            ReleaseAssert(!m_operandRegMap.count(purpose.Ord()));
            m_operandRegMap[purpose.Ord()] = reg;
        }
    }

    bool m_finalized;

    // GPR regs comes first, then FPR regs
    //
    PhyRegPurpose m_purpose[x_numGprRegs + x_numFprRegs];

    // First dimension: Scratch = 0, PassThru = 1
    // Second dimension: NonExtGPR = 0, ExtGPR = 1, FPR = 2
    //
    uint8_t m_ordCounter[2][3];

    std::unordered_map<uint8_t /*operandOrd*/, X64Reg> m_operandRegMap;
};

struct StencilRegisterFileContextSetupHelper
{
    StencilRegisterFileContextSetupHelper();

    StencilRegisterFileContext* Ctx()
    {
        ReleaseAssert(m_ctx.get() != nullptr);
        return m_ctx.get();
    }

    // Set all remaining registers as scratch, finish setup and return the context
    //
    std::unique_ptr<StencilRegisterFileContext> WARN_UNUSED FinalizeAndGet();

    bool HasAvailableGprGroup1() { return HasFreeRegInList(RegClass::GprGroup1); }
    bool HasAvailableGprGroup2() { return HasFreeRegInList(RegClass::GprGroup2); }
    bool HasAvailableFpr() { return HasFreeRegInList(RegClass::Fpr); }

    X64Reg ConsumeGprGroup1(PhyRegPurpose purpose)
    {
        return ConsumeFromRegList(RegClass::GprGroup1, purpose);
    }

    X64Reg ConsumeGprGroup2(PhyRegPurpose purpose)
    {
        return ConsumeFromRegList(RegClass::GprGroup2, purpose);
    }

    X64Reg ConsumeFpr(PhyRegPurpose purpose)
    {
        return ConsumeFromRegList(RegClass::Fpr, purpose);
    }

private:
    enum class RegClass
    {
        GprGroup1,
        GprGroup2,
        Fpr,
        X_END_OF_ENUM
    };

    std::vector<X64Reg>& GetFreeRegList(RegClass rc)
    {
        ReleaseAssert(static_cast<size_t>(rc) < m_freeRegLists.size());
        return m_freeRegLists[static_cast<size_t>(rc)];
    }

    bool HasFreeRegInList(RegClass rc)
    {
        return GetFreeRegList(rc).size() > 0;
    }

    X64Reg ConsumeFromRegList(RegClass rc, PhyRegPurpose purpose)
    {
        std::vector<X64Reg>& freeList = GetFreeRegList(rc);
        ReleaseAssert(!freeList.empty());
        X64Reg reg = freeList.back();
        freeList.pop_back();
        Ctx()->SetRegPurpose(reg, purpose);
        return reg;
    }

    std::unique_ptr<StencilRegisterFileContext> m_ctx;
    std::vector<std::vector<X64Reg>> m_freeRegLists;
};

struct StencilRegRenamePatchItem
{
    StencilRegIdent m_ident;
    uint16_t m_byteOffset;
    uint8_t m_bitOffset;
    bool m_isFlipped;
};

// Compact encoded data stream for stencil reg patching at runtime
//
// See comments in drt/dfg_codegen_register_renamer.h
//
struct EncodedStencilRegPatchStream
{
    std::vector<uint16_t> m_data;

    // How many reg patches showed up in each code byte, for audit purpose only
    //
    std::vector<uint8_t> m_numRegsInEachCodeByte;

    bool IsValid() const { return m_data.size() > 0; }

    bool IsEmpty() const
    {
        ReleaseAssert(IsValid());
        ReleaseAssertImp(m_data.size() == 1, m_data[0] == 0);
        return m_data.size() == 1 && m_data[0] == 0;
    }

    // Applying this reg patch stream on the given code, currently for assertion and audit purpose
    //
    std::vector<uint8_t> WARN_UNUSED ApplyOnCode(const std::vector<uint8_t>& code, StencilRegisterFileContext* regCtx) const;

    // Emit the reg patch stream data as a LLVM constant global [i16 x XX]
    //
    llvm::GlobalVariable* WARN_UNUSED EmitDataAsLLVMConstantGlobal(llvm::Module* module) const;

    // Encode the more-readable patches into the compact data stream that can used for runtime reg renaming
    // DEVNOTE:
    //     The input machine code sequence is modified!
    //     The resulted data stream must be applied on the modified code, not the original code!
    //
    static EncodedStencilRegPatchStream WARN_UNUSED Create(
        std::vector<uint8_t>& machineCode /*inout*/,
        const std::vector<StencilRegRenamePatchItem>& patches);
};

struct StencilRegRenameParseResult
{
    // All the patches
    //
    std::vector<StencilRegRenamePatchItem> m_patches;
    // True if there exists an indirect call, which means that the stencil is not eligible for regalloc,
    // since we would need to save most of the registers across the call anyway
    //
    bool m_hasIndirectCall;
    // A list of offsets into the code where the direct call target relative offsets are stored
    // Regalloc is normally disabled for JIT code with C call (see above), but with the exception of
    // IC body calls and rarely-happening runtime calls. We will handle it specially, see below.
    //
    std::vector<size_t> m_directCallTargetOffsets;
    // We want to enable regalloc for stencils with C calls if the C call happens rarely.
    // Those calls uses preserve_most calling conv, which is same as C calling conv except that it preserves
    // all GPR except r11. That is, arguments are passed in rdi, rsi, rdx, rcx, r8, r9 and return values are in rax and rdx
    //
    // We don't care about rax, rcx and rdx since they already cannot be renamed (since AH/CH/DH subregisters are special)
    // so all we need to know is where rdi, rsi, r8 and r9 are mapped to during the renaming.
    //
    // We can then rewrite the call to call an AOT stub function instead. The stub function would save
    // rdi, rsi, r8, r9 and all the relavent FPU registers, then move the expected values to rdi, rsi, r8, r9,
    // do the call, and restore the context afterwards.
    //
    StencilRegIdent m_rsi;
    StencilRegIdent m_rdi;
    StencilRegIdent m_r8;
    StencilRegIdent m_r9;

    // A bitmask of all the used FPU registers, needed for saving context for C calls
    //
    uint64_t m_usedFpuRegs;

    StencilRegRenameParseResult()
        : m_hasIndirectCall(false)
        , m_usedFpuRegs(0)
    { }

    void Parse(StencilRegisterFileContext& ctx, std::vector<uint8_t> machineCode);
};

}   // namespace dast
