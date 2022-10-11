#pragma once

#include "watchpoint.h"
#include "vm.h"

inline DeferredWatchpointFire::DeferredWatchpointFire()
{
    m_set = VM::GetActiveVMForCurrentThread()->AllocateFromSpdsRegionUninitialized<WatchpointSet>();
    ConstructInPlace(m_set, true /*bornWatched*/);
}

inline DeferredWatchpointFire::~DeferredWatchpointFire()
{
    assert(m_set != nullptr);
    m_set->TriggerFireEvent();
    assert(WatchpointSet::IsWatchedButNoWatchers(m_set));
    VM::GetActiveVMForCurrentThread()->DeallocateSpdsRegionObject(m_set);
    m_set = nullptr;
}
