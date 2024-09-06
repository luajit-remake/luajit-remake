#include <memory_ptr.h>

thread_local VM* activeVMForCurrentThread = nullptr;

extern "C" void* __attribute__((__const__)) WARN_UNUSED DeegenImpl_GetVMBasePointer() {
    return activeVMForCurrentThread;
}
