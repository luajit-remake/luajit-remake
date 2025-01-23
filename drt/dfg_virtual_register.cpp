#include "dfg_virtual_register.h"
#include "dfg_node.h"

namespace dfg {

#ifdef TESTBUILD
void VirtualRegisterMappingInfo::AssertIsConstantOrUnboxedConstantNode(Node* node)
{
    TestAssert(node != nullptr);
    TestAssert(node->IsConstantNode() || node->IsUnboxedConstantNode());
}
#endif

}   // namespace dfg
