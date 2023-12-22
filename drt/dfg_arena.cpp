#include "dfg_arena.h"

void InitializeDfgAllocationArenaIfNeeded()
{
    using namespace dfg;
    // Must use g_arena, not DfgAlloc() here, since we are doing the initialization!
    //
    if (g_arena == nullptr)
    {
        assert(g_arena == nullptr);
        g_arena = Arena::Create();
        assert(g_arena != nullptr);
    }
}
