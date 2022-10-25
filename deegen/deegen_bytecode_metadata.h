#pragma once

#include "common_utils.h"
#include "misc_llvm_helper.h"

namespace dast {

// One element in the bytecode metadata struct.
// This is an "atom" construct: i.e., we will not try to do fancy things with its internal layout.
//
// The pointer of this class is used to identify this class in the LLVM module (i.e., the address of
// this class is baked into the name of the dummy annotation function so we can easily identify it).
// This is why this class is not copyable or movable.
//
class BytecodeMetadataElement
{
    MAKE_NONCOPYABLE(BytecodeMetadataElement);
    MAKE_NONMOVABLE(BytecodeMetadataElement);
public:
    BytecodeMetadataElement(size_t alignment, size_t size)
        : m_alignment(alignment)
        , m_size(size)
        , m_hasInitValue(false)
        , m_offsetInFinalStruct(static_cast<size_t>(-1))
    {
        ReleaseAssert(is_power_of_2(alignment));
        ReleaseAssert(size > 0 && size % alignment == 0);
    }

    // TODO: we need more traits so that GC can understand how to interpret the metadata struct
    //
    size_t WARN_UNUSED GetAlignment() const { return m_alignment; }
    size_t WARN_UNUSED GetSize() const { return m_size; }

    template<typename T>
    void SetInitValue(T initVal)
    {
        static_assert(std::is_trivial_v<T>);
        m_hasInitValue = true;
        ReleaseAssert(m_size == sizeof(T));
        m_initValue.resize(sizeof(T));
        memcpy(m_initValue.data(), &initVal, sizeof(T));
    }

    void SetInitValueCI(llvm::ConstantInt* initVal)
    {
        ReleaseAssert(initVal->getType()->getBitWidth() <= 64);
        ReleaseAssert(m_size * 8 == initVal->getType()->getBitWidth());
        uint64_t val = initVal->getZExtValue();
        if (m_size == 1) {
            SetInitValue<uint8_t>(BitwiseTruncateTo<uint8_t>(val));
        } else if (m_size == 2) {
            SetInitValue<uint16_t>(BitwiseTruncateTo<uint16_t>(val));
        } else if (m_size == 4) {
            SetInitValue<uint32_t>(BitwiseTruncateTo<uint32_t>(val));
        } else {
            ReleaseAssert(m_size == 8);
            SetInitValue<uint64_t>(val);
        }
    }

    void RemoveInitValue() { m_hasInitValue = false; }
    bool HasInitValue() const { return m_hasInitValue; }
    std::vector<uint8_t> WARN_UNUSED GetInitValue() const { ReleaseAssert(HasInitValue()); return m_initValue; }

    void AssignFinalOffset(size_t offset)
    {
        ReleaseAssert(offset != static_cast<size_t>(-1) && m_offsetInFinalStruct == static_cast<size_t>(-1));
        ReleaseAssert(offset % m_alignment == 0);
        m_offsetInFinalStruct = offset;
    }

    bool HasAssignedFinalOffset() const { return m_offsetInFinalStruct != static_cast<size_t>(-1); }
    size_t WARN_UNUSED GetStructOffset() const
    {
        ReleaseAssert(HasAssignedFinalOffset());
        return m_offsetInFinalStruct;
    }

    llvm::Instruction* WARN_UNUSED EmitGetAddress(llvm::Module* module, llvm::Value* metadataStructPtr, llvm::Instruction* insertBefore) const;
    llvm::Instruction* WARN_UNUSED EmitGetAddress(llvm::Module* module, llvm::Value* metadataStructPtr, llvm::BasicBlock* insertAtEnd) const;

private:
    // The alignment of this element, must be a power of 2
    //
    size_t m_alignment;
    // The size of this element, must be a multiple of 'm_alignment'
    //
    size_t m_size;

    // Whether this element is required to have an initial value, and the byte sequence of the initial value
    //
    bool m_hasInitValue;
    std::vector<uint8_t> m_initValue;

    // The final assigned offset of this element in the structure
    //
    size_t m_offsetInFinalStruct;
};

enum class BytecodeMetadataStructKind
{
    // A list of structs or values
    //
    Struct,
    // A tagged union consisting of a uint8_t tag and a union of a list of structs
    //
    TaggedUnion,
    // A leaf element (BytecodeMetadataElement)
    //
    Element
};

// Describes the metadata definition of a bytecode
//
class BytecodeMetadataStructBase
{
    MAKE_NONCOPYABLE(BytecodeMetadataStructBase);
    MAKE_NONMOVABLE(BytecodeMetadataStructBase);
public:
    BytecodeMetadataStructBase() { }
    virtual ~BytecodeMetadataStructBase() { }
    virtual BytecodeMetadataStructKind GetKind() const = 0;

    // Collect all elements into a list. Note that these elements may potentially overlap with each other due to union
    //
    virtual std::vector<BytecodeMetadataElement*> CollectAllElements() const = 0;

    virtual std::vector<BytecodeMetadataElement*> CollectInitializationInfo() const = 0;

    struct StructInfo
    {
        // The alignment of this struct
        //
        size_t alignment;
        // This size includes the tail padding (so must be a multiple of alignment)
        //
        size_t allocSize;
        // This size does not include tail padding
        //
        size_t storeSize;
    };

    // May only be called once
    // Assign offset to each BytecodeMetadataElement in this struct
    // Returns the alignment and size of this struct
    //
    StructInfo WARN_UNUSED FinalizeStructAndAssignOffsets();

    // Must be called after 'FinalizeStructAndAssignOffsets'
    // Populate the implementation of all inserted BytecodeMetadataElement::GetAddress placeholder functions
    //
    void LowerAll(llvm::Module* module) const;
};

class BytecodeMetadataStructElement final : public BytecodeMetadataStructBase
{
public:
    BytecodeMetadataStructElement(std::unique_ptr<BytecodeMetadataElement> element)
        : m_element(std::move(element))
    { }

    virtual BytecodeMetadataStructKind GetKind() const override { return BytecodeMetadataStructKind::Element; }

    virtual std::vector<BytecodeMetadataElement*> CollectAllElements() const override
    {
        return { m_element.get() };
    }

    virtual std::vector<BytecodeMetadataElement*> CollectInitializationInfo() const override
    {
        if (m_element->HasInitValue())
        {
            return { m_element.get() };
        }
        else
        {
            return { };
        }
    }

    BytecodeMetadataElement* GetElement() const { return m_element.get(); }

private:
    std::unique_ptr<BytecodeMetadataElement> m_element;
};

class BytecodeMetadataTaggedUnion;

class BytecodeMetadataStruct final : public BytecodeMetadataStructBase
{
public:
    virtual BytecodeMetadataStructKind GetKind() const override { return BytecodeMetadataStructKind::Struct; }

    virtual std::vector<BytecodeMetadataElement*> CollectAllElements() const override
    {
        std::vector<BytecodeMetadataElement*> res;
        for (auto& it : m_members)
        {
            std::vector<BytecodeMetadataElement*> other = it->CollectAllElements();
            for (BytecodeMetadataElement* e : other) { res.push_back(e); }
        }
        return res;
    }

    virtual std::vector<BytecodeMetadataElement*> CollectInitializationInfo() const override
    {
        std::vector<BytecodeMetadataElement*> res;
        for (auto& it : m_members)
        {
            std::vector<BytecodeMetadataElement*> other = it->CollectInitializationInfo();
            for (BytecodeMetadataElement* e : other) { res.push_back(e); }
        }
        return res;
    }

    BytecodeMetadataElement* WARN_UNUSED AddElement(size_t alignment, size_t size)
    {
        return AddElement(std::make_unique<BytecodeMetadataElement>(alignment, size));
    }

    BytecodeMetadataElement* AddElement(std::unique_ptr<BytecodeMetadataElement> e)
    {
        BytecodeMetadataElement* res = e.get();
        AddStructElementImpl(std::make_unique<BytecodeMetadataStructElement>(std::move(e)));
        return res;
    }

    BytecodeMetadataStruct* WARN_UNUSED AddStruct()
    {
        return AddStruct(std::make_unique<BytecodeMetadataStruct>());
    }

    BytecodeMetadataStruct* AddStruct(std::unique_ptr<BytecodeMetadataStruct> s)
    {
        BytecodeMetadataStruct* res = s.get();
        AddStructElementImpl(std::move(s));
        return res;
    }

    BytecodeMetadataTaggedUnion* WARN_UNUSED AddTaggedUnion();
    BytecodeMetadataTaggedUnion* AddTaggedUnion(std::unique_ptr<BytecodeMetadataTaggedUnion> tu);

    std::vector<BytecodeMetadataStructBase*> WARN_UNUSED GetMembers() const
    {
        std::vector<BytecodeMetadataStructBase*> res;
        for (auto& it : m_members) { res.push_back(it.get()); }
        return res;
    }

    BytecodeMetadataStructBase* GetMember(size_t ord) const
    {
        ReleaseAssert(ord < m_members.size());
        return m_members[ord].get();
    }

private:
    void AddStructElementImpl(std::unique_ptr<BytecodeMetadataStructBase> e)
    {
        m_members.push_back(std::move(e));
    }

    std::vector<std::unique_ptr<BytecodeMetadataStructBase>> m_members;
};

class BytecodeMetadataTaggedUnion final : public BytecodeMetadataStructBase
{
public:
    BytecodeMetadataTaggedUnion()
    {
        m_tagMayHaveInvalidValue = true;
        m_tag = std::make_unique<BytecodeMetadataElement>(1 /*alignment*/, 1 /*size*/);
        // The default is to stay safe, so m_tagMayHaveInvalidValue == true and m_tag always
        // has a safe invalid value of 255. User logic can easily undo them if they are unnecessary.
        //
        m_tag->SetInitValue<uint8_t>(255);
    }

    virtual BytecodeMetadataStructKind GetKind() const override { return BytecodeMetadataStructKind::TaggedUnion; }

    virtual std::vector<BytecodeMetadataElement*> CollectAllElements() const override
    {
        std::vector<BytecodeMetadataElement*> res;
        res.push_back(m_tag.get());
        for (auto& it : m_members)
        {
            std::vector<BytecodeMetadataElement*> other = it->CollectAllElements();
            for (BytecodeMetadataElement* e : other) { res.push_back(e); }
        }
        return res;
    }

    virtual std::vector<BytecodeMetadataElement*> CollectInitializationInfo() const override
    {
        for (auto& it : m_members)
        {
            // Union members should not have any initializer since it doesn't make sense
            //
            std::vector<BytecodeMetadataElement*> tmp = it->CollectInitializationInfo();
            ReleaseAssert(tmp.empty());
        }

        if (m_tag->HasInitValue())
        {
            return { m_tag.get() };
        }
        else
        {
            return { };
        }
    }

    void SetTagMayHaveInvalidValue(bool value) { m_tagMayHaveInvalidValue = value; }
    bool WARN_UNUSED TagMayHaveInvalidValue() const { return m_tagMayHaveInvalidValue; }
    BytecodeMetadataElement* WARN_UNUSED GetTag() const { return m_tag.get(); }
    size_t WARN_UNUSED GetNumCases() const { return m_members.size(); }

    std::vector<BytecodeMetadataStruct*> WARN_UNUSED GetMembers() const
    {
        std::vector<BytecodeMetadataStruct*> res;
        for (auto& it : m_members) { res.push_back(it.get()); }
        return res;
    }

    std::pair<uint8_t /*caseOrd*/, BytecodeMetadataStruct*> WARN_UNUSED AddNewCase()
    {
        ReleaseAssert(m_members.size() < 250);
        uint8_t caseOrd = SafeIntegerCast<uint8_t>(m_members.size());
        m_members.push_back(std::make_unique<BytecodeMetadataStruct>());
        BytecodeMetadataStruct* caseStruct = m_members.back().get();
        return std::make_pair(caseOrd, caseStruct);
    }

private:
    // Denote whether 'm_tag' may contain invalid value at runtime
    //
    bool m_tagMayHaveInvalidValue;
    std::unique_ptr<BytecodeMetadataElement> m_tag;
    std::vector<std::unique_ptr<BytecodeMetadataStruct>> m_members;
};

inline BytecodeMetadataTaggedUnion* BytecodeMetadataStruct::AddTaggedUnion(std::unique_ptr<BytecodeMetadataTaggedUnion> tu)
{
    BytecodeMetadataTaggedUnion* res = tu.get();
    AddStructElementImpl(std::move(tu));
    return res;
}

inline BytecodeMetadataTaggedUnion* WARN_UNUSED BytecodeMetadataStruct::AddTaggedUnion()
{
    return AddTaggedUnion(std::make_unique<BytecodeMetadataTaggedUnion>());
}

}   // namespace dast
