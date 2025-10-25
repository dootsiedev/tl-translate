// this is GROSS, but I don't want to copy it so it's better than nothing...
#include "../../code/core/global.h"
#include "../../code/translation_parser.h"

#include <variant>
#include <vector>

// make TL and Comment into a class... I should just move this back into the parser...
struct tl_key
{
	std::string key;
	std::optional<std::string> value;
};

struct tl_comment
{
	std::string key;
};

typedef std::variant<tl_comment, tl_info, tl_no_match> translation_variant;

// build an AST
struct entry_ast
{
	std::vector<translation_variant> extra;
	std::optional<tl_key> key;
};

struct merge_handler : public tl_parse_observer
{
	std::vector<entry_ast> ast_root;
	void on_warning(const char* msg) override
	{
		slog(msg);
	}
	void on_error(const char* msg) override
	{
		serr(msg);
	}

	void add_entry_if_starting()
	{
		if(ast_root.empty())
		{
			ast_root.push_back(entry_ast{});
		}
		if(ast_root.back().key.has_value())
		{
			ast_root.push_back(entry_ast{});
		}
	}

	TL_RESULT on_header(tl_header& header) override
	{
		(void)header;
		return TL_RESULT::SUCCESS;
	}
	TL_RESULT on_translation(std::string& key, std::optional<std::string>& value) override
	{
		add_entry_if_starting();
		ASSERT(!ast_root.back().key.has_value());
		// yes I can move the values in (even if I didn't do utf8 conversion)
		// but I don't use && because I plan on switching to string_view's anyway & no difference.
		ast_root.back().key = tl_key{std::move(key), std::move(value)};
		return TL_RESULT::SUCCESS;
	}
	TL_RESULT on_comment(std::string& comment) override
	{
		add_entry_if_starting();
		ast_root.back().extra.emplace_back(tl_comment{std::move(comment)});
		return TL_RESULT::SUCCESS;
	}

	TL_RESULT on_info(tl_info& info) override
	{
		add_entry_if_starting();
		ast_root.back().extra.emplace_back(std::move(info));
		return TL_RESULT::SUCCESS;
	}
	TL_RESULT on_no_match(tl_no_match& no_match) override
	{
		add_entry_if_starting();
		ast_root.back().extra.emplace_back(std::move(no_match));
		return TL_RESULT::SUCCESS;
	}
};

#include "../../code/core/cvar.h"

static REGISTER_CVAR_STRING(
	cv_merge_from,
	"NULL",
	"english_ref.inl to use",
	CVAR_T::STARTUP);
static REGISTER_CVAR_STRING(
	cv_merge_to,
	"NULL",
	"english_ref.inl to overwrite (see cv_merge_out)",
	CVAR_T::STARTUP);
static REGISTER_CVAR_STRING(
	cv_merge_out,
	"NULL",
	"the actual output for the merge",
	CVAR_T::STARTUP);

static bool load_translation_ast()
{
	merge_handler merge_strings;

	std::string slurp_string;
	// copy the file into the string
	if(!slurp_stdio(slurp_string, cv_merge_from.data().c_str()))
	{
		return false;
	}
#ifdef CHECK_TIMER
	// small file with like 1000 characters
	// 0.2 - 0.05 ms on reldeb, 10ms on debug san.
	TIMER_U t1 = timer_now();
#endif
	if(!parse_translation_file(merge_strings, slurp_string, cv_merge_from.data().c_str()))
	{
		return false;
	}
#ifdef CHECK_TIMER
	TIMER_U t2 = timer_now();
	slogf("time: %" TIMER_FMT "\n", timer_delta_ms(t1, t2));
#endif

	// now I can use the data.

	return true;
}

int main(int argc, char** argv)
{
	switch(load_cvar(argc, argv))
	{
	case CVAR_LOAD::SUCCESS: break;
	case CVAR_LOAD::ERROR: show_error("cvar error", serr_get_error().c_str()); return 1;
	case CVAR_LOAD::CLOSE: return 0;
	}

	if(cv_merge_from.data() == "NULL")
	{
		serr("you must set cv_merge_from!\n");
		return 1;
	}
	if(cv_merge_to.data() == "NULL")
	{
		serr("you must set cv_merge_to!\n");
		return 1;
	}
	if(cv_merge_out.data() == "NULL")
	{
		serr("you must set cv_merge_out!\n");
		return 1;
	}

	if(!load_translation_ast())
	{
		return 1;
	}

	return 0;
}