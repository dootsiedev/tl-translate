#include "core/global.h"
#include "translate.h"
#include "translation_context.h"

// sanity check.
static_assert(L'☃' == L'\x2603', "File encoding appears not to be UTF-8");

#ifdef TL_COMPILE_TIME_ASSERTS
// you should compile this file at the end of the cmake lists
// so that english static asserts are done first before the other languages
static_assert([] {
#define TL(key, value) static_assert(get_text_index(key) != 0, "translation not found");
#include "../translations/tl_begin_macro.txt"
#include "../translations/tl_all_languages.txt"
#include "../translations/tl_end_macro.txt"
	return true;
}());


const char* translate_gettext(const char* text, tl_index index)
{
	// I could inline/constexpr this function, and it would get rid of the lambda hack,
	// but I doubt there would be any useful performance gain.
#ifdef TL_COMPILE_TIME_TRANSLATION
	switch(get_translation_context().current_lang)
	{
	case TL_LANG::English: return text;
#define TL_START(lang, ...) \
	case TL_LANG::lang:     \
		switch(index)       \
		{
#define TL(x, y) case get_text_index(x): return (y != nullptr) ? y : text;
#define TL_END() \
	}            \
	break;
#include "../translations/tl_begin_macro.txt"
#include "../translations/tl_all_languages.txt"
#include "../translations/tl_end_macro.txt"
	}
	ASSERT_M("translation not found", text);
	return text;
#else
	return get_translation_context().get_text(text, index);
#endif
}
#else
const char* translate_gettext(const char* text)
{
#ifdef TL_COMPILE_TIME_TRANSLATION
	switch(get_translation_context().current_lang)
	{
	case TL_LANG::English: return text;
// switch statement entry
#define TL_START(lang, ...) case TL_LANG::lang:
#define TL(x, y) if(strcmp(x, text) == 0) return (y != nullptr) ? y : text;
#define TL_END() break;
#include "../translations/tl_begin_macro.txt"
#include "../translations/tl_all_languages.txt"
#include "../translations/tl_end_macro.txt"
	}
	ASSERT_M("translation not found", text);
	return text;
#else
	return get_translation_context().get_text(text);
#endif
}
#endif
