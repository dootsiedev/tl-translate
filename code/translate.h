#pragma once

#ifdef TL_COMPILE_TIME_ASSERTS
#include "translate_get_index.h"
// I add an index so that I can use it to just look up the string.
const char* translate_gettext(const char* text, tl_index index);
// abusing lambda to use static_assert in an expression
// I can remove the lambda from the stack using the comma operator,
// but I feel like reducing the number of times get_text_index is called is more efficient?
#define _T(x)                                               \
	[] {                                                    \
		constexpr auto index = get_text_index(x);           \
		static_assert(index != 0, "translation not found"); \
		return translate_gettext(x, index);                 \
	}()
#else
// simple gettext style translation done during runtime.
// I am not using gettext, but I could if I really wanted to.
// no plural forms, no domain, but that's not essential for my needs.
// I should add domain if I had a lot of strings that are hidden behind a macro,
// since it would cause unresolved translation warnings in the extractor,
// but I want to avoid having multiple translation files.
const char* translate_gettext(const char* text);
#define _T(x) translate_gettext(x)
#endif // TL_COMPILE_TIME_ASSERTS
