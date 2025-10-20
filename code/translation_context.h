#pragma once

#include "core/global.h"

#define TL_IMPLEMENT_GET_INDEX 1
#include "translate.h"

#include <vector>


#define TL_START(lang, ...) lang,
#include "../translations/tl_begin_macro.txt"
enum class TL_LANG{
#include "../translations/english_ref.inl"
#include "../translations/tl_all_languages.txt"
};
#include "../translations/tl_end_macro.txt"

#ifndef TL_COMPILE_TIME_TRANSLATION
#include "translation_parser.h"
#include <map>
#endif // TL_COMPILE_TIME_TRANSLATION


struct translation_context
#ifndef TL_COMPILE_TIME_TRANSLATION
: public tl_parse_observer
#endif
{
#ifdef TL_COMPILE_TIME_TRANSLATION
	TL_LANG current_lang = TL_LANG::English;
#else // TL_COMPILE_TIME_TRANSLATION

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

	void load_index(tl_index index, std::string_view value);

	void on_error(const char* msg) override;
	void on_warning(const char* msg) override;

	TL_RESULT on_header(tl_header_tuple& header) override;
	TL_RESULT on_translation(std::string& key, std::string& value) override;

	std::vector<language_entry> language_list;

	// a string that contains null terminating strings.
	// maybe I could insitu load the file,
	// or just use an arena.
	std::string memory;

	// I lookup a string based off english_ref.inl
	// so the ID is ordered based on the location the string is in the english_ref.inl
	// this contains the offset inside of memory.

	std::vector<tl_index> translations;

	// to compare with the compile time number of translations,
	// so I don't need to loop through the vector for unloaded translations.
	size_t num_loaded_translations = 0;

#ifdef TL_COMPILE_TIME_ASSERTS
	const char* get_text(const char* text, tl_index index);
#else
	const char* get_text(const char* text);
#endif

#endif // TL_COMPILE_TIME_TRANSLATION

	// load the translation file (could be replaced with a overloaded cvar...)
	NDSERR bool init();
};

translation_context &get_translation_context();