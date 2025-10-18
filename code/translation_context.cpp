#include "translation_context.h"

#include "core/cvar.h"

translation_context g_translation_context;

// one problem is that cvar errors depend on the language cvar,
// so cvar errors will always be printed in english
// (I could set the language using a cvar wrapper,
// so at least proceeding errors will be translated,
// but doing initialization in cvars should be avoided,
// it's harmless for compile time translation, but not runtime translation)
// But it should not matter because the cvars names should not be translated.
// (the comment should be translated, but it's not printed during an error)
// and I don't believe translating fatal errors is useful for non-english users,
// only GUI and other user errors should be translated).
static REGISTER_CVAR_STRING(cv_language, "EN", "set to JP for japanese", CVAR_T::RUNTIME);

#ifdef TL_COMPILE_TIME_TRANSLATION
static int has_compile_time_translations = 1;
#else
static int has_compile_time_translations = 0;
#endif
static REGISTER_CVAR_INT(
	cv_has_compile_time_translations,
	has_compile_time_translations,
	"0 = uses translations loaded during runtime, 1 = translations are hardcoded",
	CVAR_T::READONLY);

#ifdef TL_COMPILE_TIME_TRANSLATION

const char* get_lang_string(TL_LANG lang)
{
#define TL_START(lang, ...) \
	case TL_LANG::lang: return #lang;
#include "../translations/tl_begin_macro.txt"
	switch(lang)
	{
#include "../translations/english_ref.inl"
#include "../translations/tl_all_languages.txt"
	}
#include "../translations/tl_end_macro.txt"
	return "<UNKNOWN LANG>";
}

std::vector<TL_LANG> get_supported_languages()
{
	std::vector<TL_LANG> languages;
#define TL_START(lang, ...) languages.push_back(TL_LANG::lang);
#include "../translations/tl_begin_macro.txt"
#include "../translations/english_ref.inl"
#include "../translations/tl_all_languages.txt"
#include "../translations/tl_end_macro.txt"
	return languages;
}

// TODO: this should be a custom cvar callback override
bool translation_context::init()
{
	slogf("Languages Available:\n");
	for(auto& entry : get_supported_languages())
	{
		slogf("-%s\n", get_lang_string(entry));
	}

	slogf("Using Language: %s\n", cv_language->c_str());

// I would use a case insensitive compare if I had one.
#define TL_START(lang, lang_short, ...)                                  \
	if(cv_language.data() == #lang || cv_language.data() == #lang_short) \
	{                                                                    \
		current_lang = TL_LANG::lang;                                    \
		return true;                                                     \
	}
#include "../translations/tl_begin_macro.txt"
#include "../translations/english_ref.inl"
#include "../translations/tl_all_languages.txt"
#include "../translations/tl_end_macro.txt"
#undef TL_START
	serrf(
		"failed to find language (%s = `%s`)\n", cv_language.cvar_key, cv_language.data().c_str());
	// TODO: print a list of languages
	return false;
}

#else // TL_COMPILE_TIME_TRANSLATION

#include "core/RWops.h"

// I would have used SDL because I don't want to use a library I don't need to use...
#include <filesystem>



static constexpr uint16_t get_number_of_translations()
{
	uint16_t index = 0;
#define TL_START(lang, ...) static_assert(std::string_view(#lang) == "English");
#define TL(key, value) index++;
#include "../translations/tl_begin_macro.txt"
#include "../translations/english_ref.inl"
#include "../translations/tl_end_macro.txt"
	return index;
}

// This only gets the english memory size, which I use as an OK estimate.
static constexpr int get_translation_memory_size()
{
	// index 0 is uninitialized.
	uint16_t size = 1;
#define TL_START(lang, ...) static_assert(std::string_view(#lang) == "English");
#define TL(key, value) size += std::string_view(key).size() + 1;
#include "../translations/tl_begin_macro.txt"
#include "../translations/english_ref.inl"
#include "../translations/tl_end_macro.txt"
	return size;
}

const char* translation_context::get_text(const char* text)
{
	// index 0 is uninitialized.
	ASSERT_M(const_get_text(text) != 0 && "translation not found", text);
	return memory.data() + translations[const_get_text(text)];
}
bool translation_context::init()
{
	parse_headers = true;
	std::filesystem::path translations_path(u8"translations");
	if(!std::filesystem::exists(translations_path))
	{
		serrf("no translation folder: %s\n", translations_path.string().c_str());
		return false;
	}
	if(!std::filesystem::is_directory(translations_path))
	{
		serrf("not a translation folder: %s\n", translations_path.string().c_str());
		return false;
	}
	for(const auto& dir_entry : std::filesystem::directory_iterator{translations_path})
	{
		if(std::filesystem::is_regular_file(dir_entry) && dir_entry.path().extension() == ".inl")
		{
			// this string gets moved after loading the header.
			// but I think it would still just work because I am using a string_view.
			// unless
			loading_path = dir_entry.path().string();
			slogf("info: found translation: %s\n", loading_path.c_str());

			std::string str;
			// copy the file into the string
			if(!slurp_stdio(str, loading_path.c_str()))
			{
				return false;
			}
#define CHECK_TIMER
#ifdef CHECK_TIMER
			// small file with like 1000 characters
			// 0.2 - 0.05 ms on reldeb, 10ms on debug san.
			TIMER_U t1 = timer_now();
#endif
			// TODO: make a parse_translation_header for performance...
			//  but I do like that it makes sure no errors exist.
			//  maybe keep it for !NDEBUG?
			if(!parse_translation_file(*this, str, loading_path))
			{
				return false;
			}
#ifdef CHECK_TIMER
			TIMER_U t2 = timer_now();
			slogf("time: %" TIMER_FMT "\n", timer_delta_ms(t1, t2));
#endif
		}
	}
	parse_headers = false;

	translations.clear();
	// index 0 must be an error.
	translations.resize(get_number_of_translations() + 1);
	translations.push_back(0);
	memory.clear();
	// this is not a tight fitting size, this is the english size used as an estimate.
	memory.reserve(get_translation_memory_size());
	// the character used for the error index.
	memory.push_back('@');
	memory.push_back('\0');

	slogf("Languages Available:\n");
	for(auto& lang : language_list)
	{
		slogf("-%s\n", lang.long_name.c_str());
	}

	for(auto& lang: language_list)
	{
		if(lang.long_name != cv_language->c_str() && lang.short_name != cv_language->c_str())
		{
			continue;
		}
		slogf("Using Language: %s\n", lang.long_name.c_str());

		std::string str;
		// copy the file into the string
		if(!slurp_stdio(str, lang.translation_file.c_str()))
		{
			return false;
		}
#ifdef CHECK_TIMER
		// small file with like 1000 characters
		// 0.2 - 0.05 ms on reldeb, 10ms on debug san.
		TIMER_U t1 = timer_now();
#endif
		if(!parse_translation_file(*this, str, lang.translation_file))
		{
			return false;
		}
#ifdef CHECK_TIMER
		TIMER_U t2 = timer_now();
		slogf("time: %" TIMER_FMT "\n", timer_delta_ms(t1, t2));
#endif
		return true;
	}
	serrf("Failed to find language (%s): %s\n", cv_language.cvar_key, cv_language->c_str());

	return false;
}

const char* translation_context::on_header(tl_header_tuple& header)
{
#ifdef TL_PRINT_FILE
	slogf(
		"lang: %s %s \"%s\"\n",
		std::get<tl_header_get::long_name>(header).c_str(),
		std::get<tl_header_get::short_name>(header).c_str(),
		std::get<tl_header_get::native_name>(header).c_str());
	slogf("date: %s\n", std::get<tl_header_get::date>(header).c_str());
	slogf("git hash: %s\n", std::get<tl_header_get::git_hash>(header).c_str());
#endif

	if(parse_headers)
	{
		// I should use emplace_back and designated initializers, but I use C++17.
		language_entry entry;
		entry.long_name = std::get<tl_header_get::long_name>(header);
		entry.short_name = std::get<tl_header_get::short_name>(header);
		entry.native_name = std::get<tl_header_get::native_name>(header);
		// I am tempted to move this in, but this string is being used by the parser...
		entry.translation_file = loading_path;
		language_list.push_back(std::move(entry));
	}

	return nullptr;
}
// should be optional, but I like printing.
const char* translation_context::on_info(tl_info_tuple& info)
{
#ifdef TL_PRINT_FILE
	slogf(
		"file: %s\n"
		"function: %s\n"
		"line: %d\n",
		std::get<tl_info_get::source_file>(info).c_str(),
		std::get<tl_info_get::function>(info).c_str(),
		std::get<tl_info_get::line>(info));
#endif
	return nullptr;
}
const char* translation_context::on_translation(std::string&& key, std::string&& value)
{
#ifdef TL_PRINT_FILE
	if(!key.empty() && key.back() == '\n')
	{
		key.pop_back();
	}
	if(!value.empty() && value.back() == '\n')
	{
		value.pop_back();
	}
	// yep, it's possible that I have a entry that has "NULL" instead of NULL
	// but I think NULL is universal, and want to avoid std::optional
	// for now I want to print it.
	// if(value == "NULL") value.clear();
	slogf(
		"translate: %s\n"
		"to: %s\n",
		key.c_str(),
		value.c_str());
#endif
	if(parse_headers)
	{
		// I only care about the header
		return nullptr;
	}
	uint16_t index = const_get_text(key);
	if(index == 0)
	{
		return "text not found";
	}
	ASSERT(index < translations.size());
	if(translations[index] != 0)
	{
		return "duplicate entry";
	}

	// this is a hack because my parser will accept NULL as a string.
	// I need to either use const char* or std::optional<std::string>
	if(value == "NULL")
	{
		value = std::move(key);
	}

	uint16_t offset = memory.size();
	memory.insert(memory.end(), value.begin(), value.end());
	memory.push_back('\0');

	// I don't set the c_str() address because of reallocation
	// so at the end, I set the address offset.
	translations[index] = offset;
	return nullptr;
}

#endif // TL_COMPILE_TIME_TRANSLATION