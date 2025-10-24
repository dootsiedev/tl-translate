#pragma once

#include "core/global.h"
#include <optional>

#ifdef TL_COMPILE_TIME_TRANSLATION
#error "the parser is only for runtime translation."
#endif

// I want to add a flag, it could be a unicode character, for no reason whatsoever.
// but I feel like this is the tipping point of how long this header could be...
// just too many parameters without labels...
struct tl_header
{
	std::string long_name;
	std::string short_name;
	std::string native_name;
	std::string date;
	std::string git_hash;
};

struct tl_info
{
	std::string function;
	std::string source_file;
	int line;
	int column;
};

struct tl_no_match
{
	std::string date;
	std::string git_hash;
};

enum class TL_RESULT
{
	SUCCESS,
	WARNING,
	FAILURE
};

class tl_parse_state
{
public:
	virtual void report_error(const char* msg) = 0;
	virtual void report_warning(const char* msg) = 0;
	virtual ~tl_parse_state() = default;
};

class tl_parse_observer
{
public:
	// set while parsing.
	tl_parse_state* tl_parser_ctx = nullptr;

	// returns the formatted message.
	virtual void on_warning(const char* msg) = 0;
	virtual void on_error(const char* msg) = 0;

	// since my code does a lot of conversion through between utf8 and u32string,
	// I could reduce the pressure a tiny bit by supplying the strings by char*
	// and store it all on some arena allocator.
	// But ideally I should not need to allocate anything,
	// I should just reference the file memory in-situ...
	// I think boost parser does this with string_views (C++20)
	// but it only works with continuous memory,
	// I use boost parser's range span to convert to utf32 (non-continuous)
	// And I "need" utf32 for better error handling (column position),
	// and there is a chance that converting the whole file to u32string for u32string_view(?)
	// is slower than the range... (or maybe not! I don't really want to benchmark...)
	// also a compromise middle ground could be to use utf16 since all languages fit in ucs2.
	// but not emojis (but boost parser does not mention char16_t anywhere...).
	//
	// virtual char* on_alloc_other(uint16_t size) = 0;
	// only for the translation value, not the key, for a compact translation buffer.
	// virtual char* on_alloc_translation_value(uint16_t size) = 0;

	virtual TL_RESULT on_header(tl_header& header) = 0;
	virtual TL_RESULT on_translation(std::string& key, std::optional<std::string>& value) = 0;
	virtual TL_RESULT on_comment(std::string& comment)
	{
		(void)comment;
		return TL_RESULT::SUCCESS;
	}

	// not used by the translation context
	virtual TL_RESULT on_info(tl_info& info)
	{
		(void)info;
		return TL_RESULT::SUCCESS;
	}
	virtual TL_RESULT on_no_match(tl_no_match& no_match)
	{
		(void)no_match;
		return TL_RESULT::SUCCESS;
	}

	virtual ~tl_parse_observer() = default;
};

// testing.
bool parse_translation_file(
	tl_parse_observer& o, std::string_view file_contents, std::string_view path_name);