// This is an independent project of an individual developer. Dear PVS-Studio, please check it.
// PVS-Studio Static Code Analyzer for C, C++, C#, and Java: https://pvs-studio.com
// this is GROSS, but I don't want to copy it so it's better than nothing...
#include "../../code/translation_parser.h"
#include "../../code/util/string_tools.h"

// NOLINTBEGIN(*-container-contains)

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
// I can't use stdout for info logs because I want to use stdout for parsing with stdout/stdin
// I intend on using stdin for having merge conflicts,
// mainly for AI (using json?), but potentially for humans too
// (for humans, I could set slog to stdout, and prompt stdin to fix NO_MATCH's
// but manual merging could be done by manually fixing NO_MATCH() by hand.
// but manually merging is really annoying, I don't want to deal with that)
#define info(msg) OutputDebugStringA(msg)
__attribute__((format(printf, 1, 2))) static void infof(MY_MSVC_PRINTF const char* fmt, ...)
{
	std::string str;

	va_list args;
	va_start(args, fmt);
	str_vasprintf(str, fmt, args);
	va_end(args);

	// I could convert utf8 to utf16 for OutputDebugStringW
	// BUT clion already understands utf8.
	OutputDebugStringA(str.c_str());
}
#if 0
#define slogf(fmt, ...)                                         \
	do                                                          \
	{                                                           \
		std::string formatted_debug_string;                     \
		str_asprintf(formatted_debug_string, fmt, __VA_ARGS__); \
		OutputDebugStringA(formatted_debug_string.c_str());     \
	} while(false)
#endif
#else
#define info(msg)
#define infof(fmt, ...)
#endif

static bool g_use_werror = false;
static bool g_werror_has_error = false;
// I could add a werrf, but this is ONLY for the parser.
static void werr(const char* msg)
{
	if(g_use_werror)
	{
		g_werror_has_error = true;
		serr(msg);
	}
	else
	{
		info(msg);
	}
}

__attribute__((format(printf, 1, 2))) static void werrf(MY_MSVC_PRINTF const char* fmt, ...)
{
	std::string str;

	va_list args;
	va_start(args, fmt);
	str_vasprintf(str, fmt, args);
	va_end(args);

	werr(str.c_str());
}

#include <variant>
#include <vector>
#include <stdio.h>

struct tl_comment
{
	std::string comment;
};

typedef std::variant<tl_comment, tl_info, tl_no_match> translation_variant;

// build an AST
struct entry_ast
{
	std::string key;
	std::optional<std::string> value;
	std::vector<translation_variant> extra;
	bool format_text = false;
	entry_ast() = default;
	// C++17 emplace back pain
	entry_ast(
		std::string&& key_,
		std::optional<std::string>&& value_,
		std::vector<translation_variant>&& extra_,
		bool format = false)
	: key(std::move(key_))
	, value(std::move(value_))
	, extra(std::move(extra_))
	, format_text(format)
	{
	}
};

struct load_ast_handler : public tl_parse_observer
{
	std::vector<translation_variant> extra_stack;
	std::vector<entry_ast> ast_root;
	tl_header header;
	bool is_patch = false;

	void on_warning(const char* msg) override
	{
		werr(msg);
	}
	void on_error(const char* msg) override
	{
		serr(msg);
	}

	TL_RESULT on_header(tl_header& header_) override
	{
		header = std::move(header_);
		return TL_RESULT::SUCCESS;
	}


	TL_RESULT on_insert(std::string& key, std::optional<std::string>& value, bool format)
	{
		// NOTE: is the key captured as a reference or a copy if I [key]?
		auto find_it = std::find_if(ast_root.begin(), ast_root.end(), [&key](const entry_ast& ast) -> bool {
			return ast.key == key;
		});

		if(find_it != ast_root.end())
		{
			if(find_it->format_text != format)
			{
				tl_parser_ctx->report_error("duplicate mismatching TL_TEXT with TL_FORMAT!");
				return TL_RESULT::FAILURE;
			}
			if(!is_patch)
			{
				// I don't return TL_RESULT::WARNING because it does nothing.
				tl_parser_ctx->report_warning("merging duplicate (this shouldn't happen)");
			}
			for(auto& extra : extra_stack)
			{
				find_it->extra.emplace_back(std::move(extra));
			}
			extra_stack.clear();
		}
		else
		{
			ast_root.emplace_back(std::move(key), std::move(value), std::move(extra_stack), format);
		}
		return TL_RESULT::SUCCESS;
	}

	TL_RESULT on_translation(std::string& key, std::optional<annotated_string>& value) override
	{
		std::optional<std::string> val;
		if(value.has_value()){val = std::make_optional<std::string>(value->data);}
		return on_insert(key, val, false);
	}
#ifdef TL_ENABLE_FORMAT
	TL_RESULT on_format(std::string& key, std::optional<annotated_string>& value) override
	{
		std::optional<std::string> val;
		if(value.has_value()){val = std::make_optional<std::string>(value->data);}
		return on_insert(key, val, true);
	}
#endif
	TL_RESULT on_comment(std::string& comment) override
	{
		extra_stack.emplace_back(tl_comment{std::move(comment)});
		return TL_RESULT::SUCCESS;
	}

	TL_RESULT on_info(tl_info& info) override
	{
		extra_stack.emplace_back(std::move(info));
		return TL_RESULT::SUCCESS;
	}
	TL_RESULT on_no_match(tl_no_match& no_match) override
	{
		extra_stack.emplace_back(std::move(no_match));
		return TL_RESULT::SUCCESS;
	}

	//  internally I already check the specifiers while loading,
	// but I want to check it while merging, but to do so, is very ugly.
	// TODO: This should be moved back into the spirit header,
	//  It's not ATM because it takes forever to compile.
	class tl_check_wrapper : public tl_parse_state
	{
	public:
		std::string error_message;
		void report_error(const char* msg) override
		{
			// I don't want to print to serr because I instead just add TL_ERROR.
			// TODO: I have not implemented werror...
			//got_error = true;
			//serr(msg);
			error_message = msg;
		}
		void report_warning(const char* msg) override
		{
			info(msg);
		}
	};

	typedef decltype(ast_root)::iterator iter_type;
	typedef decltype(ast_root)::const_iterator c_iter_type;

	// This is messy, not too happy about it.
	// I wish I could use const, but it's not possible without a mess.
	static std::vector<iter_type> find_missing(load_ast_handler& to_, load_ast_handler& from_, bool merge_to)
	{
		ASSERT(to_.is_patch != from_.is_patch);
		auto& to = to_.ast_root;
		auto& from = from_.ast_root;
		std::vector<iter_type> missing;
		for(auto it = from.begin(); it != from.end(); ++it)
		{
			auto find_it =
				std::find_if(to.begin(), to.end(), [it](const entry_ast& ast) -> bool {
					return ast.key == it->key;
				});
			if(find_it != to.end())
			{
				ASSERT(find_it->format_text == it->format_text && "this should have been already checked during parsing!");
				if(merge_to)
				{
					find_it->extra.clear();
					find_it->extra.insert(find_it->extra.end(), it->extra.begin(), it->extra.end());
				}
			}
			else
			{
				missing.push_back(it);
			}
		}
		return missing;
	}

	// I use iterators so that I could add annotations, but I don't know how helpful it is...
	bool check_printf(iter_type missing_patch, c_iter_type missing_old_text)
	{
		//tl_check_wrapper wrapper;
		//if(!wrapper.check_printf_specifiers(
		//		   missing_patch->key.c_str(), missing_old_text->key.c_str()))
		{
			//missing_patch->extra.insert(missing_patch->extra.begin(), TL_ERROR)
		}
		return true;
	}

	//returns true if merged.
	bool auto_merge(iter_type missing_patch, c_iter_type missing_old_text)
	{
		for(const auto &extra : missing_old_text->extra)
		{
			const auto *info_result = std::get_if<tl_info>(&extra);
			if(info_result != nullptr)
			{
				for(auto& jextra : missing_patch->extra)
				{
					auto* jinfo_result = std::get_if<tl_info>(&jextra);
					if(jinfo_result != nullptr)
					{
						if(info_result->source_file != jinfo_result->source_file)
						{
							continue;
						}
						if(!check_printf(missing_patch, missing_old_text))
						{

						}
						return true;
					}
				}
			}
		}
		return false;
	}

	// add the patch to the ast.
	bool merge(load_ast_handler& patch)
	{
		ASSERT(!is_patch);
		ASSERT(patch.is_patch);

		// possibly new translations or failed to match.
		std::vector<iter_type> missing_patches = find_missing(*this, patch, true);
		// possibly removed text or failed to match
		const std::vector<iter_type> missing_old_text = find_missing(patch, *this, false);

		/*
		for(auto it : missing_patches)
		{
			info(it->key.c_str());
		}
		for(auto it : missing_old_text)
		{
			info(it->key.c_str());
		}*/
		for(auto it : missing_old_text)
		{
			for(auto jt : missing_patches)
			{
				if(auto_merge(jt, it))
				{
					infof("found merge with: `%s` == `%s`\n", jt->key.c_str(), it->key.c_str());
					it->extra.clear();
					it->extra = jt->extra;
				}
			}
		}

		// add TL_NO_MATCH
		for(auto it : missing_old_text)
		{
			bool found = false;
			for(auto &extra : it->extra)
			{
				auto *result = std::get_if<tl_no_match>(&extra);
				if(result != nullptr)
				{
					found = true;
					break;
				}
			}
			if(!found)
			{
				it->extra.insert(it->extra.begin(),tl_no_match{header.date,header.git_hash});
			}
		}

		// add the patches.
		// I add it to the front because I don't want to scroll down to see missing translations
		// since I assume this will be used iteratively,
		// optionally after you translate everything, can sort it
		// THIS WILL INVALIDATE missing_old_text ITERATORS
		ast_root.insert(ast_root.begin(), missing_patches.size(), entry_ast{});
		int i = 0;
		for(auto it : missing_patches)
		{
			ast_root[i] = *it;
			++i;
		}

		return true;
	}
	bool post_parse()
	{
		// I could technically make this a rule in boost parser,
		// but this should still be good for sanity for the parser.
		if(!extra_stack.empty())
		{
			serrf("Error expected TL() before TL_END(), leftover values! (%zu)\n", extra_stack.size());
			return false;
		}
		return true;
	}
};
struct format_ast_visitor
{
	FILE* file = nullptr;
	explicit format_ast_visitor(FILE* file_) : file(file_){}
	bool operator()(tl_comment& comment)
	{
		ASSERT(file != nullptr);
		std::string escape_buffer;
		if(!escape_string(escape_buffer, comment.comment))
		{
			return false;
		}
		if(fprintf(file, "TL_COMMENT(\"%s\")\n", escape_buffer.c_str()) < 0)
		{
			serrf("fprintf: %s\n", strerror(errno));
			return false;
		}

		return true;
	}
	bool operator()(tl_info& info)
	{
		ASSERT(file != nullptr);

		// TODO: I don't check for escaped string, but I should make that a parser rule.
		if(fprintf(
			   file,
			   "TL_INFO(\"%s\", \"%s\", %d, %d)\n",
			   info.function.c_str(),
			   info.source_file.c_str(),
			   info.line,
			   info.column) < 0)
		{
			serrf("fprintf: %s\n", strerror(errno));
			return false;
		}
		return true;
	}
	bool operator()(tl_no_match& no_match)
	{
		ASSERT(file != nullptr);

		// TODO: I don't check for escaped string, but I should make that a parser rule.
		if(fprintf(
			   file,
			   "TL_NO_MATCH(\"%s\", \"%s\")\n",
			   no_match.date.c_str(),
			   no_match.git_hash.c_str()) < 0)
		{
			serrf("fprintf: %s\n", strerror(errno));
			return false;
		}
		return true;
	}
};

// TODO: I should add a string for the file name for better errors.
static bool dump_formatted_ast(FILE* file, std::vector<entry_ast>& ast_root, tl_header& header)
{
	// TODO: I don't check for escaped strings, but I should make that a parser rule.
	if(fprintf(file,
		   "TL_START(%s, %s, \"%s\", \"%s\", \"%s\")\n\n",
		   header.long_name.c_str(),
		   header.short_name.c_str(),
		   header.native_name.c_str(),
		   header.date.c_str(),
		   header.git_hash.c_str()) < 0)
	{
		serrf("fprintf: %s\n", strerror(errno));
		return false;
	}

	format_ast_visitor astVisitor(file);
	for(auto& entry : ast_root)
	{
		for(auto& extra : entry.extra)
		{
			if(!std::visit(astVisitor, extra))
			{
				return false;
			}
		}

		std::string key_buffer;
		if(!escape_string(key_buffer, entry.key))
		{
			return false;
		}
		std::string value_buffer;
		if(entry.value.has_value())
		{
			// it should be +2, but I add +1 for a single newline escape.
			value_buffer.reserve(entry.value->size() + 3);
			value_buffer += '\"';
			if(!escape_string(value_buffer, *entry.value))
			{
				return false;
			}
			value_buffer += '\"';
		}
		else
		{
			value_buffer = "NULL";
		}

		bool has_value = entry.value.has_value();
		if(fprintf(
			   file,
			   entry.format_text ? "TL_FORMAT(\"%s\", %s)\n\n" : "TL_TEXT(\"%s\", %s)\n\n",
			   key_buffer.c_str(),
			   value_buffer.c_str()) < 0)
		{
			serrf("fprintf: %s\n", strerror(errno));
			return false;
		}
	}
	if(fputs("TL_END()\n", file) < 0)
	{
		serrf("fputsf: %s\n", strerror(errno));
		return false;
	}

	if(ferror(file) != 0)
	{
		serr("error indicator set\n");
		return false;
	}

	return true;
}

#include <sys/types.h>
#include <sys/stat.h>

#ifndef _WIN32
// I could use _CRT_INTERNAL_NONSTDC_NAMES
// but I forgot why I don't.
#define _fileno fileno
#define _fstat fstat
#define _stat stat
#endif

// yep it's stupid, I use underscore for linux, but don't for S_ISDIR
#if defined __WIN32__ || defined _WIN32 || defined _Windows
#if !defined S_ISDIR
#define S_ISDIR(m) (((m) & _S_IFDIR) == _S_IFDIR)
#endif
#endif


static bool slurp_stdio(std::string& out, FILE* fp, const char* name)
{
	ASSERT(fp != NULL);
	ASSERT(name != NULL);
	if(ferror(fp) != 0)
	{
		serrf("error indicator set: `%s`\n", name);
		return false;
	}
	struct _stat info;
	int ret = _fstat(_fileno(fp), &info);
	if(ret != 0)
	{
		serrf(
			"fstat error: `%s`, reason: %s (return: %d)\n",
			name,
			strerror(errno),
			ret);
		return false;
	}
	out.resize(info.st_size);
	size_t bytes_read = fread(out.data(), 1, info.st_size, fp);
	if(bytes_read != info.st_size)
	{
		serrf(
			"fread error: `%s`, reason: %s (return: %zu)\n",
			name,
			strerror(errno),
			bytes_read);
		return false;
	}

	return true;
}

static bool slurp_file(std::string& out, const char* path)
{
	ASSERT(path != NULL);
	FILE* fp = fopen(path, "rb");
	if(fp == nullptr)
	{
		serrf(
			"Failed to read `%s`, reason: %s\n",
			path,
			strerror(errno));
		return false;
	}
	bool success = slurp_stdio(out, fp, path);
	fclose(fp);
	return success;
}

#if 0
// this only finds duplicates inside of the patch.
static bool merge_patch(std::vector<entry_ast>& ast_patch)
{
	bool success = true;
#ifndef NDEBUG
	// slow test to make sure this is actually a patch file.
	for(auto& ast : ast_patch)
	{
		if(ast.key.value.has_value())
		{
			serrf("merge_patch: Patch files must not have values stored!: `%s`", ast.key.key.c_str());
			success = false;
		}
	}
#endif
	// I looked on the internet for "remove duplicates from vector"
	// and I found a dozen solutions, but none match this.
	// I think it might be possible that just sorting this and doing std::unique is faster.
	// but I don't want to actually sort anything
	// (sorting would fix the problem of the translation file being "updated" in git
	// if you change the order of files in cmake or within the code,
	// but I kind of like unsorted translations... technically it's still ordered in a way
	// BUT this won't happen for incremental patches, since it will only append to the end. )
	auto rit = std::remove_if(
		ast_patch.rbegin(), ast_patch.rend(), [&ast_patch, &success](entry_ast& cur_ast) -> bool {
			// the pointer offset only works for std::vector
			// I could have a custom std::remove_if that passes the iterator / index
			auto index = (&cur_ast - ast_patch.data());
			ASSERT(index < ast_patch.size());
			auto it_end = std::next(ast_patch.begin(), index);
			auto find_it =
				std::find_if(ast_patch.begin(), it_end, [&cur_ast](entry_ast& ast) -> bool {
					return ast.key.key == cur_ast.key.key;
				});
			if(find_it != it_end)
			{
				if(find_it->format_text != cur_ast.format_text)
				{
					// TODO: I could probably pretty print with extra data showing the file and line
					serrf(
						"merge_patch: mismatching TL_TEXT with TL_FORMAT! %s",
						find_it->key.key.c_str());
					success = false;
					return false;
				}
				find_it->extra.insert(
					find_it->extra.begin(), cur_ast.extra.begin(), cur_ast.extra.end());
			}
			return true;
		});
	// this could be avoided if I used a range
	ast_patch.erase(ast_patch.begin(), ast_patch.begin() + std::distance(rit, ast_patch.rend()));

	return success;
}

// modifies ast_out with the patch
static bool merge_ast(std::vector<entry_ast>& ast_out, const std::vector<entry_ast>& ast_patch)
{
	ASSERT(ast_out.data() != ast_patch.data());
	std::vector<unsigned int> missing_entry;
	for(auto it = ast_out.begin(); it != ast_out.end(); ++it)
	{
		auto find_it = std::find_if(ast_patch.begin(), ast_patch.end(), [it](const entry_ast& ast) {
			return ast.key == it->key;
		});
		if(find_it != ast_out.end())
		{
			// TODO: the PATCH will ADD INFO (inside itself), the MERGE will REPLACE (+warn on duplicates)!
			//  I need a separate function for fixing the patch
			it->extra.insert(it->extra.end(), find_it->extra.begin(), find_it->extra.end());
		}
		else{
			missing_entry.push_back(std::distance(ast_out.begin(), it));
		}
	}
	for(auto slot: missing_entry)
	{
		infof("missing: %s\n", ast_patch[slot].key.c_str());
	}
	return true;
}
#endif

#define CXXOPTS_NO_EXCEPTIONS
#include <cxxopts.hpp>

static cxxopts::Options options("test", "A brief description");
static cxxopts::ParseResult res;

static bool load_translation_ast()
{
	std::string slurp_string;

	if(res.count("patch") == 0)
	{
		serr("error: --patch not defined\n");
		return false;
	}

	std::string merge_patch = res["patch"].as<std::string>();

	infof("info: loading patch: %s\n", merge_patch.c_str());
	if(!slurp_file(slurp_string, merge_patch.c_str()))
	{
		return false;
	}

	// now merge the patch to the ast of the file
	// I just add the contents to the end of the previous file.
	// merge_apply_patch_handler after_patch;
	// after_patch.patch_root = std::move(patch.ast_root);

	load_ast_handler patch_ast;
	patch_ast.is_patch = true;
	if(!parse_translation_file(patch_ast, slurp_string, merge_patch.c_str()))
	{
		return false;
	}
	if(!patch_ast.post_parse())
	{
		return false;
	}

	if(!dump_formatted_ast(stdout, patch_ast.ast_root, patch_ast.header))
	{
		return false;
	}

	std::vector<std::string> merge_files = res["filenames"].as<std::vector<std::string>>();

	if(merge_files.empty())
	{
		serr("expected files, none provided\n");
		return false;
	}

	std::optional<std::string> lang_directory;
	if(res.count("lang-dir") != 0)
	{
		lang_directory = res["lang-dir"].as<std::string>();
		const char* dir_path = lang_directory->c_str();
		struct _stat sb;
		if(_stat(dir_path, &sb) == 0)
		{
			if(!S_ISDIR(sb.st_mode))
			{
				serrf("error: --lang-dir '%s' is not a directory.\n", dir_path);
				return false;
			}
		}
		else
		{
			serrf("error: --lang-dir '%s': %s\n", dir_path, strerror(errno));
			return false;
		}
	}

	std::optional<std::string> stage_directory;
	if (res.count("stage_dir") != 0)
	{
		stage_directory = res["stage_dir"].as<std::string>();
		const char* dir_path = stage_directory->c_str();
		struct _stat sb;
		if(_stat(dir_path, &sb) == 0)
		{
			if(!S_ISDIR(sb.st_mode))
			{
				serrf("error: --stage_dir '%s' is not a directory.\n", dir_path);
				return false;
			}
		}
		else
		{
			serrf("error: --stage_dir '%s': %s\n", dir_path, strerror(errno));
			return false;
		}
	}

	for(auto& path : merge_files)
	{
		// TODO: I could use the log, since I just use the positional variables...
		infof("info: loading: %s\n", path.c_str());

		{
			FILE* to_fp = fopen(path.c_str(), "rb");

			if(to_fp == NULL)
			{
				serrf("error: failed to open: `%s`, reason: %s\n", path.c_str(), strerror(errno));
				return false;
			}

			// copy the file into the string
			if(!slurp_stdio(slurp_string, to_fp, path.c_str()))
			{
				fclose(to_fp);
				return false;
			}
			fclose(to_fp);
		}

		// now merge the patch to the ast of the file
		// I just add the contents to the end of the previous file.
		// merge_apply_patch_handler after_patch;
		// after_patch.patch_root = std::move(patch.ast_root);

		load_ast_handler ast;
		if(!parse_translation_file(ast, slurp_string, path.c_str()))
		{
			return false;
		}
		if(!ast.post_parse())
		{
			return false;
		}

		if(!ast.merge(patch_ast))
		{
			return false;
		}

		if(!dump_formatted_ast(stdout, ast.ast_root, ast.header))
		{
			return false;
		}

	}

	return true;
}

int main(int argc, char** argv)
{
	// Maybe I could add a flag to strip INFO? Remove Extra Newlines?
	// clang-format off
	options.add_options()
		("p,patch", "input file to patch/merge files, required", cxxopts::value<std::string>())
		("lang-dir", "relative path to the translation files", cxxopts::value<std::string>())
		("stage-dir", "write merged files into a folder instead of overwriting", cxxopts::value<std::string>())
		("filenames", "the list of files to apply the patch to", cxxopts::value<std::vector<std::string>>())
		("werror", "warnings return errors and won't write files")
		("tool", "one of [auto,rollback,json,tty]\n"
			"\tauto = merge with very basic rules (this will not fuzzy match)\n"
			"\tjson = use a json format for stdout/stdin for AI fuzzy matching\n"
			// originally I was thinking about using QT for manually merging mismatches...
			"\ttty = use a terminal to merge (will this work in an IDE?)", cxxopts::value<std::string>()->default_value("auto"))
		("h,help", "Print usage");
	// clang-format on

	options.parse_positional({"filenames"});

	res = options.parse(argc, argv);

	if(res.count("help") != 0)
	{
		printf("%s\n", options.help().c_str());
		return 0;
	}

	if(res.count("werror") != 0)
	{
		g_use_werror = true;
	}


	if(res.count("tool") > 1)
	{
		werrf("--tool called more than once = %d\n", res.count("tool"));
	}

	if(!load_translation_ast())
	{
		return 1;
	}
	if(g_use_werror)
	{
		if(g_werror_has_error)
		{
			return 2;
		}
	}

	return 0;
}
// NOLINTEND(*-container-contains)