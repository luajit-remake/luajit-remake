#include "dfg_arena.h"
#include "deegen_options.h"

namespace dfg {

Arena* const g_arena = x_allow_baseline_jit_tier_up_to_optimizing_jit ? Arena::Create() : nullptr;

}   // namespace dfg
