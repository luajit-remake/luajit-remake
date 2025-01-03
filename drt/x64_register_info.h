#pragma once

#include "common_utils.h"

// For use as non-type template parameter, implicitly convertible from X64Reg
//
struct X64RegNTTP
{
    uint8_t m_compositeVal;
};

class X64Reg
{
private:
    uint8_t m_compositeVal;

public:
    static constexpr size_t x_totalNumGprs = 16;
    static constexpr size_t x_totalNumFprs = 32;

    constexpr X64Reg() : m_compositeVal(0) { }

    constexpr X64Reg(X64RegNTTP value)
        : m_compositeVal(value.m_compositeVal)
    {
        TestAssert(CheckRegValid());
    }

    constexpr X64Reg(bool isGpr, size_t mcOrd)
    {
        m_compositeVal = static_cast<uint8_t>((isGpr ? 0 : 128) + mcOrd);
        TestAssert(CheckRegValid());
    }

    constexpr operator X64RegNTTP() const
    {
        return { .m_compositeVal = m_compositeVal };
    }

    constexpr bool CheckRegValid() const
    {
        if (IsGPR())
        {
            return MachineOrd() < x_totalNumGprs;
        }
        else
        {
            return MachineOrd() < x_totalNumFprs;
        }
    }

    static constexpr X64Reg GPR(size_t mcOrd) { return X64Reg(true, mcOrd); }
    static constexpr X64Reg FPR(size_t mcOrd) { return X64Reg(false, mcOrd); }

    constexpr bool IsGPR() const { return (m_compositeVal & 128) == 0; }
    constexpr bool IsFPR() const { return !IsGPR(); }
    constexpr uint8_t MachineOrd() const { return m_compositeVal & 127; }

    const char* GetName() const
    {
        static constexpr const char* gprNames[16] = {
            "RAX",
            "RCX",
            "RDX",
            "RBX",
            "RSP",
            "RBP",
            "RSI",
            "RDI",
            "R8",
            "R9",
            "R10",
            "R11",
            "R12",
            "R13",
            "R14",
            "R15"
        };
        static constexpr const char* fprNames[32] = {
            "XMM0", "XMM1", "XMM2", "XMM3", "XMM4", "XMM5", "XMM6", "XMM7",
            "XMM8", "XMM9", "XMM10", "XMM11", "XMM12", "XMM13", "XMM14", "XMM15",
            "XMM16", "XMM17", "XMM18", "XMM19", "XMM20", "XMM21", "XMM22", "XMM23",
            "XMM24", "XMM25", "XMM26", "XMM27", "XMM28", "XMM29", "XMM30", "XMM31"
        };

        if (IsGPR())
        {
            Assert(MachineOrd() < 16);
            return gprNames[MachineOrd()];
        }
        else
        {
            Assert(MachineOrd() < 32);
            return fprNames[MachineOrd()];
        }
    }

    // Get the 8/16/32-bit subregister name for GPR registers
    //
    template<size_t bitWidth>
    const char* GetSubRegisterName() const
    {
        TestAssert(IsGPR());
        if constexpr(bitWidth == 8)
        {
            static constexpr const char* gpr8[16] = {
                "AL", "CL", "DL", "BL",
                "SPL", "BPL", "SIL", "DIL",
                "R8L", "R9L", "R10L", "R11L",
                "R12L", "R13L", "R14L", "R15L"
            };
            Assert(MachineOrd() < 16);
            return gpr8[MachineOrd()];
        }
        else if constexpr(bitWidth == 16)
        {
            static constexpr const char* gpr16[16] = {
                "AX", "CX", "DX", "BX",
                "SP", "BP", "SI", "DI",
                "R8W", "R9W", "R10W", "R11W",
                "R12W", "R13W", "R14W", "R15W"
            };
            Assert(MachineOrd() < 16);
            return gpr16[MachineOrd()];
        }
        else
        {
            static_assert(bitWidth == 32);
            static constexpr const char* gpr32[16] = {
                "EAX", "ECX", "EDX", "EBX",
                "ESP", "EBP", "ESI", "EDI",
                "R8D", "R9D", "R10D", "R11D",
                "R12D", "R13D", "R14D", "R15D"
            };
            Assert(MachineOrd() < 16);
            return gpr32[MachineOrd()];
        }
    }

    const char* GetSubRegisterName(size_t bitWidth) const
    {
        if (bitWidth == 8)
        {
            return GetSubRegisterName<8>();
        }
        else if (bitWidth == 16)
        {
            return GetSubRegisterName<16>();
        }
        else
        {
            TestAssert(bitWidth == 32);
            return GetSubRegisterName<32>();
        }
    }

    constexpr uint8_t GetCompositeValueForStdHash() const { return m_compositeVal; }

    constexpr bool WARN_UNUSED operator==(const X64Reg& rhs) const
    {
        return m_compositeVal == rhs.m_compositeVal;
    }

    static const X64Reg RAX;
    static const X64Reg RCX;
    static const X64Reg RDX;
    static const X64Reg RBX;
    static const X64Reg RSP;
    static const X64Reg RBP;
    static const X64Reg RSI;
    static const X64Reg RDI;
    static const X64Reg R8;
    static const X64Reg R9;
    static const X64Reg R10;
    static const X64Reg R11;
    static const X64Reg R12;
    static const X64Reg R13;
    static const X64Reg R14;
    static const X64Reg R15;
    static const X64Reg XMM0;
    static const X64Reg XMM1;
    static const X64Reg XMM2;
    static const X64Reg XMM3;
    static const X64Reg XMM4;
    static const X64Reg XMM5;
    static const X64Reg XMM6;
    static const X64Reg XMM7;
};

constexpr X64Reg X64Reg::RAX = X64Reg::GPR(0);
constexpr X64Reg X64Reg::RCX = X64Reg::GPR(1);
constexpr X64Reg X64Reg::RDX = X64Reg::GPR(2);
constexpr X64Reg X64Reg::RBX = X64Reg::GPR(3);
constexpr X64Reg X64Reg::RSP = X64Reg::GPR(4);
constexpr X64Reg X64Reg::RBP = X64Reg::GPR(5);
constexpr X64Reg X64Reg::RSI = X64Reg::GPR(6);
constexpr X64Reg X64Reg::RDI = X64Reg::GPR(7);
constexpr X64Reg X64Reg::R8 = X64Reg::GPR(8);
constexpr X64Reg X64Reg::R9 = X64Reg::GPR(9);
constexpr X64Reg X64Reg::R10 = X64Reg::GPR(10);
constexpr X64Reg X64Reg::R11 = X64Reg::GPR(11);
constexpr X64Reg X64Reg::R12 = X64Reg::GPR(12);
constexpr X64Reg X64Reg::R13 = X64Reg::GPR(13);
constexpr X64Reg X64Reg::R14 = X64Reg::GPR(14);
constexpr X64Reg X64Reg::R15 = X64Reg::GPR(15);
constexpr X64Reg X64Reg::XMM0 = X64Reg::FPR(0);
constexpr X64Reg X64Reg::XMM1 = X64Reg::FPR(1);
constexpr X64Reg X64Reg::XMM2 = X64Reg::FPR(2);
constexpr X64Reg X64Reg::XMM3 = X64Reg::FPR(3);
constexpr X64Reg X64Reg::XMM4 = X64Reg::FPR(4);
constexpr X64Reg X64Reg::XMM5 = X64Reg::FPR(5);
constexpr X64Reg X64Reg::XMM6 = X64Reg::FPR(6);
constexpr X64Reg X64Reg::XMM7 = X64Reg::FPR(7);

template<>
struct ::std::hash<X64Reg>
{
    std::size_t operator()(const X64Reg& reg) const noexcept
    {
        return std::hash<uint8_t>{}(reg.GetCompositeValueForStdHash());
    }
};

template<typename ActionFunc>
void ALWAYS_INLINE ForEachX64GPR(const ActionFunc& action)
{
    for (size_t i = 0; i < X64Reg::x_totalNumGprs; i++)
    {
        action(X64Reg::GPR(i));
    }
}

template<typename ActionFunc>
void ALWAYS_INLINE ForEachX64FPR(const ActionFunc& action)
{
    for (size_t i = 0; i < X64Reg::x_totalNumFprs; i++)
    {
        action(X64Reg::FPR(i));
    }
}

template<typename ActionFunc>
void ALWAYS_INLINE ForEachX64Register(const ActionFunc& action)
{
    ForEachX64GPR(action);
    ForEachX64FPR(action);
}
