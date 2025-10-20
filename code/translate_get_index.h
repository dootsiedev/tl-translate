#pragma once
#include "core/global.h"
// I don't want to copy paste this function
// but it needs to be inside the header,
// and if I always put in the header, it will cause redundant rebuilds
// (necessary for TL_COMPILE_TIME_ASSERTS checks).

typedef uint16_t tl_index;

inline constexpr tl_index get_text_index(std::string_view text)
{
	// index 0 is uninitialized.
	tl_index index = 1;
#define TL(key, value) if(std::string_view(key) == text) {return index;} index++;
#include "../translations/tl_begin_macro.txt"
#include "../translations/english_ref.inl"
#include "../translations/tl_end_macro.txt"
	return 0;
}