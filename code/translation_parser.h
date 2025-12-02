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
	: iter(iter_), data(std::move(data_))
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
	tl_buffer_type::iterator eiter, const char* message, const char* filename);

class tl_parse_state
{
public:
	virtual void report_error(const char* msg) = 0;
	virtual void report_error(const char* msg, tl_buffer_type::iterator eiter) = 0;
	virtual void report_warning(const char* msg) = 0;
	virtual void report_warning(const char* msg, tl_buffer_type::iterator eiter) = 0;
	// gets the current iterator location, for report_error(msg, eiter)
	virtual tl_buffer_type::iterator get_iterator() = 0;
	virtual ~tl_parse_state() = default;


#ifdef TL_ENABLE_FORMAT
	// the iterator must match the beginning of the value within the file buffer.
	// I don't have an iterator for key because it's unlikely any errors happen for key.
	// (because the format string is set during compile time)
	// You still get an errors checked for key, but with the wrong position
	bool check_printf_specifiers(std::string_view key, std::string_view value, tl_buffer_type::iterator vbegin);


private:
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
	// returns true with specifier = 0 if no specifier is found.
	// will not set specifier, so you must clear it before using it.
	bool find_specifier(printf_specifier_parser_state& state,
										std::string_view::iterator& cur,
										std::string_view::iterator end);
#endif
};

class tl_parse_observer
{
public:
	// set while parsing.
	// TODO: I should make this a callback parameter,
	//  since this will not work if you try to use it outside the callback
	tl_parse_state* tl_parser_ctx = nullptr;

	// send to stdout or whatever
	virtual void on_warning(const char* msg) = 0;
	virtual void on_error(const char* msg) = 0;

	virtual TL_RESULT on_header(tl_header& header) = 0;
	virtual TL_RESULT on_translation(std::string& key, std::optional<annotated_string>& value) = 0;
#ifdef TL_ENABLE_FORMAT
	// value_offset is the offset from tl_parse_state::get_iterator(), for printing diagnostics.
	// I SHOULD have it for the key as well, but I don't.
	virtual TL_RESULT on_format(std::string& key, std::optional<annotated_string>& value) = 0;
#endif

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

bool parse_translation_file(
	tl_parse_observer& o, std::string_view file_contents, const char* path_name);

#endif // TL_COMPILE_TIME_TRANSLATION