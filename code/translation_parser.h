#pragma once

#include "core/global.h"

#ifdef TL_COMPILE_TIME_TRANSLATION
#error "the parser is only for runtime translation."
#endif

// these could be normal structs if I used boost hana tuples AKA use vcpkg boost parser.
// I might consider just using FetchContent to avoid boost dependencies.
enum tl_header_get
{
	long_name,
	short_name,
	native_name,
	date,
	git_hash
};

typedef std::tuple<std::string, std::string, std::string, std::string, std::string> tl_header_tuple;

enum tl_info_get
{
	source_file,
	function,
	line,
};
typedef std::tuple<std::string, std::string, int> tl_info_tuple;

class parse_observer
{
public:
	virtual bool on_header(tl_header_tuple& header) = 0;
	virtual bool on_info(tl_info_tuple& header) = 0;
	virtual bool on_translation(std::string&& key, std::string&& value) = 0;
	virtual ~parse_observer() = default;
};

//testing.
bool parse_translation_file(parse_observer& o, std::string_view file_contents, std::string_view path_name);