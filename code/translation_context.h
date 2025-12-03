#pragma once

#include "core/global.h"

#include "translate.h"

#include <vector>

#define TL_START(lang, ...) lang,
#include "tl_begin_macro.txt"
enum class TL_LANG
{
#include "../translations/english_ref.inl"
#include "tl_all_languages.txt"
};
#include "tl_end_macro.txt"

#ifndef TL_COMPILE_TIME_ASSERTS
// because "translate.h" doesn't include it
#include "translate_get_index.h"
#endif

#ifndef TL_COMPILE_TIME_TRANSLATION
#include "translation_parser.h"
struct translation_context : public tl_parse_observer
#else
struct translation_context
#endif // TL_COMPILE_TIME_TRANSLATION
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

	void on_error(const char* msg) override;
	void on_warning(const char* msg) override;

	TL_RESULT on_header(tl_parse_state& tl_state, tl_header& header) override;
	TL_RESULT on_translation(tl_parse_state& tl_state, std::string& key, std::optional<annotated_string>& value) override;

	std::vector<language_entry> language_list;

	// a buffer for loading files.
	std::string slurp_string;

	struct translation_table
	{
		// a string that contains null terminating strings.
		// maybe I could insitu load the file,
		// or just use an arena.
		// I think I could also make sure that the strings are never modified by locking the memory
		// using OS functions.
		std::string translation_memory;

		// I lookup a string based off english_ref.inl
		// so the ID is ordered based on the location the string is in the english_ref.inl
		// this contains the offset inside of memory.

		std::vector<tl_index> translations;

		// to compare with the compile time number of translations,
		// so I don't need to loop through the vector for unloaded translations.
		size_t num_loaded_translations = 0;

		void reset();

		void set_index(tl_index index, std::string_view value);

#ifdef TL_COMPILE_TIME_ASSERTS
		const char* get_text(const char* text, tl_index index);
#else
		const char* get_text(const char* text);
#endif

		bool validate_translation(const char* lang_file);

		// I could use CRTP or function pointers, but I like this.
		virtual const char* get_index_key(tl_index find_index) = 0;
		virtual tl_index get_num_translations() = 0;
#ifndef TL_COMPILE_TIME_ASSERTS
		// with asserts, the index is already found inside of the macro.
		virtual tl_index get_index(const char* text) = 0;
#endif
		virtual ~translation_table() = default;
	};
	struct text_translations : public translation_table
	{
		const char* get_index_key(tl_index find_index) override;
		tl_index get_num_translations() override;
#ifndef TL_COMPILE_TIME_ASSERTS
		tl_index get_index(const char* text) override;
#endif
	};
	text_translations text_table;

#ifdef TL_ENABLE_FORMAT
	struct format_translations : public translation_table
	{
		const char* get_index_key(tl_index find_index) override;
		tl_index get_num_translations() override;
#ifndef TL_COMPILE_TIME_ASSERTS
		tl_index get_index(const char* text) override;
#endif
	};
	format_translations format_table;

	TL_RESULT on_format(
		tl_parse_state& tl_state,
		std::string& key,
		std::optional<annotated_string>& value) override;
#endif

	bool load_languages(const char* folder);
	// print warnings about languages.
	void check_languages();

	bool load_language(language_entry& lang);

#endif // TL_COMPILE_TIME_TRANSLATION

	// load the translation file (could be replaced with a overloaded cvar...)
	NDSERR bool init();
};

translation_context& get_translation_context();