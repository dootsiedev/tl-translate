#pragma once

#include "core/global.h"

// NOTE: I could clean this up so that I don't include this.
#include "translate.h"

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
		std::string long_name;
		// en / jp
		std::string short_name;
		// non-english id.
		std::string native_name;
		// path to translation
		std::string translation_file;
	};

	// I don't want 2 observers (one for headers, and one for strings),
	// so instead I check init_languages.
	bool parse_headers = false;
	// pass the path of the file here.
	// NOTE: would a fs::path be any faster?
	std::string loading_path;

	const char* on_header(tl_header_tuple& header) override;
	const char* on_info(tl_info_tuple& header) override;
	const char* on_translation(std::string&& key, std::string&& value) override;

	std::vector<language_entry> language_list;

	// a string that contains null terminating strings.
	// maybe I could insitu load the file,
	// or just use an arena.
	std::string memory;

	// I lookup a string based off english_ref.inl
	// so the ID is ordered based on the location the string is in the english_ref.inl
	// this contains the offset inside of memory.
	// uint16_t might be small, but if you had more than 65k letters,
	// you probably need to use something different for better compile times (enums).
	std::vector<uint16_t> translations;

#ifdef TL_PERFECT_HASH_TRANSLATION
	const char* get_hashed_text(const char* text, translate_hash_type hash);
#else
	const char* get_text(const char* text);
#endif

#endif // TL_COMPILE_TIME_TRANSLATION

	// load the translation file (could be replaced with a overloaded cvar...)
	NDSERR bool init();
};

extern translation_context g_translation_context;