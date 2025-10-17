#pragma once

#include "core/global.h"

#ifdef TL_PERFECT_HASH_TRANSLATION
// because I awkwardly need translate_hash_type
// should be a header if the constexpr hash is more than 100 lines
#include "translate.h"
#endif

#include <vector>

#ifdef TL_COMPILE_TIME_TRANSLATION
// don't define this in headers, the path is relative to where you #include

#define TL_START(lang, ...) lang,
#include "../translations/tl_begin_macro.txt"
enum class TL_LANG{
#include "../translations/english_ref.inl"
#include "../translations/tl_all_languages.txt"
};
#include "../translations/tl_end_macro.txt"

const char* get_lang_string(TL_LANG lang);
std::vector<TL_LANG> get_supported_languages();
#else
#include "translation_parser.h"
#include <map>
#endif // TL_COMPILE_TIME_TRANSLATION


struct translation_context
#ifndef TL_COMPILE_TIME_TRANSLATION
: public parse_observer
#endif
{
#ifdef TL_COMPILE_TIME_TRANSLATION
	// TODO: should be a cvar
	TL_LANG current_lang = TL_LANG::English;
	TL_LANG get_lang() const
	{
		return current_lang;
	}
	void set_lang(TL_LANG lang)
	{
		current_lang = lang;
	}
#else // TL_COMPILE_TIME_TRANSLATION

	// TODO: this should be a cvar

	// -1 = no language loaded (english)
	int current_lang = -1;

	struct language_entry
	{
		std::string long_enum;
		// en / jp
		std::string short_enum;
		// non-english id.
		std::string native_name;
		// path to translation.
		std::string translation_file;
	};

	bool on_header(tl_header_tuple& header) override;
	bool on_info(tl_info_tuple& header) override;
	bool on_translation(std::string&& key, std::string&& value) override;

	std::vector<language_entry> language_list;

	// loaded translation.
	std::map<std::string, std::string> strings;

	// TODO: this was the faster option, but I want to try using std::map first.
#if 0
	// a string that contains null terminating strings.
	// maybe I could insitu load the file, but I was thinking of using a basic arena.
	std::string memory;

	// probably sorted so that I can use a faster lookup.
	std::vector<std::string_view> translations;

#endif

#ifdef TL_PERFECT_HASH_TRANSLATION
	std::vector<translate_hash_type> hash_keys;
	const char* get_hashed_text(const char* text, translate_hash_type hash);
#else
	std::vector<std::string_view> keys;
	const char* get_text(const char* text);
#endif

#endif // TL_COMPILE_TIME_TRANSLATION

	// load the translation file (could be replaced with a overloaded cvar...)
	NDSERR bool init();
};

extern translation_context g_translation_context;