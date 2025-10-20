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
// msvc clang crash if an exception is thrown unless it's reldeb & no asan (dunno why, update clang?)
// clang-cfi crashes clang during linking (try separate add_library, or update clang?)
// tons of ubsan implicit conversion errors (separate add_library? or suppressions?)
//
// boost parser requires exceptions
// boost spirit x3/x4 has no-exception support, but vcpkg spirit uses 500mb for x64-windows.
//
// I would have used spirit-po but gettext on native is not tempting.
// my compile time mode is intended to be very low effort to get working,
// other than the fact that you need to get and run the string-extractor (similar to xgettext)
// but spirit-po does have plural handling that I don't.
//
// If C++ exceptions or optimized binary bloat became a hard blocker (wasm),
// I could follow the same footsteps of gettext and use flatbuffers/etc to make a binary .mo file
// yep, it will be harder to modify, but it optionally has a json thing.
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

// for parser | bp::as_utf32, it does not give good results for me.
// #include <boost/parser/transcode_view.hpp>

#include <sstream>

namespace bp = boost::parser;
namespace tl_parser
{
	template<class T>
	class report_wrapper : public tl_parse_state
	{
	public:
		T& context;
		explicit report_wrapper(T& context_) : context(context_){}
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

auto const header_action = [](auto& ctx) {
	auto & globals = bp::_globals(ctx);
	tl_header_tuple& info = bp::_attr(ctx);

	report_wrapper wrap(ctx);
	globals.tl_parser_ctx = &wrap;

	switch(globals.on_header(info))
	{
	case TL_RESULT::SUCCESS: break;
	case TL_RESULT::WARNING: break;
	case TL_RESULT::FAILURE: bp::_pass(ctx) = false; break;
	}
	globals.tl_parser_ctx = nullptr;
};
auto const key_action = [](auto& ctx) {
	auto & globals = bp::_globals(ctx);
	auto& info = bp::_attr(ctx);

	report_wrapper wrap(ctx);
	globals.tl_parser_ctx = &wrap;

	switch(globals.on_translation(std::get<0>(info), std::get<1>(info)))
	{
	case TL_RESULT::SUCCESS: break;
	case TL_RESULT::WARNING: break;
	case TL_RESULT::FAILURE: bp::_pass(ctx) = false; break;
	}
	globals.tl_parser_ctx = nullptr;
};

auto const info_action = [](auto& ctx) {
	auto & globals = bp::_globals(ctx);
	tl_info_tuple& info = bp::_attr(ctx);

	report_wrapper wrap(ctx);
	globals.tl_parser_ctx = &wrap;

	switch(globals.on_info(info))
	{
	case TL_RESULT::SUCCESS: break;
	case TL_RESULT::WARNING: break;
	case TL_RESULT::FAILURE: bp::_pass(ctx) = false; break;
	}
	globals.tl_parser_ctx = nullptr;
};

bp::rule<class header_lang, tl_header_tuple> const header_lang =
	"TL_START(long_name, short_name, date, git_hash)";
bp::rule<class tl_key, std::tuple<std::string, std::string>> const tl_key = "TL(key, value)";
bp::rule<class tl_info, tl_info_tuple> const tl_info = "INFO(source, function, line)";
bp::rule<class tl_footer> const tl_footer = "TL_END()";

// The json example uses a utf32 transcode, but when parsing unicode characters,
// it tries to write unicode into ascii, and it also wont work with as_utf8,
// but without anything, ascii works fine with utf8 (if the terminal is utf8),
// as long as you are not counting codepoints, such as the column pos in error reporter.
// it's possible that boost parser is supports wcout or something weird.
bp::rule<class string_char, uint32_t> const string_char =
	"code point (code points <= U+001F must be escaped)";
bp::rule<class quoted_string, std::string> const quoted_string = "quoted string";
// bp::rule<struct header_p, header> header_p = "TL_START";
bp::rule<class single_escaped_char, uint32_t> const single_escaped_char =
	"'\"', '\\', '/', 'b', 'f', 'n', 'r', or 't'";

bp::rule<class string_enum, std::string> const string_enum = "enum";

bp::rule<class root_p> const root_p = "'TL_START', 'TL_END'";

// If I make the >> into > for closing the comment, it crashes if it's missing...
auto const comment = "/*" >> *(bp::char_ - "*/") >> "*/" |
					 "//" >> *(bp::char_ - bp::eol) >> (bp::eol | bp::eoi);

auto const skipper = comment | bp::ws;

bp::symbols<uint32_t> const single_escaped_char_def = {
	{"\"", 0x0022u},
	{"\\", 0x005cu},
	{"/", 0x002fu},
	{"b", 0x0008u},
	{"f", 0x000cu},
	{"n", 0x000au},
	{"r", 0x000du},
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
	"TL_START"_l
		> '('
		> string_enum > ','
		> string_enum > ','
		> quoted_string > ','
		> quoted_string > ','
		> quoted_string
		> ')';

auto const tl_key_def =
	"TL"_l
		>> '('
		> quoted_string > ','
		> (quoted_string | (bp::char_('N') > bp::char_('U') > bp::char_('L') > bp::char_('L') ))
		> ')';

auto const tl_info_def =
	"INFO"_l
	>> '('
		> quoted_string > ','
		> quoted_string > ','
		> bp::int_
	> ')';

auto const tl_footer_def= "TL_END"_l > '(' > ')' > bp::eoi;

auto const root_p_def =
	header_lang[header_action]
	> *( tl_key[key_action]
		| tl_info[info_action]
		) > tl_footer;
// clang-format on

BOOST_PARSER_DEFINE_RULES(
	single_escaped_char,
	string_char,
	quoted_string,
	string_enum,
	root_p,
	header_lang,
	tl_key,
	tl_info,
	tl_footer);

struct logging_error_handler
{
	explicit logging_error_handler(tl_parse_observer& o_, std::string_view filename)
	: o(o_), filename_(filename)
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
		case bp::diagnostic_kind::error: o.on_error(oss.str().c_str()); errors_printed = true; break;
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
	tl_parser::logging_error_handler err(o,path_name);

	auto const parse = bp::with_error_handler(bp::with_globals(tl_parser::root_p, o), err);

	auto it = file_contents.begin();
	// NOTE: I could enable tracing, but it generates thousands of lines, bp::trace::on);
	if(!bp::prefix_parse(it, file_contents.end(), parse, tl_parser::skipper))
	{
		// it's really stupid that parse wont print an error if nothing is parsed at the start.
		// but at least this works as expected.
		if(!parse.error_handler_.errors_printed)
		{
			std::ostringstream oss;
			bp::write_formatted_message(
				oss,
				path_name,
				file_contents.begin(),
				it,
				file_contents.end(),
				"expected TL_START");
			o.on_error(oss.str().c_str());
		}
		return false;
	}
	return true;
}

#endif // TL_COMPILE_TIME_TRANSLATION