#pragma once

#include "core/global.h"
#include <optional>

#ifdef TL_COMPILE_TIME_TRANSLATION
#error "the parser is only for runtime translation."
#endif

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
	std::string source_file;
	std::string function;
	int line;
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

	virtual TL_RESULT on_header(tl_header& header) = 0;
	virtual TL_RESULT on_translation(std::string& key, std::optional<std::string>& value) = 0;

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