#include "dfg_node.h"
#include "bytecode_builder.h"

namespace dfg {

constexpr BCKind x_bcKindEndOfEnum = BCKind::X_END_OF_ENUM;

static_assert(static_cast<size_t>(BCKind::X_END_OF_ENUM) + static_cast<size_t>(NodeKind_FirstAvailableGuestLanguageNodeKind) <= std::numeric_limits<std::underlying_type_t<NodeKind>>::max());

}   // namespace dfg
