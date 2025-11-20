#pragma once

#ifdef __has_feature
#if __has_feature(address_sanitizer)
#define MY_HAS_ASAN
#endif
#elif defined(__SANITIZE_ADDRESS__)
#define MY_HAS_ASAN
#endif

#ifdef MY_HAS_ASAN
#include <sanitizer/asan_interface.h>

void my_asan_handler(const char* msg);

void init_asan();
#endif