// This is an independent project of an individual developer. Dear PVS-Studio, please check it.
// PVS-Studio Static Code Analyzer for C, C++, C#, and Java: https://pvs-studio.com

#include "global_pch.h"
#include "stacktrace.h"
#include "breakpoint.h"

static int has_stacktrace
#ifdef HAS_STACKTRACE_PROBABLY
	= 1;
#else
	= 0;
#endif

REGISTER_CVAR_INT(
	cv_has_stacktrace,
	has_stacktrace,
	"if stacktraces are supported, 0 = not supported, 1 = supported",
	CVAR_T::READONLY);

// I have been very hesitant to use C++ newer than 17, but <stacktrace> is convenient for testing.
// cfi icall is triggered by msvc <stacktrace> (but icall does not work with a lot of libraries)
// dbghelp does not work either, but it's fixable with __attribute__((no_sanitize("cfi_icall")))
// (but that's because I dynamically load dbghelp, linking might fix that)
// I need to use an ignorelist for <stacktrace> since the error is internal.
#if __cpp_lib_stacktrace
#include <stacktrace>
static int has_stacktrace_cpp23 = 1;
static CVAR_T disable_cvar_if_no_cpp32_stacktrace = CVAR_T::RUNTIME;
#else
static int has_stacktrace_cpp23 = 0;
static CVAR_T disable_cvar_if_no_cpp32_stacktrace = CVAR_T::DISABLED;
#endif

static REGISTER_CVAR_INT(
	cv_has_stacktrace_cpp23, has_stacktrace_cpp23, "0 = not found, 1 = found", CVAR_T::READONLY);

// a trap is annoying because without a debugger, it just causes the application to exit!
static int default_trap_value
#if defined(_WIN32) && !defined(HAS_STACKTRACE_PROBABLY) && !defined(NDEBUG)
	// windows only: only trap if a debugger is attached.
	= 2;
#else
	= 0;
#endif

static CVAR_T disable_cvar_if_no_stacktrace
#ifdef HAS_STACKTRACE_PROBABLY
	= CVAR_T::RUNTIME;
#else
	= CVAR_T::DISABLED;
#endif

REGISTER_CVAR_INT(
	cv_bt_trap,
	default_trap_value,
	"replace all stacktraces with a debug trap, 0 (off), 1 (on), 2 (windows only: only trap inside a debugger)",
	disable_cvar_if_no_stacktrace);
REGISTER_CVAR_INT(
	cv_bt_demangle,
	1,
	"stacktrace pretty function names, 0 (off), 1 (on)",
	disable_cvar_if_no_stacktrace);
REGISTER_CVAR_INT(
	cv_bt_full_paths,
	0,
	"stacktrace full file paths, 0 (off), 1 (on)",
	disable_cvar_if_no_stacktrace);
REGISTER_CVAR_INT(
	cv_bt_max_depth, 30, "max number of frames in a stacktrace", disable_cvar_if_no_stacktrace);

// TODO: cv_bt_ignore_skip
REGISTER_CVAR_INT(
	cv_bt_ignore_skip,
	0,
	"some stack trace frame may be inlined incorrectly with optimizations, 0 = off, 1 = print all frames",
	CVAR_T::RUNTIME);

static REGISTER_CVAR_INT(
	cv_bt_force_cpp23_stacktrace,
	0,
	"override dbghelp or libbacktrace with <stacktrace>, 0 (off), 1 (on), -1 (both)",
	disable_cvar_if_no_cpp32_stacktrace);

// I try to leave 1 frame below, so that at least one frame is from SDL3.dll
// if more frames would help, I could make a positive number leak more frames in.
REGISTER_CVAR_INT(
	cv_bt_trim_stacktrace,
	1,
	"Remove unhelpful frames at the bottom of the stacktrace, 0 = off (print all frames), 1 = on (trim)",
	disable_cvar_if_no_stacktrace);

#ifdef HAS_STACKTRACE_PROBABLY
thread_local void* g_trim_return_address;

void trim_stacktrace_print_helper(debug_stacktrace_observer& printer, int trim_count)
{
	printer.print_string_fmt(
		"trimmed frames: %d (%s = %d)\n",
		trim_count,
		cv_bt_trim_stacktrace.cvar_key,
		cv_bt_trim_stacktrace.data());
}
#endif

#ifndef __EMSCRIPTEN__

#if __cpp_lib_stacktrace

MY_NOINLINE bool write_cpp23_stacktrace(debug_stacktrace_observer& printer, int skip)
{
	bool success = true;
#if 0
	TIMER_U tick1;
	TIMER_U tick2;
	tick1 = timer_now();
#endif
	auto bt = std::stacktrace::current(skip + 1);
	skip = 0; // skip++; I figured out <stacktrace> has it's on skip too late...
	int i = 0;
	int trimmed_frame_start = 0;
	for(auto& frame : bt)
	{
		// NOTE: I think it would be cool if instead of truncating the trace, I also print the very
		// last 10~ frames
		if(i > cv_bt_max_depth.data())
		{
			printer.print_string_fmt(
				"max depth reached (%s = %d)\n", cv_bt_max_depth.cvar_key, cv_bt_max_depth.data());
			break;
		}

		if(i >= skip || cv_bt_ignore_skip.data() == 1)
		{
			std::string name = frame.description();
			std::string file = frame.source_file();
			int line = frame.source_line();

			if(cv_bt_full_paths.data() == 0)
			{
				// I think there is a std::string function with multi delimiter.
				const char* temp_filename = strrchr(file.c_str(), '\\');
				if(temp_filename != NULL)
				{
					// could be avoided
					file = temp_filename + 1;
				}
				temp_filename = strrchr(file.c_str(), '/');
				if(temp_filename != NULL)
				{
					// could be avoided
					file = temp_filename + 1;
				}
			}

			// not sure if this is right.
			uintptr_t address_copy;
			auto lvalue_temp = frame.native_handle();
			memcpy(&address_copy, &lvalue_temp, sizeof(address_copy));

			debug_stacktrace_info info{
				.index = i,
				.addr = address_copy,
				.module = NULL,
				.function = name.empty() ? nullptr : name.c_str(),
				.file = file.empty() ? nullptr : file.c_str(),
				.line = line};

			if(!printer.print_line(&info))
			{
				success = false;
			}
		}

		if(cv_bt_trim_stacktrace.data() != 0 && g_trim_return_address != nullptr)
		{
			if(frame.native_handle() == g_trim_return_address)
			{
				if(trimmed_frame_start != 0)
				{
					printer.print_string_fmt("info: trim function found again at: %d\n", i);
				}
				else
				{
					// I start trimming 1 frame below the target.
					trimmed_frame_start = i + 1;
					skip = 999;
				}
			}
		}

		++i;
	}

	if(cv_bt_trim_stacktrace.data() != 0 && g_trim_return_address != nullptr)
	{
		trim_stacktrace_print_helper(
			printer, trimmed_frame_start == 0 ? -1 : (i - trimmed_frame_start));
	}

#if 0
	tick2 = timer_now();
	slogf("bt time = %f\n", timer_delta_ms(tick1, tick2));
#endif
	return success;
}

#endif

MY_NOINLINE bool debug_stacktrace(debug_stacktrace_observer& printer, int skip)
{
#if defined(_WIN32)
	if(cv_bt_trap.data() == 1 || (cv_bt_trap.data() == 2 && (IsDebuggerPresent() != 0)))
#else
	if(cv_bt_trap.data() == 1)
#endif
	{
		MY_BREAKPOINT;
	}

#if __cpp_lib_stacktrace
	if(cv_bt_force_cpp23_stacktrace.data() != 0)
	{
		bool ret = write_cpp23_stacktrace(printer, skip + 1);

		// setting it to -1 will allow both stacktraces to run both. Why? I don't know.
		if(cv_bt_force_cpp23_stacktrace.data() != -1)
		{
			return ret;
		}
	}
#endif

#ifdef USE_WIN32_DEBUG_INFO
	if(write_win32_stacktrace(printer, skip + 1))
	{
		return true;
	}
#endif

#ifdef USE_LIBBACKTRACE
	if(write_libbacktrace_stacktrace(callback, ud, skip + 1))
	{
		return true;
	}
#endif

#if __cpp_lib_stacktrace
	return write_cpp23_stacktrace(printer, skip + 1);
#endif
	return false;
}

// if you wanted to print the name of a function
bool debug_write_function_info(debug_stacktrace_observer& printer, void* address)
{
#ifdef USE_WIN32_DEBUG_INFO
	if(debug_win32_write_function_detail(reinterpret_cast<uintptr_t>(address), printer))
	{
		return true;
	}
#endif

#ifdef USE_LIBBACKTRACE
	// TODO: libbacktrace?
	if(write_libbacktrace_stacktrace(callback, ud, skip + 1))
	{
		return true;
	}
#endif

	debug_stacktrace_info info{
		1, reinterpret_cast<uintptr_t>(address), nullptr, nullptr, nullptr, 0};
	return printer.print_line(&info);
}

bool debug_stacktrace_string_printer::print_line(debug_stacktrace_info* data)
{
	// can't call ASSERT here because ASSERT uses this function.
	if(data->module != NULL)
	{
		str_asprintf(str, "%s ! ", data->module);
	}
	if(data->function != NULL)
	{
		str_asprintf(str, "%s", data->function);
	}
	else
	{
		str_asprintf(str, "%" PRIxPTR, data->addr);
	}
	if(data->file != NULL)
	{
		str_asprintf(str, " [%s @ %d]\n", data->file, data->line);
	}
	else
	{
		str.push_back('\n');
	}
	return true;
}
void debug_stacktrace_string_printer::print_string_fmt(const char* fmt, ...)
{
	// can't call ASSERT here because ASSERT uses this function.
	va_list args;
	va_start(args, fmt);
	str_vasprintf(str, fmt, args);
	va_end(args);
}
#endif