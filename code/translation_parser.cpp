// This is an independent project of an individual developer. Dear PVS-Studio, please check it.
// PVS-Studio Static Code Analyzer for C, C++, C#, and Java: https://pvs-studio.com

//
// TODO: supress ubsan unsigned overflow errors...
// TODO: convert the utf8 to wide, and print to the win32 console on windows... (or utf8 manifest)
// OPT: check out the C++20 stringview attribute, could be significantly faster.
//
// u32string is 2x slower than std::string,
// but I assume I MUST use it for getting the correct unicode position of the formatted error.
// but I think maybe I could write my own formatter that parses the utf8
// (but it probably needs to scan for newlines...)
//
// OPT: kind of late, but can a code-gen parsing library
// replace boost parser with the same level of simplicity?
//
// long wall of not very useful info
//
// so the results of this is that parsing wastes a lot of binary space,
// and it's painfully slow to build + LSP this one file (5 seconds).
//
// but boost parser was easy (as long as I was not reading any errors...),
// and the errors are 100x better than anything I could write,
// and I think I could add something new in a few minutes.
// And I don't need boost, unlike boost spirit x3/x4
//
// boost spirit x3/x4 has no-exception support (unlike parser),
// but vcpkg spirit uses 500mb for x64-windows (I THOUGHT IT WAS HEADER ONLY!).
// I tried boost spirit x4 from github but I got errors due to lacking boost libraries.
// (I did get pretty close to getting it working, I think it didn't understand tuples or something)
// however boost parser without boost hana needs ugly tuples... (I think)
//
// I might be leaning towards including the library with FetchContent and keeping the ugly tuples
// (unless vcpkg boost parser is header only and setup takes the same time as FetchContent)
//
// these numbers are old.
// msvc release build is 300kb and 10mb of debug info (50kb + 3mb without parser)
// msvc debug-san build is 3mb and 30mb of debug info (600kb + 2mb without parser)
//
// clang-cl crashes if a parsing error occurs (can't do exceptions) unless it's reldeb & no asan.
// clang-cfi crashes on old versions, it works tested with LLVM 21
//
// If C++ exceptions or optimized binary bloat became a hard blocker (wasm),
// - I could make a custom cmake command that converts the files into json or something.
//	 I think I could use the -E preprocessor command, but it won't be formatted nicely...
//	 or for speed, I could use a binary flatbuffer
// - I could modify parser and remove the exceptions and just print the error handler and exit().
//  2 problems:
//  I don't exactly know if I can call the error handler in the throw, yet (the parser rethrows)
//  I need a utf8 library because parser will throw utf8 errors (I think).
//
// I would have looked into spirit-po if it did not use C++ exceptions.
// spirit-po does have plural handling that I don't support.
//
//

#ifndef TL_COMPILE_TIME_TRANSLATION

#include "translation_parser.h"

// this is a hack to avoid making this file include global.h
// so I could include this file in a project without global.h

#include "core/global.h"

#include "util/string_tools.h"

// custom assert, boost parser also has BOOST_PARSER_NO_RUNTIME_ASSERTIONS
// which uses static assert, which is better, but why is it not always enabled?
// can parser parse in consteval? does that mean I can replace my macros?
#ifndef DISABLE_CUSTOM_ASSERT
#define BOOST_DISABLE_ASSERTS 0
#define BOOST_PARSER_HAVE_BOOST_ASSERT
#define BOOST_ASSERT ASSERT
#endif

// remove this when done.
// #define BOOST_PARSER_TRACE_TO_VS_OUTPUT

// I was really hoping boost parser is just built different
// but nope, it takes seconds for clangd to catch up.
#include <boost/parser/parser.hpp>

// for parser | bp::as_utf8, it makes the column location unicode aware
// (but the terminal needs a fixed width font, and windows default terminals wont print it utf8)
// #include <boost/parser/transcode_view.hpp>

#include "util/utf8_stuff.h"

#include <sstream>
#include <ranges>
// NOLINTBEGIN (bugprone-chained-comparison, google-readability-casting,
// bugprone-easily-swappable-parameters)
#ifdef __clang__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Woverloaded-shift-op-parentheses"
#endif

// This is leftover from when I was using u32string for bp::write_formatted_message
// I want to support string_view (C++20) but this won't apply to enums ATM
typedef std::string my_string_type;

std::string tl_parse_print_formatted_error(
	tl_buffer_type::iterator first,
	tl_buffer_type::iterator last,
	tl_buffer_type::iterator eiter, const char* message, const char* filename)
{
	ASSERT(first <= last);
	ASSERT(first <= eiter);
	ASSERT(eiter <= last);
	ASSERT(message != NULL);

	int line_number = 1;
	int column_number = 0;
	std::string underlying;

	auto cur = first;
	auto newline_start = first;
	for(; cur != eiter; ++cur)
	{
		if(*cur == '\n')
		{
			line_number++;
			newline_start = cur + 1;
		}
	}

	for(; cur != last; ++cur)
	{
		// the \r goes before the newline, which is why I ignore it above.
		if(*cur == '\n' || *cur == '\r')
		{
			break;
		}
	}
	underlying = std::string(newline_start, cur);

	std::string result;
	if(newline_start < eiter)
	{
		cur = newline_start;
		utf8::internal::utf_error err_code;
		do
		{
			uint32_t cp;
			err_code = utf8::internal::validate_next(cur, eiter, cp);
			switch(err_code)
			{
			case utf8::internal::UTF8_OK: column_number++; break;
			default:
				// I have not tested how this looks.
				result = "utf8 error: ";
				result += utf8cpp_get_error(err_code);
				result += '\n';
			}
		} while(err_code == utf8::internal::UTF8_OK && cur < eiter);
	}

	str_asprintf(
		result,
		"%s:%d:%d: %s here%s\n"
		"%s\n"
		"%*s^\n",
		filename,
		line_number,
		column_number,
		message,
		(cur == last) ? " (end of input)" : "",
		underlying.c_str(),
		column_number,
		"");

	return result;
}


namespace bp = boost::parser;


#ifdef TL_ENABLE_FORMAT
#if 0
bool tl_parse_state::check_printf_specifiers(std::string_view key_, std::string_view value_)
{
	const char *key = key_.data();
	const char *value = value_.data();
	// count the number, so I can print it.
	int key_count = 0;
	int value_count = 0;
	const char* found_key = key;
	const char* found_value = value;
	do
	{
		// this would probably be simpler & faster if I didn't use strchr...
		if(found_key != nullptr) found_key = strchr(found_key, '%');
		if(found_value != nullptr) found_value = strchr(found_value, '%');
		if(found_key != nullptr)
		{
			switch(found_key[1])
			{
			case 'f':
			case 'F':
			case 'g':
			case 'G':
			case 'e':
			case 'E':
				// above are all floats, I want to allow mixing, but it does not matter.
			case '%':
			case 'd':
			case 'u':
			case 's':
			case 'c':
			case 'x':
			case 'X':
			case 'p':
				if(found_value != nullptr && found_key[1] != found_value[1])
				{
					std::string str;
					str_asprintf(
						str,
						// TODO: use utf8::internal::validate_next to check + print range...
						(found_value[1] >= 0 && found_value[1] <= 0x001fu)
							// control codes are not possible because quoted_string checks.
							? "mismatching %% format specifier! (%%%c != #%d) NOTE: control codes should not be possible\n"
							: "mismatching %% format specifier! (%%%c != %%%c)\n",
						found_key[1],
						found_value[1]);
					report_error(str.c_str());
					return false;
				}
				break;
			default: {
				std::string str;
				str_asprintf(
					str,
					(found_key[1] >= 0 && found_key[1] <= 0x001fu)
						? "unknown key %% format specifier! (#%d) NOTE: control codes should not be possible\n"
						: "unknown key %% format specifier! (%%%c)\n",
					found_key[1]);
				report_error(str.c_str());
				return false;
			}
			}
		}

		if(found_key != nullptr)
		{
			found_key++;
			if(*(found_key) == '%')
			{
				found_key++;
			}
			key_count++;
		}
		if(found_value != nullptr)
		{
			found_value++;
			if(*(found_value) == '%')
			{
				found_value++;
			}
			value_count++;
		}
	} while(found_key != nullptr || found_value != nullptr);

	if(key_count != value_count)
	{
		std::string str;
		str_asprintf(
			str, "mismatching %% format specifier count! (%d != %d)\n", key_count, value_count);
		report_error(str.c_str());
		return false;
	}

	if(key_count == 0)
	{
		report_warning("no % specifiers found for printf style format\n");
	}

	return true;
}
#endif

namespace parse_printf_specifier
{

// I think spirit (parser) is not the right tool for parsing this
// because (Correct me if I am wrong) printf formatting has no backtracking.
// (my code does backtrack, but I believe it's possible to avoid it).
// which means a parser with full diagnostics could be as simple as like 200~ lines of code.
// since this is probably slower compared to asprintf or std::runtime_format
// and maybe even C++ string streams extractors (this doesn't even print anything)
// and even if it matched the speed, it still has spirit's compile time + binary growth.
// (it added 100kb + 3mb debug info on the release build (total 400kb + 15mb), ontop of TL).
// But what this could potentially do that std::runtime_format can't,
// is I could go with an embedded style binary log,
// where I store the format string index + arguments in raw binary, and that would be fast,
// maybe fast enough that I could use it to substitute sentry breadcrumbs.
// BUT I still wish I could write the formatter from scratch, using musl or something for ref.
// AND it would be wrong to store the log files in binary instead of plain text, because
// it breaks between every update, and it's possible that the log system itself crashes.
// and that's bad because I would try to send the log with error reporting software.
bp::rule<class any_specifier, int> const any_specifier = "'%c', '%s', '%d', or '%g', and etc";
bp::rule<class specifier_root, std::optional<int>> const specifier_root = "'%c', '%s', '%d', or '%g', and etc";
bp::symbols<int> const any_specifier_def = {
	{"c", 1},
	{"f", 2},
	{"F", 2},
	{"g", 2},
	{"G", 2},
	{"d", 5},
	{"i", 5},
	{"u", 6},
	{"x", 6},
	{"X", 6},
	// technically I could treat z and ll as prefixes for u/d/i, but would that help?
	{"zu", 7},

	{"llu", 9},
	{"llx", 9},
	{"llX", 9},
	{"lld", 10},
	{"lli", 10},
	{"s", 4},
	{"p", 8}};

// a real dumb hack. I also considered using bp::transform, but it would be uglier.
auto set_min_width_flag = [](auto& ctx) {
	ASSERT(!bp::_globals(ctx).variable_min_width_flag);
	bp::_globals(ctx).variable_min_width_flag = true;
};
auto set_field_width_flag = [](auto& ctx) {
	ASSERT(!bp::_globals(ctx).variable_field_width_flag);
	bp::_globals(ctx).variable_field_width_flag = true;
};

auto check_number = [](auto& ctx) {
	// if you had control of the string, a lot worse could be done,
	// but I assume this is an error
	const int max_specifier_size = 1000;
	if(std::abs(_attr(ctx)) > max_specifier_size)
	{
		std::string err;
		str_asprintf(err, "specifier size larger than %d: %d", max_specifier_size, _attr(ctx));
		bp::_report_warning(ctx, err.c_str());
	}
};

auto const number_ignore =
	// NOTE: is omit the same as & (?)
	bp::omit[bp::int_[check_number]];

//*.* or 1.1 or 1 or 1. (including negative signs)
auto const min_width_and_field_width =
	(number_ignore | bp::lit('*')[set_min_width_flag] | bp::eps) >>
	(bp::lit('.') >> (number_ignore | bp::lit('*')[set_field_width_flag] | bp::eps));

auto const add_specifier = [](auto& ctx) {
	ASSERT(bp::_globals(ctx).specifier == 0);
	bp::_globals(ctx).specifier = bp::_attr(ctx);
};

//I manually check for % before running the parser
// I also use bp::eps > ... on bp::prefix_parse,
// I tried to put it here, but it didn't print a nice error I think
auto const specifier_root_def = -min_width_and_field_width >> any_specifier[add_specifier];

BOOST_PARSER_DEFINE_RULES(any_specifier, specifier_root);

// pass the error handler to the parent handler
// I wonder if it would be better if I just merged the parsers instead of splitting them...
struct format_specifier_error_handler
{
	explicit format_specifier_error_handler(tl_parse_state& o_, tl_buffer_type::iterator vbegin_)
	: o(o_), vbegin(vbegin_)
	{
	}

	template<typename Iter, typename Sentinel>
	bp::error_handler_result
		operator()(Iter first, Sentinel last, bp::parse_error<Iter> const& e) const
	{
		errors_printed = true;

		std::string error = "Expected ";
		error += e.what();

		o.report_error(error.c_str(), vbegin + std::distance(first, e.iter));
		return bp::error_handler_result::fail;
	}

	// This function is for users to call within a semantic action to produce
	// a diagnostic.
	template<typename Context, typename Iter>
	void diagnose(
		bp::diagnostic_kind kind, std::string_view message, Context const& ctx, Iter it) const
	{
		ASSERT(*(message.data() + message.size()) == '\0');

		switch(kind)
		{
		case bp::diagnostic_kind::error:
			o.report_error(message.data(), vbegin + std::distance(bp::_begin(ctx), it));
			errors_printed = true;
			break;
		case bp::diagnostic_kind::warning:
			o.report_warning(message.data(), vbegin + std::distance(bp::_begin(ctx), it));
			break;
		}
	}

	// This is just like the other overload of diagnose(), except that it
	// determines the Iter parameter for the other overload by calling
	// _where(ctx).
	template<typename Context>
	void diagnose(bp::diagnostic_kind kind, std::string_view message, Context const& context) const
	{
		diagnose(kind, message, context, bp::_where(context).begin());
	}

	mutable bool errors_printed = false;
	tl_parse_state& o;
	tl_buffer_type::iterator vbegin;
};

} // namespace parse_printf_specifier

bool tl_parse_state::find_specifier(
	parse_printf_specifier::printf_specifier_parser_state& state,
	std::string_view::iterator& cur,
	std::string_view::iterator end,
	tl_buffer_type::iterator vbegin)
{
	// the other flags should be ignored if specifier == 0
	state.specifier = 0;
	// set to a known invalid value, so I could assert it.
	state.where = end;

	auto start = cur;
	while(cur != end)
	{
		cur = std::find(cur, end, '%');
		if(cur == end)
		{
			ASSERT(state.specifier == 0);
			return true;
		}
		// I could let the parser take care of parsing the % specifier.
		// not sure if it's worth it however...
		cur++;
		if(cur == end)
		{
			report_error(
				"% specifier reached end of string", vbegin + std::distance(start, cur));
			return false;
		}
		if(*cur == '%')
		{
			// keep searching
			++cur;
			continue;
		}
		state.where = cur;
		break;
	}
	if(cur == end)
	{
		ASSERT(state.specifier == 0);
		return true;
	}

	parse_printf_specifier::format_specifier_error_handler err(
		*this, vbegin + std::distance(start, cur));

	auto const parse = bp::with_error_handler(
		bp::with_globals(bp::eps > parse_printf_specifier::specifier_root, state), err);

	// NOTE: I could enable tracing, but it generates thousands of lines, bp::trace::on);
	if(!bp::prefix_parse(cur, end, parse))
	{
		ASSERT(parse.error_handler_.errors_printed && "expected errors to be printed");
		return false;
	}
	// if I set the specifier, I must have set the state.where
	ASSERT(state.specifier != 0 || state.where == end);
	ASSERT(state.specifier == 0 || state.where != end);
	return true;
}

bool tl_parse_state::check_printf_specifiers(std::string_view key, std::string_view value, tl_buffer_type::iterator vbegin)
{
	auto kit = key.begin();
	auto kend = key.end();
	auto vit = value.begin();
	auto vend = value.end();
	while(kit != kend)
	{
		parse_printf_specifier::printf_specifier_parser_state key_state;
		// TODO: i pass in get_iterator as a placeholder, it's wrong.
		if(!find_specifier(key_state, kit, kend, get_iterator()))
		{
			return false;
		}
		if(key_state.specifier == 0)
		{
			break;
		}
		if(vit != vend)
		{
			parse_printf_specifier::printf_specifier_parser_state value_state;
			if(!find_specifier(value_state, vit, vend, vbegin))
			{
				return false;
			}
			if(value_state.specifier == 0)
			{
				report_error("missing value specifier", vbegin);
				return false;
			}

			// Should I add another flag, or should I treat specifier = 0 as a skip?
			if(key_state != value_state)
			{
				// TODO: print this better
				std::string message;
				str_asprintf(message, "mismatching specifier (%d %d %d != %d %d %d)",
							 key_state.specifier,
							 key_state.variable_min_width_flag,
							 key_state.variable_field_width_flag,
							 value_state.specifier,
							 value_state.variable_min_width_flag,
							 value_state.variable_field_width_flag);
				// It's funny I ONLY added annotations for the value
				// yet it is completely broken with unicode...
				// If the terminal / dialogs supported unicode combinations,
				// that could be used to replace the arrow, but I don't know which glyph to use...
				report_error(message.c_str(), vbegin + std::distance(value.begin(), value_state.where));

				// if I annotated the key...
				//report_error("from here", vbegin + (value_state.where - value.begin()));

				return false;
			}
		}
	}
	// check if value has any specifiers remaining.
	if(vit != vend)
	{
		parse_printf_specifier::printf_specifier_parser_state value_state;
		if(!find_specifier(value_state, vit, vend, vbegin))
		{
			return false;
		}
		if(value_state.specifier != 0)
		{
			report_error("too many specifiers for the value!", vbegin);
			return false;
		}
	}

	return true;
}

#endif


namespace tl_parser
{

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

typedef std::tuple<std::string, std::string, my_string_type, my_string_type, my_string_type>
	tl_header_tuple;

enum class tl_info_get
{
	function,
	source_file,
	line,
	column
};
typedef std::tuple<my_string_type, my_string_type, int, int> tl_info_tuple;

enum class tl_no_match_get
{
	date,
	git_hash
};
typedef std::tuple<my_string_type, my_string_type> tl_no_match_tuple;

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

template<class T>
class report_wrapper : public tl_parse_state
{
public:
	T& context;
	explicit report_wrapper(T& context_)
	: context(context_)
	{
	}
	void report_error(const char* msg) override
	{
		bp::_report_error(context, msg);
	}
	void report_error(const char* msg, tl_buffer_type::iterator eiter) override
	{
		bp::_report_error(context, msg, eiter);
	}
	void report_warning(const char* msg) override
	{
		bp::_report_warning(context, msg);
	}
	void report_warning(const char* msg, tl_buffer_type::iterator eiter) override
	{
		bp::_report_warning(context, msg, eiter);
	}
	tl_buffer_type::iterator get_iterator() override
	{
		return bp::_where(context).begin();
	}
};

using namespace bp::literals;

auto const header_action = [](auto& ctx) {
	auto& globals = bp::_globals(ctx);
	tl_header_tuple& header = bp::_attr(ctx);

	report_wrapper wrap(ctx);
	globals.tl_parser_ctx = &wrap;

	tl_header entry;
	entry.long_name = std::get<(int)tl_header_get::long_name>(header);
	entry.short_name = std::get<(int)tl_header_get::short_name>(header);
	entry.native_name = std::get<(int)tl_header_get::native_name>(header);
	entry.date = std::get<(int)tl_header_get::date>(header);
	entry.git_hash = std::get<(int)tl_header_get::git_hash>(header);

	switch(globals.on_header(entry))
	{
	case TL_RESULT::SUCCESS:
	case TL_RESULT::WARNING: break;
	case TL_RESULT::FAILURE: bp::_pass(ctx) = false; break;
	}
	globals.tl_parser_ctx = nullptr;
};
auto const key_action = [](auto& ctx) {
	auto& globals = bp::_globals(ctx);
	auto& info = bp::_attr(ctx);

	report_wrapper wrap(ctx);
	globals.tl_parser_ctx = &wrap;

	switch(globals.on_translation(std::get<0>(info), std::get<1>(info)))
	{
	case TL_RESULT::SUCCESS:
	case TL_RESULT::WARNING: break;
	case TL_RESULT::FAILURE: bp::_pass(ctx) = false; break;
	}
	globals.tl_parser_ctx = nullptr;
};
#ifdef TL_ENABLE_FORMAT
auto const format_action = [](auto& ctx) {
	auto& globals = bp::_globals(ctx);
	auto& info = bp::_attr(ctx);

	report_wrapper wrap(ctx);
	globals.tl_parser_ctx = &wrap;

	switch(globals.on_format(std::get<0>(info), std::get<1>(info)))
	{
	case TL_RESULT::SUCCESS:
	case TL_RESULT::WARNING: break;
	case TL_RESULT::FAILURE: bp::_pass(ctx) = false; break;
	}
	globals.tl_parser_ctx = nullptr;
};
#endif

auto const info_action = [](auto& ctx) {
	auto& globals = bp::_globals(ctx);
	tl_info_tuple& info = bp::_attr(ctx);

	report_wrapper wrap(ctx);
	globals.tl_parser_ctx = &wrap;

	tl_info entry;
	entry.function = std::get<(int)tl_info_get::function>(info);
	entry.source_file = std::get<(int)tl_info_get::source_file>(info);
	entry.line = std::get<(int)tl_info_get::line>(info);
	entry.column = std::get<(int)tl_info_get::column>(info);

	switch(globals.on_info(entry))
	{
	case TL_RESULT::SUCCESS:
	case TL_RESULT::WARNING: break;
	case TL_RESULT::FAILURE: bp::_pass(ctx) = false; break;
	}
	globals.tl_parser_ctx = nullptr;
};
auto const no_match_action = [](auto& ctx) {
	auto& globals = bp::_globals(ctx);
	tl_no_match_tuple& no_match_info = bp::_attr(ctx);

	report_wrapper wrap(ctx);
	globals.tl_parser_ctx = &wrap;

	tl_no_match entry;
	entry.date = std::get<(int)tl_no_match_get::date>(no_match_info);
	entry.git_hash = std::get<(int)tl_no_match_get::git_hash>(no_match_info);

	switch(globals.on_no_match(entry))
	{
	case TL_RESULT::SUCCESS:
	case TL_RESULT::WARNING: break;
	case TL_RESULT::FAILURE: bp::_pass(ctx) = false; break;
	}
	globals.tl_parser_ctx = nullptr;
};

auto const comment_action = [](auto& ctx) {
	auto& globals = bp::_globals(ctx);

	report_wrapper wrap(ctx);
	globals.tl_parser_ctx = &wrap;

	switch(globals.on_comment(bp::_attr(ctx)))
	{
	case TL_RESULT::SUCCESS:
	case TL_RESULT::WARNING: break;
	case TL_RESULT::FAILURE: bp::_pass(ctx) = false; break;
	}
	globals.tl_parser_ctx = nullptr;
};

// TODO: make naming more consistent...
bp::rule<class header_lang, tl_header_tuple> const header_lang =
	"TL_START(long_name, short_name, date, git_hash)";
// I don't think I need
bp::rule<class tl_key_r, std::tuple<my_string_type, std::optional<annotated_string>>> const
	tl_key_r = "TL_TEXT(text, translated_text)";
#ifdef TL_ENABLE_FORMAT
bp::rule<class tl_format_r, std::tuple<my_string_type, std::optional<annotated_string>>> const
	tl_format_r = "TL_FORMAT(text, translated_text)";
#endif

bp::rule<class tl_comment_r, my_string_type> const tl_comment_r = "TL_COMMENT(text)";
bp::rule<class tl_info_r, tl_info_tuple> const tl_info_r = "TL_INFO(source, function, line)";
bp::rule<class tl_no_match_r, tl_no_match_tuple> const tl_no_match_r =
	"TL_NO_MATCH(date, git_hash)";
bp::rule<class tl_footer> const tl_footer = "'TL_END' 'TL_TEXT' 'TL_INFO' 'TL_NO_MATCH' etc";

// The json example uses a utf32 transcode, but when parsing unicode characters,
// it tries to write unicode into ascii, and it also wont work with as_utf8,
// but without anything, ascii works fine with utf8 (if the terminal is utf8),
// as long as you are not counting codepoints, such as the column pos in error reporter.
// it's possible that boost parser is supports wcout or something weird.
bp::rule<class string_char, uint32_t> const string_char =
	"code point (code points <= U+001F must be escaped)";
bp::rule<class single_escaped_char, uint32_t> const single_escaped_char = "'\"', '\\', 'n', or 't'";
bp::rule<class quoted_string, my_string_type> const quoted_string = "quoted string";
bp::rule<class nullable_quoted_string, std::optional<annotated_string>> const nullable_quoted_string =
	"quoted string or NULL";

bp::rule<class string_enum, std::string> const string_enum = "enum";

// If I make the >> into > for closing the comment, it crashes if it's missing...
auto const comment = "/*" >> *(bp::char_ - "*/") >> "*/" |
					 "//" >> *(bp::char_ - bp::eol) >> (bp::eol | bp::eoi);

auto const skipper = comment | bp::ws;

bp::symbols<uint32_t> const single_escaped_char_def = {
	{"\"", 0x0022u}, {"\\", 0x005cu}, {"n", 0x000au}, {"t", 0x0009u}};

auto const string_char_def =
	('\\'_l > single_escaped_char) | (bp::cp - bp::char_(0x0000u, 0x001fu));

// IGNORE: I want to add multi-line strings by "" "" But I think I NEED to use a vector<u32string>
// If I allowed the strings to split, it would break the possibility of using the preprocessor -E
// to generate a JSON file using the macros.
// but I don't know if it would work (valid json) and or if it's useful... (removing exceptions)
// I would need to re-format it (pretty print) as well
auto const quoted_string_def = bp::lexeme['"' >> *(string_char - '"') > '"'];

auto add_annotation = [](auto& ctx) {
	// store annotation, I do +1 because it includes the quotations.
	// I could technically add the annotation directly inside the quoted string to avoid that.
	bp::_val(ctx) = annotated_string(bp::_where(ctx).begin() + 1, std::move(bp::_attr(ctx)));
};

// TODO: I really want to add the annotation for NULL as well,
//  so I have annotated_string{optional<string>,iter}
//  I need to manually create the attribute however, since it's not automatic anymore.
auto const nullable_quoted_string_def = quoted_string[add_annotation] | "NULL"_l;
//auto const nullable_quoted_string_def = quoted_string | "NULL"_l;

// this is not a true C compatible syntax
auto const string_enum_def = bp::lexeme[+(bp::no_case[bp::char_('a', 'z')] | bp::char_('_'))];

// clang-format off
// auto const header_arguments_def = ;
auto const header_lang_def=
	bp::eps > "TL_START"_l
		>> '('
		> string_enum > ','
		> string_enum > ','
		> quoted_string > ','
		> quoted_string > ','
		> quoted_string
		> ')';

auto const tl_key_r_def =
	"TL_TEXT"_l
		>> '('
		> quoted_string > ',' > nullable_quoted_string
		> ')';
#ifdef TL_ENABLE_FORMAT

auto const tl_format_r_def =
	"TL_FORMAT"_l
		>> '('
		> quoted_string > ',' > nullable_quoted_string
		> ')';
#endif
auto const tl_comment_r_def =
	"TL_COMMENT"_l >> '(' > quoted_string > ')';
auto const tl_info_r_def =
	"TL_INFO"_l
	>> '('
		> quoted_string > ','
		> quoted_string > ','
		> bp::int_ > ','
		> bp::int_
	> ')';

auto const tl_no_match_r_def =
	"TL_NO_MATCH"_l >> '(' > quoted_string > ',' > quoted_string > ')';

auto const tl_footer_def= "TL_END"_l > '(' > ')' > bp::eoi;

auto const root_p =
	header_lang[header_action]
	> *( tl_key_r[key_action]
#ifdef TL_ENABLE_FORMAT
		| tl_format_r[format_action]
#endif
		| tl_comment_r[comment_action]
		| tl_info_r[info_action]
		| tl_no_match_r[no_match_action]
		) > tl_footer;
// clang-format on

BOOST_PARSER_DEFINE_RULES(
	single_escaped_char,
	string_char,
	quoted_string,
	nullable_quoted_string,
	string_enum,
	header_lang,
	tl_key_r,
	tl_comment_r,
	tl_info_r,
	tl_no_match_r,
	tl_footer);

#ifdef TL_ENABLE_FORMAT
BOOST_PARSER_DEFINE_RULES(tl_format_r);
#endif

struct logging_error_handler
{
	explicit logging_error_handler(tl_parse_observer& o_, const char* filename_)
	: o(o_)
	, filename(filename_)
	{
		ASSERT(filename != NULL);
	}

	// This is the function called by Boost.Parser after a parser fails the
	// parse at an expectation point and throws a parse_error.  It is expected
	// to create a diagnostic message, and put it where it needs to go.  In
	// this case, we're writing it to a log file.  This function returns a
	// bp::error_handler_result, which is an enum with two enumerators -- fail
	// and rethrow.  Returning fail fails the top-level parse; returning
	// rethrow just re-throws the parse_error exception that got us here in
	// the first place.
	template<typename Iter, typename Sentinel>
	bp::error_handler_result
		operator()(Iter first, Sentinel last, bp::parse_error<Iter> const& e) const
	{
		errors_printed = true;

		std::string error = "error: Expected ";
		error += e.what();
		std::string message = tl_parse_print_formatted_error(first, last, e.iter, error.c_str(), filename);

		o.on_error(message.c_str());
		return bp::error_handler_result::fail;
	}

	// This function is for users to call within a semantic action to produce
	// a diagnostic.
	template<typename Context, typename Iter>
	void diagnose(
		bp::diagnostic_kind kind, std::string_view message, Context const& context, Iter it) const
	{
		ASSERT(*(message.data() + message.size()) == '\0');

		std::string result = tl_parse_print_formatted_error(bp::_begin(context), bp::_end(context), it, message.data(), filename);
		switch(kind)
		{
		case bp::diagnostic_kind::error:
			o.on_error(result.c_str());
			errors_printed = true;
			break;
		case bp::diagnostic_kind::warning: o.on_warning(result.c_str()); break;
		}
	}

	// This is just like the other overload of diagnose(), except that it
	// determines the Iter parameter for the other overload by calling
	// _where(ctx).
	template<typename Context>
	void diagnose(bp::diagnostic_kind kind, std::string_view message, Context const& context) const
	{
		diagnose(kind, message, context, bp::_where(context).begin());
	}

	mutable bool errors_printed = false;
	tl_parse_observer& o;
	const char* filename;
};

} // namespace tl_parser

bool parse_translation_file(
	tl_parse_observer& o, std::string_view file_contents, const char* path_name)
{
	// validate utf8 upfront, if I get an error while parsing I will know it's my fault...
#ifndef NDEBUG
	auto str_cur = file_contents.begin();
	auto str_end = file_contents.end();

	int line_num = 1;
	while(str_cur != str_end)
	{
		uint32_t codepoint;
		utf8::internal::utf_error err_code =
			utf8::internal::validate_next(str_cur, str_end, codepoint);
		if(err_code != utf8::internal::UTF8_OK)
		{
			serrf("error: %s:%d bad utf8: %s\n", path_name, line_num, utf8cpp_get_error(err_code));
			return false;
		}
		if(codepoint == '\n')
		{
			line_num++;
		}
	}
#endif

	tl_parser::logging_error_handler err(o, path_name);

	auto const parse = bp::with_error_handler(bp::with_globals(tl_parser::root_p, o), err);

	// NOTE: I could enable tracing, but it generates thousands of lines, bp::trace::on);
	if(!bp::parse(file_contents, parse, tl_parser::skipper))
	{
		// I use bp::eps > at the start so that I get an error in an empty file.
		// This should be a CHECK because this isn't an error I care about,
		// but MY_DISABLE_GLOBAL_DEP
		ASSERT(parse.error_handler_.errors_printed && "expected errors to be printed");
		return false;
	}
	return true;
}
#ifdef __clang__
#pragma clang diagnostic pop
#endif
// NOLINTEND (bugprone-chained-comparison, google-readability-casting,
// bugprone-easily-swappable-parameters)

#endif // TL_COMPILE_TIME_TRANSLATION