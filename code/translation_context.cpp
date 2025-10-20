#include "translation_context.h"

#include "core/cvar.h"

translation_context &get_translation_context()
{
	static translation_context g_translation_context;
	return g_translation_context;
}

// forward declare.
static const char* get_lang_short_name(TL_LANG lang);

// one problem is that cvar errors depend on the language cvar,
// so cvar errors will always be printed in english
// I could load the translations using a cvar wrapper,
// so that proceeding errors will be translated,
// but I don't want to actually translate the cvar errors
// because cvars are in english and the errors should be english as well.
// and I don't like having a weird behavior of only translating after cv_language.
static REGISTER_CVAR_STRING(
	cv_language,
	get_lang_short_name(TL_LANG::English),
	"set to JP for japanese, you can use the short or long name",
	CVAR_T::STARTUP);

static const char* get_lang_long_name(TL_LANG lang)
{
#define TL_START(lang, ...) case TL_LANG::lang: return #lang;
#include "../translations/tl_begin_macro.txt"
	switch(lang)
	{
#include "../translations/english_ref.inl"
#include "../translations/tl_all_languages.txt"
	}
#include "../translations/tl_end_macro.txt"
	return "<UNKNOWN LANG>";
}
static const char* get_lang_short_name(TL_LANG lang)
{
#define TL_START(lang,short_name, ...) case TL_LANG::lang: return #short_name;
#include "../translations/tl_begin_macro.txt"
	switch(lang)
	{
#include "../translations/english_ref.inl"
#include "../translations/tl_all_languages.txt"
	}
#include "../translations/tl_end_macro.txt"
	return "<UNKNOWN LANG>";
}

static int get_language_count()
{
	int count = 0;
#define TL_START(lang, ...) count++;
#include "../translations/tl_begin_macro.txt"
#include "../translations/english_ref.inl"
#include "../translations/tl_all_languages.txt"
#include "../translations/tl_end_macro.txt"
	return count;
}


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

// TODO: this should be a custom cvar callback override
bool translation_context::init()
{
	slogf("Languages Available:\n");
	for(int i = 0; i != get_language_count(); ++i)
	{
		TL_LANG lang = static_cast<TL_LANG>(i);
		slogf("- %s (%s)\n", get_lang_long_name(lang), get_lang_short_name(lang));
	}

	slogf("Using Language: %s\n", cv_language->c_str());

	for(int i = 0; i != get_language_count(); ++i)
	{
		TL_LANG lang = static_cast<TL_LANG>(i);
		if(cv_language.data() == get_lang_long_name(lang) || cv_language.data() == get_lang_short_name(lang))
		{
			current_lang = lang;
			return true;
		}
	}

	serrf(
		"failed to find language (%s = `%s`)\n", cv_language.cvar_key, cv_language.data().c_str());
	return false;
}

#else // TL_COMPILE_TIME_TRANSLATION


#include "translate_get_index.h"

#include "core/RWops.h"

// I would have used SDL because I don't want to use a library I don't need to use...
#include <filesystem>


// translations in a file
static constexpr tl_index get_number_of_translations()
{
	tl_index index = 0;
#define TL(key, value) index++;
#include "../translations/tl_begin_macro.txt"
#include "../translations/english_ref.inl"
#include "../translations/tl_end_macro.txt"
	return index;
}
// used as a fallback to fix missing translations during runtime.
static constexpr const char* get_index_key(tl_index find_index)
{
	// index 0 is uninitialized.
	tl_index index = 1;
#define TL(key, _) if(find_index == index) {return key;} index++;
#include "../translations/tl_begin_macro.txt"
#include "../translations/english_ref.inl"
#include "../translations/tl_end_macro.txt"
	return "<get_index_key:notfound>";
}

// This only gets the english memory size, which I use as an OK estimate.
static constexpr tl_index get_translation_memory_size()
{
	// index 0 is uninitialized.
	tl_index size = 1;
#define TL(key, value) size += std::string_view(key).size() + 1;
#include "../translations/tl_begin_macro.txt"
#include "../translations/english_ref.inl"
#include "../translations/tl_end_macro.txt"
	return size;
}

#ifdef TL_COMPILE_TIME_ASSERTS
const char* translation_context::get_text(const char* text, tl_index index)
#else
const char* translation_context::get_text(const char* text)
#endif
{
	ASSERT(text != NULL);

	// if this is english or an error occurred.
	if(current_lang == -1)
	{
		return text;
	}
#ifndef TL_COMPILE_TIME_ASSERTS
	auto index = get_text_index(text);
#endif
	ASSERT_M(index != 0 && "translation not found", text);
	ASSERT(!memory.empty());
	ASSERT(!translations.empty());
	ASSERT(index <= translations.size());
	ASSERT(translations[index] <= memory.size());
	return &memory[translations[index]];
}
bool translation_context::init()
{
	// reset back to english (init() can be called again to load a new language).
	current_lang = -1;
	// fastpath, if english, skip everything and only use english.
#ifdef NDEBUG
	if(cv_language.data() == get_lang_short_name(TL_LANG::English) || cv_language.data() == get_lang_long_name(TL_LANG::English))
	{
		return true;
	}
#endif
	parse_headers = true;
	std::filesystem::path translations_path("translations");
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
	std::string slurp_string;
	for(const auto& dir_entry : std::filesystem::directory_iterator{translations_path})
	{
		if(std::filesystem::is_regular_file(dir_entry) && dir_entry.path().extension() == ".inl")
		{
			// loading_path is used for the parsing callbacks.
			loading_path = dir_entry.path().string();
			slogf("info: found translation: %s\n", loading_path.c_str());

			slurp_string.clear();
			// copy the file into the string
			if(!slurp_stdio(slurp_string, loading_path.c_str()))
			{
				return false;
			}
#define CHECK_TIMER
#ifdef CHECK_TIMER
			// small file with like 1000 characters
			// 0.2 - 0.05 ms on reldeb, 10ms on debug san.
			// but nothing is stopping me from making this multi-threaded.
			TIMER_U t1 = timer_now();
#endif
			// TODO: make a parse_translation_header for performance...
			//  But I feel like it wont actually make a difference unless I throw an exception...
			if(!parse_translation_file(*this, slurp_string, loading_path))
			{
				return false;
			}
			ASSERT(!language_list.empty());
#ifdef CHECK_TIMER
			TIMER_U t2 = timer_now();
			slogf("time: %" TIMER_FMT "\n", timer_delta_ms(t1, t2));
#endif

// clang supports #embed with C++23 with extensions, but it won't set __cpp_pp_embed,
// but __has_embed works.
// I could try to replace all_languages.inl to use #embed but I bet it wont respect macros.
#if defined(__has_embed) && !defined(NDEBUG) // __cpp_pp_embed
//#if __has_embed("../translations/english_ref.inl") != __STDC_EMBED_NOT_FOUND__
			// check to see if the english ref matches the hash made during compilation.
			// this seems to defeat the purpose of runtime translations,
			// but I find it to annoying if I accidentally load the wrong translations.

			if(language_list.back().short_name == get_lang_short_name(TL_LANG::English))
			{
				constexpr char english_data[] = {
#embed "../translations/english_ref.inl" suffix(, 0)
				};
				auto original_hash = hash_fnv1a_const2(english_data);
				auto slurp_hash = hash_fnv1a_const2(slurp_string.c_str());
				if(original_hash != slurp_hash)
				{
					slogf(
						"info: compile time hash mismatch (size: %zu, found: %zu): %s\n",
						std::size(english_data) - 1, slurp_string.size(),
						// I should print the absolute path,
						// but I feel like absolute paths in logs is a privacy issue.
						loading_path.c_str());
				}
			}
#endif // __has_embed
		}
	}
	if(language_list.empty())
	{
		slogf("info: found no translations: %s\n", translations_path.string().c_str());
		return true;
	}

	// check if any languages are missing from the compile time enum.
	for(int i = 0; i < get_language_count(); ++i)
	{
		TL_LANG lang_enum = static_cast<TL_LANG>(i);
		bool found = false;
		for(auto& lang : language_list)
		{
			if(lang.short_name == get_lang_short_name(lang_enum) &&
			   lang.long_name == get_lang_long_name(lang_enum))
			{
				found = true;
				break;
			}
		}
		if(!found)
		{
			slogf(
				"warning: language missing from compile time: %s (%s)\n",
				get_lang_long_name(lang_enum),
				get_lang_short_name(lang_enum));
		}
	}

	// TODO: move this into a function
	// check for duplicates.
	for(auto it = language_list.begin(); it != language_list.end(); ++it)
	{
		for(auto jt = it + 1; jt < language_list.begin(); ++jt)
		{
			if(it->long_name == jt->long_name || it->short_name == jt->short_name)
			{
				slog("warning: duplicate languages\n");
				slogf(
					"- %s (%s, %s) \n",
					it->translation_file.c_str(),
					it->long_name.c_str(),
					it->short_name.c_str());
				slogf(
					"- %s (%s, %s) \n",
					jt->translation_file.c_str(),
					jt->long_name.c_str(),
					jt->short_name.c_str());
			}
		}
	}

	parse_headers = false;

	// TODO: move this into a function
	translations.clear();
	// index 0 must be an error.
	translations.resize(get_number_of_translations() + 1);
	num_loaded_translations = 0;
	memory.clear();
	// this is an estimate.
	memory.reserve(get_translation_memory_size() * 2);
	// what to show on the error index.
	std::string error_string = "<error string>\n";
	memory.insert(memory.end(), error_string.begin(), error_string.end());
	memory.push_back('\0');

	slogf("Languages Available:\n");
	for(auto& lang : language_list)
	{
		slogf("- %s (%s)\n", lang.long_name.c_str(), lang.short_name.c_str());
	}

	int lang_index = 0;
	for(auto& lang: language_list)
	{
		if(lang.long_name != cv_language->c_str() && lang.short_name != cv_language->c_str())
		{
			lang_index++;
			continue;
		}
		slogf("Using Language: %s\n", lang.long_name.c_str());

		// english stays -1 because it's faster.
		if(lang.short_name != get_lang_short_name(TL_LANG::English))
		{
			current_lang = lang_index;
		}

		slurp_string.clear();
		// copy the file into the string
		if(!slurp_stdio(slurp_string, lang.translation_file.c_str()))
		{
			return false;
		}
#ifdef CHECK_TIMER
		// small file with like 1000 characters
		// 0.2 - 0.05 ms on reldeb, 10ms on debug san.
		TIMER_U t1 = timer_now();
#endif
		if(!parse_translation_file(*this, slurp_string, lang.translation_file))
		{
			// missing strings will turn into @, I would rather have english.
			translations.clear();
			num_loaded_translations = 0;
			memory.clear();
			cv_language.cvar_revert_to_default();
			return false;
		}
#ifdef CHECK_TIMER
		TIMER_U t2 = timer_now();
		slogf("time: %" TIMER_FMT "\n", timer_delta_ms(t1, t2));
#endif
		if(!CHECK(num_loaded_translations <= get_number_of_translations()))
		{
			return false;
		}
		// check and fix missing translations.
		if(num_loaded_translations != get_number_of_translations())
		{
			size_t missing_count = get_number_of_translations() - num_loaded_translations;
			slogf("missing translations (%zu): %s\n", missing_count, lang.translation_file.c_str());
			size_t found_count = 0;
			for(auto it = translations.begin(); it != translations.end(); ++it)
			{
				// this is the error string placeholder
				if(it == translations.begin())
				{
					continue;
				}
				// the error offset
				if(*it == 0)
				{
					auto index = std::distance(translations.begin(), it);
					++found_count;
					std::string key = get_index_key(index);
					// TODO: every time I print a key or value, I should convert escape codes
					if(key.back() == '\n') key.pop_back();
					slogf("- `%s`\n", key.c_str());
					load_index(index, key);
				}
			}
			if(!CHECK(found_count == missing_count))
			{
				return false;
			}
		}

		return true;
	}
	serrf("Failed to find language (%s = %s)\n", cv_language.cvar_key, cv_language->c_str());

	return false;
}


void translation_context::on_error(const char* msg)
{
	slog(msg);
}
void translation_context::on_warning(const char* msg)
{
	serr(msg);
}

TL_RESULT translation_context::on_header(tl_header_tuple& header)
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
		entry.long_name = std::get<(int)tl_header_get::long_name>(header);
		entry.short_name = std::get<(int)tl_header_get::short_name>(header);
		entry.native_name = std::get<(int)tl_header_get::native_name>(header);
		// I am tempted to move this in, but this string is being used by the parser...
		entry.translation_file = loading_path;
		language_list.push_back(std::move(entry));
		return TL_RESULT::SUCCESS;
	}
#ifndef NDEBUG
	// make sure we are using the right language.
	std::string& found_short = std::get<(int)tl_header_get::short_name>(header);
	if(current_lang == -1)
	{
		if(found_short != get_lang_short_name(TL_LANG::English))
		{
			std::string msg;
			str_asprintf(msg, "expected english (got: %s)\n", found_short.c_str());
			tl_parser_ctx->report_error(msg.c_str());
			return TL_RESULT::FAILURE;
		}
		return TL_RESULT::SUCCESS;
	}
	ASSERT(current_lang < language_list.size());
	std::string& expected = language_list[current_lang].short_name;
	if(expected != found_short)
	{
		// I should print the file location as well, too lazy.
		std::string msg;
		str_asprintf(msg, "unexpected language (expected: %s, got: %s)\n", expected.c_str(), found_short.c_str());
		tl_parser_ctx->report_error(msg.c_str());
		return TL_RESULT::FAILURE;
	}
#endif

	return TL_RESULT::SUCCESS;
}

void translation_context::load_index(tl_index index, std::string_view value)
{
	tl_index offset = memory.size();

	memory.insert(memory.end(), value.begin(), value.end());

	memory.push_back('\0');

	// I don't set the c_str() address because of reallocation
	// so at the end, I set the address offset.
	translations[index] = offset;

	num_loaded_translations++;

	ASSERT(num_loaded_translations <= translations.size());
}
TL_RESULT translation_context::on_translation(std::string& key, std::string& value)
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
		return TL_RESULT::SUCCESS;
	}

	// this is using compile time strings.
	auto index = get_text_index(key);
	if(index == 0)
	{
		// TODO: make tl-string extractor add UNRESOLVED, and ignore it?
		tl_parser_ctx->report_warning("text does not exist");
		return TL_RESULT::WARNING;
	}
	ASSERT(index < translations.size());
	if(translations[index] != 0)
	{
		load_index(index, key);
		tl_parser_ctx->report_warning("duplicate entry");
		return TL_RESULT::WARNING;
	}

	// this is a hack because my parser will accept NULL as a string.
	// I need to either use const char* or std::optional<std::string>
	// but I don't understand boost parser well enough to parse that.
	if(value == "NULL")
	{
		load_index(index, key);
		// ignore english, there is no translation.
		if(current_lang != -1)
		{
			tl_parser_ctx->report_warning("untranslated");
			return TL_RESULT::WARNING;
		}
		return TL_RESULT::SUCCESS;
	}

	load_index(index, value);

	return TL_RESULT::SUCCESS;
}

#endif // TL_COMPILE_TIME_TRANSLATION