// This is an independent project of an individual developer. Dear PVS-Studio, please check it.
// PVS-Studio Static Code Analyzer for C, C++, C#, and Java: https://pvs-studio.com
#include "core/global.h"
#include "translate.h"
#include "translation_context.h"

// sanity check.
static_assert(L'☃' == L'\x2603', "File encoding appears not to be UTF-8");

#ifdef TL_COMPILE_TIME_ASSERTS
// you should compile this file at the end of the cmake lists
// so that english static asserts are done first before the other languages
// maybe I should put this into it's own file because I want the files to be sorted...
static_assert([] {
#define TL_TEXT(key, value) static_assert(get_text_index(key) != 0, "translation not found");
#include "tl_begin_macro.txt"
#include "tl_all_languages.txt"
#include "tl_end_macro.txt"
	return true;
}());

const char* translate_gettext(const char* text, tl_index index)
{
#ifdef TL_COMPILE_TIME_TRANSLATION
	switch(get_translation_context().current_lang)
	{
	case TL_LANG::English: return text;
#define TL_START(lang, ...) \
	case TL_LANG::lang:     \
		switch(index)       \
		{
// clang-format off
#define TL_TEXT(x, y)  case get_text_index(x): return (y != nullptr) ? y : text;
// clang-format on
#define TL_END() \
	}            \
	break;
#include "tl_begin_macro.txt"
#include "tl_all_languages.txt"
#include "tl_end_macro.txt"
	}
	ASSERT_M("translation not found", text);
	return text;
#else
	return get_translation_context().text_table.get_text(text, index);
#endif
}
#ifdef TL_ENABLE_FORMAT
const char* translate_get_format(const char* text, tl_index index)
{
#ifdef TL_COMPILE_TIME_TRANSLATION
	switch(get_translation_context().current_lang)
	{
	case TL_LANG::English: return text;
#define TL_START(lang, ...) \
	case TL_LANG::lang:     \
		switch(index)       \
		{
		// clang-format off
#define TL_FORMAT(x, y)  case get_format_index(x): return (y != nullptr) ? y : text;
// clang-format on
#define TL_END() \
	}            \
	break;
#include "tl_begin_macro.txt"
#include "tl_all_languages.txt"
#include "tl_end_macro.txt"
	}
	ASSERT_M("translation not found", text);
	return text;
#else
	return get_translation_context().format_table.get_text(text, index);
#endif
}
#endif
#else
// TODO: I don't need to copy-paste, I can use get_format_index instead of strcmp
// NOLINTBEGIN("readability-duplicate-include")
// Technically I could use get_text_index, to avoid copy paste,
// but there would be no optimization, get_text_index is only fast if it's constexpr.
const char* translate_gettext(const char* text)
{
#ifdef TL_COMPILE_TIME_TRANSLATION
	// clang-format off
	switch(get_translation_context().current_lang)
	{
	case TL_LANG::English: return text;
// switch statement entry
#define TL_START(lang, ...) case TL_LANG::lang:
#define TL_TEXT(x, y) if(strcmp(x, text) == 0) { return (y != nullptr) ? y : text; }
#define TL_END() break;
#include "tl_begin_macro.txt"
#include "tl_all_languages.txt"
#include "tl_end_macro.txt"
	}
	ASSERT_M("translation not found", text);
	return text;
// clang-format on
#else
	return get_translation_context().text_table.get_text(text);
#endif
}
#ifdef TL_ENABLE_FORMAT
const char* translate_get_format(const char* text)
{
#ifdef TL_COMPILE_TIME_TRANSLATION
	switch(get_translation_context().current_lang)
	{
	case TL_LANG::English: return text;
#define TL_START(lang, ...) \
	case TL_LANG::lang:     \
		switch(index)       \
		{
		// clang-format off
#define TL_FORMAT(x, y)  case get_format_index(x): return (y != nullptr) ? y : text;
// clang-format on
#define TL_END() \
	}            \
	break;
#include "tl_begin_macro.txt"
#include "tl_all_languages.txt"
#include "tl_end_macro.txt"
	}
	ASSERT_M("translation not found", text);
	return text;
#else
	return get_translation_context().format_table.get_text(text);
#endif
}
#endif // TL_ENABLE_FORMAT
// NOLINTEND("readability-duplicate-include")
#endif
