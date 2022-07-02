#pragma once

#include <cstring>

// The buffer length required to call dragonbox_stringify_double
//
constexpr size_t x_dragonbox_stringify_double_buffer_length = 25;

// Print roundtrip-safe double value, terminated by '\0'
// 'output' must be a string buffer of capacity at least x_dragonbox_stringify_double_buffer_length
// Returns the pointer to the '\0' character
//
char* dragonbox_stringify_double(double v, char* output /*out*/);
