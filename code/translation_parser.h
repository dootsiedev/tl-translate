#pragma once

#include "core/global.h"

#ifdef TL_COMPILE_TIME_TRANSLATION
#error "the parser is only for runtime translation."
#endif

// these could be normal structs if I used boost hana tuples AKA use vcpkg boost parser.
// I might consider just using FetchContent to avoid boost dependencies.
#if 0
// this is an alternative way of using tuples I didn't know about until after I used tuples...
// but the question is, does it work with boost parser without boost hana?
struct FirstName {
   std::string val;
};

struct LastName {
   std::string val;
};

using FullName = std::tuple<FirstName,LastName>;

int main() {
  auto user = FullName({"John"},{"Deer"});
  std::cout << std::get<FirstName>(user).val << std::endl;
  std::cout << std::get<LastName>(user).val << std::endl;
}
#endif
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


enum class TL_RESULT{
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

class parse_observer
{
public:
	// set while parsing.
	tl_parse_state *tl_parser_ctx = nullptr;

	// returns the formatted message.
	virtual void on_warning(const char* msg) = 0;
	virtual void on_error(const char* msg) = 0;

	virtual TL_RESULT on_header(tl_header_tuple& header) = 0;
	virtual TL_RESULT on_info(tl_info_tuple& header) = 0;
	virtual TL_RESULT on_translation(std::string& key, std::string& value) = 0;

	virtual ~parse_observer() = default;
};

//testing.
bool parse_translation_file(parse_observer& o, std::string_view file_contents, std::string_view path_name);