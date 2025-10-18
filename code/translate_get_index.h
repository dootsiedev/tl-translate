#pragma once
#include "core/global.h"
// I don't want to copy paste this function
// and I want to get it out of translate.h if asserts are disabled to reduce rebuilding.

inline constexpr uint16_t get_text_index(std::string_view text)
{
	// index 0 is uninitialized.
	uint16_t index = 1;
#define TL(key, value) if(std::string_view(key) == text) {return index;} index++;
#include "../translations/tl_begin_macro.txt"
#include "../translations/english_ref.inl"
#include "../translations/tl_end_macro.txt"
	return 0;
}