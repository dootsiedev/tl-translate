#pragma once

#include "core/global.h"

#ifdef TL_COMPILE_TIME_TRANSLATION
#error "the parser is only for runtime translation."
#endif

// I think the examples of boost parser using structs requires boost hana.
// this is not very pretty.
// I have not tried to make each variable into a class of std::string / int / etc for get<date>
// and it would be better than casting the enum to an int, but it would be more verbose...
enum class tl_header_get
{
	long_name,
	short_name,
	native_name,
	date,
	git_hash
};

typedef std::tuple<std::string, std::string, std::string, std::string, std::string> tl_header_tuple;

enum class tl_info_get
{
	source_file,
	function,
	line,
};
typedef std::tuple<std::string, std::string, int> tl_info_tuple;

enum class tl_unresolved_get
{
	date,
	git_hash
};
typedef std::tuple<std::string, std::string> tl_unresolved_tuple;

#if 0
enum class tl_maybe_get
{
	original_key,
	source_file,
	function,
	line,
	date,
	git_hash
};
typedef std::tuple<std::string, std::string, std::string, int, std::string, std::string> tl_maybe_tuple;
#endif

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

	virtual TL_RESULT on_header(tl_header_tuple& header) = 0;
	virtual TL_RESULT on_translation(std::string& key, std::string& value) = 0;
	virtual TL_RESULT on_info(tl_info_tuple& info)
	{
		(void)info;
		return TL_RESULT::SUCCESS;
	}
	virtual TL_RESULT on_unresolved(tl_unresolved_tuple& unresolved)
	{
		(void)unresolved;
		return TL_RESULT::SUCCESS;
	}

	virtual ~tl_parse_observer() = default;
};

// testing.
bool parse_translation_file(
	tl_parse_observer& o, std::string_view file_contents, std::string_view path_name);