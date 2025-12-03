#pragma once

#include "../core/global.h"

#include <string>
#include <string_view>
#include <cstring>

inline const char* remove_file_path(const char* file)
{
	const char* temp_filename = strrchr(file, '\\');
	if(temp_filename != NULL)
	{
		// could be avoided
		file = temp_filename + 1;
	}
	temp_filename = strrchr(file, '/');
	if(temp_filename != NULL)
	{
		// could be avoided
		file = temp_filename + 1;
	}
	return file;
}

// checks if any characters are escape codes.
// so you can skip the allocation.
inline bool escape_string_check_contains(std::string_view input_string)
{
	for(char c : input_string)
	{
		switch(c)
		{
		case '\n':
		case '\t':
		case '\"':
		case '\\': return true;
		}
	}
	return false;
}

// a string conversion so '\n' turns into "\\\n"
// Also, I find it really neat that libfmt / C++20 std::println will escape strings for you.
// And I really hate passing the string as a parameter,
// but it's either this or std::optional or exceptions
inline bool escape_string(std::string& output_string, std::string_view input_string)
{
	// pretty much all my log strings have a newline, so I add a +1
	output_string.reserve(input_string.size() + 1);
	for(auto it = input_string.begin(); it != input_string.end(); ++it)
	{
		char c = *it;
		switch(c)
		{
		case '\n':
			output_string.push_back('\\');
			output_string.push_back('n');
			break;
		case '\t':
			output_string.push_back('\\');
			output_string.push_back('t');
			break;
		case '\"':
			output_string.push_back('\\');
			output_string.push_back('\"');
			break;
		case '\\':
			output_string.push_back('\\');
			output_string.push_back('\\');
			break;
		default:
			// if it is a control code.
			if(c >= 0 && c <= 0x001f)
			{
				serrf(
					"got a unexpected control code, got: #%d, offset: %zu\n",
					c,
					std::distance(input_string.begin(), it));
				return false;
			}
			output_string.push_back(c);
		}
	}
	return true;
}

inline bool rem_escape_string(char* input_string)
{
	ASSERT(input_string != NULL);

	int i = 0;
	int j = 0;
	while(input_string[i] != '\0')
	{
		if(input_string[i] == '\\')
		{
			++i;
			switch(input_string[i])
			{
			case 'n': input_string[j] = '\n'; break;
			case 't': input_string[j] = '\t'; break;
			case '\"': input_string[j] = '\"'; break;
			case '\\': input_string[j] = '\\'; break;
			case '\0': serr("expected escape code, got null\n"); return false;
			default:
				if(input_string[i] >= 0 && input_string[i] <= 0x001f)
				{
					serrf("expected escape code, got: #%d, offset: %d\n", input_string[i], i);
					return false;
				}
				serrf("expected escape code, got: %c, offset: %d\n", input_string[i], i);
				return false;
			}
		}
		// if it's an escape code.
		if(input_string[i] >= 0 && input_string[i] <= 0x001f)
		{
			serrf(
				"code points <= U+001F must be escaped, got: #%d, offset: %d\n",
				input_string[i],
				i);
			return false;
		}
		if(j < i)
		{
			input_string[j] = input_string[i];
		}
		++j;
		++i;
	}
	input_string[j] = '\0';
	return true;
}

#include <cstdarg>

#ifndef MY_MSVC_PRINTF
// msvc doesn't support __attribute__, unless it is clang-cl.
#if defined(_MSC_VER) && !defined(__clang__)
#ifndef __attribute__
#define __attribute__(x)
#endif
// You need enable /ANALYZE for _Printf_format_string_ to work
#define MY_MSVC_PRINTF _Printf_format_string_
#else
#define MY_MSVC_PRINTF
#endif
#endif

#ifdef __cpp_lib_string_resize_and_overwrite
#include <cassert>
#endif
inline void str_vasprintf(std::string& out, const char* fmt, va_list args)
{
	// you cannot use ASSERT here because ASSERT uses asprintf
	// ASSERT(fmt != NULL);
	if(fmt == NULL)
	{
		out += "<asprintf_error>: null fmt\n";
		return;
	}

	int ret;
	size_t offset = out.size();
	// pop_errno_t errno_raii;
	va_list temp_args;

	// it says you should copy if you use valist more than once.
	va_copy(temp_args, args);
#ifdef _WIN32
	// win32 has a compatible C standard library, but annex k prevents exploits or something.
	ret = _vscprintf(fmt, temp_args);
#else
	ret = vsnprintf(NULL, 0, fmt, temp_args);
#endif
	va_end(temp_args);

	if(ret == -1) goto err;
#ifdef __cpp_lib_string_resize_and_overwrite
	// I try to pay attention to the fact that you should avoid writing to the null terminator.
	out.resize_and_overwrite(offset + ret + 1, [&](char* p, std::size_t n) -> size_t {
		int expected_ret = ret;
#ifdef _WIN32
		ret = vsprintf_s(p + offset, ret + 1, fmt, args);
#else
		ret = vsnprintf(p + offset, ret + 1, fmt, args);
#endif
		if(ret == -1) return offset;
		assert(ret == n - offset - 1 && "this should be true because of the null terminator.");
		assert(ret == expected_ret);
		return n - 1;
	});
	if(ret == -1) goto err;
#else
	// vsnprintf is going to overwrite the null terminator.
	// if a sanitizer tool as a problem with it, go ahead and +1 and pop_back().
	out.resize(offset + ret, '?');

#ifdef _WIN32
	ret = vsprintf_s(out.data() + offset, ret + 1, fmt, args);
#else
	ret = vsnprintf(out.data(), ret + 1, fmt, args);
#endif
	if(ret == -1) goto err;
#endif

	return;
err:
	out += "<asprintf_error>: ";
	out += strerror(errno);
}

// void str_vasprintf(std::string& out, const char* fmt, va_list args);
__attribute__((format(printf, 2, 3))) inline void
	str_asprintf(std::string& out, MY_MSVC_PRINTF const char* fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	str_vasprintf(out, fmt, args);
	va_end(args);
}