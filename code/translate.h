#pragma once

// maybe for benchmarking compile time cost
#ifdef TL_DISABLE_TRANSLATE
#define _T(x) x
#ifdef TL_ENABLE_FORMAT
#define slogf_T slogf
#endif // TL_ENABLE_FORMAT
#else
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
#ifdef TL_ENABLE_FORMAT
const char* translate_get_format(const char* text, tl_index index);
// Translating formatted strings is a big NO NO for security, -Wformat-security will complain
// (if I made translate_get_format constexpr, maybe it will suppress it? But that's awful...
// or I could make a dummy call all the translations with printf,
// but this will not suppress Wformat-security... I need to disable it...)
// if a hacker hacked your translations, they can easily cause the code to segfault / or more.
// but... the workaround is so ugly that it gets in the way of translations...
// SO, instead I manually check the format specifiers, and make sure they match (during init).
// also note slogf type checking on MSVC requires /ANALYZE, so I use printf for the dummy.
#define slogf_T(fmt, ...)                                     \
	do                                                        \
	{                                                         \
		if(0)                                                 \
		{                                                     \
			/* get diagnostic warnings */                     \
			printf(fmt, __VA_ARGS__);                         \
		}                                                     \
		constexpr auto index = get_format_index(fmt);         \
		static_assert(index != 0, "translation not found");   \
		slogf(translate_get_format(fmt, index), __VA_ARGS__); \
	} while(0)
#define str_asprintf_T(str, fmt, ...)                                     \
	do                                                                    \
	{                                                                     \
		if(0)                                                             \
		{                                                                 \
			/* get diagnostic warnings */                                 \
			printf(fmt, __VA_ARGS__);                                     \
		}                                                                 \
		constexpr auto index = get_format_index(fmt);                     \
		static_assert(index != 0, "translation not found");               \
		str_asprintf(str, translate_get_format(fmt, index), __VA_ARGS__); \
	} while(0)
#endif // TL_ENABLE_FORMAT
#else
// simple gettext style translation done during runtime.
// I am not using gettext, but I could if I really wanted to.
// no plural forms, no domain, but that's not essential for my needs.
// I should add domain if I had a lot of strings that are hidden behind a macro,
// since it would cause unresolved translation warnings in the extractor,
// but I want to avoid having multiple translation files.
const char* translate_gettext(const char* text);
#define _T(x) translate_gettext(x)
#ifdef TL_ENABLE_FORMAT
const char* translate_get_format(const char* text);
#define slogf_T(fmt, ...)                              \
	do                                                 \
	{                                                  \
		if(0)                                          \
		{                                              \
			/* get diagnostic warnings */              \
			slogf(fmt, __VA_ARGS__);                   \
		}                                              \
		slogf(translate_get_format(fmt), __VA_ARGS__); \
	} while(0)
#endif // TL_ENABLE_FORMAT
#endif // TL_COMPILE_TIME_ASSERTS
#endif // TL_DISABLE_TRANSLATE
