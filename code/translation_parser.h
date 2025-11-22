#pragma once

#include <string>
#include <optional>

#ifdef TL_COMPILE_TIME_TRANSLATION
#error "the parser is only for runtime translation."
#else

// I feel like this is the tipping point of how long this header could be...
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
	// TODO: I should remove warning, because it does nothing compared to SUCCESS.
	WARNING,
	FAILURE
};

class tl_parse_state
{
public:
	virtual void report_error(const char* msg) = 0;
	virtual void report_warning(const char* msg) = 0;
	virtual ~tl_parse_state() = default;

#ifdef TL_ENABLE_FORMAT
	// this is a weird place to put this...
	// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
	bool check_printf_specifiers(const char* key, const char* value);
#endif
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
#ifdef TL_ENABLE_FORMAT
	virtual TL_RESULT on_format(std::string& key, std::optional<std::string>& value) = 0;
#endif

	// not used by the translation context
	// NOTE: I would put this into a macro if I could make the compile time faster...
	//  the problem is that I don't think it would reduce compile time...
	//  but parsing should be faster because I avoid utf8 conversion
	//  (but it would be EVEN faster if I used a string_view and disable utf8 conversion)
	virtual TL_RESULT on_comment(std::string& comment)
	{
		(void)comment;
		return TL_RESULT::SUCCESS;
	}

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
	tl_parse_observer& o, std::string_view file_contents, const char* path_name);

#endif // TL_COMPILE_TIME_TRANSLATION