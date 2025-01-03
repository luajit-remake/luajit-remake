#pragma once

#include "watchpoint.hpp"
#include "vm.h"

constexpr size_t x_watchpointClassOffsetForWatchpointKind[x_numWatchpointKinds + 1] = {
#define macro(wpk) static_cast<size_t>(WPK_CPP_NAME(wpk)::WPK_CPP_OFFSET(wpk)) ,
PP_FOR_EACH(macro, WATCHPOINT_KIND_LIST)
#undef macro
    static_cast<size_t>(-1)
};

template<WatchpointEnumKind kind>
void EmbeddedWatchpointOnFireDispatcher(HeapPtrTranslator translator, EmbeddedWatchpointNode* wp)
{
    Assert(wp->GetKind() == kind);
    constexpr size_t offset = x_watchpointClassOffsetForWatchpointKind[static_cast<size_t>(kind)];
    using T = WatchpointClassForWatchpointEnumKind<kind>;
    T* holder = reinterpret_cast<T*>(reinterpret_cast<uint8_t*>(wp) - offset);
    holder->OnFire(translator, wp);
}

template<WatchpointEnumKind kind>
void EmbeddedWatchpointOnWatchpointSetDestructDispatcher(HeapPtrTranslator translator, EmbeddedWatchpointNode* wp)
{
    Assert(wp->GetKind() == kind);
    constexpr size_t offset = x_watchpointClassOffsetForWatchpointKind[static_cast<size_t>(kind)];
    using T = WatchpointClassForWatchpointEnumKind<kind>;
    T* holder = reinterpret_cast<T*>(reinterpret_cast<uint8_t*>(wp) - offset);
    holder->OnWatchpointSetDestruct(translator, wp);
}

void EmbeddedWatchpointNode::OnFire(HeapPtrTranslator translator)
{
    WatchpointEnumKind kind = GetKind();
    Assert(kind != WatchpointEnumKind::X_END_OF_ENUM);
    switch (kind)
    {
#define macro(ord, wpk)                                                             \
    case static_cast<WatchpointEnumKind>(ord): {                                    \
        EmbeddedWatchpointOnFireDispatcher<static_cast<WatchpointEnumKind>(ord)>(   \
            translator, this);                                                      \
        return;                                                                     \
    }
PP_FOR_EACH_UNPACK_TUPLE(macro, PP_ZIP_TWO_LISTS((PP_NATURAL_NUMBERS_LIST), (WATCHPOINT_KIND_LIST)))
#undef macro
    case WatchpointEnumKind::X_END_OF_ENUM:
        __builtin_unreachable();
    }
}

void EmbeddedWatchpointNode::OnWatchpointSetDestruct(HeapPtrTranslator translator)
{
    WatchpointEnumKind kind = GetKind();
    Assert(kind != WatchpointEnumKind::X_END_OF_ENUM);
    switch (kind)
    {
#define macro(ord, wpk)                                                                             \
    case static_cast<WatchpointEnumKind>(ord): {                                                    \
        EmbeddedWatchpointOnWatchpointSetDestructDispatcher<static_cast<WatchpointEnumKind>(ord)>(  \
            translator, this);                                                                      \
        return;                                                                                     \
    }
PP_FOR_EACH_UNPACK_TUPLE(macro, PP_ZIP_TWO_LISTS((PP_NATURAL_NUMBERS_LIST), (WATCHPOINT_KIND_LIST)))
#undef macro
    case WatchpointEnumKind::X_END_OF_ENUM:
        __builtin_unreachable();
    }
}

void WatchpointSet::InvalidateKnowingContainingOnlyOneWatchpoint()
{
    Assert(WatchpointList::ContainsExactlyOneElement(&m_watchpoints));
    HeapPtrTranslator translator = VM::GetActiveVMForCurrentThread()->GetHeapPtrTranslator();
    WatchpointNodeBase* wp = translator.TranslateToRawPtr(WatchpointList::GetAny(&m_watchpoints).AsPtr());
    SetStateToInvalidated(this);
    wp->OnFire(translator);
}

// This is only called on the temporary watchpoint set holding all the watchpoints to fire
// The original watchpoint set has been set to invalidated, as the watchpoint code expects
//
void WatchpointSet::TriggerFireEvent()
{
    HeapPtrTranslator translator = VM::GetActiveVMForCurrentThread()->GetHeapPtrTranslator();

    Assert(IsWatched(this));
    while (!WatchpointList::IsEmpty(&m_watchpoints))
    {
        WatchpointNodeBase* wp = translator.TranslateToRawPtr(WatchpointList::GetAny(&m_watchpoints).AsPtr());
        WatchpointNodeBase::RemoveFromList(wp);
        wp->OnFire(translator);
    }
}

void WatchpointSet::TriggerDestructionEvent()
{
    HeapPtrTranslator translator = VM::GetActiveVMForCurrentThread()->GetHeapPtrTranslator();

    Assert(IsWatched(this));
    while (!WatchpointList::IsEmpty(&m_watchpoints))
    {
        WatchpointNodeBase* wp = translator.TranslateToRawPtr(WatchpointList::GetAny(&m_watchpoints).AsPtr());
        WatchpointNodeBase::RemoveFromList(wp);
        wp->OnWatchpointSetDestruct(translator);
    }
}

void WatchpointSet::SlowpathInvalidate()
{
    // A relatively-fast path for the case that the WatchpointSet contains only one watchpoint
    //
    if (WatchpointList::ContainsExactlyOneElement(&m_watchpoints))
    {
        InvalidateKnowingContainingOnlyOneWatchpoint();
        return;
    }

    // First move all watchpoints to a temporary list, then set watchpoint state to invalidated, then fire watchpoints
    // Why do we have to do this? Because a fired Watchpoint expects to see the WatchpointSet having been invalidated,
    // but removing node from the linked list could overwrite 'm_head' (which we have set to x_invalidatedState)..
    //
    DeferredWatchpointFire deferObj;
    TransferWatchpointsTo(this, deferObj);
    SetStateToInvalidated(this);
}
