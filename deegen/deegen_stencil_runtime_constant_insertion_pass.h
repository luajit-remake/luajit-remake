#pragma once

#include "common_utils.h"
#include "misc_llvm_helper.h"
#include "anonymous_file.h"
#include "llvm/IR/ConstantRange.h"

namespace dast {

class CpPlaceholderExprPrinter;

// TODO: for best results, we should hash cons the runtime-constant expressions,
// as not hash-consing them prevents LLVM from merging identical basic blocks,
// and as a result, some of our JIT slow path currently contains redundant logic..
//
class CPRuntimeConstantNodeBase
{
protected:
    CPRuntimeConstantNodeBase(uint32_t bitWidth)
        : m_bitWidth(bitWidth)
        , m_range(bitWidth, true /*isFullSet*/)
    {
        ReleaseAssert(bitWidth == 8 || bitWidth == 16 || bitWidth == 32 || bitWidth == 64);
    }

    CPRuntimeConstantNodeBase(uint32_t bitWidth, int64_t value)
        : m_bitWidth(bitWidth)
        , m_range(llvm::APInt(bitWidth, static_cast<uint64_t>(value), true /*isSigned*/))
    {
        ReleaseAssert(bitWidth == 8 || bitWidth == 16 || bitWidth == 32 || bitWidth == 64);
    }

public:
    virtual ~CPRuntimeConstantNodeBase() { }

    virtual bool IsRawRuntimeConstant() { return false; }

    void SetFullRange()
    {
        m_range = llvm::ConstantRange(m_bitWidth, true /*isFullSet*/);
    }

    std::string WARN_UNUSED PrintExpr(CpPlaceholderExprPrinter*);

    virtual std::string WARN_UNUSED PrintExprImpl(CpPlaceholderExprPrinter*) = 0;

    std::string WARN_UNUSED GetSignedCppType()
    {
        return std::string("int") + std::to_string(m_bitWidth) + "_t";
    }

    std::string WARN_UNUSED GetUnsignedCppType()
    {
        return std::string("u") + GetSignedCppType();
    }

    // The bit-width of the value: 8, 16, 32 or 64
    //
    uint32_t m_bitWidth;
    llvm::ConstantRange m_range;
};

class CpPlaceholderExprPrinter
{
public:
    // Generates a piece of C++ code that computes the values of the expression list
    //
    static std::string WARN_UNUSED Print(const std::vector<CPRuntimeConstantNodeBase*>& exprs);

    std::string WARN_UNUSED CreateTmpVar()
    {
        std::string res = std::string(x_tmpVarPrefix) + std::to_string(m_curTmpVarOrd);
        m_curTmpVarOrd++;
        return res;
    }

    std::string WARN_UNUSED GetInputVarName(size_t label)
    {
        return std::string(x_inputVarPrefix) + std::to_string(label);
    }

    std::string GetResultVarName(size_t ord)
    {
        return std::string(x_resultVarPrefix) + std::to_string(ord);
    }

    FILE* GetFd() { return m_fd; }
    AnonymousFile& GetFile() { return m_buf; }

    std::unordered_map<CPRuntimeConstantNodeBase*, std::string /*varName*/> m_alreadyPrintedMap;

    CpPlaceholderExprPrinter() : m_curTmpVarOrd(0) { m_fd = m_buf.GetFStream("w"); }

private:

    // The input variable names for raw runtime constant: deegen_rc_input_* where * is the hole label
    //
    static constexpr const char* x_inputVarPrefix = "deegen_rc_input_";

    // The result variable names are named 'deegen_stencil_patch_value_*
    //
    static constexpr const char* x_resultVarPrefix = "deegen_stencil_patch_value_";

    // The variable names for intermediate computations
    //
    static constexpr const char* x_tmpVarPrefix = "deegen_tmp_value_";

    AnonymousFile m_buf;
    FILE* m_fd;
    size_t m_curTmpVarOrd;
};

// Describes a "root origin" of a runtime constant,
// i.e., the "raw value" that comes from the bytecode struct or inline cache struct, etc
//
class CPRawRuntimeConstant final : public CPRuntimeConstantNodeBase
{
public:
    // Note that in our current API, the range is [min, max], not [min, max)!!
    //
    CPRawRuntimeConstant(uint64_t label, uint32_t bitWidth, int64_t min, int64_t max)
        : CPRuntimeConstantNodeBase(bitWidth)
        , m_label(label)
    {
        ReleaseAssert(min <= max);
        if (bitWidth == 64)
        {
            if (min != std::numeric_limits<int64_t>::min() || max != std::numeric_limits<int64_t>::max())
            {
                ReleaseAssert(max != std::numeric_limits<int64_t>::max());  // for now..
                m_range = llvm::ConstantRange(llvm::APInt(bitWidth, static_cast<uint64_t>(min), true /*isSigned*/),
                                              llvm::APInt(bitWidth, static_cast<uint64_t>(max + 1), true /*isSigned*/));
            }
        }
        else
        {
            int64_t msk = (static_cast<int64_t>(1) << static_cast<int64_t>(bitWidth)) - 1;
            if (max - min < msk)    // not full range
            {
                // For sanity, also assert that the range [min, max] should makes sense for either signed/unsigned interpretation
                //
                if (0 <= min && max <= msk)
                {
                    if (max == msk) { max = 0; } else { max++; }
                    m_range = llvm::ConstantRange(llvm::APInt(bitWidth, static_cast<uint64_t>(min), false /*isSigned*/),
                                                  llvm::APInt(bitWidth, static_cast<uint64_t>(max), false /*isSigned*/));
                }
                else
                {
                    ReleaseAssert(-(msk / 2 + 1) <= min && max <= msk / 2);
                    if (max == msk / 2) { max = -(msk / 2 + 1); } else { max++; }
                    m_range = llvm::ConstantRange(llvm::APInt(bitWidth, static_cast<uint64_t>(min), true /*isSigned*/),
                                                  llvm::APInt(bitWidth, static_cast<uint64_t>(max), true /*isSigned*/));
                }
            }
        }
    }

    virtual std::string WARN_UNUSED PrintExprImpl(CpPlaceholderExprPrinter*) override;

    virtual bool IsRawRuntimeConstant() override { return true; }

    // A unique label for identification
    //
    uint64_t m_label;
};

// The following classes describe various forms of *derived* runtime constants.
// The value of a derived runtime constant is computed by an expression of the raw runtime constants.
//
// Ultimately, a derived runtime constant will be used by a non-runtime-constant computation, typically an addressing or comparison operation.
// Such a derived runtime constant becomes a "hole" in the copy-and-patch stencil
//

// Describes a fixed constant value, like 123
//
class CPExprFixedConstant final : public CPRuntimeConstantNodeBase
{
public:
    CPExprFixedConstant(uint32_t bitWidth, int64_t value)
        : CPRuntimeConstantNodeBase(bitWidth, value)
        , m_value(value)
    { }

    virtual std::string WARN_UNUSED PrintExprImpl(CpPlaceholderExprPrinter*) override;

    int64_t m_value;
};

// Describes an unary operation
//
class CPExprUnaryOp final : public CPRuntimeConstantNodeBase
{
public:
    enum Kind
    {
        ZExt,
        SExt,
        Trunc
    };

    CPExprUnaryOp(Kind kind, CPRuntimeConstantNodeBase* operand, uint32_t newWidth);

    virtual std::string WARN_UNUSED PrintExprImpl(CpPlaceholderExprPrinter*) override;

    Kind m_kind;
    CPRuntimeConstantNodeBase* m_operand;
};

// Describes a binary operation
//
class CPExprBinaryOp final : public CPRuntimeConstantNodeBase
{
public:
    enum Kind
    {
        // For now, just implement the simple ones
        // Support more operations later as we need them..
        //
        Add,
        Sub,
        Mul
    };

    CPExprBinaryOp(Kind kind, CPRuntimeConstantNodeBase* lhs, CPRuntimeConstantNodeBase* rhs);

    virtual std::string WARN_UNUSED PrintExprImpl(CpPlaceholderExprPrinter*) override;

    Kind m_kind;
    CPRuntimeConstantNodeBase* m_lhs;
    CPRuntimeConstantNodeBase* m_rhs;
};

class StencilRuntimeConstantInserter
{
public:
    // Add a raw runtime constant labelled 'label' with range [lb, ub]
    //
    void AddRawRuntimeConstant(uint64_t label, int64_t lb, int64_t ub)
    {
        ReleaseAssert(!m_rcRanges.count(label));
        ReleaseAssert(lb <= ub);
        m_rcRanges[label] = std::make_pair(lb, ub);
    }

    // Widen the range of an existing label to make it cover [lb, ub]
    //
    void WidenRange(uint64_t label, int64_t lb, int64_t ub)
    {
        ReleaseAssert(lb <= ub);
        ReleaseAssert(m_rcRanges.count(label));
        auto oldRange = m_rcRanges[label];
        ReleaseAssert(oldRange.first <= oldRange.second);
        lb = std::min(lb, oldRange.first);
        ub = std::max(ub, oldRange.second);
        ReleaseAssert(lb <= ub);
        m_rcRanges[label] = std::make_pair(lb, ub);
    }

    static constexpr int64_t GetLowAddrRangeUB()
    {
        return (static_cast<int64_t>(1) << 31) - (16 << 20) - 2048;
    }

    void AddRawRuntimeConstantAsLowAddressFnPointer(uint64_t label)
    {
        AddRawRuntimeConstant(label, 1, GetLowAddrRangeUB());
    }

    // Rewrite the function, returning a list of definitions for each derived runtime constant
    //
    std::vector<CPRuntimeConstantNodeBase*> WARN_UNUSED RunOnFunction(llvm::Function* func);

private:
    std::unordered_map<uint64_t /*label*/, std::pair<int64_t, int64_t> /*range*/> m_rcRanges;
};

llvm::GlobalVariable* WARN_UNUSED DeegenInsertOrGetCopyAndPatchPlaceholderSymbol(llvm::Module* module, uint64_t ord);

}   // namespace dast
