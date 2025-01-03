#pragma once

#include "common.h"
#include "heap_ptr_utils.h"
#include "memory_ptr.h"

template<typename CRTP>
class SpdsDoublyLinkedListNode
{
public:
    SpdsDoublyLinkedListNode()
    {
        TCSet(m_prevNode, SpdsOrSystemHeapPtr<Node> { 0 });
        TCSet(m_nextNode, SpdsOrSystemHeapPtr<Node> { 0 });
    }

    template<typename T, typename = std::enable_if_t<IsPtrOrHeapPtr<T, CRTP>>>
    static bool IsOnDoublyLinkedList(T self)
    {
        SpdsOrSystemHeapPtr<Node> prev = TCGet(self->m_prevNode);
#ifndef NDEBUG
        SpdsOrSystemHeapPtr<Node> next = TCGet(self->m_nextNode);
        Assert(prev.IsInvalidPtr() == next.IsInvalidPtr());
#endif
        return !prev.IsInvalidPtr();
    }

    bool IsOnDoublyLinkedList() { return IsOnDoublyLinkedList(static_cast<CRTP*>(this)); }

    template<typename T, typename = std::enable_if_t<IsPtrOrHeapPtr<T, CRTP>>>
    static void RemoveFromDoublyLinkedList(T nodeToRemove)
    {
        Assert(IsOnDoublyLinkedList(nodeToRemove));

        SpdsOrSystemHeapPtr<Node> prev = TCGet(nodeToRemove->m_prevNode);
        SpdsOrSystemHeapPtr<Node> next = TCGet(nodeToRemove->m_nextNode);

        TCSet(next->m_prevNode, prev);
        TCSet(prev->m_nextNode, next);

        TCSet(nodeToRemove->m_prevNode, SpdsOrSystemHeapPtr<Node> { 0 });
        TCSet(nodeToRemove->m_nextNode, SpdsOrSystemHeapPtr<Node> { 0 });

        Assert(!IsOnDoublyLinkedList(nodeToRemove));
    }

    void RemoveFromDoublyLinkedList() { RemoveFromDoublyLinkedList(static_cast<CRTP*>(this)); }

    using Node = SpdsDoublyLinkedListNode;

    template<typename T> friend class SpdsDoublyLinkedList;

    SpdsOrSystemHeapPtr<Node> m_prevNode;
    SpdsOrSystemHeapPtr<Node> m_nextNode;
};

template<typename CRTP>
class SpdsDoublyLinkedList
{
    MAKE_NONCOPYABLE(SpdsDoublyLinkedList);
    MAKE_NONMOVABLE(SpdsDoublyLinkedList);

public:
    using Node = SpdsDoublyLinkedListNode<CRTP>;
    static_assert(sizeof(Node) == 8);
    static_assert(std::is_base_of_v<Node, CRTP>);

    SpdsDoublyLinkedList()
    {
        m_anchor.m_prevNode = &m_anchor;
        m_anchor.m_nextNode = &m_anchor;
    }

    bool IsEmpty()
    {
        Assert(!m_anchor.m_prevNode.IsInvalidPtr());
        Assert(!m_anchor.m_nextNode.IsInvalidPtr());
        AssertIff(m_anchor.m_prevNode == &m_anchor, m_anchor.m_nextNode == &m_anchor);
        return m_anchor.m_prevNode == &m_anchor;
    }

    void InsertAtHead(CRTP* p)
    {
        Assert(!m_anchor.m_prevNode.IsInvalidPtr());
        Assert(!m_anchor.m_nextNode.IsInvalidPtr());
        AssertIsSpdsPointer(p);

        SpdsOrSystemHeapPtr<Node> head = m_anchor.m_nextNode;
        p->m_prevNode = TranslateToHeapPtr(&m_anchor);
        p->m_nextNode = head;
        TCSet(head->m_prevNode, SpdsOrSystemHeapPtr<Node> { p });
        m_anchor.m_nextNode = SpdsOrSystemHeapPtr<Node> { p };
    }

    struct Iterator
    {
        Iterator(VM* vm, Node* ptr) : m_vm(vm), m_ptr(ptr) { }

        CRTP* operator*() const { return static_cast<CRTP*>(m_ptr); }

        Iterator& operator++()
        {
            m_ptr = TranslateToRawPointer(m_vm, m_ptr->m_nextNode.AsPtr());
            return *this;
        }

        friend bool operator==(const Iterator& a, const Iterator& b) { return a.m_ptr == b.m_ptr; }
        friend bool operator!=(const Iterator& a, const Iterator& b) { return a.m_ptr != b.m_ptr; }

        VM* m_vm;
        Node* m_ptr;
    };

    struct Elements
    {
        Iterator begin() const { return Iterator(m_vm, TranslateToRawPointer(m_vm, m_anchorPtr->m_nextNode.AsPtr())); }
        Iterator end() const { return Iterator(m_vm, m_anchorPtr); }

        VM* m_vm;
        Node* m_anchorPtr;
    };

    template<typename T, typename = std::enable_if_t<IsPtrOrHeapPtr<T, SpdsDoublyLinkedList>>>
    static Elements elements(T self)
    {
        VM* vm = VM_GetActiveVMForCurrentThread();
        return { .m_vm = vm, .m_anchorPtr = TranslateToRawPointer(vm, &(self->m_anchor)) };
    }

    Elements elements()
    {
        return elements(this);
    }

private:
    Node m_anchor;
};
