#include "core/global.h"
//
// long wall of not very useful info
// TODO: fix warnings...
// TODO: add a function rule so that it says "expect function ex: " instead of "TL_END"
// TODO: test annotations for the string extractor (tl-extract?)
//		I am worried that the annotations don't include the whole string...
// TODO: why don't I get a error when I mess with TL_START? can I get more info?
// NOTE: should I bother with setting the terminal to utf8?
//
// so the results of this is that parsing wastes a lot of binary space,
// and it's painfully slow to build this one file.
//
// but boost parser was easy, and the errors are 100x better than anything I could write,
// and I think I could add something new in a few minutes (as long as I don't get any errors...)
//
// I think I am leaning towards FetchContent and keeping the ugly tuples
// (unless vcpkg boost parser is header only and setup takes the same time as FetchContent)
//
// these numbers are probably old.
// msvc release build is 300kb, and includes 10mb of debug info (50kb + 3mb with TL_COMPILE)
// msvc debug-san build is 3mb and 30mb of debug info (600kb + 2mb with TL_COMPILE)
//
// msvc clang crash if an exception is thrown unless it's reldeb & no asan (dunno why, update
// clang?) clang-cfi crashes because the VS installer is old, it works tested with LLVM 21, failed
// on 19 tons of ubsan implicit conversion errors (separate add_library? or suppressions?)
//
// boost parser requires exceptions
// boost spirit x3/x4 has no-exception support, but vcpkg spirit uses 500mb for x64-windows.
//
// TODO: I could modify parser and remove the exceptions and just printing the error handler and
//  exit. Pretty much only for wasm, it does not require exception unwinding,
//  2 problems:
//  I don't know if I have the context / error handler in the throw, yet (the parser rethrows)
//  I need a utf8 library because parser will throw utf8 errors.
//
// I would have used spirit-po but gettext is not tempting.
// my compile time mode is intended to be low effort to get working.
// (but my code might have quirks that prevent it from being a standalone libraries ATM)
// however, spirit-po does have plural handling that I don't.
//
// If C++ exceptions or optimized binary bloat became a hard blocker (wasm),
// I could make a custom cmake command that converts the files into json or something.
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


#ifdef __clang__
#pragma clang diagnostic push
#pragma ide diagnostic ignored "bugprone-chained-comparison"
#pragma ide diagnostic ignored "google-readability-casting"
#pragma ide diagnostic ignored "bugprone-easily-swappable-parameters"
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
	source_file,
	function,
	line,
};
typedef std::tuple<std::u32string, std::u32string, int> tl_info_tuple;

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
	std::ranges::copy(str | bp::as_utf8, std::back_inserter(out));
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
	entry.source_file = to_utf8(std::get<(int)tl_info_get::source_file>(info));
	entry.function = to_utf8(std::get<(int)tl_info_get::function>(info));
	entry.line = std::get<(int)tl_info_get::line>(info);

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

bp::rule<class header_lang, tl_header_tuple> const header_lang =
	"TL_START(long_name, short_name, date, git_hash)";
bp::rule<class tl_key, std::tuple<std::u32string, std::optional<std::u32string>>> const tl_key =
	"TL(text, translated_text)";

bp::rule<class tl_info, tl_info_tuple> const tl_info = "INFO(source, function, line)";
bp::rule<class tl_no_match, tl_no_match_tuple> const tl_no_match = "NO_MATCH(date, git_hash)";
bp::rule<class tl_footer> const tl_footer = "'TL_END' 'TL' 'INFO' 'NO_MATCH' etc";

// The json example uses a utf32 transcode, but when parsing unicode characters,
// it tries to write unicode into ascii, and it also wont work with as_utf8,
// but without anything, ascii works fine with utf8 (if the terminal is utf8),
// as long as you are not counting codepoints, such as the column pos in error reporter.
// it's possible that boost parser is supports wcout or something weird.
bp::rule<class string_char, uint32_t> const string_char =
	"code point (code points <= U+001F must be escaped)";
bp::rule<class quoted_string, std::u32string> const quoted_string = "quoted string";
bp::rule<class single_escaped_char, uint32_t> const single_escaped_char =
	"'\"', '\\', 'n', or 't'";

bp::rule<class string_enum, std::string> const string_enum = "enum";

// If I make the >> into > for closing the comment, it crashes if it's missing...
auto const comment = "/*" >> *(bp::char_ - "*/") >> "*/" |
					 "//" >> *(bp::char_ - bp::eol) >> (bp::eol | bp::eoi);

auto const skipper = comment | bp::ws;

bp::symbols<uint32_t> const single_escaped_char_def = {
	{"\"", 0x0022u},
	{"\\", 0x005cu},
	{"n", 0x000au},
	{"t", 0x0009u}};

auto const string_char_def =
	('\\'_l > single_escaped_char) | (bp::cp - bp::char_(0x0000u, 0x001fu));

// TODO: I want to add multi-line strings by "" \n ""
auto const quoted_string_def = bp::lexeme['"' >> *(string_char - '"') > '"'];

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

auto const tl_key_def =
	"TL"_l
		>> '('_l
		> quoted_string > (','_l >> (quoted_string | "NULL"))
		> ')';

auto const tl_info_def =
	"INFO"_l
	>> '('
		> quoted_string > ','
		> quoted_string > ','
		> bp::int_
	> ')';

auto const tl_no_match_def =
	"NO_MATCH"_l >> '(' > quoted_string > ',' > quoted_string > ')';

auto const tl_footer_def= "TL_END"_l > '(' > ')' > bp::eoi;

auto const root_p =
	header_lang[header_action]
	> *( tl_key[key_action]
		| tl_info[info_action]
		| tl_no_match[no_match_action]
		) > tl_footer;
// clang-format on

BOOST_PARSER_DEFINE_RULES(
	single_escaped_char,
	string_char,
	quoted_string,
	string_enum,
	header_lang,
	tl_key,
	tl_info,
	tl_no_match,
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
	// Unicode is annoying because I don't know what to do.
	// I want to print the errors in unicode codepoints because the column line is accurate.
	// but it is kind of slow due to the fact I MUST use u32strings
	// or else it gets truncated to ascii (basically no translations).
	// so I convert everything... And I do notice a 2x slowdown (but my file size is tiny).
	if(!bp::parse(file_contents | bp::as_utf32, parse, tl_parser::skipper))
	{
		CHECK(parse.error_handler_.errors_printed && "expected errors to be printed");
		// I use bp::eps > at the start so that I get an error for a empty file.
		return false;
	}
	return true;
}
#ifdef __clang__
#pragma clang diagnostic pop
#endif

#endif // TL_COMPILE_TIME_TRANSLATION