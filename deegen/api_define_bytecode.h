#pragma once

#include "tvalue.h"
#include "deegen_for_each_bytecode_intrinsic.h"

enum class DeegenBytecodeOperandType
{
    INVALID_TYPE,
    BytecodeSlotOrConstant,     // this is a pseudo-type that must be concretized in each Variant()
    BytecodeSlot,
    Constant,
    BytecodeRangeRO,
    BytecodeRangeRW,
    Int8,
    UInt8,
    Int16,
    UInt16,
    Int32,
    UInt32
};

enum class DeegenSpecializationKind : uint8_t
{
    // Must be first member
    //
    NotSpecialized,
    Literal,
    SpeculatedTypeForOptimizer,
    BytecodeSlot,
    BytecodeConstantWithType
};

namespace detail
{

constexpr bool DeegenBytecodeOperandIsLiteralType(DeegenBytecodeOperandType value)
{
    switch (value)
    {
    case DeegenBytecodeOperandType::INVALID_TYPE:
        ReleaseAssert(false);
    case DeegenBytecodeOperandType::BytecodeSlotOrConstant:
    case DeegenBytecodeOperandType::BytecodeSlot:
    case DeegenBytecodeOperandType::Constant:
    case DeegenBytecodeOperandType::BytecodeRangeRO:
    case DeegenBytecodeOperandType::BytecodeRangeRW:
        return false;
    case DeegenBytecodeOperandType::Int8:
    case DeegenBytecodeOperandType::UInt8:
    case DeegenBytecodeOperandType::Int16:
    case DeegenBytecodeOperandType::UInt16:
    case DeegenBytecodeOperandType::Int32:
    case DeegenBytecodeOperandType::UInt32:
        return true;
    }
}

}   // namespace detail

struct DeegenFrontendBytecodeDefinitionDescriptor
{
    constexpr static size_t x_maxOperands = 10;
    constexpr static size_t x_maxQuickenings = 1;   // currently only hot-cold splitting supported
    constexpr static size_t x_maxVariants = 50;

    struct Operand
    {
        const char* m_name;
        DeegenBytecodeOperandType m_type;

        consteval Operand() : m_name(), m_type(DeegenBytecodeOperandType::INVALID_TYPE) { }
        consteval Operand(const char* name, DeegenBytecodeOperandType ty) : m_name(name), m_type(ty) { }
    };

    static consteval Operand BytecodeSlotOrConstant(const char* name)
    {
        return Operand { name, DeegenBytecodeOperandType::BytecodeSlotOrConstant };
    }

    static consteval Operand BytecodeSlot(const char* name)
    {
        return Operand { name, DeegenBytecodeOperandType::BytecodeSlot };
    }

    static consteval Operand Constant(const char* name)
    {
        return Operand { name, DeegenBytecodeOperandType::Constant };
    }

    template<typename T>
    static consteval Operand Literal(const char* name)
    {
        if constexpr(std::is_same_v<T, uint8_t>)
        {
            return Operand { name, DeegenBytecodeOperandType::UInt8 };
        }
        else if constexpr(std::is_same_v<T, int8_t>)
        {
            return Operand { name, DeegenBytecodeOperandType::Int8 };
        }
        else if constexpr(std::is_same_v<T, uint16_t>)
        {
            return Operand { name, DeegenBytecodeOperandType::UInt16 };
        }
        else if constexpr(std::is_same_v<T, int16_t>)
        {
            return Operand { name, DeegenBytecodeOperandType::Int16 };
        }
        else if constexpr(std::is_same_v<T, uint32_t>)
        {
            return Operand { name, DeegenBytecodeOperandType::UInt32 };
        }
        else
        {
            static_assert(std::is_same_v<T, int32_t>, "unhandled type");
            return Operand { name, DeegenBytecodeOperandType::Int32 };
        }
    }

    static consteval Operand BytecodeRangeBaseRO(const char* name)
    {
        return Operand { name, DeegenBytecodeOperandType::BytecodeRangeRO };
    }

    static consteval Operand BytecodeRangeBaseRW(const char* name)
    {
        return Operand { name, DeegenBytecodeOperandType::BytecodeRangeRW };
    }

    enum class RegHint : uint8_t
    {
        // The operand should be put into GPR
        //
        GPR,
        // The operand should be put into FPR
        //
        FPR,
        X_END_OF_ENUM
    };

    struct OperandRegPriorityInfo
    {
        constexpr OperandRegPriorityInfo() : m_numTerms(static_cast<uint8_t>(-1)), m_priorityList() { }

        static constexpr size_t x_maxTerms = static_cast<size_t>(RegHint::X_END_OF_ENUM);

        consteval void Append(RegHint value)
        {
            ReleaseAssert(m_numTerms < x_maxTerms);
            m_priorityList[m_numTerms] = value;
            m_numTerms++;
        }

        uint8_t m_numTerms;
        RegHint m_priorityList[x_maxTerms];
    };

    struct RegPriorityInfo
    {
        constexpr RegPriorityInfo() : m_data() { }

        consteval void Set(size_t ord, OperandRegPriorityInfo info)
        {
            ReleaseAssert(ord < x_maxOperands);
            ReleaseAssert(info.m_numTerms != static_cast<size_t>(-1));
            ReleaseAssert(info.m_numTerms <= info.x_maxTerms);
            for (size_t i = 0; i < info.m_numTerms; i++)
            {
                ReleaseAssert(info.m_priorityList[i] != RegHint::X_END_OF_ENUM);
                for (size_t j = i + 1; j < info.m_numTerms; j++)
                {
                    ReleaseAssert(info.m_priorityList[i] != info.m_priorityList[j]);
                }
            }
            m_data[ord] = info;
        }

        OperandRegPriorityInfo m_data[x_maxOperands];
    };

    struct SpecializedOperand
    {
        constexpr SpecializedOperand() : m_kind(DeegenSpecializationKind::NotSpecialized), m_value(0), m_regHint() { }
        constexpr SpecializedOperand(DeegenSpecializationKind kind, uint64_t value) : m_kind(kind), m_value(value), m_regHint() { }
        constexpr SpecializedOperand(OperandRegPriorityInfo regHint) : SpecializedOperand() { m_regHint = regHint; }

        DeegenSpecializationKind m_kind;
        // The interpretation of 'm_value' depends on 'm_kind'
        // m_kind == Literal: m_value is the value of the literal casted to uint64_t
        // m_kind == SpeculatedTypeForOptimizer: m_value is the type speculation mask
        // m_kind == BytecodeSlot: m_value is unused
        // m_kind == BytecodeConstantWithType: m_value is the type speculation mask
        //
        uint64_t m_value;
        OperandRegPriorityInfo m_regHint;
    };

    struct SpecializedOperandRef
    {
        template<typename... Args>
        consteval SpecializedOperandRef WARN_UNUSED RegHint(Args... args)
        {
            constexpr size_t nargs = sizeof...(args);
            std::array<DeegenFrontendBytecodeDefinitionDescriptor::RegHint, nargs> arr { args... };
            OperandRegPriorityInfo info;
            ReleaseAssert(nargs <= OperandRegPriorityInfo::x_maxTerms);
            info.m_numTerms = nargs;
            for (size_t i = 0; i < nargs; i++)
            {
                info.m_priorityList[i] = arr[i];
            }
            SpecializedOperandRef ret = *this;
            ReleaseAssert(ret.m_operand.m_regHint.m_numTerms == static_cast<uint8_t>(-1));
            ret.m_operand.m_regHint = info;
            return ret;
        }

        SpecializedOperand m_operand;
        size_t m_ord;
    };

    struct OperandRef
    {
        template<typename T>
        consteval SpecializedOperandRef WARN_UNUSED HasValue(T val)
        {
            static_assert(std::is_integral_v<T>);
            ReleaseAssert(m_ord != static_cast<size_t>(-1));
            ReleaseAssert(detail::DeegenBytecodeOperandIsLiteralType(m_operand.m_type));
            uint64_t spVal64 = static_cast<uint64_t>(static_cast<std::conditional_t<std::is_signed_v<T>, int64_t, uint64_t>>(val));
            return { .m_operand = { DeegenSpecializationKind::Literal, spVal64 }, .m_ord = m_ord };
        }

        consteval SpecializedOperandRef WARN_UNUSED HasType(TypeMaskTy typeMask)
        {
            ReleaseAssert(m_ord != static_cast<size_t>(-1));
            ReleaseAssert(m_operand.m_type == DeegenBytecodeOperandType::BytecodeSlotOrConstant || m_operand.m_type == DeegenBytecodeOperandType::BytecodeSlot || m_operand.m_type == DeegenBytecodeOperandType::Constant);
            return { .m_operand = { DeegenSpecializationKind::SpeculatedTypeForOptimizer, typeMask }, .m_ord = m_ord };
        }

        template<typename T>
        consteval SpecializedOperandRef WARN_UNUSED HasType()
        {
            static_assert(IsValidTypeSpecialization<T>);
            return HasType(x_typeMaskFor<T>);
        }

        consteval SpecializedOperandRef WARN_UNUSED IsBytecodeSlot()
        {
            ReleaseAssert(m_ord != static_cast<size_t>(-1));
            ReleaseAssert(m_operand.m_type == DeegenBytecodeOperandType::BytecodeSlotOrConstant);
            return { .m_operand = { DeegenSpecializationKind::BytecodeSlot, 0 }, .m_ord = m_ord };
        }

        template<typename T = tTop>
        consteval SpecializedOperandRef WARN_UNUSED IsConstant()
        {
            static_assert(IsValidTypeSpecialization<T>);
            ReleaseAssert(m_ord != static_cast<size_t>(-1));
            ReleaseAssert(m_operand.m_type == DeegenBytecodeOperandType::BytecodeSlotOrConstant || m_operand.m_type == DeegenBytecodeOperandType::Constant);
            return { .m_operand = { DeegenSpecializationKind::BytecodeConstantWithType, x_typeMaskFor<T> }, .m_ord = m_ord };
        }

        // Specify the register kind(s) (GPR/FPR) an operand should reside in for reg alloc
        // Order matters: earlier option has higher priority
        //
        template<typename... Args>
        consteval SpecializedOperandRef WARN_UNUSED RegHint(Args... args)
        {
            constexpr size_t nargs = sizeof...(args);
            std::array<DeegenFrontendBytecodeDefinitionDescriptor::RegHint, nargs> arr { args... };
            OperandRegPriorityInfo info;
            ReleaseAssert(nargs <= OperandRegPriorityInfo::x_maxTerms);
            info.m_numTerms = nargs;
            for (size_t i = 0; i < nargs; i++)
            {
                info.m_priorityList[i] = arr[i];
            }
            return { .m_operand = { info }, .m_ord = m_ord };
        }

        size_t m_ord;
        Operand m_operand;
    };

    // This struct must not contain any pointers!
    //
    struct OperandExprNode
    {
        enum Kind
        {
            Number,
            Operand,
            Infinity,
            Add,
            Sub,
            BadKind
        };

        consteval OperandExprNode() : m_kind(Kind::BadKind), m_left(0), m_right(0), m_number(0)
        {
            for (size_t i = 0; i < x_maxOperandNameLength; i++) { m_operandName[i] = '\0'; }
        }

        consteval bool HasLeftChild()
        {
            return m_kind == Kind::Add || m_kind == Kind::Sub;
        }

        consteval bool HasRightChild()
        {
            return m_kind == Kind::Add || m_kind == Kind::Sub;
        }

        std::string WARN_UNUSED PrintCppExpression(const std::string& operandStructName, OperandExprNode* holder)
        {
            switch (m_kind)
            {
            case Kind::Number:
            {
                return "(" + std::to_string(m_number) + ")";
            }
            case Kind::Operand:
            {
                return "(" + operandStructName + "." + std::string(m_operandName) + ".AsRawValue())";
            }
            case Kind::Infinity:
            {
                ReleaseAssert(this == holder && "Infinity must be an expression by itself, it cannot be used in computation!");
                return "(-1)";
            }
            case Kind::Add:
            {
                return "(" + holder[m_left].PrintCppExpression(operandStructName, holder) + "+"
                    + holder[m_right].PrintCppExpression(operandStructName, holder) + ")";
            }
            case Kind::Sub:
            {
                return "(" + holder[m_left].PrintCppExpression(operandStructName, holder) + "-"
                    + holder[m_right].PrintCppExpression(operandStructName, holder) + ")";
            }
            case Kind::BadKind:
            {
                ReleaseAssert(false);
            }
            }   /*switch*/
        }

        static constexpr size_t x_maxOperandNameLength = 32;
        Kind m_kind;
        size_t m_left;
        size_t m_right;
        char m_operandName[x_maxOperandNameLength];
        int64_t m_number;
    };

    // This struct must not contain any pointers!
    //
    struct OperandExpr
    {
        constexpr OperandExpr()
            : m_numNodes(0)
        { }

        consteval void Put(OperandExpr other, size_t start)
        {
            ReleaseAssert(start + other.m_numNodes <= x_maxNodes);
            for (size_t i = 0; i < other.m_numNodes; i++)
            {
                m_nodes[start + i] = other.m_nodes[i];
                if (m_nodes[start + i].HasLeftChild())
                {
                    ReleaseAssert(0 <= m_nodes[start + i].m_left && m_nodes[start + i].m_left < other.m_numNodes);
                    m_nodes[start + i].m_left += start;
                }
                if (m_nodes[start + i].HasRightChild())
                {
                    ReleaseAssert(0 <= m_nodes[start + i].m_right && m_nodes[start + i].m_right < other.m_numNodes);
                    m_nodes[start + i].m_right += start;
                }
            }
        }

        static consteval OperandExpr WARN_UNUSED ConstructBinaryExpr(OperandExprNode::Kind kind, OperandExpr lhs, OperandExpr rhs)
        {
            ReleaseAssert(lhs.m_numNodes > 0 && rhs.m_numNodes > 0);
            OperandExpr res;
            res.m_numNodes = lhs.m_numNodes + rhs.m_numNodes + 1;
            ReleaseAssert(res.m_numNodes <= x_maxNodes);
            res.m_nodes[0].m_kind = kind;
            res.m_nodes[0].m_left = 1;
            res.m_nodes[0].m_right = 1 + lhs.m_numNodes;
            res.Put(lhs, 1);
            res.Put(rhs, 1 + lhs.m_numNodes);
            return res;
        }

        static consteval OperandExpr WARN_UNUSED ConstructNumber(int64_t value)
        {
            OperandExpr res;
            res.m_numNodes = 1;
            res.m_nodes[0].m_kind = OperandExprNode::Number;
            res.m_nodes[0].m_number = value;
            return res;
        }

        static consteval OperandExpr WARN_UNUSED ConstructOperandRef(const char* name)
        {
            OperandExpr res;
            res.m_numNodes = 1;
            res.m_nodes[0].m_kind = OperandExprNode::Operand;
            std::string_view strName { name };
            ReleaseAssert(strName.length() < OperandExprNode::x_maxOperandNameLength);
            for (size_t i = 0; i < strName.length(); i++)
            {
                res.m_nodes[0].m_operandName[i] = strName[i];
            }
            res.m_nodes[0].m_operandName[strName.length()] = '\0';
            return res;
        }

        static consteval OperandExpr WARN_UNUSED ConstructInfinity()
        {
            OperandExpr res;
            res.m_numNodes = 1;
            res.m_nodes[0].m_kind = OperandExprNode::Infinity;
            return res;
        }

        std::string WARN_UNUSED PrintCppExpression(const std::string& operandStructName)
        {
            ReleaseAssert(m_numNodes > 0);
            return m_nodes[0].PrintCppExpression(operandStructName, m_nodes);
        }

        bool IsInfinity()
        {
            ReleaseAssert(m_numNodes > 0);
            ReleaseAssertImp(m_nodes[0].m_kind == OperandExprNode::Infinity, m_numNodes == 1);
            return m_nodes[0].m_kind == OperandExprNode::Infinity;
        }

        static constexpr size_t x_maxNodes = 10;
        size_t m_numNodes;
        OperandExprNode m_nodes[x_maxNodes];
    };

    struct Infinity { };

    struct Range
    {
        consteval Range()
            : m_start()
            , m_len()
            , m_typeDeductionRuleFn(nullptr)
            , m_shouldValueProfileRange(false)
            , m_isExplicitlyNoProfile(false)
            , m_isFixedOutputTypeMask(false)
            , m_fixedOutputTypeMask(0)
        { }

        consteval Range(OperandExpr start, OperandExpr len)
            : m_start(start)
            , m_len(len)
            , m_typeDeductionRuleFn(nullptr)
            , m_shouldValueProfileRange(false)
            , m_isExplicitlyNoProfile(false)
            , m_isFixedOutputTypeMask(false)
            , m_fixedOutputTypeMask(0)
        { }

        consteval Range(OperandExpr start, OperandRef len)
            : Range(start,
                    OperandExpr::ConstructOperandRef(len.m_operand.m_name))
        { }

        consteval Range(OperandExpr start, int64_t len)
            : Range(start,
                    OperandExpr::ConstructNumber(len))
        { }

        consteval Range(OperandRef start, OperandExpr len)
            : Range(OperandExpr::ConstructOperandRef(start.m_operand.m_name),
                    len)
        { }

        consteval Range(OperandRef start, OperandRef len)
            : Range(OperandExpr::ConstructOperandRef(start.m_operand.m_name),
                    OperandExpr::ConstructOperandRef(len.m_operand.m_name))
        { }

        consteval Range(OperandRef start, int64_t len)
            : Range(OperandExpr::ConstructOperandRef(start.m_operand.m_name),
                    OperandExpr::ConstructNumber(len))
        { }

        consteval Range(int64_t start, OperandExpr len)
            : Range(OperandExpr::ConstructNumber(start),
                    len)
        { }

        consteval Range(int64_t start, OperandRef len)
            : Range(OperandExpr::ConstructNumber(start),
                    OperandExpr::ConstructOperandRef(len.m_operand.m_name))
        { }

        consteval Range(int64_t start, int64_t len)
            : Range(OperandExpr::ConstructNumber(start),
                    OperandExpr::ConstructNumber(len))
        { }

        // For internal use only
        //
        consteval Range(OperandExpr start, Infinity /*unused*/)
            : Range(start,
                    OperandExpr::ConstructInfinity())
        { }

        // For internal use only
        //
        consteval Range(OperandRef start, Infinity /*unused*/)
            : Range(OperandExpr::ConstructOperandRef(start.m_operand.m_name),
                    OperandExpr::ConstructInfinity())
        { }

        // For internal use only
        //
        consteval Range(int64_t start, Infinity /*unused*/)
            : Range(OperandExpr::ConstructNumber(start),
                    OperandExpr::ConstructInfinity())
        { }

        // Specify the type deduction rule for the range of values written by the bytecode
        // The function should start with a 'size_t ord' parameter, and returns the type mask for ordinal 'ord' of the range
        //
        template<typename T>
        consteval std::pair<Range, T> WARN_UNUSED TypeDeductionRule(T v)
        {
            return std::make_pair(*this, v);
        }

        OperandExpr m_start;
        OperandExpr m_len;
        void* m_typeDeductionRuleFn;
        bool m_shouldValueProfileRange;
        bool m_isExplicitlyNoProfile;
        bool m_isFixedOutputTypeMask;
        TypeMaskTy m_fixedOutputTypeMask;
    };

    struct VariadicArguments
    {
        consteval VariadicArguments() : m_value(true) { }
        consteval VariadicArguments(bool value) : m_value(value) { }
        bool m_value;
    };

    struct VariadicResults
    {
        consteval VariadicResults() : m_value(true) { }
        consteval VariadicResults(bool value) : m_value(value) { }
        bool m_value;
    };

    struct DeclareRWCInfo
    {
        constexpr DeclareRWCInfo(bool isForWriteDecl)
            : m_apiCalled(false)
            , m_isForWriteDecl(isForWriteDecl)
            , m_accessesVariadicArgs(false)
            , m_accessesVariadicResults(false)
            , m_numRanges(0)
        { }

        template<typename... Args>
        consteval void Process(DeegenFrontendBytecodeDefinitionDescriptor* owner, VariadicArguments va, Args... args)
        {
            m_apiCalled = true;
            if (va.m_value)
            {
                ReleaseAssert(!m_accessesVariadicArgs);
                m_accessesVariadicArgs = true;
            }
            Process(owner, args...);
        }

        template<typename... Args>
        consteval void Process(DeegenFrontendBytecodeDefinitionDescriptor* owner, VariadicResults vr, Args... args)
        {
            m_apiCalled = true;
            if (vr.m_value)
            {
                ReleaseAssert(!m_accessesVariadicResults);
                m_accessesVariadicResults = true;
            }
            Process(owner, args...);
        }

        template<typename... Args>
        consteval void Process(DeegenFrontendBytecodeDefinitionDescriptor* owner, Range range, Args... args)
        {
            m_apiCalled = true;
            ReleaseAssert(m_numRanges < x_maxRangeAnnotations && "too many read ranges, please raise x_maxRangeAnnotations");
            m_ranges[m_numRanges] = range;
            m_numRanges++;
            Process(owner, args...);
        }

        template<typename T, typename... Args>
        consteval void Process(DeegenFrontendBytecodeDefinitionDescriptor* owner, std::pair<Range, T> rangeWithTDR, Args... args)
        {
            m_apiCalled = true;
            ReleaseAssert(m_numRanges < x_maxRangeAnnotations && "too many read ranges, please raise x_maxRangeAnnotations");
            ReleaseAssert(m_isForWriteDecl && "TypeDeductionRule should only be specified for DeclareWrite!");
            m_ranges[m_numRanges] = rangeWithTDR.first;
            if constexpr(std::is_same_v<T, ValueProfileTagEnum>)
            {
                if (rangeWithTDR.second == ValueProfile)
                {
                    m_ranges[m_numRanges].m_shouldValueProfileRange = true;
                }
                else
                {
                    ReleaseAssert(rangeWithTDR.second == DoNotProfile);
                    m_ranges[m_numRanges].m_isExplicitlyNoProfile = true;
                }
            }
            else if constexpr(std::is_same_v<T, AlwaysOutputMask>)
            {
                m_ranges[m_numRanges].m_isFixedOutputTypeMask = true;
                m_ranges[m_numRanges].m_fixedOutputTypeMask = rangeWithTDR.second.m_mask;
            }
            else
            {
                owner->ValidateTypeDeductionRulePrototype<true /*forRange*/>(rangeWithTDR.second);
                m_ranges[m_numRanges].m_typeDeductionRuleFn = FOLD_CONSTEXPR(reinterpret_cast<void*>(+(rangeWithTDR.second)));
            }
            m_numRanges++;
            Process(owner, args...);
        }

        consteval void Process(DeegenFrontendBytecodeDefinitionDescriptor*) { m_apiCalled = true; }

        static constexpr size_t x_maxRangeAnnotations = 4;      // Raise as necessary

        bool m_apiCalled;
        bool m_isForWriteDecl;
        bool m_accessesVariadicArgs;
        bool m_accessesVariadicResults;
        size_t m_numRanges;
        Range m_ranges[x_maxRangeAnnotations];
    };

    struct SpecializedVariant
    {
        constexpr SpecializedVariant()
            : m_enableHCS(false)
            , m_numQuickenings(0)
            , m_numOperands(0)
        {
            for (size_t i = 0; i < x_maxOperands; i++)
            {
                m_operandTypes[i] = DeegenBytecodeOperandType::INVALID_TYPE;
            }
        }

        struct Quickening
        {
            SpecializedOperand value[x_maxOperands];
        };

        template<typename... Args>
        consteval SpecializedVariant& AddQuickeningCandidate(Args... args)
        {
            // TODO: this is not implemented yet..
            //
            ReleaseAssert(!m_enableHCS && "this function call must not be used together with 'EnableHotColdSplitting'.");
            AddNewQuickening(args...);
            return *this;
        }

        template<typename... Args>
        consteval SpecializedVariant& EnableHotColdSplitting(Args... args)
        {
            ReleaseAssert(m_numQuickenings == 0 && "this function call must not be used together with 'AddQuickeningCandidate'.");
            m_enableHCS = true;
            AddNewQuickening(args...);
            return *this;
        }

        bool m_enableHCS;
        size_t m_numQuickenings;
        size_t m_numOperands;
        DeegenBytecodeOperandType m_operandTypes[x_maxOperands];
        SpecializedOperand m_base[x_maxOperands];
        OperandRegPriorityInfo m_baseOutputRegPriority;
        Quickening m_quickenings[x_maxQuickenings];

    private:
        template<typename... Args>
        consteval void AddNewQuickening(Args... args)
        {
            constexpr size_t n = sizeof...(Args);
            std::array<SpecializedOperandRef, n> arr { args... };

            for (size_t i = 0; i < n; i++)
            {
                for (size_t j = 0; j < i; j++)
                {
                    ReleaseAssert(arr[i].m_ord != arr[j].m_ord);
                }
            }

            ReleaseAssert(m_numQuickenings < x_maxQuickenings);
            Quickening& q = m_quickenings[m_numQuickenings];
            m_numQuickenings++;

            for (size_t i = 0; i < n; i++)
            {
                ReleaseAssert(arr[i].m_ord < m_numOperands);
                ReleaseAssert(arr[i].m_operand.m_kind == DeegenSpecializationKind::SpeculatedTypeForOptimizer ||
                              arr[i].m_operand.m_kind == DeegenSpecializationKind::NotSpecialized);
                size_t ord = arr[i].m_ord;
                DeegenBytecodeOperandType originalOperandType = m_operandTypes[ord];
                ReleaseAssert(originalOperandType == DeegenBytecodeOperandType::BytecodeSlot ||
                              originalOperandType == DeegenBytecodeOperandType::BytecodeSlotOrConstant ||
                              originalOperandType == DeegenBytecodeOperandType::Constant);

                q.value[ord] = arr[i].m_operand;
            }
        }
    };

    template<bool forDfg, typename... Args>
    consteval SpecializedVariant& AddVariantImpl(Args... args)
    {
        ReleaseAssert(m_operandTypeListInitialized);

        constexpr size_t n = sizeof...(Args);
        std::array<SpecializedOperandRef, n> arr { args... };

        for (size_t i = 0; i < n; i++)
        {
            for (size_t j = 0; j < i; j++)
            {
                ReleaseAssert(arr[i].m_ord != arr[j].m_ord);
            }
        }

        if (forDfg)
        {
            ReleaseAssert(m_numDfgVariants < x_maxVariants);
        }
        else
        {
            ReleaseAssert(m_numVariants < x_maxVariants);
        }

        SpecializedVariant& r = forDfg ? m_dfgVariants[m_numDfgVariants] : m_variants[m_numVariants];

        if (forDfg)
        {
            m_numDfgVariants++;
        }
        else
        {
            m_numVariants++;
        }

        r.m_numOperands = m_numOperands;
        for (size_t i = 0; i < m_numOperands; i++)
        {
            r.m_operandTypes[i] = m_operandTypes[i].m_type;
        }

        for (size_t i = 0; i < n; i++)
        {
            if (arr[i].m_ord == static_cast<size_t>(-1))
            {
                ReleaseAssert(arr[i].m_operand.m_kind == DeegenSpecializationKind::NotSpecialized);
                r.m_baseOutputRegPriority = arr[i].m_operand.m_regHint;
            }
            else
            {
                ReleaseAssert(arr[i].m_ord < m_numOperands);
                r.m_base[arr[i].m_ord] = arr[i].m_operand;
            }
        }

        for (size_t i = 0; i < m_numOperands; i++)
        {
            SpecializedOperand o = r.m_base[i];
            switch (m_operandTypes[i].m_type)
            {
            case DeegenBytecodeOperandType::INVALID_TYPE:
            {
                ReleaseAssert(false);
                break;
            }
            case DeegenBytecodeOperandType::BytecodeSlotOrConstant:
            {
                if (forDfg)
                {
                    ReleaseAssert(o.m_kind == DeegenSpecializationKind::NotSpecialized || o.m_kind == DeegenSpecializationKind::SpeculatedTypeForOptimizer);
                }
                else
                {
                    ReleaseAssert((o.m_kind == DeegenSpecializationKind::BytecodeSlot || o.m_kind == DeegenSpecializationKind::BytecodeConstantWithType) && "All BytecodeSlotOrConstant must be specialized in each variant");
                }
                break;
            }
            case DeegenBytecodeOperandType::BytecodeSlot:
            {
                if (forDfg)
                {
                    ReleaseAssert(o.m_kind == DeegenSpecializationKind::NotSpecialized || o.m_kind == DeegenSpecializationKind::SpeculatedTypeForOptimizer);
                }
                else
                {
                    ReleaseAssert(o.m_kind == DeegenSpecializationKind::NotSpecialized);
                }
                break;
            }
            case DeegenBytecodeOperandType::Constant:
            {
                if (forDfg)
                {
                    ReleaseAssert(o.m_kind == DeegenSpecializationKind::NotSpecialized || o.m_kind == DeegenSpecializationKind::SpeculatedTypeForOptimizer);
                }
                else
                {
                    ReleaseAssert(o.m_kind == DeegenSpecializationKind::NotSpecialized || o.m_kind == DeegenSpecializationKind::BytecodeConstantWithType);
                }
                break;
            }
            case DeegenBytecodeOperandType::BytecodeRangeRO:
            case DeegenBytecodeOperandType::BytecodeRangeRW:
            {
                ReleaseAssert(o.m_kind == DeegenSpecializationKind::NotSpecialized);
                break;
            }
            case DeegenBytecodeOperandType::Int8:
            case DeegenBytecodeOperandType::UInt8:
            case DeegenBytecodeOperandType::Int16:
            case DeegenBytecodeOperandType::UInt16:
            case DeegenBytecodeOperandType::Int32:
            case DeegenBytecodeOperandType::UInt32:
            {
                ReleaseAssert(o.m_kind == DeegenSpecializationKind::NotSpecialized || o.m_kind == DeegenSpecializationKind::Literal);
                break;
            }
            }
        }

        return r;
    }

    template<typename... Args>
    consteval SpecializedVariant& Variant(Args... args)
    {
        return AddVariantImpl<false /*forDfg*/>(args...);
    }

    template<typename... Args>
    consteval SpecializedVariant& DfgVariant(Args... args)
    {
        return AddVariantImpl<true /*forDfg*/>(args...);
    }

    template<typename... Args>
    consteval void Operands(Args... args)
    {
        ReleaseAssert(!m_operandTypeListInitialized);
        m_operandTypeListInitialized = true;

        constexpr size_t n = sizeof...(Args);
        static_assert(n < x_maxOperands);

        m_numOperands = n;
        std::array<Operand, n> arr { args... };
        for (size_t i = 0; i < n; i++)
        {
            for (size_t j = 0; j < i; j++)
            {
                ReleaseAssert(std::string_view { arr[i].m_name } != std::string_view { arr[j].m_name });
            }
            ReleaseAssert(arr[i].m_type != DeegenBytecodeOperandType::INVALID_TYPE);
            m_operandTypes[i] = arr[i];
        }
    }

    struct RangedInputTypeMaskGetter
    {
        // ordInRange must be valid (declared to be read by the range using DeclareReads)!
        //
        TypeMask WARN_UNUSED ALWAYS_INLINE Get(size_t ordInRange)
        {
            TypeMask* msk = reinterpret_cast<TypeMask**>(this)[ordInRange];
            return *msk;
        }
    };

    // The type deduction rule function should have the following prototype based on the bytecode operands:
    //   TValue operand -> TypeMask
    //   Range operand -> RangeTsmGetter*
    //   Literal operand -> the literal type
    //
    // If the rule is for an output range, a size_t should come preceding all the operands,
    // which is the ordinal of the value in the range to query
    //
    template<bool forRange, size_t bcOperandOrd, typename T>
    consteval void ValidateTypeDeductionRulePrototypeImpl()
    {
        constexpr size_t argOrd = bcOperandOrd + (forRange ? 1 : 0);
        static_assert(argOrd <= num_args_in_function<T>);
        if constexpr(argOrd == num_args_in_function<T>)
        {
            ReleaseAssert(bcOperandOrd == m_numOperands);
            return;
        }
        else
        {
            ReleaseAssert(bcOperandOrd < m_numOperands);
            using Arg = arg_nth_t<T, argOrd>;
            DeegenBytecodeOperandType opType = m_operandTypes[bcOperandOrd].m_type;
            switch (opType)
            {
            case DeegenBytecodeOperandType::BytecodeSlotOrConstant:
            case DeegenBytecodeOperandType::BytecodeSlot:
            case DeegenBytecodeOperandType::Constant:
            {
                ReleaseAssert((std::is_same_v<Arg, TypeMask>));
                break;
            }
            case DeegenBytecodeOperandType::BytecodeRangeRO:
            case DeegenBytecodeOperandType::BytecodeRangeRW:
            {
                ReleaseAssert((std::is_same_v<Arg, RangedInputTypeMaskGetter*>));
                break;
            }
            case DeegenBytecodeOperandType::Int8:
            {
                ReleaseAssert((std::is_same_v<Arg, int8_t>));
                break;
            }
            case DeegenBytecodeOperandType::UInt8:
            {
                ReleaseAssert((std::is_same_v<Arg, uint8_t>));
                break;
            }
            case DeegenBytecodeOperandType::Int16:
            {
                ReleaseAssert((std::is_same_v<Arg, int16_t>));
                break;
            }
            case DeegenBytecodeOperandType::UInt16:
            {
                ReleaseAssert((std::is_same_v<Arg, uint16_t>));
                break;
            }
            case DeegenBytecodeOperandType::Int32:
            {
                ReleaseAssert((std::is_same_v<Arg, int32_t>));
                break;
            }
            case DeegenBytecodeOperandType::UInt32:
            {
                ReleaseAssert((std::is_same_v<Arg, uint32_t>));
                break;
            }
            case DeegenBytecodeOperandType::INVALID_TYPE:
                ReleaseAssert(false);
            }
            ValidateTypeDeductionRulePrototypeImpl<forRange, bcOperandOrd + 1, T>();
        }
    }

    template<bool forRange, typename T>
    consteval void ValidateTypeDeductionRulePrototype(T v)
    {
        // +v forces a cast to plain function pointer if v is a lambda
        // https://stackoverflow.com/questions/18889028/a-positive-lambda-what-sorcery-is-this
        //
        if constexpr(forRange)
        {
            static_assert(std::is_same_v<arg_nth_t<decltype(+v), 0>, size_t>, "Range output deduction rule should start with a size_t argument");
            ReleaseAssert(num_args_in_function<decltype(+v)> == m_numOperands + 1 && "Range output deduction rule should have #operands+1 arguments");
        }
        else
        {
            ReleaseAssert(num_args_in_function<decltype(+v)> == m_numOperands && "Output deduction rule should have #operands arguments");
        }

        ValidateTypeDeductionRulePrototypeImpl<forRange, 0, decltype(+v)>();
        static_assert(std::is_same_v<function_return_type_t<decltype(+v)>, TypeMask>);
        std::ignore = v;
    }

    enum ValueProfileTagEnum
    {
        ValueProfile,
        // Explicitly specify to not profile or speculate on the type of the value
        //
        DoNotProfile
    };

    struct AlwaysOutputMask
    {
        constexpr AlwaysOutputMask(TypeMaskTy mask) : m_mask(mask) { }
        TypeMaskTy m_mask;
    };

    template<typename T>
    static constexpr AlwaysOutputMask AlwaysOutput = AlwaysOutputMask(x_typeMaskFor<T>);

    // Specify output type deduction rule
    //
    template<typename T>
    consteval void TypeDeductionRule(T func)
    {
        ReleaseAssert(m_hasTValueOutput);
        if constexpr(std::is_same_v<T, ValueProfileTagEnum>)
        {
            if (func == ValueProfile)
            {
                m_outputShouldBeValueProfiled = true;
            }
            else
            {
                ReleaseAssert(func == DoNotProfile);
                m_outputShouldExplicitlyNotProfiled = true;
            }
        }
        else if constexpr(std::is_same_v<T, AlwaysOutputMask>)
        {
            m_outputHasFixedTypeMask = true;
            m_outputFixedTypeMask = func.m_mask;
        }
        else
        {
            ValidateTypeDeductionRulePrototype<false /*forRange*/>(func);
            m_outputTypeDeductionRule = FOLD_CONSTEXPR(reinterpret_cast<void*>(+func));
        }
    }

    enum BytecodeResultKind
    {
        // Returns nothing as output
        // Note that the bytecode may still write to BytecodeRangeRW (in general,
        // everything >= the slot specified in BytecodeRangeRW is assumed to be clobbered by us)
        //
        NoOutput,
        // Returns exactly one TValue
        //
        BytecodeValue,
        // This bytecode may use the 'ReturnAndBranch' API to perform a branch
        //
        ConditionalBranch
    };

    consteval void Result(BytecodeResultKind resKind)
    {
        ReleaseAssert(!m_resultKindInitialized);
        m_resultKindInitialized = true;
        m_hasTValueOutput = (resKind == BytecodeResultKind::BytecodeValue);
        m_canPerformBranch = (resKind == BytecodeResultKind::ConditionalBranch);
    }

    consteval void Result(BytecodeResultKind resKind1, BytecodeResultKind resKind2)
    {
        ReleaseAssert(!m_resultKindInitialized);
        m_resultKindInitialized = true;

        BytecodeResultKind resKind;
        if (resKind1 == BytecodeResultKind::ConditionalBranch)
        {
            resKind = resKind2;
        }
        else if (resKind2 == BytecodeResultKind::ConditionalBranch)
        {
            resKind = resKind1;
        }
        else
        {
            ReleaseAssert(false && "bad result kind combination");
        }

        ReleaseAssert(resKind != BytecodeResultKind::ConditionalBranch && "bad result kind combination");

        m_canPerformBranch = true;
        m_hasTValueOutput = (resKind == BytecodeResultKind::BytecodeValue);
    }

    template<size_t ord, typename T>
    consteval void ValidateImplementationPrototype()
    {
        static_assert(is_no_return_function_v<T>, "the function should be annotated with NO_RETURN");
        static_assert(ord <= num_args_in_function<T>);
        if constexpr(ord == num_args_in_function<T>)
        {
            ReleaseAssert(ord == m_numOperands);
            return;
        }
        else
        {
            using Arg = arg_nth_t<T, ord>;
            ReleaseAssert(ord < m_numOperands);
            DeegenBytecodeOperandType opType = m_operandTypes[ord].m_type;
            switch (opType)
            {
            case DeegenBytecodeOperandType::BytecodeSlotOrConstant:
            case DeegenBytecodeOperandType::BytecodeSlot:
            case DeegenBytecodeOperandType::Constant:
            {
                ReleaseAssert((std::is_same_v<Arg, TValue>));
                break;
            }
            case DeegenBytecodeOperandType::BytecodeRangeRO:
            {
                ReleaseAssert((std::is_same_v<Arg, const TValue*>));
                break;
            }
            case DeegenBytecodeOperandType::BytecodeRangeRW:
            {
                ReleaseAssert((std::is_same_v<Arg, TValue*>));
                break;
            }
            case DeegenBytecodeOperandType::Int8:
            {
                ReleaseAssert((std::is_same_v<Arg, int8_t>));
                break;
            }
            case DeegenBytecodeOperandType::UInt8:
            {
                ReleaseAssert((std::is_same_v<Arg, uint8_t>));
                break;
            }
            case DeegenBytecodeOperandType::Int16:
            {
                ReleaseAssert((std::is_same_v<Arg, int16_t>));
                break;
            }
            case DeegenBytecodeOperandType::UInt16:
            {
                ReleaseAssert((std::is_same_v<Arg, uint16_t>));
                break;
            }
            case DeegenBytecodeOperandType::Int32:
            {
                ReleaseAssert((std::is_same_v<Arg, int32_t>));
                break;
            }
            case DeegenBytecodeOperandType::UInt32:
            {
                ReleaseAssert((std::is_same_v<Arg, uint32_t>));
                break;
            }
            case DeegenBytecodeOperandType::INVALID_TYPE:
                ReleaseAssert(false);
            }
            ValidateImplementationPrototype<ord + 1, T>();
        }
    }

    template<typename T>
    consteval void Implementation(T v)
    {
        ReleaseAssert(!m_implementationInitialized);
        m_implementationInitialized = true;
        m_implementationFn = FOLD_CONSTEXPR(reinterpret_cast<void*>(v));
        ValidateImplementationPrototype<0, T>();
    }

    // If this API is called with 'value' = true, interpreter may tier up to baseline JIT at this bytecode.
    // This works for any bytecode, but the tier-up check has overhead,
    // so normally one should only add this property for bytecodes used to implement loop back-edges.
    //
    consteval void CheckForInterpreterTierUp(bool value)
    {
        m_isInterpreterTierUpPoint = value;
    }

    consteval OperandRef Op(std::string_view name)
    {
        ReleaseAssert(m_operandTypeListInitialized);
        if (name == "output")
        {
            return { .m_ord = static_cast<size_t>(-1), .m_operand = { "output", DeegenBytecodeOperandType::BytecodeSlot }};
        }
        for (size_t i = 0; i < m_numOperands; i++)
        {
            if (std::string_view { m_operandTypes[i].m_name } == name)
            {
                return { .m_ord = i, .m_operand = m_operandTypes[i] };
            }
        }
        ReleaseAssert(false);
    }

    // Declare all the locals that may be read by this bytecode
    // All reads must be declared here (except the operands that are already locals, which are obviously read)
    //
    template<typename... Args>
    consteval void DeclareReads(Args... args)
    {
        m_bcDeclareReadsInfo.Process(this, args...);
    }

    template<typename... Args>
    consteval void DeclareWrites(Args... args)
    {
        m_bcDeclareWritesInfo.Process(this, args...);
    }

    // Declare that the bytecode execution may clobber certain locations in the range (i.e., these locations
    // will have garbage values after execution), not considering in-place call
    //
    // Note that properly declaring clobbers is required for correctness, since it could affect the maximum
    // length of the range used by the bytecode. For example, if the bytecode reads and writes range[0],
    // but clobbers range[1], without the clobber declaration, the runtime would incorrectly assume that
    // a range of length 1 would satisfy the bytecode, while actually it wouldn't.
    //
    template<typename... Args>
    consteval void DeclareClobbers(Args... args)
    {
        m_bcDeclareClobbersInfo.Process(this, args...);
    }

    // Declare that an in-place call may happen at >= startLoc, so everything >= it could be implicitly clobbered by the in-place call
    //
    template<typename Arg>
    consteval void DeclareUsedByInPlaceCall(Arg startLoc)
    {
        DeclareClobbers(Range(startLoc, Infinity()));
    }

    consteval void DeclareImplicitClobbersDueToTailCall()
    {
        DeclareClobbers(VariadicArguments(), Range(0, Infinity()));
    }

    template<typename... Args>
    consteval void RegAllocHint(Args... args)
    {
        constexpr size_t nargs = sizeof...(args);
        std::array<SpecializedOperandRef, nargs> arr = { args... };
        for (size_t i = 0; i < nargs; i++)
        {
            SpecializedOperandRef& op = arr[i];
            ReleaseAssert(op.m_operand.m_kind == DeegenSpecializationKind::NotSpecialized);
            ReleaseAssert(op.m_operand.m_regHint.m_numTerms != static_cast<uint8_t>(-1));
            if (op.m_ord == static_cast<size_t>(-1))
            {
                ReleaseAssert(m_hasTValueOutput);
                m_outputRegPriorityInfo = op.m_operand.m_regHint;
            }
            else
            {
                ReleaseAssert(op.m_ord < m_numOperands);
                m_bcRegPriorityInfo.Set(op.m_ord, op.m_operand.m_regHint);
            }
        }
    }

    consteval void RegAllocMayBeDisabledDespiteRegHintGiven()
    {
        m_disableRegAllocMustBeEnabledAssert = true;
    }

    struct Intrinsic
    {
#define macro2(intrinsicName, ...) struct intrinsicName { __VA_OPT__(OperandRef __VA_ARGS__;) constexpr auto AsArray() { return make_array<OperandRef>(__VA_ARGS__); } };
#define macro(item) macro2 item
        PP_FOR_EACH(macro, DEEGEN_BYTECODE_INTRINSIC_LIST)
#undef macro
#undef macro2

        template<typename T>
        static constexpr size_t GetIntrinsicOrd()
        {
            size_t res = 0;
#define macro2(intrinsicName, ...) if constexpr(std::is_same_v<Intrinsic::intrinsicName, T>) { } else { res += 1;
#define macro(item) macro2 item
            PP_FOR_EACH(macro, DEEGEN_BYTECODE_INTRINSIC_LIST)
#undef macro
#undef macro2
            static_assert(type_dependent_false<T>::value, "T is not an intrinsic type!");
#define macro(item) }
            PP_FOR_EACH(macro, DEEGEN_BYTECODE_INTRINSIC_LIST)
#undef macro
            return res;
        }
    };

    static constexpr size_t x_maxIntrinsicArgCount = 5;

    template<typename T>
    consteval void DeclareAsIntrinsic(T info)
    {
        constexpr size_t intrinsicOrd = Intrinsic::GetIntrinsicOrd<T>();
        auto data = info.AsArray();
        ReleaseAssert(data.size() <= x_maxIntrinsicArgCount);

        ReleaseAssert(m_intrinsicOrd == static_cast<size_t>(-1));
        m_intrinsicOrd = intrinsicOrd;
        m_numIntrinsicArgs = data.size();
        for (size_t i = 0; i < data.size(); i++)
        {
            m_intrinsicArgOperandOrd[i] = data[i].m_ord;
        }
    }

    consteval DeegenFrontendBytecodeDefinitionDescriptor()
        : m_operandTypeListInitialized(false)
        , m_implementationInitialized(false)
        , m_resultKindInitialized(false)
        , m_hasTValueOutput(false)
        , m_hasVariadicResOutput(false)
        , m_canPerformBranch(false)
        , m_isInterpreterTierUpPoint(false)
        , m_disableRegAllocMustBeEnabledAssert(false)
        , m_implementationFn(nullptr)
        , m_outputShouldBeValueProfiled(false)
        , m_outputShouldExplicitlyNotProfiled(false)
        , m_outputHasFixedTypeMask(false)
        , m_numOperands(0)
        , m_numVariants(0)
        , m_numDfgVariants(0)
        , m_operandTypes()
        , m_variants()
        , m_dfgVariants()
        , m_bcDeclareReadsInfo(false /*isForWriteDecl*/)
        , m_bcDeclareWritesInfo(true /*isForWriteDecl*/)
        , m_bcDeclareClobbersInfo(false /*isForWriteDecl*/)
        , m_bcRegPriorityInfo()
        , m_outputRegPriorityInfo()
        , m_intrinsicOrd(static_cast<size_t>(-1))
        , m_numIntrinsicArgs(0)
        , m_intrinsicArgOperandOrd()
        , m_outputTypeDeductionRule(nullptr)
        , m_outputFixedTypeMask(0)
    { }

    bool m_operandTypeListInitialized;
    bool m_implementationInitialized;
    bool m_resultKindInitialized;
    bool m_hasTValueOutput;
    bool m_hasVariadicResOutput;
    bool m_canPerformBranch;
    bool m_isInterpreterTierUpPoint;
    bool m_disableRegAllocMustBeEnabledAssert;
    void* m_implementationFn;
    bool m_outputShouldBeValueProfiled;
    bool m_outputShouldExplicitlyNotProfiled;
    bool m_outputHasFixedTypeMask;
    size_t m_numOperands;
    size_t m_numVariants;
    size_t m_numDfgVariants;
    Operand m_operandTypes[x_maxOperands];
    SpecializedVariant m_variants[x_maxVariants];
    SpecializedVariant m_dfgVariants[x_maxVariants];
    DeclareRWCInfo m_bcDeclareReadsInfo;
    DeclareRWCInfo m_bcDeclareWritesInfo;
    DeclareRWCInfo m_bcDeclareClobbersInfo;
    RegPriorityInfo m_bcRegPriorityInfo;
    OperandRegPriorityInfo m_outputRegPriorityInfo;
    size_t m_intrinsicOrd;
    size_t m_numIntrinsicArgs;
    size_t m_intrinsicArgOperandOrd[x_maxIntrinsicArgCount];
    void* m_outputTypeDeductionRule;
    TypeMaskTy m_outputFixedTypeMask;
};

using DFBDD = DeegenFrontendBytecodeDefinitionDescriptor;
inline consteval DFBDD::OperandExpr operator+(DFBDD::OperandExpr lhs, DFBDD::OperandExpr rhs)
{
    return DFBDD::OperandExpr::ConstructBinaryExpr(DFBDD::OperandExprNode::Add, lhs, rhs);
}

inline consteval DFBDD::OperandExpr operator+(DFBDD::OperandExpr lhs, DFBDD::OperandRef rhs)
{
    return DFBDD::OperandExpr::ConstructBinaryExpr(DFBDD::OperandExprNode::Add, lhs, DFBDD::OperandExpr::ConstructOperandRef(rhs.m_operand.m_name));
}

inline consteval DFBDD::OperandExpr operator+(DFBDD::OperandExpr lhs, int64_t rhs)
{
    return DFBDD::OperandExpr::ConstructBinaryExpr(DFBDD::OperandExprNode::Add, lhs, DFBDD::OperandExpr::ConstructNumber(rhs));
}

inline consteval DFBDD::OperandExpr operator+(DFBDD::OperandRef lhs, DFBDD::OperandExpr rhs)
{
    return DFBDD::OperandExpr::ConstructBinaryExpr(DFBDD::OperandExprNode::Add, DFBDD::OperandExpr::ConstructOperandRef(lhs.m_operand.m_name), rhs);
}

inline consteval DFBDD::OperandExpr operator+(DFBDD::OperandRef lhs, DFBDD::OperandRef rhs)
{
    return DFBDD::OperandExpr::ConstructBinaryExpr(DFBDD::OperandExprNode::Add,
                                                   DFBDD::OperandExpr::ConstructOperandRef(lhs.m_operand.m_name),
                                                   DFBDD::OperandExpr::ConstructOperandRef(rhs.m_operand.m_name));
}

inline consteval DFBDD::OperandExpr operator+(DFBDD::OperandRef lhs, int64_t rhs)
{
    return DFBDD::OperandExpr::ConstructBinaryExpr(DFBDD::OperandExprNode::Add,
                                                   DFBDD::OperandExpr::ConstructOperandRef(lhs.m_operand.m_name),
                                                   DFBDD::OperandExpr::ConstructNumber(rhs));
}

inline consteval DFBDD::OperandExpr operator+(int64_t lhs, DFBDD::OperandExpr rhs)
{
    return DFBDD::OperandExpr::ConstructBinaryExpr(DFBDD::OperandExprNode::Add, DFBDD::OperandExpr::ConstructNumber(lhs), rhs);
}

inline consteval DFBDD::OperandExpr operator+(int64_t lhs, DFBDD::OperandRef rhs)
{
    return DFBDD::OperandExpr::ConstructBinaryExpr(DFBDD::OperandExprNode::Add,
                                                   DFBDD::OperandExpr::ConstructNumber(lhs),
                                                   DFBDD::OperandExpr::ConstructOperandRef(rhs.m_operand.m_name));
}

inline consteval DFBDD::OperandExpr operator-(DFBDD::OperandExpr lhs, DFBDD::OperandExpr rhs)
{
    return DFBDD::OperandExpr::ConstructBinaryExpr(DFBDD::OperandExprNode::Sub, lhs, rhs);
}

inline consteval DFBDD::OperandExpr operator-(DFBDD::OperandExpr lhs, DFBDD::OperandRef rhs)
{
    return DFBDD::OperandExpr::ConstructBinaryExpr(DFBDD::OperandExprNode::Sub, lhs, DFBDD::OperandExpr::ConstructOperandRef(rhs.m_operand.m_name));
}

inline consteval DFBDD::OperandExpr operator-(DFBDD::OperandExpr lhs, int64_t rhs)
{
    return DFBDD::OperandExpr::ConstructBinaryExpr(DFBDD::OperandExprNode::Sub, lhs, DFBDD::OperandExpr::ConstructNumber(rhs));
}

inline consteval DFBDD::OperandExpr operator-(DFBDD::OperandRef lhs, DFBDD::OperandExpr rhs)
{
    return DFBDD::OperandExpr::ConstructBinaryExpr(DFBDD::OperandExprNode::Sub, DFBDD::OperandExpr::ConstructOperandRef(lhs.m_operand.m_name), rhs);
}

inline consteval DFBDD::OperandExpr operator-(DFBDD::OperandRef lhs, DFBDD::OperandRef rhs)
{
    return DFBDD::OperandExpr::ConstructBinaryExpr(DFBDD::OperandExprNode::Sub,
                                                   DFBDD::OperandExpr::ConstructOperandRef(lhs.m_operand.m_name),
                                                   DFBDD::OperandExpr::ConstructOperandRef(rhs.m_operand.m_name));
}

inline consteval DFBDD::OperandExpr operator-(DFBDD::OperandRef lhs, int64_t rhs)
{
    return DFBDD::OperandExpr::ConstructBinaryExpr(DFBDD::OperandExprNode::Sub,
                                                   DFBDD::OperandExpr::ConstructOperandRef(lhs.m_operand.m_name),
                                                   DFBDD::OperandExpr::ConstructNumber(rhs));
}

inline consteval DFBDD::OperandExpr operator-(int64_t lhs, DFBDD::OperandExpr rhs)
{
    return DFBDD::OperandExpr::ConstructBinaryExpr(DFBDD::OperandExprNode::Sub, DFBDD::OperandExpr::ConstructNumber(lhs), rhs);
}

inline consteval DFBDD::OperandExpr operator-(int64_t lhs, DFBDD::OperandRef rhs)
{
    return DFBDD::OperandExpr::ConstructBinaryExpr(DFBDD::OperandExprNode::Sub,
                                                   DFBDD::OperandExpr::ConstructNumber(lhs),
                                                   DFBDD::OperandExpr::ConstructOperandRef(rhs.m_operand.m_name));
}

namespace detail
{

template<int v> struct deegen_end_bytecode_definitions_macro_used : deegen_end_bytecode_definitions_macro_used<v-1> { };
template<> struct deegen_end_bytecode_definitions_macro_used<-1> { static constexpr bool value = false; };

template<int v> struct deegen_bytecode_definition_info : deegen_bytecode_definition_info<v-1> { using tuple_type = typename deegen_bytecode_definition_info<v-1>::tuple_type; };
template<> struct deegen_bytecode_definition_info<-1> {
    using tuple_type = std::tuple<>;
    static constexpr std::array<const char*, 0> value { };
};

template<int v> struct deegen_bytecode_same_length_constraint_info : deegen_bytecode_same_length_constraint_info<v-1> { };
template<> struct deegen_bytecode_same_length_constraint_info<-1> {
    static constexpr std::array<const char*, 0> value { };
};

template<typename T>
struct deegen_get_bytecode_def_list_impl
{
    static constexpr size_t numElements = std::tuple_size_v<T>;

    using ArrTy = std::array<DeegenFrontendBytecodeDefinitionDescriptor, numElements>;
    static constexpr ArrTy value = []()
    {
        constexpr auto get_array = [](auto&& ... x) { return ArrTy { std::forward<decltype(x)>(x) ... }; };
        return std::apply(get_array, T());
    }();

    static_assert([](){
        for (size_t i = 0; i < numElements; i++)
        {
            ReleaseAssert(value[i].m_operandTypeListInitialized);
            ReleaseAssert(value[i].m_implementationInitialized);
            ReleaseAssert(value[i].m_resultKindInitialized);
            ReleaseAssert(value[i].m_numVariants > 0);
        }
        return true;
    }());
};

}   // namespace detail

// DEEGEN_END_BYTECODE_DEFINITIONS:
//   Must be used exactly once per translation unit
//   Must be put after all uses of 'DEEGEN_DEFINE_BYTECODE'
//
#define DEEGEN_END_BYTECODE_DEFINITIONS DEEGEN_END_BYTECODE_DEFINITIONS_IMPL(__COUNTER__)
#define DEEGEN_END_BYTECODE_DEFINITIONS_IMPL(counter)                                                                                                                                                       \
    static_assert(!detail::deegen_end_bytecode_definitions_macro_used<counter>::value, "DEEGEN_END_BYTECODE_DEFINITIONS should only be used once per translation unit, after all DEEGEN_DEFINE_BYTECODE");  \
    namespace detail { template<> struct deegen_end_bytecode_definitions_macro_used<counter + 1> { static constexpr bool value = true; }; }                                                                 \
    __attribute__((__used__)) inline constexpr auto x_deegen_impl_all_bytecode_same_length_constraints_in_this_tu = detail::std_array_to_llvm_friendly_array(                                               \
        detail::deegen_bytecode_same_length_constraint_info<counter>::value);                                                                                                                               \
    __attribute__((__used__)) inline constexpr auto x_deegen_impl_all_bytecode_names_in_this_tu = detail::std_array_to_llvm_friendly_array(detail::deegen_bytecode_definition_info<counter>::value);        \
    __attribute__((__used__)) inline constexpr auto x_deegen_impl_all_bytecode_defs_in_this_tu = detail::std_array_to_llvm_friendly_array(detail::deegen_get_bytecode_def_list_impl<                        \
        typename detail::deegen_bytecode_definition_info<counter>::tuple_type>::value);                                                                                                                     \
    static_assert(x_deegen_impl_all_bytecode_names_in_this_tu.size() == x_deegen_impl_all_bytecode_defs_in_this_tu.size());

// DEEGEN_DEFINE_BYTECODE(name):
//   Define a bytecode
//
#define DEEGEN_DEFINE_BYTECODE(name) DEEGEN_DEFINE_BYTECODE_IMPL(name, __COUNTER__)
#define DEEGEN_DEFINE_BYTECODE_IMPL(name, counter)                                                                                                                          \
    static_assert(!detail::deegen_end_bytecode_definitions_macro_used<counter>::value, "DEEGEN_DEFINE_BYTECODE should not be used after DEEGEN_END_BYTECODE_DEFINITIONS");  \
    namespace {                                                                                                                                                             \
    /* define in anonymous namespace to trigger compiler warning if user forgot to write 'DEEGEN_END_BYTECODE_DEFINITIONS' at the end of the file */                        \
    struct DeegenUserBytecodeDefinitionImpl_ ## name final : public DeegenFrontendBytecodeDefinitionDescriptor {                                                            \
        consteval DeegenUserBytecodeDefinitionImpl_ ## name ();                                                                                                             \
    };                                                                                                                                                                      \
    }   /* anonymous namespace */                                                                                                                                           \
    namespace detail {                                                                                                                                                      \
    template<> struct deegen_bytecode_definition_info<counter> {                                                                                                            \
        using tuple_type = tuple_append_element_t<typename deegen_bytecode_definition_info<counter-1>::tuple_type, DeegenUserBytecodeDefinitionImpl_ ## name>;              \
        static constexpr auto value = constexpr_std_array_concat(                                                                                                           \
                    deegen_bytecode_definition_info<counter-1>::value, std::array<const char*, 1> { PP_STRINGIFY(name) });                                                  \
    };                                                                                                                                                                      \
    }   /* namespace detail */                                                                                                                                              \
    consteval DeegenUserBytecodeDefinitionImpl_ ## name :: DeegenUserBytecodeDefinitionImpl_ ## name()

// DEEGEN_DEFINE_BYTECODE_TEMPLATE(tplname, template args...):
//   Define a bytecode template
//
#define DEEGEN_DEFINE_BYTECODE_TEMPLATE(name, ...) DEEGEN_DEFINE_BYTECODE_TEMPLATE_IMPL(name, __COUNTER__, __VA_ARGS__)
#define DEEGEN_DEFINE_BYTECODE_TEMPLATE_IMPL(name, counter, ...)                                                                                                                    \
    static_assert(!detail::deegen_end_bytecode_definitions_macro_used<counter>::value, "DEEGEN_DEFINE_BYTECODE_TEMPLATE should not be used after DEEGEN_END_BYTECODE_DEFINITIONS"); \
    namespace {                                                                                                                                                                     \
        /* define in anonymous namespace to trigger compiler warning if user forgot to write 'DEEGEN_END_BYTECODE_DEFINITIONS' at the end of the file */                            \
        struct DeegenUserBytecodeDefinitionTemplateImpl_ ## name : public DeegenFrontendBytecodeDefinitionDescriptor {                                                              \
            template<__VA_ARGS__>                                                                                                                                                   \
            consteval void user_create_impl();                                                                                                                                      \
        };                                                                                                                                                                          \
    }   /* anonymous namespace */                                                                                                                                                   \
    template<__VA_ARGS__>                                                                                                                                                           \
    consteval void DeegenUserBytecodeDefinitionTemplateImpl_ ## name :: user_create_impl()

// DEEGEN_DEFINE_BYTECODE_BY_TEMPLATE_INSTANTIATION(bytecodeName, templateName, tplArgs...):
//   Define a bytecode by template instantiation
//
#define DEEGEN_DEFINE_BYTECODE_BY_TEMPLATE_INSTANTIATION(name, tplname, ...) DEEGEN_DEFINE_BYTECODE_BY_TEMPLATE_INSTANTIATION_IMPL(name, tplname, __COUNTER__, __VA_ARGS__)
#define DEEGEN_DEFINE_BYTECODE_BY_TEMPLATE_INSTANTIATION_IMPL(name, tplname, counter, ...)                                                                                      \
    namespace {                                                                                                                                                                 \
        /* define in anonymous namespace to trigger compiler warning if user forgot to write 'DEEGEN_END_BYTECODE_DEFINITIONS' at the end of the file */                        \
        struct DeegenUserBytecodeDefinitionImpl_ ## name final : public DeegenUserBytecodeDefinitionTemplateImpl_ ## tplname {                                                  \
            consteval DeegenUserBytecodeDefinitionImpl_ ## name () { user_create_impl< __VA_ARGS__ > (); }                                                                      \
        };                                                                                                                                                                      \
    }   /* anonymous namespace */                                                                                                                                               \
    namespace detail {                                                                                                                                                          \
        template<> struct deegen_bytecode_definition_info<counter> {                                                                                                            \
            using tuple_type = tuple_append_element_t<typename deegen_bytecode_definition_info<counter-1>::tuple_type, DeegenUserBytecodeDefinitionImpl_ ## name>;              \
            static constexpr auto value = constexpr_std_array_concat(                                                                                                           \
                        deegen_bytecode_definition_info<counter-1>::value, std::array<const char*, 1> { PP_STRINGIFY(name) });                                                  \
        };                                                                                                                                                                      \
    }   /* namespace detail */                                                                                                                                                  \
    static_assert(!detail::deegen_end_bytecode_definitions_macro_used<counter>::value, "DEEGEN_DEFINE_BYTECODE should not be used after DEEGEN_END_BYTECODE_DEFINITIONS")

// DEEGEN_ADD_BYTECODE_SAME_LENGTH_CONSTRAINT(bytecodeName1, bytecodeName2):
//     Add a constraint that bytecodeName1 must have the same length as bytecodeName2
//     This means that all variants of the two bytecode classes are forced to pad to the maximum length among them.
//     This allows the frontend bytecode builder to late-replace a bytecode of type 'bytecodeName1' in the bytecode
//     stream by a bytecode of type 'bytecodeName2' (or vice versa).
//
#define DEEGEN_ADD_BYTECODE_SAME_LENGTH_CONSTRAINT(bytecodeName1, bytecodeName2) DEEGEN_ADD_BYTECODE_SAME_LENGTH_CONSTRAINT_IMPL(bytecodeName1, bytecodeName2, __COUNTER__)
#define DEEGEN_ADD_BYTECODE_SAME_LENGTH_CONSTRAINT_IMPL(bytecodeName1, bytecodeName2, counter)              \
    namespace detail {                                                                                      \
        template<> struct deegen_bytecode_same_length_constraint_info<counter> {                            \
            static constexpr auto value = constexpr_std_array_concat(                                       \
                deegen_bytecode_same_length_constraint_info<counter-1>::value, std::array<const char*, 2> { \
                    PP_STRINGIFY(bytecodeName1), PP_STRINGIFY(bytecodeName2) });                            \
        };                                                                                                  \
    }   /* namespace detail */                                                                              \
    static_assert(!detail::deegen_end_bytecode_definitions_macro_used<counter>::value,                      \
    "DEEGEN_ADD_BYTECODE_SAME_LENGTH_CONSTRAINT should not be used after DEEGEN_END_BYTECODE_DEFINITIONS")

// Example usage:
// DEEGEN_DEFINE_BYTECODE(add) { ... }
// DEEGEN_DEFINE_BYTECODE(sub) { ... }
// DEEGEN_DEFINE_BYTECODE(mul) { ... }
// DEEGEN_END_BYTECODE_DEFINITIONS
//
