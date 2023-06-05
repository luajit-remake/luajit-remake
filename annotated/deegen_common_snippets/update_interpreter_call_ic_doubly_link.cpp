#include "define_deegen_common_snippet.h"
#include "runtime_utils.h"

static void DeegenSnippet_UpdateInterpreterCallIcDoublyLink(HeapPtr<ExecutableCode> calleeCbHeapPtr, uint8_t* doublyLink)
{
    uint8_t* prev = doublyLink + UnalignedLoad<int32_t>(doublyLink);
    uint8_t* next = doublyLink - UnalignedLoad<int32_t>(doublyLink + 4);
    {
        int32_t diff = SafeIntegerCast<int32_t>(prev - next);
        UnalignedStore<int32_t>(prev + 4, diff);
        UnalignedStore<int32_t>(next, diff);
    }

    HeapPtr<ExecutableCode::InterpreterCallIcAnchor> cbAnchor = &calleeCbHeapPtr->m_interpreterCallIcList;
    HeapPtr<uint8_t> cbAnchorNext = reinterpret_cast<HeapPtr<uint8_t>>(cbAnchor) - cbAnchor->m_nextOffset;

    SystemHeapPointer<uint8_t> cbAnchorU8 { cbAnchor };
    SystemHeapPointer<uint8_t> doublyLinkU8 { doublyLink };
    SystemHeapPointer<uint8_t> cbAnchorNextU8 { cbAnchorNext };

    {
        int32_t diff = static_cast<int32_t>(cbAnchorU8.m_value - doublyLinkU8.m_value);
        cbAnchor->m_nextOffset = diff;
        UnalignedStore<int32_t>(doublyLink, diff);
    }

    {
        int32_t diff = static_cast<int32_t>(doublyLinkU8.m_value - cbAnchorNextU8.m_value);
        UnalignedStore<int32_t>(doublyLink + 4, diff);
        UnalignedStore<int32_t>(cbAnchorNext, diff);
    }
}

DEFINE_DEEGEN_COMMON_SNIPPET("UpdateInterpreterCallIcDoublyLink", DeegenSnippet_UpdateInterpreterCallIcDoublyLink)
