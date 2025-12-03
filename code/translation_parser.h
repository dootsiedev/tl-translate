#pragma once

#include <string>
#include <optional>

#ifdef TL_COMPILE_TIME_TRANSLATION
#error "the parser is only for runtime translation."
#else

typedef std::string_view tl_buffer_type;

struct annotated_string
{
	tl_buffer_type::iterator iter;
	std::string data;
	// c++17 cope
	annotated_string(tl_buffer_type::iterator iter_, std::string&& data_)
	: iter(iter_)
	, data(std::move(data_))
	{
	}
};

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

std::string tl_parse_print_formatted_error(
	tl_buffer_type::iterator first,
	tl_buffer_type::iterator last,
	tl_buffer_type::iterator eiter,
	const char* message,
	const char* filename);

namespace parse_printf_specifier
{
// private use.
struct printf_specifier_parser_state
{
	// I could probably fit all the info into 1 byte.
	// but will it be faster?
	int specifier = 0;
	// when you pass *. this will not truncate.
	bool variable_min_width_flag = false;
	// when you pass .*
	bool variable_field_width_flag = false;
	// the last location of the specifier...
	// There is probably a better way...
	tl_buffer_type::iterator where;
	bool operator==(printf_specifier_parser_state& rhs)
	{
		return specifier == rhs.specifier &&
			   variable_min_width_flag == rhs.variable_min_width_flag &&
			   variable_field_width_flag == rhs.variable_field_width_flag;
	}
	bool operator!=(printf_specifier_parser_state& rhs)
	{
		return !(*this == rhs);
	}
};
} // namespace parse_printf_specifier

// this lets me handle some errors without rebuilding the cursed spirit(boost parser) file.
// and it's required because I load the data in different ways depending on the tool.
// (into an AST for merging or into a translation table).
// unfortunately, because I combine all the values into a tuple,
// it does not allow me to get the proper error location
// (unless I annotate the index into the tuple, which is not too great)
class tl_parse_state
{
public:
	virtual void report_error(const char* msg) = 0;
	virtual void report_warning(const char* msg) = 0;
	// You must have an annotated variable for these to work.
	// it's a lot better if I could just make each parameter into a callback,
	// but I worry that I will just end up putting it into a struct + annotations in the end.
	// (ATM: I don't use this in any useful way, I just did it to see if it's possible
	// this might even increase the spirit(parser) overhead to an unacceptable level)
	virtual void report_error(const char* msg, tl_buffer_type::iterator eiter) = 0;
	virtual void report_warning(const char* msg, tl_buffer_type::iterator eiter) = 0;

	// gets the current location for the line
	// you can't get the location of individual combined values, you need annotations for that.
	// but annotating every variable is cursed. Don't do it.
	virtual tl_buffer_type::iterator get_iterator() = 0;
	virtual ~tl_parse_state() = default;

#ifdef TL_ENABLE_FORMAT
	// the iterator must match the beginning of the value within the file buffer.
	// I don't have an iterator for key because it's unlikely any errors happen for key.
	// (because the format string is set during compile time)
	// You still get an errors checked for key, but with the wrong position
	bool check_printf_specifiers(
		std::string_view key, std::string_view value, tl_buffer_type::iterator vbegin);

private:
	// returns true with specifier = 0 if no specifier is found.
	bool find_specifier(
		parse_printf_specifier::printf_specifier_parser_state& state,
		std::string_view::iterator& cur,
		std::string_view::iterator end,
		tl_buffer_type::iterator vbegin);

#endif
};

class tl_parse_observer
{
public:
	// send to stdout or whatever
	virtual void on_warning(const char* msg) = 0;
	virtual void on_error(const char* msg) = 0;

	virtual TL_RESULT on_header(tl_parse_state& tl_state, tl_header& header) = 0;
	virtual TL_RESULT on_translation(tl_parse_state& tl_state, std::string& key, std::optional<annotated_string>& value) = 0;
#ifdef TL_ENABLE_FORMAT
	// value_offset is the offset from tl_parse_state::get_iterator(), for printing diagnostics.
	// I SHOULD have it for the key as well, but I don't.
	virtual TL_RESULT on_format(tl_parse_state& tl_state, std::string& key, std::optional<annotated_string>& value) = 0;
#endif

	virtual TL_RESULT on_comment(tl_parse_state& tl_state, std::string& comment)
	{
		(void)tl_state;
		(void)comment;
		return TL_RESULT::SUCCESS;
	}

	virtual TL_RESULT on_info(tl_parse_state& tl_state, tl_info& info)
	{
		(void)tl_state;
		(void)info;
		return TL_RESULT::SUCCESS;
	}
	virtual TL_RESULT on_no_match(tl_parse_state& tl_state, tl_no_match& no_match)
	{
		(void)tl_state;
		(void)no_match;
		return TL_RESULT::SUCCESS;
	}

	virtual ~tl_parse_observer() = default;
};

bool parse_translation_file(
	tl_parse_observer& o, std::string_view file_contents, const char* path_name);

#endif // TL_COMPILE_TIME_TRANSLATION