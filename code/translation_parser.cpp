// This is an independent project of an individual developer. Dear PVS-Studio, please check it.
// PVS-Studio Static Code Analyzer for C, C++, C#, and Java: https://pvs-studio.com
#include "core/global.h"
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
#include <boost/parser/transcode_view.hpp>

#include <sstream>
#include <ranges>
// NOLINTBEGIN (bugprone-chained-comparison, google-readability-casting,
// bugprone-easily-swappable-parameters)
#ifdef __clang__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Woverloaded-shift-op-parentheses"
#endif

namespace bp = boost::parser;
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

typedef std::tuple<std::string, std::string, std::u32string, std::u32string, std::u32string>
	tl_header_tuple;

enum class tl_info_get
{
	function,
	source_file,
	line,
	column
};
typedef std::tuple<std::u32string, std::u32string, int, int> tl_info_tuple;

enum class tl_no_match_get
{
	date,
	git_hash
};
typedef std::tuple<std::u32string, std::u32string> tl_no_match_tuple;

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
	void report_warning(const char* msg) override
	{
		bp::_report_warning(context, msg);
	}
};

using namespace bp::literals;

// this will throw exceptions, maybe replace it with utfcpp (internal functions)
static std::string to_utf8(std::u32string& str)
{
	std::string out;
	out.reserve(str.size());
	for(auto c : str | bp::as_utf8)
	{
		out.push_back(c);
	}
	// needs C++20
	// std::ranges::copy(str | bp::as_utf8, std::back_inserter(out));
	return out;
};

auto const header_action = [](auto& ctx) {
	auto& globals = bp::_globals(ctx);
	tl_header_tuple& header = bp::_attr(ctx);

	report_wrapper wrap(ctx);
	globals.tl_parser_ctx = &wrap;

	tl_header entry;
	entry.long_name = std::get<(int)tl_header_get::long_name>(header);
	entry.short_name = std::get<(int)tl_header_get::short_name>(header);
	entry.native_name = to_utf8(std::get<(int)tl_header_get::native_name>(header));
	entry.date = to_utf8(std::get<(int)tl_header_get::date>(header));
	entry.git_hash = to_utf8(std::get<(int)tl_header_get::git_hash>(header));

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

	std::string key = to_utf8(std::get<0>(info));
	std::optional<std::string> value;
	if(std::get<1>(info).has_value())
	{
		value = to_utf8(*std::get<1>(info));
	}

	switch(globals.on_translation(key, value))
	{
	case TL_RESULT::SUCCESS:
	case TL_RESULT::WARNING: break;
	case TL_RESULT::FAILURE: bp::_pass(ctx) = false; break;
	}
	globals.tl_parser_ctx = nullptr;
};

auto const info_action = [](auto& ctx) {
	auto& globals = bp::_globals(ctx);
	tl_info_tuple& info = bp::_attr(ctx);

	report_wrapper wrap(ctx);
	globals.tl_parser_ctx = &wrap;

	tl_info entry;
	entry.function = to_utf8(std::get<(int)tl_info_get::function>(info));
	entry.source_file = to_utf8(std::get<(int)tl_info_get::source_file>(info));
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
	entry.date = to_utf8(std::get<(int)tl_no_match_get::date>(no_match_info));
	entry.git_hash = to_utf8(std::get<(int)tl_no_match_get::git_hash>(no_match_info));

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

	auto comment = to_utf8(bp::_attr(ctx));

	switch(globals.on_comment(comment))
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
bp::rule<class tl_key_r, std::tuple<std::u32string, std::optional<std::u32string>>> const tl_key_r =
	"TL(text, translated_text)";

bp::rule<class tl_comment_r, std::u32string> const tl_comment_r = "COMMENT(text)";
bp::rule<class tl_info_r, tl_info_tuple> const tl_info_r = "INFO(source, function, line)";
bp::rule<class tl_no_match_r, tl_no_match_tuple> const tl_no_match_r = "NO_MATCH(date, git_hash)";
bp::rule<class tl_footer> const tl_footer = "'TL_END' 'TL' 'INFO' 'NO_MATCH' etc";

// The json example uses a utf32 transcode, but when parsing unicode characters,
// it tries to write unicode into ascii, and it also wont work with as_utf8,
// but without anything, ascii works fine with utf8 (if the terminal is utf8),
// as long as you are not counting codepoints, such as the column pos in error reporter.
// it's possible that boost parser is supports wcout or something weird.
bp::rule<class string_char, uint32_t> const string_char =
	"code point (code points <= U+001F must be escaped)";
bp::rule<class single_escaped_char, uint32_t> const single_escaped_char = "'\"', '\\', 'n', or 't'";
bp::rule<class quoted_string, std::u32string> const quoted_string = "quoted string";
bp::rule<class nullable_quoted_string, std::optional<std::u32string>> const nullable_quoted_string =
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

auto const nullable_quoted_string_def = quoted_string | "NULL";

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
	"TL"_l
		>> '('
		> quoted_string > ',' > nullable_quoted_string
		> ')';
auto const tl_comment_r_def =
	"COMMENT"_l >> '(' > quoted_string > ')';
auto const tl_info_r_def =
	"INFO"_l
	>> '('
		> quoted_string > ','
		> quoted_string > ','
		> bp::int_ > ','
		> bp::int_
	> ')';

auto const tl_no_match_r_def =
	"NO_MATCH"_l >> '(' > quoted_string > ',' > quoted_string > ')';

auto const tl_footer_def= "TL_END"_l > '(' > ')' > bp::eoi;

auto const root_p =
	header_lang[header_action]
	> *( tl_key_r[key_action]
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

struct logging_error_handler
{
	explicit logging_error_handler(tl_parse_observer& o_, std::string_view filename)
	: o(o_)
	, filename_(filename)
	{
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
		std::ostringstream oss;
		// NOLINTNEXTLINE(performance-unnecessary-value-param)
		bp::write_formatted_expectation_failure_error_message(oss, filename_, first, last, e);
		o.on_error(oss.str().c_str());
		return bp::error_handler_result::fail;
	}

	// This function is for users to call within a semantic action to produce
	// a diagnostic.
	template<typename Context, typename Iter>
	void diagnose(
		bp::diagnostic_kind kind, std::string_view message, Context const& context, Iter it) const
	{
		std::ostringstream oss;
		bp::write_formatted_message(
			oss, filename_, bp::_begin(context), it, bp::_end(context), message);
		switch(kind)
		{
		case bp::diagnostic_kind::error:
			o.on_error(oss.str().c_str());
			errors_printed = true;
			break;
		case bp::diagnostic_kind::warning: o.on_warning(oss.str().c_str()); break;
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
	std::string_view filename_;
};

} // namespace tl_parser

bool parse_translation_file(
	tl_parse_observer& o, std::string_view file_contents, std::string_view path_name)
{
	tl_parser::logging_error_handler err(o, path_name);

	auto const parse = bp::with_error_handler(bp::with_globals(tl_parser::root_p, o), err);

	// NOTE: I could enable tracing, but it generates thousands of lines, bp::trace::on);
	if(!bp::parse(file_contents | bp::as_utf8, parse, tl_parser::skipper))
	{
		// I use bp::eps > at the start so that I get an error for a empty file.
		CHECK(parse.error_handler_.errors_printed && "expected errors to be printed");
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