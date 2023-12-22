#pragma once

#include "common.h"

// Same as munmap except that it logs a warning on failure
//
void do_munmap(void* ptr, size_t size);

// This function works by doing a larger mmap then unmap the unneeded parts
// 'alignment' must be power-of-2 and >page_size
//
void* WARN_UNUSED do_mmap_with_custom_alignment(size_t alignment, size_t length, int prot_flags, int map_flags);
