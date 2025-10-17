#include "core/global.h"
#include "translate.h"
#include "translation_context.h"

// sanity check.
static_assert(L'☃' == L'\x2603', "File encoding appears not to be UTF-8");

// found means that you found the string, but it's NULL: TL("something", NULL)
static void print_missing_translation(const char* text, bool found)
{
	// TODO: should be thread local, and just make a thread_local a macro for emscripten
	static int counter = 0;
	static const char* prev_string = nullptr;
	// TODO: add cvar max counter
	static int max_counter = 10;

	if(counter >= max_counter)
	{
		return;
	}
	if(prev_string == text)
	{
		return;
	}
	counter++;
	prev_string = text;

	// missing translation means you did not run the extractor,
	// or the extractor missed the string somehow (not in the file).
	// NULL translation means the text was never translated  TL("something", NULL)
	// NOTE: If my logs were noisy, it would be nice if there was a exit report for warnings/info.
	slogf("<%s: <start>%s<end>>\n", found ? "NULL translation" : "missing translation", text);

	if(counter >= max_counter)
	{
		slogf("info: missing translation counter reached (%d), no more will be shown.\n", 10);
	}
}

#ifdef TL_COMPILE_TIME_TRANSLATION
// switch statement entry
#define TL_START(lang, ...) case TL_LANG::lang:
#define TL_END() break;
#include "../translations/tl_begin_macro.txt"
#endif // TL_COMPILE_TIME_TRANSLATION

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
	switch(g_translation_context.get_lang())
	{
// for !NDEBUG, do things a little bit slower.
#ifndef NDEBUG
// every english template entry is unimplemented, so remove print_missing_translation
#undef TL
#define TL(x, y) \
	if(strcmp(x, text) == 0) return (y != nullptr) ? y : text;
#include "../translations/english_ref.inl"
#undef TL
#define TL(x, y)                                   \
	if(strcmp(x, text) == 0)                       \
	{                                              \
		if(y == nullptr)                           \
		{                                          \
			print_missing_translation(text, true); \
			return text;                           \
		}                                          \
		return y;                                  \
	}
#include "../translations/tl_all_languages.txt"
#else // NDEBUG
	case TL_LANG::English: return text;
#undef TL
#define TL(x, y) \
	if(strcmp(x, text) == 0) return (y != nullptr) ? y : text;
#include "../translations/tl_all_languages.txt"
#endif // NDEBUG
	}
	print_missing_translation(text, false);
	// TODO: print a warning that this string was not found.
	return text;
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

#ifdef TL_COMPILE_TIME_TRANSLATION
#include "../translations/tl_end_macro.txt"
#endif