// This is an independent project of an individual developer. Dear PVS-Studio, please check it.
// PVS-Studio Static Code Analyzer for C, C++, C#, and Java: https://pvs-studio.com

#include "global_pch.h"
#include "global.h"

#include "cvar.h"

#include "stacktrace.h"
#include "../util/string_tools.h"

// DISABLE_CONSOLE is old and pointless, it was used to remove console.h
// for a small test project, but I am keeping this in case I have another test project.
// (I would need to make a few changes for it to work, mainly ASSERT/ASSERT_M)
#ifndef DISABLE_CONSOLE
#include "console.h"
#endif

#include <cstring>

// I think I need this for exit()
#include <cstdlib>

#if defined(HAS_STACKTRACE_PROBABLY)
static int has_stacktraces = 1;
static CVAR_T disable_if_no_stacktrace = CVAR_T::RUNTIME;
#else
static int has_stacktraces = 0;
static CVAR_T disable_if_no_stacktrace = CVAR_T::DISABLED;
#endif

// I don't know where to put cv_serr_bt and I don't want to put it into global.h
// this is externed into main.cpp to give a warning if you don't have this enabled during a leak
extern cvar_int cv_serr_bt;
REGISTER_CVAR_INT(
	cv_serr_bt,
	has_stacktraces,
	// I don't reccomend "always stacktrace" because some errors are nested,
	// which will make the error very hard to read.
	// a "capture" is a error that is handled, using serr_get_error()
	"0 = nothing, 1 = stacktrace (once per capture), 2 = always stacktrace (spam)",
	disable_if_no_stacktrace);

// I would use this if I was profiling, or if the logs were spamming.
static REGISTER_CVAR_INT(cv_disable_log, 0, "0 = keep log, 2 = disable all logs", CVAR_T::RUNTIME);

static REGISTER_CVAR_INT(
	cv_ignore_serr_leak,
	0,
	"disables the popup notifying serr leak, 0 (off), 1 (disable popup), 2 (also disable on exit)",
	CVAR_T::RUNTIME);

#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

std::string WIN_WideToUTF8(const wchar_t* buffer, int size)
{
	int length = WideCharToMultiByte(CP_UTF8, 0, buffer, size, NULL, 0, NULL, NULL);
	std::string output(length, 0);
	WideCharToMultiByte(CP_UTF8, 0, buffer, size, output.data(), length, NULL, NULL);
	return output;
}

std::wstring WIN_UTF8ToWide(const char* buffer, int size)
{
	int length = MultiByteToWideChar(CP_UTF8, 0, buffer, size, NULL, 0);
	std::wstring output(length, 0);
	MultiByteToWideChar(CP_UTF8, 0, buffer, size, output.data(), length);
	return output;
}

std::string WIN_GetFormattedGLE(unsigned long dwErr) // NOLINT(*-runtime-int)
{
	// Retrieve the system error message for the last-error code

	wchar_t* lpMsgBuf = NULL;

	if(dwErr == 0)
	{
		dwErr = GetLastError();
	}

	// It is possible to set the current codepage to utf8, and get utf8 messages from this
	// but then I would need to use unique_ptr<> with a custom deleter for LocalFree
	// to get the data without any redundant copying.
	int buflen = FormatMessageW( // NOLINT(*-narrowing-conversions)
		FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
			FORMAT_MESSAGE_IGNORE_INSERTS |
			FORMAT_MESSAGE_MAX_WIDTH_MASK, // this removes the extra newline
		NULL,
		dwErr,
		MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
		reinterpret_cast<LPWSTR>(&lpMsgBuf),
		0,
		NULL);

	std::string str(WIN_WideToUTF8(lpMsgBuf, buflen));

	LocalFree(lpMsgBuf);

	return str;
}
#endif //_WIN32

#if 0
#ifndef LOG_FILENAME
#define LOG_FILENAME "log.txt"
#endif

struct log_wrapper : nocopy
{
	FILE* fp = NULL;

	log_wrapper()
	{
#ifdef DISABLE_CONSOLE
		fp = fopen(LOG_FILENAME, "w");
#else
		// I need the w+ because I read in the in game console
		fp = fopen(LOG_FILENAME, "w+");
#endif
		if(fp == NULL)
		{
			printf("Failed to open log: `%s`, reason: %s\n", LOG_FILENAME, strerror(errno));
			return;
		}
	}

	~log_wrapper()
	{
		ASSERT(fp != NULL);
		int prev_error = ferror(fp);
		int ret = fclose(fp);
		fp = NULL;
		if(ret != 0 && prev_error != 0)
		{
			printf(
				"Failed to close log: `%s`, reason: %s (return: %d)\n",
				LOG_FILENAME,
				strerror(errno),
				ret);
		}
	}
};
FILE* get_global_log_file()
{
#ifdef DISABLE_LOG_FILE
	return NULL;
#else
	static log_wrapper log;
	return log.fp;
#endif
}
#endif

static
#ifndef __EMSCRIPTEN__
	thread_local
#endif
	std::shared_ptr<std::string>
		internal_serr_buffer;

std::shared_ptr<std::string> internal_get_serr_buffer()
{
	// serr buffer lazy initialized.
	if(!internal_serr_buffer)
	{
		internal_serr_buffer = std::make_shared<std::string>();
	}
	return internal_serr_buffer;
}

std::string serr_get_error()
{
	// this is a good idea, but I would like to see if there is a breaking point anyways.
#if 0
	size_t max_size = 10000;
	if(get_serr_buffer()->size() > max_size)
	{
		slogf(
			"info: %s truncating message (size: %zu, max: %zu)\n",
			__func__,
			get_serr_buffer()->size(),
			max_size);
		get_serr_buffer()->resize(max_size);
	}
#endif
	return std::move(*internal_get_serr_buffer());
}
bool serr_check_error()
{
	return internal_serr_buffer.operator bool() && !internal_serr_buffer->empty();
}

// hint 0 = this is a startup/runtime call, 1 = exit
bool serr_check_serr_leaks(const char* title, int hint)
{
	// TODO: I could probably use the return address to eliminate repetitive leaks.
	if(serr_check_error())
	{
		// keep the error in the buffer until exit.
		// if you tried to use serr_check_error() to signal success conditions,
		// this would cause that signal to break (good, that's terrible code).
		if(hint == 0 && cv_ignore_serr_leak.data() == 1)
		{
			return false;
		}
		// on exit.
		if(cv_ignore_serr_leak.data() == 2)
		{
			serr_get_error();
			return false;
		}

		serr("\ninfo: use cv_ignore_serr_leak to ignore this popup\n");

		std::string err = serr_get_error();

		std::string title_combined;
		title_combined += "Uncaught error ";
		title_combined += title;

		show_error(title_combined.c_str(), err.c_str());
#ifdef HAS_STACKTRACE_PROBABLY
		if(cv_serr_bt.data() == 0)
		{
			slog("info: use cv_serr_bt to find the location of the leak");
		}
#endif
		return true;
	}
	return false;
}

static MY_NOINLINE void serr_safe_stacktrace(int skip)
{
	// NOTE: I am thinking of making the stacktrace only appear in stdout
	// and in the console error section.
	(void)skip;
	if(cv_serr_bt.data() == 2 || (cv_serr_bt.data() == 1 && !serr_check_error()))
	{
		std::string msg;
		str_asprintf(msg, "StackTrace (%s = %d): \n", cv_serr_bt.cvar_key, cv_serr_bt.data());
		debug_stacktrace_string_printer handler(msg);
		debug_stacktrace(handler, skip + 1);
		msg += '\n';

		internal_get_serr_buffer()->append(msg);
		fwrite(msg.c_str(), 1, msg.size(), stdout);
#ifndef DISABLE_CONSOLE
		{
#ifndef __EMSCRIPTEN__
			std::lock_guard<std::mutex> lk(g_log_mut);
#endif
			g_log.push(CONSOLE_MESSAGE_TYPE::ERR, msg.c_str(), msg.size());
		}
#endif
	}
}

void slog_raw(const char* msg, size_t len)
{
	ASSERT(msg != NULL);
	ASSERT(len != 0);
	if(cv_disable_log.data() != 0)
	{
		return;
	}
	// on win32, if did a /subsystem:windows, I would probably
	// replace stdout with OutputDebugString on the debug build.
	fwrite(msg, 1, len, stdout);

#ifndef DISABLE_CONSOLE
	{
#ifndef __EMSCRIPTEN__
		std::lock_guard<std::mutex> lk(g_log_mut);
#endif
		g_log.push(CONSOLE_MESSAGE_TYPE::INFO, msg, len);
	}
#endif
}
MY_NOINLINE void serr_raw(const char* msg, size_t len)
{
	ASSERT(msg != NULL);
	ASSERT(len != 0);
	if(cv_disable_log.data() != 0)
	{
		// if I didn't do this, there would be side effects
		// since I sometimes depend on serr_check_error for checking.
		*internal_get_serr_buffer() = '!';
		return;
	}
	serr_safe_stacktrace(1);

	internal_get_serr_buffer()->append(msg, msg + len);
	fwrite(msg, 1, len, stdout);

#ifndef DISABLE_CONSOLE
	{
#ifndef __EMSCRIPTEN__
		std::lock_guard<std::mutex> lk(g_log_mut);
#endif
		g_log.push(CONSOLE_MESSAGE_TYPE::ERR, msg, len);
	}
#endif
}

void slog(const char* msg)
{
	slog_raw(msg, strlen(msg));
}

MY_NOINLINE void serr(const char* msg)
{
	serr_raw(msg, strlen(msg));
}

__attribute__((format(printf, 1, 2))) void slogf(MY_MSVC_PRINTF const char* fmt, ...)
{
	ASSERT(fmt != NULL);
	if(cv_disable_log.cvar_init_once && cv_disable_log.data() != 0)
	{
		return;
	}
	va_list args;
	va_start(args, fmt);
#ifndef DISABLE_CONSOLE
	va_list temp_args;
	va_copy(temp_args, args);
#endif

#ifdef _WIN32
	// win32 has a compatible C standard library, but annex k prevents exploits or something.
	vfprintf_s(stdout, fmt, args);
#else
	vfprintf(stdout, fmt, args);
#endif
	va_end(args);
#ifndef DISABLE_CONSOLE
	va_start(temp_args, fmt);

	{
#ifndef __EMSCRIPTEN__
		std::lock_guard<std::mutex> lk(g_log_mut);
#endif
		g_log.push_vargs(CONSOLE_MESSAGE_TYPE::INFO, fmt, temp_args);
	}
	va_end(temp_args);
#endif
}

MY_NOINLINE __attribute__((format(printf, 1, 2))) void serrf(MY_MSVC_PRINTF const char* fmt, ...)
{
	ASSERT(fmt != NULL);
	if(cv_disable_log.cvar_init_once && cv_disable_log.data() != 0)
	{
		// if I didn't do this, there would be side effects
		// since I sometimes depend on serr_check_error for checking.
		*internal_get_serr_buffer() = '!';
		return;
	}

	serr_safe_stacktrace(1);

	va_list args;
	va_list temp_args;
	va_start(args, fmt);
	// I think asprintf + reuse would be faster.
	va_copy(temp_args, args);

#ifdef _WIN32
	// win32 has a compatible C standard library, but annex k prevents exploits or something.
	vfprintf_s(stdout, fmt, args);
#else
	vfprintf(stdout, fmt, args);
#endif
	va_end(args);

	size_t serr_offset = internal_get_serr_buffer()->size();
	va_start(temp_args, fmt);
	str_vasprintf(*internal_get_serr_buffer(), fmt, temp_args);
	va_end(temp_args);
#ifndef DISABLE_CONSOLE
	{
#ifndef __EMSCRIPTEN__
		std::lock_guard<std::mutex> lk(g_log_mut);
#endif
		g_log.push(
			CONSOLE_MESSAGE_TYPE::ERR,
			internal_get_serr_buffer()->c_str() + serr_offset,
			internal_get_serr_buffer()->size() - serr_offset);
	}
#endif
}

