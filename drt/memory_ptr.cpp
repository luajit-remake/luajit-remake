#include <memory_ptr.h>

thread_local VM* activeVMForCurrentThread = nullptr;

extern "C" void* WARN_UNUSED DeegenImpl_GetVMBasePointer() {
    return activeVMForCurrentThread;
}
