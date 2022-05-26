#pragma once

#include "common_utils.h"
#include "memory_ptr.h"

namespace ToyLang
{

using namespace CommonUtils;

// There are two use patterns for watchpoint classes:
// (1) A class containing a fixed number of watchpoint nodes.
// (2) A class containing a variant number of watchpoint nodes.
//
// For pattern (1), one should register all class names below, and use EmbeddedWatchpointNode as watchpoint member type.
// For pattern (2), one should make the class inherit "WatchpointWithVirtualTable", and use VTWatchpointNode as watchpoint member type.
//
// Either way, every watchpoint class should provide two functions:
// (1) OnWatchpointSetDestruct(HeapPtrTranslator translator, WatchpointNodeBase* watchpointNode):
//         Called when a watchpoint set holding a watchpoint node is destructed.
// (2) OnFire(HeapPtrTranslator translator, WatchpointNodeBase* watchpointNode):
//         Called when a watchpoint node is fired.
//
// In both APIs, the watchpoint node parameter denote the node that triggered the action,
// so one can know which watchpoint node is fired if the watchpoint class contains multiple watchpoint nodes.
// When either API is called, the watchpoint node no longer belongs to any watchpoint set.
//
// All Watchpoint nodes must be allocated in Spds region,
// all WatchpointSets must be allocated in either Spds region or SystemHeap region
//
#define WATCHPOINT_KIND_LIST                                                                \
  /* Holder C++ class name           Offset in holder C++ class   Comment                */ \
    (CodeJettisonWatchpoint,         x_watchpointFieldOffset,     "")

#define WPK_CPP_NAME(wpk) PP_TUPLE_GET_1(wpk)
#define WPK_CPP_OFFSET(wpk) PP_TUPLE_GET_2(wpk)
#define WPK_COMMENT(wpk) PP_TUPLE_GET_3(wpk)

enum class WatchpointEnumKind : uint8_t
{
#define macro(ord, wpk) E ## ord ,
PP_FOR_EACH_UNPACK_TUPLE(macro, PP_ZIP_TWO_LISTS((PP_NATURAL_NUMBERS_LIST), (WATCHPOINT_KIND_LIST)))
#undef macro
    X_END_OF_ENUM
};

constexpr size_t x_numWatchpointKinds = static_cast<size_t>(WatchpointEnumKind::X_END_OF_ENUM);
static_assert(x_numWatchpointKinds <= std::numeric_limits<uint8_t>::max() / 2);

#define macro(wpk) class WPK_CPP_NAME(wpk);
PP_FOR_EACH(macro, WATCHPOINT_KIND_LIST)
#undef macro

namespace internal
{

template<WatchpointEnumKind> struct WatchpointClassForWatchpointEnumKindImpl { };
#define macro(ord, wpk)                                                                                 \
    template<> struct WatchpointClassForWatchpointEnumKindImpl<static_cast<WatchpointEnumKind>(ord)> {  \
        using type = WPK_CPP_NAME(wpk);                                                                 \
    };
PP_FOR_EACH_UNPACK_TUPLE(macro, PP_ZIP_TWO_LISTS((PP_NATURAL_NUMBERS_LIST), (WATCHPOINT_KIND_LIST)))
#undef macro

}   // namespace internal

template<WatchpointEnumKind wtype>
using WatchpointClassForWatchpointEnumKind = typename internal::WatchpointClassForWatchpointEnumKindImpl<wtype>::type;

constexpr const char* x_watchpointHumanReadableComments[x_numWatchpointKinds + 1] = {
#define macro(wpk) WPK_COMMENT(wpk) ,
PP_FOR_EACH(macro, WATCHPOINT_KIND_LIST)
#undef macro
    ""
};

// We want to have a data structure holding a bag of elements (unordered),
// and each element should be able to remove itself from the bag without knowing which bag it is in.
//
// Normally a doubly linked list suffices. However, we have a unique requirement: we want the
// *head* of the doubly linked list to be as small as possible (because each WatchpointSet contains a
// list head, and we are creating a LOT of WatchpointSets!).
//
// We take advantage of the fact that the bag is unordered, and use a hack to make the doubly linked list head
// contain only one pointer instead of two..
//
// Below is the hack: let's assume the memory layout of the doubly-link is
//   [ prev ]
//   [ next ]
//
// Then the linked list looks like the following:
//
//        first element                                 last element
//          [ prev ] -------------> [      ]              [ prev ]  ---> ...
// ... <--- [ next ]                [ head ] <----------- [ next ]
//
// That is, the 'prev' field of the first element points to one word before 'head', and the 'next' field of
// the last element points at 'head'.
//
// The invariant for 'head' is that it points to either​ the first element, or​ the last element, or itself if the list is empty.
//
// Removing an element 'p' from the bag is same as doubly linked list:
//   p->next->prev = p->prev;
//   p->prev->next = p->next;
// (the order is important: it makes sure after deleting the last element 'head' points to itself)
//
// Insertion needs to check whether the head is empty, pointing to the first element or pointing to the last element.
// It's not hard to prove the correctness by proving that the invariant is always maintained.
//
template<typename CRTP>
class SmallHeadedUnorderedDoublyLinkedListNode
{
public:
    SmallHeadedUnorderedDoublyLinkedListNode()
    {
        TCSet(m_prev, SpdsOrSystemHeapPtr<Node> { 0 });
        TCSet(m_next, SpdsOrSystemHeapPtr<Node> { 0 });
    }

    template<typename T, typename = std::enable_if_t<IsPtrOrHeapPtr<T, CRTP>>>
    static bool IsOnList(T self)
    {
        using NodeT = ReinterpretCastPreservingAddressSpaceType<Node*, T>;
        NodeT p = self;
        SpdsOrSystemHeapPtr<Node> prev = TCGet(p->m_prev);
#ifndef NDEBUG
        SpdsOrSystemHeapPtr<Node> next = TCGet(p->m_next);
        assert(prev.IsInvalidPtr() == next.IsInvalidPtr());
#endif
        return !prev.IsInvalidPtr();
    }

    template<typename T, typename = std::enable_if_t<IsPtrOrHeapPtr<T, CRTP>>>
    static void RemoveFromList(T nodeToRemove)
    {
        assert(IsOnList(nodeToRemove));

        using NodeT = ReinterpretCastPreservingAddressSpaceType<Node*, T>;
        NodeT p = nodeToRemove;

        SpdsOrSystemHeapPtr<Node> prev = TCGet(p->m_prev);
        SpdsOrSystemHeapPtr<Node> next = TCGet(p->m_next);

        TCSet(next->m_prev, prev);
        TCSet(prev->m_next, next);

        TCSet(p->m_prev, SpdsOrSystemHeapPtr<Node> { 0 });
        TCSet(p->m_next, SpdsOrSystemHeapPtr<Node> { 0 });
    }

    using Node = SmallHeadedUnorderedDoublyLinkedListNode;

    Packed<SpdsOrSystemHeapPtr<Node>> m_prev;
    Packed<SpdsOrSystemHeapPtr<Node>> m_next;
};

template<typename CRTPNodeType>
class SmallHeadedUnorderedDoublyLinkedList
{
    MAKE_NONCOPYABLE(SmallHeadedUnorderedDoublyLinkedList);
    MAKE_NONMOVABLE(SmallHeadedUnorderedDoublyLinkedList);

public:
    using Node = SmallHeadedUnorderedDoublyLinkedListNode<CRTPNodeType>;

    SmallHeadedUnorderedDoublyLinkedList() : m_head(HeadAddr(this)) { }

    template<typename T, typename = std::enable_if_t<IsPtrOrHeapPtr<T, SmallHeadedUnorderedDoublyLinkedList>>>
    static void Initialize(T self)
    {
        assert(TCGet(self->m_head).IsInvalidPtr());
        TCSet(self->m_head, HeadAddr(self));
    }

    template<typename T, typename = std::enable_if_t<IsPtrOrHeapPtr<T, SmallHeadedUnorderedDoublyLinkedList>>>
    static bool IsEmpty(T self)
    {
        assert(!TCGet(self->m_head).IsInvalidPtr());
        return TCGet(self->m_head) == HeadAddr(self);
    }

    template<typename T, typename = std::enable_if_t<IsPtrOrHeapPtr<T, SmallHeadedUnorderedDoublyLinkedList>>>
    static bool ContainsExactlyOneElement(T self)
    {
        assert(!IsEmpty(self));
        return TCGet(TCGet(self->m_head)->m_next) == HeadAddr(self) &&
               TCGet(TCGet(self->m_head)->m_prev) == OneWordBeforeHeadAddr(self);
    }

    template<typename T, typename = std::enable_if_t<IsPtrOrHeapPtr<T, SmallHeadedUnorderedDoublyLinkedList>>>
    static SpdsPtr<CRTPNodeType> GetAny(T self)
    {
        assert(!IsEmpty(self));
        return SpdsPtr<CRTPNodeType> { static_cast<HeapPtr<CRTPNodeType>>(TCGet(self->m_head).AsPtr()) };
    }

    template<typename T, typename = std::enable_if_t<IsPtrOrHeapPtr<T, SmallHeadedUnorderedDoublyLinkedList>>>
    static void Insert(T self, CRTPNodeType* cp)
    {
        assert(!TCGet(self->m_head).IsInvalidPtr());
        Node* p = cp;
        AssertIsSpdsPointer(p);

        if (IsEmpty(self))
        {
            // the list is empty
            //
            TCSet(p->m_prev, OneWordBeforeHeadAddr(self));
            TCSet(p->m_next, HeadAddr(self));
        }
        else if (Packed<SpdsOrSystemHeapPtr<Node>>::Get(&self->m_head.Get()->m_next) == HeadAddr(self))
        {
            // head is pointing to the last element
            // insert after the last element
            //
            SpdsOrSystemHeapPtr<Node> head = TCGet(self->m_head);
            TCSet(p->m_prev, head);
            TCSet(p->m_next, HeadAddr(self));
            TCSet(head->m_next, SpdsOrSystemHeapPtr<Node> { p });
        }
        else
        {
            SpdsOrSystemHeapPtr<Node> head = TCGet(self->m_head);
            assert(TCGet(head->m_prev) == OneWordBeforeHeadAddr(self));
            // head is pointing to the first element
            // insert before the first element
            //
            TCSet(p->m_prev, OneWordBeforeHeadAddr(self));
            TCSet(p->m_next, self->m_head);
            TCSet(head->m_prev, SpdsOrSystemHeapPtr<Node> { p });
        }
        TCSet(self->m_head, SpdsOrSystemHeapPtr<Node> { p });
    }

    // This is really terrible.. This is O(n) since we only have either the head or the tail,
    // but we need to update the head AND the tail to point to the new list head.
    // Fortunately this only happens when the watchpoints get fired, which is already much more expensive than this traversal.
    //
    template<typename T, typename = std::enable_if_t<IsPtrOrHeapPtr<T, SmallHeadedUnorderedDoublyLinkedList>>>
    static void TransferAllElementsTo(T self, SmallHeadedUnorderedDoublyLinkedList<CRTPNodeType>& out)
    {
        assert(IsEmpty(out));
        if (IsEmpty(self))
        {
            return;
        }

        HeapPtr<Node> firstElement;
        HeapPtr<Node> lastElement;
        SpdsOrSystemHeapPtr<Node> head = TCGet(self->m_head);
        if (TCGet(head->m_next) == HeadAddr(self))
        {
            // head is pointing to the last element
            //
            lastElement = head.AsPtr();
            firstElement = lastElement;
            while (true)
            {
                SpdsOrSystemHeapPtr<Node> prev = TCGet(firstElement->m_prev);
                if (prev == OneWordBeforeHeadAddr(self))
                {
                    break;
                }
                firstElement = prev.AsPtr();
            }
        }
        else
        {
            assert(TCGet(head->m_prev) == OneWordBeforeHeadAddr(self));
            // head is pointing to the first element
            //
            firstElement = head.AsPtr();
            lastElement = firstElement;
            while (true)
            {
                SpdsOrSystemHeapPtr<Node> next = TCGet(firstElement->m_next);
                if (next == HeadAddr(self))
                {
                    break;
                }
                lastElement = next.AsPtr();
            }
        }

        TCSet(firstElement->m_prev, OneWordBeforeHeadAddr(out));
        TCSet(lastElement->m_next, HeadAddr(out));
        out.m_head = SpdsOrSystemHeapPtr<Node> { firstElement };
    }

    template<typename T, typename = std::enable_if_t<IsPtrOrHeapPtr<T, SmallHeadedUnorderedDoublyLinkedList>>>
    static ReinterpretCastPreservingAddressSpaceType<SpdsOrSystemHeapPtr<Node>*, T> GetHeadAddrUnsafe(T self)
    {
        return &self->m_head;
    }

private:
    template<typename T, typename = std::enable_if_t<IsPtrOrHeapPtr<T, SmallHeadedUnorderedDoublyLinkedList>>>
    static SpdsOrSystemHeapPtr<Node> HeadAddr(T self)
    {
        return SpdsOrSystemHeapPtr<Node> { ReinterpretCastPreservingAddressSpace<Node*>(&self->m_head) };
    }

    template<typename T, typename = std::enable_if_t<IsPtrOrHeapPtr<T, SmallHeadedUnorderedDoublyLinkedList>>>
    static SpdsOrSystemHeapPtr<Node> OneWordBeforeHeadAddr(T self)
    {
        SpdsOrSystemHeapPtr<Node> headAddr = HeadAddr(self);
        return SpdsOrSystemHeapPtr<Node> { headAddr.m_value - static_cast<int32_t>(sizeof(SpdsOrSystemHeapPtr<Node>)) };
    }

    SpdsOrSystemHeapPtr<Node> m_head;
};

class WatchpointSet;
class DeferredWatchpointFire;
class EmbeddedWatchpointNode;
class VTWatchpointNode;

class WatchpointNodeBase : public SmallHeadedUnorderedDoublyLinkedListNode<WatchpointNodeBase>
{
    MAKE_NONCOPYABLE(WatchpointNodeBase);
    MAKE_NONMOVABLE(WatchpointNodeBase);

public:
    WatchpointNodeBase() { }

    // The class holding this watchpoint destructed for whatever reason
    // We may or may not be on the list, but if we are on the list, we must remove ourself from the list
    //
    ~WatchpointNodeBase() { Uninstall(); }

    bool IsInstalled() { return IsOnList(this); }
    void Uninstall()
    {
        if (IsInstalled())
        {
            WatchpointNodeBase::RemoveFromList(this);
        }
        assert(!IsInstalled());
    }

    // If true, this is a EmbeddedWatchpointNode. Otherwise this is a VTWatchpointNode
    //
    bool IsEmbeddedWatchpointNode() const
    {
        // This relies on little endianness
        //
        uint8_t val = *reinterpret_cast<const uint8_t*>(this + 1);
        return (val & 1);
    }

    EmbeddedWatchpointNode* AsEmbeddedWatchpointNode();
    VTWatchpointNode* AsVTWatchpointNode();

private:
    friend class WatchpointSet;

    void OnFire(HeapPtrTranslator translator);
    void OnWatchpointSetDestruct(HeapPtrTranslator translator);
};
static_assert(sizeof(WatchpointNodeBase) == 8);

// This class needs to be aligned to at least 2 bytes, since we use lowest bit == 0 for distinguishing purpose in WatchpointNodeBase::IsEmbeddedWatchpointNode()
// But fortunately any class with vtable is already aligned to 8 bytes so we are good.
//
class alignas(8) WatchpointWithVirtualTable
{
public:
    virtual ~WatchpointWithVirtualTable() { }
    virtual void OnFire(HeapPtrTranslator translator, WatchpointNodeBase* watchpointNode) = 0;
    virtual void OnWatchpointSetDestruct(HeapPtrTranslator translator, WatchpointNodeBase* watchpointNode) = 0;
};
static_assert(sizeof(WatchpointWithVirtualTable) == 8);

namespace internal
{

template<auto watchpoint_member_object_ptr>
constexpr WatchpointEnumKind GetWatchpointEnumKindImpl()
{
    using T = decltype(watchpoint_member_object_ptr);
    static_assert(std::is_member_object_pointer_v<T>);
    static_assert(std::is_same_v<value_type_of_member_object_pointer_t<T>, EmbeddedWatchpointNode>);
    using WatchpointClass = class_type_of_member_object_pointer_t<T>;
    constexpr size_t offset = offsetof_member_v<watchpoint_member_object_ptr>;

    // Important to write as two nested constexpr-if instead of a single one, to suppress incomplete type errors
    //
#define macro(ord, wpk)                                                                                         \
    if constexpr(std::is_same_v<WatchpointClass, WPK_CPP_NAME(wpk)>) {                                          \
        if constexpr(offset == static_cast<size_t>(WatchpointClass::WPK_CPP_OFFSET(wpk))) {                     \
            constexpr WatchpointEnumKind r = static_cast<WatchpointEnumKind>(ord);                              \
            static_assert(static_cast<uint8_t>(r) < static_cast<uint8_t>(WatchpointEnumKind::X_END_OF_ENUM));   \
            return r;                                                                                           \
        }                                                                                                       \
    }
PP_FOR_EACH_UNPACK_TUPLE(macro, PP_ZIP_TWO_LISTS((PP_NATURAL_NUMBERS_LIST), (WATCHPOINT_KIND_LIST)))
#undef macro
    ReleaseAssert(false);
}

}   // namespace internal

template<auto watchpoint_member_object_ptr>
constexpr WatchpointEnumKind x_watchpointEnumKindFor = internal::GetWatchpointEnumKindImpl<watchpoint_member_object_ptr>();

class VTWatchpointNode final : public WatchpointNodeBase
{
public:
    VTWatchpointNode(WatchpointWithVirtualTable* holder)
        : m_holder(holder)
    { }

    VTWatchpointNode(HeapPtr<WatchpointWithVirtualTable> holder)
        : m_holder(holder)
    { }

    VTWatchpointNode(SpdsPtr<WatchpointWithVirtualTable> holder)
        : m_holder(holder)
    { }

private:
    friend class WatchpointNodeBase;

    WatchpointWithVirtualTable* GetHolder(HeapPtrTranslator translator)
    {
        return translator.TranslateToRawPtr(m_holder.AsPtr());
    }

    void OnFire(HeapPtrTranslator translator)
    {
        GetHolder(translator)->OnFire(translator, this);
    }

    void OnWatchpointSetDestruct(HeapPtrTranslator translator)
    {
        GetHolder(translator)->OnWatchpointSetDestruct(translator, this);
    }

    SpdsPtr<WatchpointWithVirtualTable> m_holder;
};
static_assert(sizeof(VTWatchpointNode) == 12);

class EmbeddedWatchpointNode final : public WatchpointNodeBase
{
public:
    // The standard pattern to call the constructor is something similar to:
    //       m_watchpoint(x_watchpointEnumKindFor<&CodeJettisonWatchpoint::m_watchpoint>)
    //
    EmbeddedWatchpointNode(WatchpointEnumKind kind)
        : m_taggedKind(static_cast<uint8_t>(kind) * 2 + 1)
    {
        assert(static_cast<uint8_t>(kind) < static_cast<uint8_t>(WatchpointEnumKind::X_END_OF_ENUM));
    }

    const char* GetHumanReadableComment() const { return x_watchpointHumanReadableComments[static_cast<size_t>(GetKind())]; }

    WatchpointEnumKind GetKind() const { return static_cast<WatchpointEnumKind>(m_taggedKind / 2); }

private:
    friend class WatchpointNodeBase;
    void OnFire(HeapPtrTranslator translator);
    void OnWatchpointSetDestruct(HeapPtrTranslator translator);

    uint8_t m_taggedKind;
};
static_assert(sizeof(EmbeddedWatchpointNode) == 9);

inline EmbeddedWatchpointNode* WatchpointNodeBase::AsEmbeddedWatchpointNode()
{
    assert(IsEmbeddedWatchpointNode());
    return static_cast<EmbeddedWatchpointNode*>(this);
}

inline VTWatchpointNode* WatchpointNodeBase::AsVTWatchpointNode()
{
    assert(!IsEmbeddedWatchpointNode());
    return static_cast<VTWatchpointNode*>(this);
}

inline void WatchpointNodeBase::OnFire(HeapPtrTranslator translator)
{
    if (IsEmbeddedWatchpointNode())
    {
        AsEmbeddedWatchpointNode()->OnFire(translator);
    }
    else
    {
        AsVTWatchpointNode()->OnFire(translator);
    }
}

inline void WatchpointNodeBase::OnWatchpointSetDestruct(HeapPtrTranslator translator)
{
    if (IsEmbeddedWatchpointNode())
    {
        AsEmbeddedWatchpointNode()->OnWatchpointSetDestruct(translator);
    }
    else
    {
        AsVTWatchpointNode()->OnWatchpointSetDestruct(translator);
    }
}

enum class WatchpointSetState
{
    Clear,
    Watching,
    Invalidated
};

class WatchpointSet
{
    MAKE_NONCOPYABLE(WatchpointSet);
    MAKE_NONMOVABLE(WatchpointSet);

public:
    WatchpointSet(bool bornWatched)
    {
        if (!bornWatched)
        {
            WatchpointList::GetHeadAddrUnsafe(&m_watchpoints)->m_value = x_clearState;
        }
        AssertIff(bornWatched, IsWatched(this));
        AssertIff(!bornWatched, IsClear(this));
    }

    ~WatchpointSet()
    {
        if (!IsWatched(this))
        {
            return;
        }

        if (WatchpointList::IsEmpty(&m_watchpoints))
        {
            return;
        }

        TriggerDestructionEvent();
    }

    template<typename T, typename = std::enable_if_t<IsPtrOrHeapPtr<T, WatchpointSet>>>
    static bool IsClear(T self) { return WatchpointList::GetHeadAddrUnsafe(&self->m_watchpoints)->m_value == x_clearState; }

    template<typename T, typename = std::enable_if_t<IsPtrOrHeapPtr<T, WatchpointSet>>>
    static bool IsWatched(T self) { return WatchpointList::GetHeadAddrUnsafe(&self->m_watchpoints)->m_value < 0; }

    template<typename T, typename = std::enable_if_t<IsPtrOrHeapPtr<T, WatchpointSet>>>
    static bool IsInvalidated(T self)
    {
        int32_t v = WatchpointList::GetHeadAddrUnsafe(&self->m_watchpoints)->m_value;
        return v == x_invalidatedState;
    }

    template<typename T, typename = std::enable_if_t<IsPtrOrHeapPtr<T, WatchpointSet>>>
    static bool IsValid(T self) { return !IsInvalidated(self); }

    template<typename T, typename = std::enable_if_t<IsPtrOrHeapPtr<T, WatchpointSet>>>
    static bool IsWatchedButNoWatchers(T self)
    {
        assert(IsWatched(self));
        return WatchpointList::IsEmpty(self->m_watchpoints);
    }

    template<typename T, typename = std::enable_if_t<IsPtrOrHeapPtr<T, WatchpointSet>>>
    static WatchpointSetState GetState(T self)
    {
        if (IsClear(self)) { return WatchpointSetState::Clear; }
        if (IsInvalidated(self)) { return WatchpointSetState::Invalidated; }
        assert(IsWatched(self));
        return WatchpointSetState::Watching;
    }

    template<typename T, typename = std::enable_if_t<IsPtrOrHeapPtr<T, WatchpointSet>>>
    static void StartWatching(T self)
    {
        assert(IsValid(self));
        if (IsClear(self))
        {
            WatchpointList::Initialize(&self->m_watchpoints);
        }
        assert(GetState(self) == WatchpointSetState::Watching);
    }

    template<typename T, typename = std::enable_if_t<IsPtrOrHeapPtr<T, WatchpointSet>>>
    static void AddWatchpoint(T self, WatchpointNodeBase* wp)
    {
        assert(!wp->IsInstalled());
        StartWatching(self);
        WatchpointList::Insert(&self->m_watchpoints, wp);
    }

    // For a slowpath violation, we may choose to not invalidate if the watchpoint is in Clear state.
    //
    template<typename T, typename = std::enable_if_t<IsPtrOrHeapPtr<T, WatchpointSet>>>
    static void HandleSlowPathViolation(T self)
    {
        if (IsClear(self))
        {
            return;
        }
        Invalidate(self);
    }

    template<typename T, typename = std::enable_if_t<IsPtrOrHeapPtr<T, WatchpointSet>>>
    static void Invalidate(T self);

    template<typename T, typename = std::enable_if_t<IsPtrOrHeapPtr<T, WatchpointSet>>>
    static void InvalidateButDeferFire(T self, DeferredWatchpointFire& deferObj /*out*/);

private:
    friend class DeferredWatchpointFire;

    // WARNING: After this operation, one must update self to either ClearState or InvalidatedState!
    //
    template<typename T, typename = std::enable_if_t<IsPtrOrHeapPtr<T, WatchpointSet>>>
    static void TransferWatchpointsTo(T self, DeferredWatchpointFire& deferredObj /*out*/);

    template<typename T, typename = std::enable_if_t<IsPtrOrHeapPtr<T, WatchpointSet>>>
    static void SetStateToInvalidated(T self)
    {
        WatchpointList::GetHeadAddrUnsafe(&self->m_watchpoints)->m_value = x_invalidatedState;
    }

    void InvalidateKnowingContainingOnlyOneWatchpoint();
    void SlowpathInvalidate();

    void TriggerDestructionEvent();

    // Only called by DeferredWatchpointFire!
    //
    void TriggerFireEvent();

    // If the pointer == 0, it means the watchpoint set is clear (not watched, and should not be invalidated if a slowpath violates the watchpoint)
    // If the pointer == 1, it means the watchpoint set is invalidated
    // Otherwise it means the watchpoint set is in watched state, and the list stores all watchers
    //
    constexpr static int32_t x_clearState = 0;
    constexpr static int32_t x_invalidatedState = 1;

    using WatchpointList = SmallHeadedUnorderedDoublyLinkedList<WatchpointNodeBase>;
    WatchpointList m_watchpoints;
};
static_assert(sizeof(WatchpointSet) == 4);

// A list of watchpoints that are temporarily deferred to fire
// All watchpoints are fired when this object is destructed
//
class DeferredWatchpointFire
{
    MAKE_NONCOPYABLE(DeferredWatchpointFire);
    MAKE_NONMOVABLE(DeferredWatchpointFire);
public:
    DeferredWatchpointFire();
    ~DeferredWatchpointFire();

private:
    friend class WatchpointSet;
    WatchpointSet* m_set;
};

template<typename T, typename>
void WatchpointSet::InvalidateButDeferFire(T self, DeferredWatchpointFire& deferObj /*out*/)
{
    if (likely(!IsWatched(self) || WatchpointList::IsEmpty(&self->m_watchpoints)))
    {
        SetStateToInvalidated(self);
        return;
    }

    TransferWatchpointsTo(self, deferObj);
    SetStateToInvalidated(self);
}

template<typename T, typename>
void WatchpointSet::Invalidate(T self)
{
    if (likely(!IsWatched(self) || WatchpointList::IsEmpty(&self->m_watchpoints)))
    {
        SetStateToInvalidated(self);
    }
    else
    {
        TranslateToRawPointer(self)->SlowpathInvalidate();
    }
}

template<typename T, typename>
void WatchpointSet::TransferWatchpointsTo(T self, DeferredWatchpointFire& deferredObj /*out*/)
{
    WatchpointList::TransferAllElementsTo(&self->m_watchpoints, deferredObj.m_set->m_watchpoints /*out*/);
}

}   // namespace ToyLang

