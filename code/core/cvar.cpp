// This is an independent project of an individual developer. Dear PVS-Studio, please check it.
// PVS-Studio Static Code Analyzer for C, C++, C#, and Java: https://pvs-studio.com
#include "global_pch.h"
#include "global.h"

#include <climits>

#include "cvar.h"

#ifndef DISABLE_SDL
// I shouldn't need this, but I need to get the window for opening a dialog box.
#include "app.h"
#endif // DISABLE_SDL

// for reading files, since I like the stream API.
#include "../datastructures/BS_stream.h"
#include "../util/escape_string.h"

// unfortunately std::from_chars for doubles doesn't have great support on compilers...
// so you need a pretty decent version of gcc or clang...
// I want this because I can use non-null terminating strings for cvar_read....
// does this decimals the same way ostream does? I really like how the trailing zeros get cut.
// #include <charconv>

// TODO: I could use typeid+RTTI to print the type name of the cvar in errors,
//  but I would prefer to be compatible with no RTTI, jolt physics has static rtti I can look at.

V_cvar* g_cvar_modified_list_head = NULL;

std::map<const char*, V_cvar&, cmp_str>& get_convars()
{
	static std::map<const char*, V_cvar&, cmp_str> convars;
	return convars;
}

cvar_int::cvar_int(
	const char* key,
	int value,
	const char* comment,
	CVAR_T type,
	cvar_cond_cb_type cb,
	const char* file,
	int line)
: V_cvar(key, comment, type, file, line)
, cond_cb(cb)
, _internal_data(value)
{
	if(internal_cvar_type == CVAR_T::STARTUP)
	{
		cvar_commit_data = _internal_data;
	}
	auto [it, success] = get_convars().try_emplace(key, *this);
	(void)success;
	// this shouldn't be possible.
	ASSERT_M(success && "cvar already registered", cvar_key);
}
cvar_int::cvar_int(const char* key, cvar_init_cb_type cb, const char* file, int line)
: V_cvar(key, file, line)
, init_cb(cb)
{
	auto [it, success] = get_convars().try_emplace(key, *this);
	(void)success;
	ASSERT_M(success && "cvar already registered", cvar_key);
}
bool cvar_int::set_data(int i SRC_LOC2_IMPL)
{
	ASSERT_M_SRC_LOC(cvar_init_once && "use of cvar before init", cvar_key);
	if(internal_cvar_type == CVAR_T::DISABLED)
	{
		serrf("Error: disabled cvar (hard coded) %s(%d) = %d\n", cvar_key, internal_data(), i);
		return false;
	}
	V_cvar* blame = cond_cb != nullptr ? cond_cb(i) : nullptr;
	if(blame != nullptr)
	{
		slogf(
			"Warning: disabled cvar %s(%d) = %d (blame: %s == %s)\n",
			cvar_key,
			internal_data(),
			i,
			blame->cvar_key,
			blame->cvar_write().c_str());
	}

	if(internal_cvar_type == CVAR_T::STARTUP)
	{
		cvar_commit_data = i;
		if(cvar_validate_lock)
		{
			_internal_data = i;
		}
	}
	else
	{
		_internal_data = i;
	}

	return true;
}
bool cvar_int::cvar_read(const char* buffer)
{
	char* end_ptr;

	pop_errno_t pop_errno;

	// unfortunately there is no strtoi, and longs could be 4 or 8 bytes...
	// NOLINTNEXTLINE(google-runtime-int)
	long value = strtol(buffer, &end_ptr, 10);

	if(errno == ERANGE)
	{
		serrf("+%s: cvar value out of range: \"%s\"\n", cvar_key, buffer);
		return false;
	}
#if INT_MAX < LONG_MAX
	static_assert(
		std::numeric_limits<int>::max() == std::numeric_limits<decltype(_internal_data)>::max());
	const int cmax = std::numeric_limits<int>::max();
	const int cmin = std::numeric_limits<int>::min();
	if(value > cmax || value < cmin)
	{
		serrf(
			"+%s: cvar value out of range: \"%s\" min: %d, max: %d, result: %ld\n",
			cvar_key,
			buffer,
			cmin,
			cmax,
			value);
		return false;
	}
#endif
	if(end_ptr == buffer)
	{
		serrf("+%s: cvar value not valid numeric input: \"%s\"\n", cvar_key, buffer);
		return false;
	}

	if(*end_ptr != '\0')
	{
		slogf("+%s: warning cvar value extra characters on input: \"%s\"\n", cvar_key, buffer);
	}

	// NOLINTNEXTLINE(bugprone-narrowing-conversions)
	return set_data(value);
}
std::string cvar_int::cvar_write()
{
	std::string str;
	str_asprintf(str, "%d", internal_data());
	return str;
}

cvar_double::cvar_double(
	const char* key,
	double value,
	const char* comment,
	CVAR_T type,
	cvar_cond_cb_type cb,
	const char* file,
	int line)
: V_cvar(key, comment, type, file, line)
, cond_cb(cb)
, _internal_data(value)
{
	if(internal_cvar_type == CVAR_T::STARTUP)
	{
		cvar_commit_data = _internal_data;
	}
	auto [it, success] = get_convars().try_emplace(key, *this);
	(void)success;
	// this shouldn't be possible.
	ASSERT_M(success && "cvar already registered", cvar_key);
}

cvar_double::cvar_double(const char* key, cvar_init_cb_type cb, const char* file, int line)
: V_cvar(key, file, line)
, init_cb(cb)
{
	auto [it, success] = get_convars().try_emplace(key, *this);
	(void)success;
	ASSERT_M(success && "cvar already registered", cvar_key);
}

bool cvar_double::set_data(double i SRC_LOC2_IMPL)
{
	ASSERT_M_SRC_LOC(cvar_init_once && "use of cvar before init", cvar_key);
	if(internal_cvar_type == CVAR_T::DISABLED)
	{
		serrf("Error: disabled cvar (hard coded) %s(%g) = %g\n", cvar_key, internal_data(), i);
		return false;
	}
	V_cvar* blame = cond_cb != nullptr ? cond_cb(i) : nullptr;
	if(blame != nullptr)
	{
		slogf(
			"Warning: disabled cvar %s(%g) = %g (blame: %s == %s)\n",
			cvar_key,
			internal_data(),
			i,
			blame->cvar_key,
			blame->cvar_write().c_str());
	}
	if(internal_cvar_type == CVAR_T::STARTUP)
	{
		cvar_commit_data = i;
		if(cvar_validate_lock)
		{
			_internal_data = i;
		}
	}
	else
	{
		_internal_data = i;
	}
	return true;
}
bool cvar_double::cvar_read(const char* buffer)
{
	char* end_ptr;

	pop_errno_t pop_errno;
	double value = strtod(buffer, &end_ptr);
	if(errno == ERANGE)
	{
		serrf("+%s: cvar value out of range: \"%s\"\n", cvar_key, buffer);
		return false;
	}
	if(end_ptr == buffer)
	{
		serrf("+%s: cvar value not valid numeric input: \"%s\"\n", cvar_key, buffer);
		return false;
	}

	if(*end_ptr != '\0')
	{
		slogf("+%s: warning cvar value extra characters on input: \"%s\"\n", cvar_key, buffer);
	}

	return set_data(value);
}
std::string cvar_double::cvar_write()
{
	std::string str;
	str_asprintf(str, "%g", internal_data());
	return str;
}

cvar_string::cvar_string(
	const char* key,
	std::string value,
	const char* comment,
	CVAR_T type,
	cvar_cond_cb_type cb,
	const char* file,
	int line)
: V_cvar(key, comment, type, file, line)
, cond_cb(cb)
, _internal_data(std::move(value))
{
	if(internal_cvar_type == CVAR_T::STARTUP)
	{
		cvar_commit_data = _internal_data;
	}
	cvar_save_quotes = true;
	auto [it, success] = get_convars().try_emplace(key, *this);
	(void)success;
	// this shouldn't be possible.
	ASSERT_M(success && "cvar already registered", cvar_key);
}
cvar_string::cvar_string(const char* key, cvar_init_cb_type cb, const char* file, int line)
: V_cvar(key, file, line)
, init_cb(cb)
{
	cvar_save_quotes = true;
	auto [it, success] = get_convars().try_emplace(key, *this);
	(void)success;
	ASSERT_M(success && "cvar already registered", cvar_key);
}

bool cvar_string::set_data(std::string&& str SRC_LOC2_IMPL)
{
	ASSERT_M_SRC_LOC(cvar_init_once && "use of cvar before init", cvar_key);
	if(internal_cvar_type == CVAR_T::DISABLED)
	{
		serrf(
			"Error: disabled cvar (hard coded) %s(%s) = %s\n",
			cvar_key,
			internal_data().c_str(),
			str.c_str());
		return false;
	}
	V_cvar* blame = cond_cb != nullptr ? cond_cb(str) : nullptr;
	if(blame != nullptr)
	{
		slogf(
			"Warning: disabled cvar %s(%s) = %s (blame: %s == %s)\n",
			cvar_key,
			internal_data().c_str(),
			str.c_str(),
			blame->cvar_key,
			blame->cvar_write().c_str());
	}
	if(internal_cvar_type == CVAR_T::STARTUP)
	{
		cvar_commit_data = std::move(str);
		if(cvar_validate_lock)
		{
			_internal_data = cvar_commit_data;
		}
	}
	else
	{
		_internal_data = std::move(str);
	}
	return true;
}
bool cvar_string::cvar_read(const char* buffer)
{
	ASSERT_M(buffer, cvar_key);
	return set_data(buffer);
}
std::string cvar_string::cvar_write()
{
	return internal_data();
}

static void cvar_init()
{
	// if you reload the cvar files, this needs to be set to false.
	// TODO: I should reload the cvar files if I reload the window.
	/*for(const auto& it : get_convars())
	{
		it.second.cvar_init_once = false;
	}
	*/
	for(const auto& it : get_convars())
	{
		it.second.cvar_init_values();
	}
}

static void print_cvar(V_cvar& cvar, bool debug = false)
{
	std::string value = cvar.cvar_write();
	const char* type = NULL;
	switch(cvar.cvar_type())
	{
	case CVAR_T::RUNTIME: type = ""; break;
	case CVAR_T::STARTUP: type = " [STARTUP]"; break;
	case CVAR_T::DEFERRED: type = " [DEFERRED]"; break;
	case CVAR_T::READONLY: type = " [READONLY]"; break;
	case CVAR_T::DISABLED: type = " [DISABLED]"; break;
	default: ASSERT("unreachable" && false);
	}
	slogf("%s%s: \"%s\"\n", cvar.cvar_key, type, value.c_str());
	if(debug)
	{
		slogf("\tFile: %s\n", cvar.cvar_debug_file);
		slogf("\tLine: %d\n", cvar.cvar_debug_line);
	}
	slogf("\t%s\n", cvar.cvar_comment);
}

int cvar_arg(CVAR_T flags_req, int argc, const char* const* argv, bool console)
{
	int i = 0;
	for(; i < argc; ++i)
	{
		const char* name = argv[i];
		bool save_value = false;
		if(name[0] == '+')
		{
			name = argv[i] + 1;
			save_value = true;
		}

		auto it = get_convars().find(name);
		if(it == get_convars().end())
		{
			if(console)
			{
				serrf(
					"ERROR: unknown command or cvar\n"
					"expression: `%s`\n",
					argv[i]);
			}
			else
			{
				serrf("ERROR: cvar not found: `%s`\n", argv[i]);
			}
			return -1;
		}

		V_cvar& cv = it->second;

		if(!save_value && console && argc == 1)
		{
			// print the value.
			print_cvar(it->second);
			continue;
		}

		bool ignore = false;

		// go to next argument.
		i++;
		if(i >= argc)
		{
			serrf("ERROR: cvar assignment missing: `%s`\n", name);
			// NOTE: should I return -2 so that I can continue parsing and print more errors?
			return -1;
		}

		// I want to check if this is a hard coded disabled cvar.
		// I ignore it because there is nothing you can do,
		// the value is based on compile time features.
		if(cv.internal_cvar_type == CVAR_T::DISABLED)
		{
			slogf("warning: cvar disabled (hard coded): `%s`\n", name);
			ignore = true;
		}

		// ignore read only values.
		if(cv.cvar_type() == CVAR_T::READONLY)
		{
			slogf("warning: cvar readonly: `%s`\n", name);
			ignore = true;
		}

		switch(flags_req)
		{
		case CVAR_T::RUNTIME:
			// I don't think flags_req is ever set to DEFERRED,
			// and I don't know what that would look like
			// (maybe a OPENGL context specific flag? but that's like startup)
		case CVAR_T::DEFERRED:
			if(cv.cvar_type() == CVAR_T::STARTUP)
			{
				ASSERT_M(!ignore, cv.cvar_key);
				if(console && !save_value)
				{
					// this warning is super annoying. but it can catch stupid issues with caching.
					slogf(
						"warning: '%s' is a startup value (cached), and not saved (add '+')\n",
						name);
				}
				else
				{
					slogf("warning: '%s' is a startup value (cached)\n", name);
				}
			}
			break;
		case CVAR_T::STARTUP: break;
		default: ASSERT_M(false && "flags_req not implemented", cv.cvar_key);
		}

		if(!ignore)
		{
			std::string old_value = cv.cvar_write();
			if(!cv.cvar_read(argv[i]))
			{
				return -1;
			}

			if(console && save_value)
			{
				cv.cvar_save_changes();
			}
			else
			{
				cv.cvar_from_file = true;
			}
			slogf("%s (%s) = %s\n", name, old_value.c_str(), argv[i]);
		}
	}
	return i;
}

// portable strtok
// probably should put this into global.h if I used it more.
static char* musl_strtok_r(char* __restrict s, const char* __restrict sep, char** __restrict p)
{
	if(!s && !(s = *p)) return NULL;
	s += strspn(s, sep);
	if(!*s) return *p = 0;
	*p = s + strcspn(s, sep);
	if(**p)
		*(*p)++ = 0;
	else
		*p = 0;
	return s;
}

static bool cvar_split_line(std::vector<const char*>& arguments, char* line)
{
	char* token = line;
	bool in_quotes = false;
	while(token != NULL)
	{
		char* next_quote = strpbrk(token, "\"\\");
		if(next_quote != NULL)
		{
			// skip escaped quotes
			if(in_quotes)
			{
				do
				{
					if(*next_quote == '\"')
					{
						// found the quote
						break;
					}
					if(*next_quote++ == '\\')
					{
						if(*next_quote++ == '\"')
						{
							// keep searching for the quote
						}
					}
					next_quote = strpbrk(next_quote, "\"\\");
				} while(next_quote != NULL);
			}

			if(next_quote != NULL && *next_quote == '\"')
			{
				*next_quote++ = '\0';
				// note the in_quotes condition is the opposite
				// because I toggle before the condition.
				in_quotes = !in_quotes;
				if(!in_quotes)
				{
					if(!rem_escape_string(token))
					{
						return false;
					}
					arguments.push_back(token);
					token = next_quote;
					continue;
				}
			}
		}

		// split by whitespace
		const char* delim = " ";
		char* next_token = NULL;
		token = musl_strtok_r(token, delim, &next_token);
		while(token != NULL)
		{
			arguments.push_back(token);
			token = musl_strtok_r(NULL, delim, &next_token);
		}

		token = next_quote;
	}

	if(in_quotes)
	{
		serrf("missing quote pair\n");
		return false;
	}
	return true;
}

bool cvar_line(CVAR_T flags_req, char* line, bool console)
{
	std::vector<const char*> arguments;
	if(!cvar_split_line(arguments, line))
	{
		return false;
	}

	const char** argv = arguments.data();
	// NOLINTNEXTLINE(bugprone-narrowing-conversions)
	int argc = arguments.size();
	while(argc > 0)
	{
		int ret = cvar_arg(flags_req, argc, argv, console);
		if(ret == -1)
		{
			return false;
		}
		argc -= ret;
		argv += ret;
	}

	return true;
}

// the function is:
// bool (*)(char* text, bool empty)
template<class Func>
static bool cvar_parse_file_lines(RWops* infile, Func line_callback)
{
	char inbuffer[1000];
	BS_ReadStream reader(infile, inbuffer, sizeof(inbuffer));

	char line_buf[1000 + 1];

	char* pos = line_buf;
	// the -1 is not necessary because the stream will put null in for you when EOF is reached.
	// and the \n is replaced with null. but the -1 makes me feel safe :)
	char* end = line_buf + sizeof(line_buf) - 1;
	while(pos < end)
	{
		*pos = reader.Take();
		if(*pos == '\n')
		{
			*pos = '\0';
			if(line_buf[0] == '#' || pos == line_buf)
			{
				// comment or empty
				if(!line_callback(line_buf, true))
				{
					return false;
				}
			}
			else
			{
				if(!line_callback(line_buf, false))
				{
					return false;
				}
			}
			pos = line_buf;

			// end of file ending with newline.
			// without this I will add an extra newline
			// to the end of the file every time I save.
			if(reader.Peek() == '\0')
			{
				break;
			}
		}
		else if(*pos == '\r')
		{
			// don't parse this.
			// windows will insert it for newlines.
			// I think this could be removed by opening in text mode, but maybe not on linux(?)
		}
		else if(*pos == '\0')
		{
			if(line_buf[0] == '#' || pos == line_buf)
			{
				if(!line_callback(line_buf, true))
				{
					return false;
				}
				break;
			}
			if(!line_callback(line_buf, false))
			{
				return false;
			}
			break;
		}
		else
		{
			++pos;
		}
	}
	if(pos == end)
	{
		size_t max_line_size = sizeof(line_buf) - 1;
		serrf("ERROR(%s): line too long: %s (max: %zu)\n", __func__, infile->name(), max_line_size);
		return false;
	}
	return true;
}

bool cvar_file(CVAR_T flags_req, RWops* file)
{
	ASSERT(file != NULL);

	int line_num = 1;
	std::string line_copy;

	return cvar_parse_file_lines(file, [&](char* line, bool empty) -> bool {
		if(!empty)
		{
			line_copy = line;
			if(!cvar_line(flags_req, line))
			{
				// TODO: I could return a number column offset instead of a bool
				serrf("in %s:%d\n%s\n^\n", file->name(), line_num, line_copy.c_str());
				return false;
			}
		}
		++line_num;
		return true;
	});
}

static void write_cvar_to_stream(BS_WriteStream& writer, V_cvar* cvar)
{
	writer.Put('+');
	for(const char* c = cvar->cvar_key; *c != '\0'; ++c)
	{
		writer.Put(*c);
	}
	writer.Put(' ');

	// save
	bool string_quotes = cvar->cvar_save_quotes;
	if(string_quotes)
	{
		writer.Put('\"');
	}
	for(char c : escape_string(cvar->cvar_write()))
	{
		writer.Put(c);
	}
	if(string_quotes)
	{
		writer.Put('\"');
	}

	writer.Put('\n');
}

static bool cvar_save_convert_line(
	BS_WriteStream& writer,
	char* line,
	std::vector<std::string>& keys,
	std::vector<V_cvar*>& values)
{
	std::vector<const char*> argv;
	if(!cvar_split_line(argv, line))
	{
		return false;
	}

	// Note: the ability to have multiple cvars in a single line is an accident for files...
	// NOLINTNEXTLINE(bugprone-narrowing-conversions)
	int argc = argv.size();
	for(int i = 0; i < argc; ++i)
	{
		// TODO: is + optional? it's consistent, but it's more of a console SAVE syntax,
		//  and for command line stuff.
		if(argv[i][0] != '+')
		{
			serrf(
				"ERROR(%s): cvar option must start with a '+'\n"
				"expression: `%s`\n",
				__func__,
				argv[i]);
			return false;
		}

		const char* key = argv[i] + 1;

		auto it = std::find(keys.begin(), keys.end(), key);
		if(it != keys.end())
		{
			size_t index = std::distance(keys.begin(), it);
			if(values[index] == NULL)
			{
				// remove duplicate.
				i++;
				continue;
			}

			// move to next argument.
			i++;
			if(i >= argc)
			{
				serrf("ERROR(%s): cvar assignment missing: `%s`\n", __func__, key);
				return false;
			}

			write_cvar_to_stream(writer, values[index]);

			// signal that this has been saved.
			values[index] = NULL;
		}
		else
		{
			auto cv = get_convars().find(key);
			if(cv == get_convars().end())
			{
				serrf(
					"ERROR(%s): unknown command or cvar\n"
					"expression: `%s`\n",
					__func__,
					key);

				return false;
			}

			ASSERT(!cv->second.cvar_commit_changes);

			if(cv->second.cvar_is_default() && !cv->second.cvar_from_file)
			{
				// slogf("info: not saving cvar because the value is already default: %s\n", key);
				return true;
			}

			// this will include changes I did not set with cvar_modified
			// write_cvar_to_stream(writer, &cv->second);

			writer.Put('+');
			for(const char* c = key; *c != '\0'; ++c)
			{
				writer.Put(*c);
			}
			writer.Put(' ');

			i++;
			if(i >= argc)
			{
				serrf("ERROR(%s): cvar assignment missing: `%s`\n", __func__, key);
				return false;
			}

			// save
			bool string_quotes = cv->second.cvar_save_quotes;
			if(string_quotes)
			{
				writer.Put('\"');
			}
			for(const char* c = argv[i]; *c != '\0'; ++c)
			{
				writer.Put(*c);
			}
			if(string_quotes)
			{
				writer.Put('\"');
			}

			writer.Put('\n');
		}
	}

	return writer.good();
}

// copy the file into a new file.
bool cvar_save_file(RWops* infile, RWops* outfile)
{
	ASSERT(outfile != NULL);

	// convert the single linked list into a sorted vector
	// This is probably slow and overengineered....
	std::vector<V_cvar*> cvar_type;
	int loop_count = 0;
	V_cvar* cur = g_cvar_modified_list_head;
	while(cur != NULL)
	{
		++loop_count;
		if(loop_count > 10000)
		{
			serrf(
				"%s: cvar g_cvar_modified_list_head infinite loop: %s\n", __func__, cur->cvar_key);
			return false;
		}
		cvar_type.push_back(cur);
		cur = cur->modified_next;
	}

	// std::sort(cvar_type.begin(), cvar_type.end(), [](V_cvar* lhs, V_cvar* rhs) {
	//	return strcmp(lhs->cvar_key, rhs->cvar_key) < 0;
	// });

	std::vector<std::string> cvar_names;
	cvar_names.reserve(cvar_type.size());
	for(V_cvar* cv : cvar_type)
	{
		cvar_names.emplace_back(cv->cvar_key);
	}

	char outbuffer[1000];
	BS_WriteStream writer(outfile, outbuffer, sizeof(outbuffer));

	if(infile != NULL)
	{
		if(!cvar_parse_file_lines(infile, [&](char* line, bool empty) -> bool {
			   if(empty)
			   {
				   for(const char* c = line; *c != '\0'; ++c)
				   {
					   writer.Put(*c);
				   }
				   writer.Put('\n');
				   return writer.good();
			   }
			   return cvar_save_convert_line(writer, line, cvar_names, cvar_type);
		   }))
		{
			return false;
		}
	}

	if(!writer.good())
	{
		return false;
	}

	ASSERT(cvar_type.size() == cvar_names.size());
	for(auto& cvar : cvar_type)
	{
		// Null means the value has been replaced with a preexisting value in the file
		if(cvar != NULL)
		{
			// TODO: it would be nice if I could remove the cvar BEFORE making the file.
			//  because this will cause empty cvar files to be written.
			if(cvar->cvar_is_default() && !cvar->cvar_from_file)
			{
				// slogf("info: not saving cvar because the value is already default: %s\n", key);
				continue;
			}

			write_cvar_to_stream(writer, cvar);
		}
	}

	// this MUST be called.
	writer.Flush();

	if(!writer.good())
	{
		return false;
	}

	// clear the list.
	loop_count = 0;
	cur = g_cvar_modified_list_head;
	while(cur != NULL)
	{
		++loop_count;
		if(loop_count > 10000)
		{
			serrf(
				"%s[2]: cvar g_cvar_modified_list_head infinite loop: %s\n",
				__func__,
				cur->cvar_key);
			return false;
		}
		V_cvar* temp = cur->modified_next;
		cur->modified_next = NULL;
		cur->cvar_commit_changes = false;
		cur = temp;
	}

	return true;
}

void cvar_list(bool debug)
{
	slog(
		"cvar types:\n"
		"-RUNTIME:\t"
		"normal, changes should take effect\n"
		"-[STARTUP]:\t"
		"requires the app to be restarted\n"
		"-[DEFERRED]:\t"
		"the value is cached in some way, so the value may not make instant changes\n"
		"-[READONLY]:\t"
		"the value cannot be set\n"
		"-[DISABLED]\t"
		"the value cannot be read or set due to platform or build options\n");
	for(const auto& it : get_convars())
	{
		print_cvar(it.second, debug);
	}
}

#ifndef DISABLE_SDL
enum cvar_repair_result
{
	CVAR_REPAIR_KEEP = 0,
	CVAR_REPAIR_REVERT = 1,
	CVAR_REPAIR_ERROR,
};

// return true if revert to default.
static int cvar_open_revert_dialog(V_cvar* self)
{
	// TODO: I should make this ignore the warning on exit if I press keep.
	// should I add in Revert w/o save? since revert will change the save file.
	// having the option to ignore a cvar would be nice too, maybe replace + with '?' or '+?'

	SDL_MessageBoxButtonData msg_buttons[2];
	memset(&msg_buttons, 0, sizeof(msg_buttons));

	msg_buttons[CVAR_REPAIR_KEEP].text = "Keep";
	msg_buttons[CVAR_REPAIR_KEEP].buttonID = 0;
	msg_buttons[CVAR_REPAIR_KEEP].flags = SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT;

	msg_buttons[CVAR_REPAIR_REVERT].text = "Revert";
	msg_buttons[CVAR_REPAIR_REVERT].buttonID = 1;
	msg_buttons[CVAR_REPAIR_REVERT].flags = SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT;

	SDL_MessageBoxData msg_data;
	memset(&msg_data, 0, sizeof(msg_data));

	msg_data.title = "cvar repair";
	msg_data.flags = SDL_MESSAGEBOX_BUTTONS_LEFT_TO_RIGHT;
	msg_data.buttons = msg_buttons;
	msg_data.numbuttons = std::size(msg_buttons);
	if(g_app.window != nullptr && SDL_IsMainThread())
	{
		msg_data.window = g_app.window;
	}

	std::string message_text;
	if(self->internal_cvar_type == CVAR_T::DISABLED)
	{
		str_asprintf(
			message_text,
			"Error: Cannot revert due to disabled hard coded cvar\n"
			"`%s` = %s\n",
			self->cvar_key,
			self->cvar_write().c_str());

		serr_raw(message_text.c_str(), message_text.size());

		// change the buttons in a hacky way.
		msg_data.numbuttons = 1;
		msg_buttons[CVAR_REPAIR_KEEP].text = "OK";
		msg_buttons[CVAR_REPAIR_KEEP].buttonID = CVAR_REPAIR_ERROR;
	}
	else
	{
		V_cvar* blame = self->cvar_get_blame();
		ASSERT_M(blame != nullptr, self->cvar_key);
		str_asprintf(
			message_text,
			"Error: disabled cvar %s = %s\n"
			"(blame: %s == %s)\n"
			"\n"
			"Keep - don't do anything\n"
			"Revert - set the values to the default\n",
			self->cvar_key,
			self->cvar_write().c_str(),
			blame->cvar_key,
			blame->cvar_write().c_str());
	}

	msg_data.message = message_text.c_str();

	int result = 0;
	if(!SDL_ShowMessageBox(&msg_data, &result))
	{
		serrf("Error(%s): SDL_ShowMessageBox: %s\n", __func__, strerror(errno));
		return CVAR_REPAIR_ERROR;
	}
	switch(result)
	{
	case CVAR_REPAIR_KEEP: slogf("info: Keeping `%s`\n", self->cvar_key); break;
	case CVAR_REPAIR_REVERT: slogf("info: Reverting to default: `%s`\n", self->cvar_key); break;
	case CVAR_REPAIR_ERROR: return CVAR_REPAIR_ERROR;
	default:
		serrf(
			"Error(%s): unknown SDL_ShowMessageBox result(%d) `%s`\n",
			__func__,
			result,
			self->cvar_key);
		return CVAR_REPAIR_ERROR;
	}
	return result;
}
#endif // DISABLE_SDL

// recursion... my favorite...
static int recursion_count = 0;
static bool cvar_propagate_blame_defaults(V_cvar* parent)
{
	ASSERT_M(recursion_count < 10, parent->cvar_key);

	#ifdef DISABLE_SDL
	if(parent->internal_cvar_type == CVAR_T::DISABLED)
	{
		serrf("Error: Cannot revert due to disabled hard coded cvar\n"
			  "`%s` = %s\n",
			  parent->cvar_key,
			  parent->cvar_write().c_str());
		return false;
	}
	V_cvar* blame = parent->cvar_get_blame();
	slogf(
		"Error: disabled cvar %s = %s\n"
		"(blame: %s == %s)\n",
		parent->cvar_key,
		parent->cvar_write().c_str(),
		blame->cvar_key,
		blame->cvar_write().c_str());
	// I don't want console prompt for keeping.
	slogf("info: doing nothing.\n");
	return true;
	#else
	int result = cvar_open_revert_dialog(parent);
	switch(result)
	{
	case CVAR_REPAIR_KEEP: return true;
	case CVAR_REPAIR_REVERT: break;
	case CVAR_REPAIR_ERROR: return false;
	default:
		serrf("Error(%s): unknown result(%d) `%s`\n", __func__, result, parent->cvar_key);
		return false;
	}

	ASSERT_M(result == CVAR_REPAIR_REVERT, parent->cvar_key);

	std::string previous_value = parent->cvar_write();
	if(!parent->cvar_revert_to_default())
	{
		return false;
	}

	slogf("%s(%s) = %s\n", parent->cvar_key, previous_value.c_str(), parent->cvar_write().c_str());

	parent->cvar_save_changes();

	V_cvar* child = parent->cvar_get_blame();
	for(; child != nullptr; child = parent->cvar_get_blame())
	{
		if(!cvar_propagate_blame_defaults(child))
		{
			return false;
		}
	}

	child = parent->cvar_get_blame();
	if(!CHECK_M(child == nullptr, child->cvar_key))
	{
		return false;
	}

	return true;
#endif
}

bool cvar_validate_conditions()
{
	bool success = true;
	for(const auto& it : get_convars())
	{
		// move the value to be real.
		it.second.cvar_set_commit();

		it.second.cvar_validate_lock = true;

		// the condition changed.

		// TODO: check for disabled cvars using the same recursion in cvar_propagate_blame_defaults,
		//  and ignore in cvar_validate_conditions
		if(it.second.internal_cvar_type != CVAR_T::DISABLED)
		{
			V_cvar* blame = it.second.cvar_get_blame();
			if(blame != nullptr)
			{
				recursion_count = 0;
				if(!cvar_propagate_blame_defaults(&it.second))
				{
					success = false;
				}
			}
		}
		it.second.cvar_validate_lock = false;
	}
	return success;
}

//
//  CVAR loading while keeping revert
//

// TODO: I could check if there is only one instance to remove the false warning.
//  - I feel like I need to move the --help like flags out of here. but cvars replace flags.
// TODO: grab the modification date using fstat() and prompt overwrite when the dates differ
// TODO: maybe give the option to revert the options to default (but the user could just delete the
// file).

static const char* cvar_file_path = "cvar.cfg";
// store the old cvar values into the temp file.
static const char* cvar_temp_file = "cvar.tmp";
static const char cvar_temp_header[] = "CVAR";
enum
{
	cvar_header_size = sizeof(cvar_temp_header) - 1
};


// TODO: this should be global, but it's more portable if I used whereami or something.
static const char* prog_name = "<unknown>";

// the cvar temp file state.
enum
{
	// saving the changes to the file on exit.
	CVAR_TEMP_UNTESTED = 0,
	// on startup if the temp file exists, and it's untested, set it to CVAR_TEMP_TEST.
	// on graceful exit the temp file should be deleted / replaced.
	// If the program does not exit gracefully (crash/error)
	// CVAR_TEMP_TEST will be detected, and it's possible the cvar is the reason for the crash.
	CVAR_TEMP_TEST = 1
};

#ifndef DISABLE_SDL
// set during loading, on graceful exit replace the main cvar file.
static bool cvar_revert_to_last_good_state = false;

static std::unique_ptr<char[]> cvar_copy_mem;
static size_t cvar_copy_size;
enum
{
	POPUP_KEEP = 0,
	POPUP_REVERT,
	POPUP_EXIT,
	MAX_POPUP
};

static bool load_cvar_and_copy(RWops* file)
{
	// copy the cvar file memory because the file might change if I opened it again.
	// and I don't want to hold onto the file because
	// I want to be able to modify it while the program is running (on windows).

	RW_ssize_t file_end = file->size();
	if(file_end == -1)
	{
		return false;
	}
	RW_ssize_t file_offset = file->tell();
	if(file_offset == -1)
	{
		return false;
	}
	cvar_copy_size = file_end - file_offset;
	cvar_copy_mem = std::make_unique<char[]>(cvar_copy_size);
	if(file->read(cvar_copy_mem.get(), 1, cvar_copy_size) != cvar_copy_size)
	{
		return false;
	}

	if(!file->seek(file_offset, SDL_IO_SEEK_SET))
	{
		return false;
	}
	if(!cvar_file(CVAR_T::STARTUP, file))
	{
		return false;
	}

	return true;
}

#ifdef _WIN32
#include "breakpoint.h" // for windows.h
#include <debugapi.h> // for IsDebuggerPresent
#endif

static int open_cvar_revert_messagebox()
{
#ifdef _WIN32
	if(IsDebuggerPresent() != 0)
	{
		slogf("info: debugger detected, default to keep cvar settings.\n");
		return POPUP_KEEP;
	}
#endif

	SDL_MessageBoxButtonData msg_buttons[MAX_POPUP];
	memset(&msg_buttons, 0, sizeof(msg_buttons));
	msg_buttons[POPUP_KEEP].text = "Keep";
	msg_buttons[POPUP_KEEP].buttonID = 0;
	msg_buttons[POPUP_KEEP].flags = SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT;

	msg_buttons[POPUP_REVERT].text = "Revert";
	msg_buttons[POPUP_REVERT].buttonID = 1;

	msg_buttons[POPUP_EXIT].text = "Quit";
	msg_buttons[POPUP_EXIT].buttonID = 2;
	msg_buttons[POPUP_EXIT].flags = SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT;

	SDL_MessageBoxData msg_data;
	memset(&msg_data, 0, sizeof(msg_data));
	msg_data.title = prog_name;
	msg_data.flags = SDL_MESSAGEBOX_BUTTONS_LEFT_TO_RIGHT;
	msg_data.message = "The previous instance has aborted while using new settings.\n"
					   "This could also be caused if you opened 2 instances.\n"
					   "Keep the settings or Revert to previous working settings:\n";
	msg_data.buttons = msg_buttons;
	msg_data.numbuttons = std::size(msg_buttons);
	if(SDL_IsMainThread())
	{
		msg_data.window = g_app.window;
	}

	int result = POPUP_KEEP;
	if(!SDL_ShowMessageBox(&msg_data, &result))
	{
		slogf("Error(%s): SDL_ShowMessageBox: %s\n", __func__, strerror(errno));
	}
	return result;
}
#endif

CVAR_LOAD load_cvar(int argc, char** argv)
{
	bool success = true;

	// load_cvar is done at the very start, so it's very unlikely for this to happen.
	serr_check_serr_leaks("pre load cvar", 0);

	cvar_init();

	// argv[0] being the program name is platform dependant,
	// there are edge cases where it might be gone.
	// in those cases, whereami would give you the name

	if(argc >= 1)
	{
		ASSERT(argv[0] != NULL);
		// I'm sure there is a one-liner to do this... too lazy.
		// TODO: put this into a function since I use it a lot in stacktrace code
		const char* temp_result = strrchr(argv[0], '\\');
		if(temp_result != NULL)
		{
			// could be avoided
			prog_name = temp_result + 1;
		}
		temp_result = strrchr(argv[0], '/');
		if(temp_result != NULL)
		{
			// could be avoided
			prog_name = temp_result + 1;
		}
		// remove program name from arguments
		--argc;
		++argv;
	}

	#ifndef DISABLE_SDL
	// '+' because read and write required.
	FILE* cvar_temp_fp = fopen(cvar_temp_file, "rb+");
	if(cvar_temp_fp == NULL)
	{
		// ENOENT = No such file or directory
		if(errno != ENOENT)
		{
			slogf(
				"warning: failed to open temp cvar file: `%s`, reason: %s\n",
				cvar_temp_file,
				strerror(errno));
		}
	}
	else
	{
		slogf("info: checking temp cvar file: `%s`\n", cvar_temp_file);
		RWops_Stdio fp_raii(cvar_temp_fp, cvar_temp_file);

		// + 1 because I use the last byte for the state.
		char header[cvar_header_size + 1];
		if(fp_raii.read(&header, 1, sizeof(header)) != sizeof(header))
		{
			serrf("error: temp cvar file no header: `%s`\n", cvar_temp_file);
			success = false;
		}
		else
		{
			if(memcmp(cvar_temp_header, header, cvar_header_size) != 0)
			{
				serrf("error: temp cvar file incorrect header: `%s`\n", cvar_temp_file);
				success = false;
			}
			else
			{
				// check the state
				if(header[cvar_header_size] == CVAR_TEMP_UNTESTED)
				{
					header[cvar_header_size] = CVAR_TEMP_TEST;
					if(!fp_raii.seek(0, SEEK_SET) ||
					   fp_raii.write(&header, 1, sizeof(header)) != sizeof(header))
					{
						success = false;
					}
				}
				else if(header[cvar_header_size] == CVAR_TEMP_TEST)
				{
					slogf("info: crash before graceful exit\n");
					int result = open_cvar_revert_messagebox();
					switch(result)
					{
					case POPUP_KEEP: slog("Keep.\n"); break;
					case POPUP_REVERT:
						slog("Reverting\n");
						slogf("info: loading cvar values from: %s\n", cvar_temp_file);
						// the temp file contains the previous cvar state.
						if(!load_cvar_and_copy(&fp_raii))
						{
							success = false;
						}
						else
						{
							cvar_revert_to_last_good_state = true;
						}
						break;
					case POPUP_EXIT: slog("Quit.\n"); return CVAR_LOAD::CLOSE;
					default: ASSERT_M(false, std::to_string(result).c_str());
					}
				}
				else
				{
					serrf(
						"error: unknown temp cvar file state: `%s` = %uc, \n",
						cvar_temp_file,
						static_cast<unsigned char>(header[cvar_header_size]));
					success = false;
				}
			}
		}

		if(!fp_raii.close())
		{
			success = false;
		}

		if(!success)
		{
			// I don't care about the temp file, clear serr but show a message box.
			success = true;
			if(!show_error("temp cvar file error (will be deleted)", serr_get_error().c_str()))
			{
				serrf("SDL_ShowSimpleMessageBox: %s\n", SDL_GetError());
				success = false;
			}

			// so that next time the error won't appear.
			if(remove(cvar_temp_file) != 0 && errno != ENOENT)
			{
				slogf(
					"Failed to remove temp cvar file: `%s`, reason: %s\n",
					cvar_temp_file,
					strerror(errno));
			}
		}
		else
		{
			slogf("info: done reading cvar temp file: `%s`.\n", cvar_temp_file);
		}
	}

	if(!cvar_revert_to_last_good_state)
#endif // DISABLE_SDL
	{
		FILE* fp = fopen(cvar_file_path, "rb");
		if(fp == NULL)
		{
			// ENOENT = No such file or directory
			if(errno == ENOENT)
			{
				// not existing is not an error
				slogf("info: %s not found\n", cvar_file_path);
			}
			else
			{
				serrf("Failed to open: `%s`, reason: %s\n", cvar_file_path, strerror(errno));
				success = false;
			}
		}
		else
		{
			slogf("info: cvar file: %s\n", cvar_file_path);
			RWops_Stdio fp_raii(fp, cvar_file_path);
#ifdef DISABLE_SDL
			if(!cvar_file(CVAR_T::STARTUP, &fp_raii))
			{
				success = false;
			}
#else
			if(!load_cvar_and_copy(&fp_raii))
			{
				success = false;
			}
#endif
			if(!fp_raii.close())
			{
				success = false;
			}
			slogf("info: done reading cvar file.\n");
		}
	}

	if(!success)
	{
		return CVAR_LOAD::ERROR;
	}

	// load cvar arguments
	for(int i = 0; i < argc; ++i)
	{
		if(strcmp(argv[i], "--help") == 0)
		{
			const char* usage_message = "Usage: %s [--options] [+cv_option \"0\"]\n"
										"\t--help\tshow this usage message\n"
										"\t--list-cvars\tlist all cv vars options\n"
										"\tnote that you must put cvars after options\n";
			slogf(usage_message, (prog_name != NULL ? prog_name : "prog_name"));
			return CVAR_LOAD::CLOSE;
		}
		if(strcmp(argv[i], "--list-cvars") == 0)
		{
			cvar_list(false);
			return CVAR_LOAD::CLOSE;
		}
		if(strcmp(argv[i], "--list-cvars-debug") == 0)
		{
			cvar_list(true);
			return CVAR_LOAD::CLOSE;
		}
		if(argv[i][0] == '+')
		{
			//slogf("%d\n", argc);
			int ret = cvar_arg(CVAR_T::STARTUP, argc - i, argv + i);
			if(ret == -1)
			{
				success = false;
				break;
			}
			argc -= ret;
			argv += ret;

			continue;
		}

		serrf("ERROR: unknown argument: %s\n", argv[i]);
		success = false;
	}

	// check that the values are correct.
	if(!cvar_validate_conditions())
	{
		success = false;
	}

	return success ? CVAR_LOAD::SUCCESS : CVAR_LOAD::ERROR;
}
#ifndef DISABLE_SDL

static bool copy_to_file(RWops* infile, RWops* outfile)
{
	char buffer[1000];
	size_t nread;
	int count = 0;
	do
	{
		count++;
		ASSERT(count < 100000 && "infinite loop");
		nread = infile->read(buffer, 1, sizeof(buffer));
		if(nread != 0 && outfile->write(buffer, 1, nread) != nread)
		{
			return false;
		}
	} while(nread == sizeof(buffer));
	return nread != 0;
}
bool save_cvar()
{
	bool success = true;

	if(!cvar_validate_conditions())
	{
		success = false;
	}

	// copy the temp file's backup back into the main cvar.
	if(cvar_revert_to_last_good_state)
	{
		if(!CHECK(cvar_copy_mem))
		{
			return false;
		}
		slogf("info: reverting cvar settings\n");
		Unique_RWops cvar_copy =
			Unique_RWops_FromMemory(cvar_copy_mem.get(), cvar_copy_size, true, "cvar_copy");
		ASSERT(cvar_copy);

		FILE* out_fp = fopen(cvar_file_path, "wb");
		if(out_fp == NULL)
		{
			serrf("Failed to write: `%s`, reason: %s\n", cvar_file_path, strerror(errno));
			success = false;
		}
		else
		{
			RWops_Stdio out_fp_raii(out_fp, cvar_file_path);
			if(!copy_to_file(cvar_copy.get(), &out_fp_raii))
			{
				success = false;
			}
			else
			{
				// rewind it.
				if(!cvar_copy->seek(0, SDL_IO_SEEK_SET))
				{
					success = false;
				}
			}
			if(!out_fp_raii.close())
			{
				success = false;
			}
		}
		if(!cvar_copy->close())
		{
			success = false;
		}
	}

	if(!success)
	{
		return false;
	}

	// no changes made.
	if(g_cvar_modified_list_head == NULL)
	{
		if(remove(cvar_temp_file) != 0)
		{
			if(errno != ENOENT)
			{
				serrf("Failed to remove: `%s`, reason: %s\n", cvar_temp_file, strerror(errno));
				success = false;
			}
		}
		else
		{
			slogf("info(%s): deleting %s\n", __func__, cvar_temp_file);
		}

		cvar_copy_mem.reset();
		return success;
	}

	slogf("info: saving cvars: `%s`\n", cvar_file_path);

	if(!cvar_copy_mem)
	{
		slog("info: no previous cvar file loaded, creating a new file.\n");

		FILE* out_fp = fopen(cvar_file_path, "wb");
		if(out_fp == NULL)
		{
			serrf("Failed to write file: `%s`, reason: %s\n", cvar_file_path, strerror(errno));
			success = false;
		}
		else
		{
			RWops_Stdio out_fp_raii(out_fp, cvar_file_path);
			if(!cvar_save_file(NULL, &out_fp_raii))
			{
				success = false;
			}
			if(!out_fp_raii.close())
			{
				success = false;
			}
		}
	}
	else
	{
		ASSERT(cvar_copy_mem);
		Unique_RWops cvar_copy;
		cvar_copy = Unique_RWops_FromMemory(cvar_copy_mem.get(), cvar_copy_size, true, "cvar_copy");

		FILE* out_fp = fopen(cvar_temp_file, "wb");
		if(out_fp == NULL)
		{
			serrf("Failed to modify file: `%s`, reason: %s\n", cvar_temp_file, strerror(errno));
			success = false;
		}
		else
		{
			{
				RWops_Stdio out_fp_raii(out_fp, cvar_temp_file);

				// + 1 because I use the last byte for the state.
				char new_header[cvar_header_size + 1];
				memcpy(new_header, cvar_temp_header, cvar_header_size);
				new_header[cvar_header_size] = CVAR_TEMP_UNTESTED;
				if(out_fp_raii.write(&new_header, 1, sizeof(new_header)) != sizeof(new_header))
				{ // NOLINT(*-branch-clone)
					success = false;
				}
				else if(!copy_to_file(cvar_copy.get(), &out_fp_raii))
				{
					success = false;
				}

				if(!out_fp_raii.close())
				{
					success = false;
				}
			}
			out_fp = fopen(cvar_file_path, "wb");
			if(out_fp == NULL)
			{
				serrf("Failed to modify file: `%s`, reason: %s\n", cvar_file_path, strerror(errno));
				success = false;
			}
			// rewind it.
			else
			{
				RWops_Stdio out_fp_raii(out_fp, cvar_file_path);
				if(!cvar_copy->seek(0, SDL_IO_SEEK_SET))
				{ // NOLINT(*-branch-clone)
					success = false;
				}
				else if(!cvar_save_file(cvar_copy.get(), &out_fp_raii))
				{
					success = false;
				}
				if(!out_fp_raii.close())
				{
					success = false;
				}
			}
		}
		if(!cvar_copy->close())
		{
			success = false;
		}
	}

	if(success)
	{
		slogf("info: done saving cvar file.\n");
	}

	cvar_copy_mem.reset();

	return success;
}
#else	// DISABLE_SDL

bool save_cvar()
{
	slog("info: save_cvar not supported due to DISABLE_SDL.\n");
	return true;
}
#endif