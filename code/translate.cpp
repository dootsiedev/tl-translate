#include "core/global.h"
#include "translate.h"
#include "translation_context.h"

// sanity check.
static_assert(L'☃' == L'\x2603', "File encoding appears not to be UTF-8");

#ifdef TL_COMPILE_TIME_ASSERTS
// I don't think the static_assert is neccessary, but I can't figure out how lambdas works.
static_assert([] {
#define TL(key, value) static_assert(const_get_text(key) != 0, "translation not found");
#include "../translations/tl_begin_macro.txt"
#include "../translations/tl_all_languages.txt"
#include "../translations/tl_end_macro.txt"
	return true;
}());
#endif

/*
// I don't need domains, but it could help with macro hidden translations (stinky code).
// only the extraction merging cares about domains (to gracefully ignore domains).
// But technically I could feed the AST parser compiler database macro defines
// but this wont work if you have mutually exclusive macros (AKA #else).
// but it's possible to make the AST parse multiple combinations using a feature matrix.
// the domain still offers the benefit of performance (not my largest concern),
// and I could use the AST to make sure that all the domain guarded translations are using the
domain. const char* translate_gettext_domain(const char* text, TL_DOMAIN domain)
{
#ifdef TL_COMPILE_TIME_TRANSLATION
	switch(g_translation_context.get_lang())
	{
		#define TL(...)
		#define TL_D(d, x, y) if(d == domain && strcmp(x, text) == 0)  return (y != nullptr) ? y :
text; #include "translation_languages.inl" #undef TL_D #undef TL
	}
}
*/

const char* translate_gettext(const char* text)
{
#ifdef TL_COMPILE_TIME_TRANSLATION
	switch(g_translation_context.current_lang)
	{
// switch statement entry
#define TL_START(lang, ...) case TL_LANG::lang:
#define TL(x, y) if(strcmp(x, text) == 0) return (y != nullptr) ? y : text;
#define TL_END() break;
#include "../translations/tl_begin_macro.txt"
#include "../translations/tl_all_languages.txt"
#include "../translations/tl_end_macro.txt"
	}

#else
	// TODO: this should be handled differently.
	const char* result = g_translation_context.get_text(text);
	if(result != nullptr)
	{
		return result;
	}
	print_missing_translation(text, false);
	return text;
#endif
}

#ifdef TL_PERFECT_HASH_TRANSLATION
const char* translate_hash(const char* text, translate_hash_type hash)
{
#ifdef TL_COMPILE_TIME_TRANSLATION
	switch(g_translation_context.get_lang())
	{
	case TL_LANG::ENGLISH: return text;
#undef TL
#define TL(x, y) \
	if(x == hash) return (y != nullptr) ? y : text;
#include "../translations/all_languages.txt"
	}
	print_missing_translation(text, false);
	return text;
#else
	return g_translation_context.get_hashed_text(text, hash);
#endif // TL_COMPILE_TIME_TRANSLATION
}
#endif // TL_PERFECT_HASH_TRANSLATION
