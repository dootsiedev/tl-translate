// This is an independent project of an individual developer. Dear PVS-Studio, please check it.
// PVS-Studio Static Code Analyzer for C, C++, C#, and Java: https://pvs-studio.com
// this is GROSS, but I don't want to copy it so it's better than nothing...
#include "../../code/translation_parser.h"
#include "../../code/util/string_tools.h"

// I should probably globally disable these.
// NOLINTBEGIN(*-container-contains, *-unnecessary-value-param, *-for-range-copy)

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


#include <sys/types.h>
#include <sys/stat.h>

#if defined __WIN32__ || defined _WIN32 || defined _Windows
#if !defined S_ISDIR
#define S_ISDIR(m) (((m) & _S_IFDIR) == _S_IFDIR)
#endif
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
	std::optional<annotated_string> value;
	std::vector<translation_variant> extra;
	// is this TL_TEXT or TL_FORMAT
	bool format_text = false;
	// the line start of the TL_FORMAT.
	// used for get_iterator (as a placeholder for key's annotation... for no reason other than laziness)
	tl_buffer_type::iterator where;
	// The failed to match error will be a real comment, aka // TL_ERROR()
	// because there is no point is parsing it, it's purely for the user to read.
	// it is formatted like a command just in case I need it.
	std::string error_comment;
	entry_ast() = default;
	// C++17 emplace back pain
	entry_ast(
		std::string&& key_,
		std::optional<annotated_string>&& value_,
		std::vector<translation_variant>&& extra_,
		bool format,
		tl_buffer_type::iterator where_)
	: key(std::move(key_))
	, value(std::move(value_))
	, extra(std::move(extra_))
	, format_text(format)
	, where(where_)
	{
	}
};

// TODO: replace with hungarian method
//  https://github.com/jamespayor/weighted-bipartite-perfect-matching/blob/master/test.cpp

struct WeightedBipartiteEdge {
	int left;
	int right;
	int cost;

	WeightedBipartiteEdge() : left(), right(), cost() {}
	WeightedBipartiteEdge(int left, int right, int cost) : left(left), right(right), cost(cost) {}
};
static std::pair<int, std::vector<int> > bruteForceInternal(const int n, const std::vector<WeightedBipartiteEdge> edges, std::vector<bool>& leftMatched, std::vector<bool>& rightMatched, const int edgeUpTo = 0, const int matchCount = 0) {
	if (matchCount == n) {
		return std::make_pair(0, std::vector<int>());
	}

	int bestCost = 1 << 20;
	std::vector<int> bestEdges;
	for (int edgeIndex = edgeUpTo; edgeIndex < edges.size(); ++edgeIndex) {
		const WeightedBipartiteEdge& edge = edges[edgeIndex];
		if (!leftMatched[edge.left] && !rightMatched[edge.right]) {
			leftMatched[edge.left] = true;
			rightMatched[edge.right] = true;
			std::pair<int, std::vector<int> > remainder = bruteForceInternal(n, edges, leftMatched, rightMatched, edgeIndex + 1, matchCount + 1);
			leftMatched[edge.left] = false;
			rightMatched[edge.right] = false;

			if (remainder.first + edge.cost < bestCost) {
				bestCost = remainder.first + edge.cost;
				bestEdges = remainder.second;
				bestEdges.push_back(edgeIndex);
			}
		}
	}

	return std::make_pair(bestCost, bestEdges);
}

// fuzzy matcher (this is for matching function names, not whole sentances! but I still want to try)
// https://github.com/philj56/fuzzy-match/tree/main
namespace fuzzy_match_stuff{


/*
 * Calculate the score for a single matching letter.
 * The scoring system is taken from fts_fuzzy_match v0.2.0 by Forrest Smith,
 * which is licensed to the public domain.
 *
 * The factors affecting score are:
 *   - Bonuses:
 *     - If there are multiple adjacent matches.
 *     - If a match occurs after a separator character.
 *     - If a match is uppercase, and the previous character is lowercase.
 *
 *   - Penalties:
 *     - If there are letters before the first match.
 *     - If there are superfluous characters in str (already accounted for).
 */
int32_t compute_score(int32_t jump, bool first_char, const char * match)
{
	const int adjacency_bonus = 15;
	const int separator_bonus = 30;
	const int camel_bonus = 30;
	const int first_letter_bonus = 15;

	const int leading_letter_penalty = -5;
	const int max_leading_letter_penalty = -15;

	int32_t score = 0;

	/* Apply bonuses. */
	if (!first_char && jump == 0) {
		score += adjacency_bonus;
	}
	if (!first_char || jump > 0) {
		if (isupper((unsigned char)*match)
		   && islower((unsigned char)*(match - 1))) {
			score += camel_bonus;
		}
		if (isalnum((unsigned char)*match)
		   && !isalnum((unsigned char)*(match - 1))) {
			score += separator_bonus;
		}
	}
	if (first_char && jump == 0) {
		/* Match at start of string gets separator bonus. */
		score += first_letter_bonus;
	}

	/* Apply penalties. */
	if (first_char) {
		score += std::max(leading_letter_penalty * jump,
					 max_leading_letter_penalty);
	}

	return score;
}
/*
 * Recursively match the whole of pattern against str.
 * The score parameter is the score of the previously matched character.
 *
 * This reaches a maximum recursion depth of strlen(pattern) + 1. However, the
 * stack usage is small (the maximum I've seen on x86_64 is 144 bytes with
 * gcc -O3), so this shouldn't matter unless pattern contains thousands of
 * characters.
 */

int musl_strncasecmp(const char *_l, const char *_r, size_t n)
{
	const unsigned char *l=(unsigned char *)_l, *r=(unsigned char *)_r;
	if (!n--) return 0;
	for (; *l && *r && n && (*l == *r || tolower(*l) == tolower(*r)); l++, r++, n--);
	return tolower(*l) - tolower(*r);
}
char *musl_strcasestr(const char *h, const char *n)
{
	size_t l = strlen(n);
	for (; *h; h++) if (!musl_strncasecmp(h, n, l)) return (char *)h;
	return 0;
}

int32_t fuzzy_match_recurse(
	const char * pattern,
	const char * str,
	int32_t score,
	bool first_char)
{
	if (*pattern == '\0') {
		/* We've matched the full pattern. */
		return score;
	}

	const char *match = str;
	const char search[2] = { *pattern, '\0' };

	int32_t best_score = INT32_MIN;

	/*
	 * Find all occurrences of the next pattern character in str, and
	 * recurse on them.
	 */
	while ((match = musl_strcasestr(match, search)) != NULL) {
		int32_t subscore = fuzzy_match_recurse(
			pattern + 1,
			match + 1,
			compute_score(match - str, first_char, match),
			false);
		best_score = std::max(best_score, subscore);
		match++;
	}

	if (best_score == INT32_MIN) {
		/* We couldn't match the rest of the pattern. */
		return INT32_MIN;
	} else {
		return score + best_score;
	}
}

} // namespace fuzzy_match_stuff
/*
 * Returns score if each character in pattern is found sequentially within str.
 * Returns INT32_MIN otherwise.
 */
int32_t fuzzy_match(const char * pattern, const char * str)
{
	const int unmatched_letter_penalty = -1;
	const size_t slen = strlen(str);
	const size_t plen = strlen(pattern);
	int32_t score = 100;

	if (*pattern == '\0') {
		return score;
	}
	if (slen < plen) {
		return INT32_MIN;
	}

	/* We can already penalise any unused letters. */
	score += unmatched_letter_penalty * (int32_t)(slen - plen);

	/* Perform the match. */
	score = fuzzy_match_stuff::fuzzy_match_recurse(pattern, str, score, true);

	return score;
}

struct load_ast_handler : public tl_parse_observer, public tl_parse_state
{
	// I need to hold onto the file because I check for format specifier errors
	// which means I need to hold onto the annotation iterator,
	// which needs the file for printing the line.
	std::string file_data;
	std::vector<translation_variant> extra_stack;
	std::vector<entry_ast> ast_root;
	tl_header header;
	// I don't actually use the ast... until you merge, but I could have just passed it...
	load_ast_handler* patch_file = nullptr;

	typedef decltype(ast_root)::iterator ast_iter;
	typedef decltype(ast_root)::const_iterator c_ast_iter;

	bool is_patch_file() const
	{
		return patch_file == nullptr;
	}

	void on_warning(const char* msg) override
	{
		werr(msg);
	}
	void on_error(const char* msg) override
	{
		serr(msg);
	}

	TL_RESULT on_header(tl_parse_state& tl_state, tl_header& header_) override
	{
		(void)tl_state;
		header = std::move(header_);
		return TL_RESULT::SUCCESS;
	}

	TL_RESULT on_insert(
		tl_parse_state& tl_state,
		std::string& key,
		std::optional<annotated_string>& value,
		bool format)
	{
		// NOTE: is the key captured as a reference or a copy if I [key]?
		auto find_it =
			std::find_if(ast_root.begin(), ast_root.end(), [&key](const entry_ast& ast) -> bool {
				return ast.key == key;
			});

		if(find_it != ast_root.end())
		{
			if(find_it->format_text != format)
			{
				tl_state.report_error("duplicate mismatching TL_TEXT with TL_FORMAT!");
				return TL_RESULT::FAILURE;
			}
			if(!is_patch_file())
			{
				// I don't return TL_RESULT::WARNING because it does nothing.
				tl_state.report_warning("merging duplicate (this shouldn't happen)");
			}
			// add the info
			for(auto& extra : extra_stack)
			{
				find_it->extra.emplace_back(std::move(extra));
			}
			extra_stack.clear();
		}
		else
		{
			ast_root.emplace_back(
				std::move(key),
				std::move(value),
				std::move(extra_stack),
				format,
				tl_state.get_iterator());
		}

		return TL_RESULT::SUCCESS;
	}

	TL_RESULT on_translation(
		tl_parse_state& tl_state, std::string& key, std::optional<annotated_string>& value) override
	{
		return on_insert(tl_state, key, value, false);
	}
#ifdef TL_ENABLE_FORMAT
	TL_RESULT on_format(
		tl_parse_state& tl_state, std::string& key, std::optional<annotated_string>& value) override
	{
		if(value.has_value() && !tl_state.check_printf_specifiers(key, value->data, value->iter))
		{
			return TL_RESULT::FAILURE;
		}
		return on_insert(tl_state, key, value, true);
	}
#endif
	TL_RESULT on_comment(tl_parse_state& tl_state, std::string& comment) override
	{
		(void)tl_state;
		extra_stack.emplace_back(tl_comment{std::move(comment)});
		return TL_RESULT::SUCCESS;
	}

	TL_RESULT on_info(tl_parse_state& tl_state, tl_info& info) override
	{
		(void)tl_state;
		extra_stack.emplace_back(std::move(info));
		return TL_RESULT::SUCCESS;
	}
	TL_RESULT on_no_match(tl_parse_state& tl_state, tl_no_match& no_match) override
	{
		(void)tl_state;
		extra_stack.emplace_back(std::move(no_match));
		return TL_RESULT::SUCCESS;
	}

	//
	// tl_parse_state
	//

	std::optional<std::string> formatting_error_message;
	tl_buffer_type::iterator formatting_iter;
	void report_error(const char* msg) override
	{
		// TODO: I am pretty sure that I need to format the text here... AGHHHH
		formatting_error_message = msg;
		info(msg);
	}
	void report_warning(const char* msg) override
	{
		// TODO: I have not implemented werror... should I just throw?
		info(msg);
	}
	void report_error(const char* msg, tl_buffer_type::iterator eiter) override
	{
		(void) eiter;
		formatting_error_message = msg;
		info(msg);
	}
	void report_warning(const char* msg, tl_buffer_type::iterator eiter) override
	{
		(void) eiter;
		info(msg);
	}
	tl_buffer_type::iterator get_iterator() override
	{
		return formatting_iter;
	}

	// this is for merging key to key.
	bool check_printf(
		c_ast_iter missing_patch, ast_iter missing_old_text) // NOLINT(*-unnecessary-value-param)
	{
		ASSERT(!is_patch_file());
		ASSERT(missing_patch->format_text);
		ASSERT(missing_old_text->format_text);
		formatting_iter = missing_old_text->where;
		if(!check_printf_specifiers(
			   missing_patch->key, missing_old_text->key, missing_old_text->where))
		{
			ASSERT(formatting_error_message.has_value());
			missing_old_text->error_comment = *formatting_error_message;
			// TODO: gotta add the file data...
			// wrapper.where = missing_patch->where;
			// wrapper.report_error("from here");
			return false;
		}
		return true;
	}

	// This is messy, not too happy about it.
	template<class T>
	static std::vector<T> find_missing(std::vector<entry_ast>& to, std::vector<entry_ast>& from, bool merge_to)
	{
		std::vector<T> missing;
		for(auto it = from.begin(); it != from.end(); ++it)
		{
			auto find_it = std::find_if(to.begin(), to.end(), [it](const entry_ast& ast) -> bool {
				return ast.key == it->key && ast.format_text != it->format_text;
			});
			if(find_it != to.end())
			{
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

	enum class merge_result
	{
		MATCH_FOUND,
		MATCH_NOT_FOUND,
		// this is only for werror atm.
		// specifier matching fail errors get passed as comments in the file.
		// but I am thinking about just using C++ exceptions for werror...
		// considering how much I depend on libraries that use exceptions...
		MATCH_FAILURE
	};

	// returns true if merged.
	merge_result auto_merge(c_ast_iter missing_patch, ast_iter missing_old_text)
	{
		if(missing_patch->format_text != missing_old_text->format_text)
		{
			return merge_result::MATCH_NOT_FOUND;
		}
		if(missing_patch->format_text)
		{
			ASSERT(missing_old_text->format_text);
			if(!check_printf(missing_patch, missing_old_text))
			{
				// TODO: make this only for werror
				// return merge_result::MATCH_FAILURE;
				return merge_result::MATCH_NOT_FOUND;
			}
		}

		for(auto& extra : missing_old_text->extra)
		{
			const auto* info_result = std::get_if<tl_info>(&extra);
			if(info_result != nullptr)
			{
				for(const auto& jextra : missing_patch->extra)
				{
					const auto* jinfo_result = std::get_if<tl_info>(&jextra);
					if(jinfo_result != nullptr)
					{
						if(info_result->source_file != jinfo_result->source_file)
						{
							continue;
						}
						if(info_result->function != jinfo_result->function)
						{
							continue;
						}
						if(info_result->line != jinfo_result->line)
						{
							continue;
						}
						// I will let the column pass.

						return merge_result::MATCH_FOUND;
					}
				}
			}
		}
		return merge_result::MATCH_NOT_FOUND;
	}

	// add the patch to the ast.
	bool merge()
	{
		ASSERT(patch_file != nullptr);

		// possibly new translations or failed to match.
		// note that all the iterators come from the second parameter.
		std::vector<c_ast_iter> missing_patches =
			find_missing<c_ast_iter>(ast_root, patch_file->ast_root, true);
		// possibly removed text or failed to match
		std::vector<ast_iter> missing_old_text =
			find_missing<ast_iter>(patch_file->ast_root, ast_root, false);

		/*
		for(auto it : missing_patches)
		{
			info(it->key.c_str());
		}
		for(auto it : missing_old_text)
		{
			info(it->key.c_str());
		}*/
		auto patch_ast_end = patch_file->ast_root.cend();
		bool success = true;
		auto rem_it =
			std::remove_if(missing_old_text.begin(), missing_old_text.end(), [&](auto it) {
				for(auto& jt : missing_patches)
				{
					if(jt == patch_file->ast_root.end())
					{
						continue;
					}
					switch(auto_merge(jt, it))
					{
					case merge_result::MATCH_FOUND:
						infof(
							"found merge with: `%s` == `%s`\n",
							escape_string(jt->key).c_str(),
							escape_string(it->key).c_str());
						it->key = jt->key;
						//it->extra.clear();
						it->extra = jt->extra;
						// mark for deletion.
						jt = patch_ast_end;
						return true;
					case merge_result::MATCH_NOT_FOUND:
						break;
						// TODO: I think this will only be for werror, and I might use exceptions...
					case merge_result::MATCH_FAILURE: success = false; return false;
					}
				}
				return false;
			});
		if(!success)
		{
			return false;
		}
		missing_old_text.erase(rem_it, missing_old_text.end());

		// delete the other pairs that were invalidated.
		missing_patches.erase(
			std::remove(missing_patches.begin(), missing_patches.end(), patch_ast_end),
			missing_patches.end());


		// TODO: do the AI merging here.
		//  if I use the flag embedding, it will give me a matrix of similarities.
		//  I could just have a threshold and if it passes, pick the best scoring value
		//  but my tiny brain is too small to know if this is a harder problem than I expect,
		//  or if I am over thinking it.
		//  It's possible that I must use the hungarian method
		//  (But, hungarian method uses unbounded numbers, the similarity values are normalized)
		//  also there is the brute force method which seems to be O(n^4)

		// add TL_NO_MATCH
		for(auto it : missing_old_text)
		{
			bool found = false;
			for(auto& extra : it->extra)
			{
				auto* result = std::get_if<tl_no_match>(&extra);
				if(result != nullptr)
				{
					found = true;
					break;
				}
			}
			if(!found)
			{
				it->extra.insert(it->extra.begin(), tl_no_match{header.date, header.git_hash});
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
			serrf(
				"Error expected TL() before TL_END(), leftover values! (%zu)\n",
				extra_stack.size());
			return false;
		}
		return true;
	}
};

#define checked_fprintf(file, ...)                   \
	do                                               \
	{                                                \
		if(fprintf(file, __VA_ARGS__) < 0)           \
		{                                            \
			serrf("fprintf: %s\n", strerror(errno)); \
			return false;                            \
		}                                            \
	} while(0)
#define checked_fputs(text, file)                  \
	do                                             \
	{                                              \
		if(fputs(text, file) < 0)                  \
		{                                          \
			serrf("fputs: %s\n", strerror(errno)); \
			return false;                          \
		}                                          \
	} while(0)

struct format_ast_visitor
{
	FILE* file = nullptr;
	explicit format_ast_visitor(FILE* file_)
	: file(file_)
	{
	}
	bool operator()(tl_comment& comment)
	{
		ASSERT(file != nullptr);
		std::string escape_buffer;
		if(!escape_string(escape_buffer, comment.comment))
		{
			return false;
		}
		checked_fprintf(file, "TL_COMMENT(\"%s\")\n", escape_buffer.c_str());

		return true;
	}
	bool operator()(tl_info& info)
	{
		ASSERT(file != nullptr);

		// TODO: I don't check for escaped string, but I should make that a parser rule.
		checked_fprintf(
			file,
			"TL_INFO(\"%s\", \"%s\", %d, %d)\n",
			info.function.c_str(),
			info.source_file.c_str(),
			info.line,
			info.column);
		return true;
	}
	bool operator()(tl_no_match& no_match)
	{
		ASSERT(file != nullptr);

		checked_fprintf(
			file,
			"TL_NO_MATCH(\"%s\", \"%s\")\n",
			no_match.date.c_str(),
			no_match.git_hash.c_str());
		return true;
	}
};

// TODO: I should add a string for the file name for better errors.
static bool dump_formatted_ast(FILE* file, std::vector<entry_ast>& ast_root, tl_header& header)
{
	checked_fputs("// this is a file generated by merge-strings\n\n", file);

	checked_fprintf(
		file,
		"TL_START(%s, %s, \"%s\", \"%s\", \"%s\")\n\n",
		header.long_name.c_str(),
		header.short_name.c_str(),
		header.native_name.c_str(),
		header.date.c_str(),
		header.git_hash.c_str());

	format_ast_visitor astVisitor(file);
	for(auto& entry : ast_root)
	{
		if(!entry.error_comment.empty())
		{
			checked_fprintf(file, "// TL_ERROR(%s)\n\n", entry.error_comment.c_str());
		}
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
			value_buffer.reserve(entry.value->data.size() + 3);
			value_buffer += '\"';
			if(!escape_string(value_buffer, entry.value->data))
			{
				return false;
			}
			value_buffer += '\"';
		}
		else
		{
			value_buffer = "NULL";
		}

		checked_fprintf(
			file,
			entry.format_text ? "TL_FORMAT(\"%s\", %s)\n\n" : "TL_TEXT(\"%s\", %s)\n\n",
			key_buffer.c_str(),
			value_buffer.c_str());
	}
	checked_fputs("TL_END()\n", file);

	if(ferror(file) != 0)
	{
		serr("error indicator set\n");
		return false;
	}

	return true;
}


static bool slurp_stdio(std::string& out, FILE* fp, const char* name)
{
	ASSERT(fp != NULL);
	ASSERT(name != NULL);
	if(ferror(fp) != 0)
	{
		serrf("error indicator set: `%s`\n", name);
		return false;
	}
	struct stat info;
	int ret = fstat(fileno(fp), &info);
	if(ret != 0)
	{
		serrf("fstat error: `%s`, reason: %s (return: %d)\n", name, strerror(errno), ret);
		return false;
	}
	out.resize(info.st_size);
	size_t bytes_read = fread(out.data(), 1, info.st_size, fp);
	if(bytes_read != info.st_size)
	{
		serrf("fread error: `%s`, reason: %s (return: %zu)\n", name, strerror(errno), bytes_read);
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
		serrf("Failed to read `%s`, reason: %s\n", path, strerror(errno));
		return false;
	}
	bool success = slurp_stdio(out, fp, path);
	fclose(fp);
	return success;
}

// it still uses exceptions, but now instead it will print an error then abort.
//#define CXXOPTS_NO_EXCEPTIONS
#include <cxxopts.hpp>

static cxxopts::Options options("test", "A brief description");
static cxxopts::ParseResult res;

static bool load_translation_ast()
{
	if(res.count("patch") == 0)
	{
		serr("error: --patch not defined\n");
		return false;
	}

	std::string merge_patch = res["patch"].as<std::string>();

	load_ast_handler patch_ast;

	infof("info: loading patch: %s\n", merge_patch.c_str());
	if(!slurp_file(patch_ast.file_data, merge_patch.c_str()))
	{
		return false;
	}

	// now merge the patch to the ast of the file
	// I just add the contents to the end of the previous file.
	// merge_apply_patch_handler after_patch;
	// after_patch.patch_root = std::move(patch.ast_root);

	if(!parse_translation_file(patch_ast, patch_ast.file_data, merge_patch.c_str()))
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
		struct stat sb;
		if(stat(dir_path, &sb) == 0)
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
	if(res.count("stage_dir") != 0)
	{
		stage_directory = res["stage_dir"].as<std::string>();
		const char* dir_path = stage_directory->c_str();
		struct stat sb;
		if(stat(dir_path, &sb) == 0)
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

		load_ast_handler ast;
		ast.patch_file = &patch_ast;

		{
			FILE* to_fp = fopen(path.c_str(), "rb");

			if(to_fp == NULL)
			{
				serrf("error: failed to open: `%s`, reason: %s\n", path.c_str(), strerror(errno));
				return false;
			}

			// copy the file into the string
			if(!slurp_stdio(ast.file_data, to_fp, path.c_str()))
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

		if(!parse_translation_file(ast, ast.file_data, path.c_str()))
		{
			return false;
		}
		if(!ast.post_parse())
		{
			return false;
		}

		if(!ast.merge())
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
		werrf("--tool called more than once = %zu\n", res.count("tool"));
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
// NOLINTEND(*-container-contains, *-unnecessary-value-param, *-for-range-copy)