// This is an independent project of an individual developer. Dear PVS-Studio, please check it.
// PVS-Studio Static Code Analyzer for C, C++, C#, and Java: https://pvs-studio.com

#include "global_pch.h"
#include "stacktrace.h"
#include "breakpoint.h"

#include "../util/string_tools.h"

// ASSERT will call debug_stacktrace, do not call ASSERT here.
// BUT one problem is that I use cvars which uses ASSERT for .data().
// but the only way it will trigger is if you call ASSERT before loading cvars.
#undef ASSERT

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
#else
static int has_stacktrace_cpp23 = 0;
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
	// TODO: C++26 adds a IsDebugger to the standard, will it work on linux?
	"debug breakpoint for the debugger, 0 (off), 1 (on), 2 (windows only: only trap inside a debugger)",
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

enum class BT_TYPES
{
	ALL = -1,
	DEFAULT = 0,
	CPP_STACKTRACE = 1,
	WIN32_DBGHELP = 2,
	LIBBACKTRACE = 3,
	// libbacktrace still works with mingw,
	// but instead of letting libbacktrace walk the stack (using libunwind?)
	// I use RtlCaptureStackBackTrace.
	// This fixes TRIM_STACKTRACE (I don't know why), but it also introduces other quirks.
	LIBBACKTRACE_MINGW = 4
};

// hacky forward declaration...
extern cvar_int cv_bt_stacktrace_override;
static void cvar_init_stacktrace_override(cvar_int& self)
{
	self._internal_data = 0;
	self.cvar_comment =
		"set which stacktrace to use, 0 = pick default, 1 = c++23 <stacktrace>, 2 = dbghelp, 3 = libbacktrace, -1 = ALL (print redundant stacktrace for comparing)";
	self.internal_cvar_type = disable_cvar_if_no_stacktrace;
	self.cond_cb = [](int value) -> V_cvar* {
		switch(static_cast<BT_TYPES>(value))
		{
		case BT_TYPES::ALL: return nullptr;
		case BT_TYPES::DEFAULT: return nullptr;
		case BT_TYPES::CPP_STACKTRACE:
			if(cv_has_stacktrace_cpp23.internal_data() == 0)
			{
				return &cv_has_stacktrace_cpp23;
			}
			return nullptr;
		case BT_TYPES::WIN32_DBGHELP:
			if(cv_has_stacktrace_dbghelp.internal_data() == 0)
			{
				return &cv_has_stacktrace_dbghelp;
			}
			return nullptr;
		case BT_TYPES::LIBBACKTRACE:
			if(cv_has_stacktrace_libbacktrace.internal_data() == 0)
			{
				return &cv_has_stacktrace_libbacktrace;
			}
			return nullptr;
		case BT_TYPES::LIBBACKTRACE_MINGW:
			if(cv_has_stacktrace_libbacktrace.internal_data() == 0)
			{
				return &cv_has_stacktrace_libbacktrace;
			}
#ifndef __MINGW32__
			slogf(
				"warning: this option is for libbacktrace mingw %s = %d\nusage: %s\n\n",
				cv_bt_stacktrace_override.cvar_key,
				value,
				cv_bt_stacktrace_override.cvar_comment);
#endif
			return nullptr;
		}
		// TODO: (this should be in the cvar source)
		//  I should include the self value, and have a function for printing warnings...
		//  for a dialog or something... the warnings should also apply to the blame.
		//  and instead of returning the blame, I should use the callback,
		//  since multiple blames could exist.
		//  And the blame should also include the location (default, argv, or file)
		slogf(
			"warning: unknown value for %s = %d\nusage: %s\n\n",
			cv_bt_stacktrace_override.cvar_key,
			value,
			cv_bt_stacktrace_override.cvar_comment);
		return nullptr;
	};
}
// TODO: since I map this to an enum... why not make this an enum cvar?
//  raw numbers are pretty bad...
cvar_int INIT_CVAR(cv_bt_stacktrace_override, cvar_init_stacktrace_override);

// I try to leave 1 frame below, so that at least one frame is from SDL3.dll
// if more frames would help, I could make a positive number leak more frames in.
REGISTER_CVAR_INT(
	cv_bt_trim_stacktrace,
	1,
	"Remove unhelpful frames at the bottom of the stacktrace, 0 = off (print all frames), 1 = on (trim)",
	disable_cvar_if_no_stacktrace);

#ifdef HAS_STACKTRACE_PROBABLY
thread_local void* g_trim_return_address;

void trim_stacktrace_print_helper(debug_stacktrace_observer& printer, int trim_start, int total)
{
	if(cv_bt_trim_stacktrace.data() != 0 && g_trim_return_address != nullptr)
	{
		if(trim_start == -1)
		{
			printer.print_string_fmt(
				"trimmed frame not found... (%s = %d)\n",
				cv_bt_trim_stacktrace.cvar_key,
				cv_bt_trim_stacktrace.data());
			return;
		}
		else
		{
			printer.print_string_fmt(
				"trimmed frames: %d (%s = %d)\n",
				(total - trim_start),
				cv_bt_trim_stacktrace.cvar_key,
				cv_bt_trim_stacktrace.data());
			return;
		}
	}
	if(total > cv_bt_max_depth.data())
	{
		printer.print_string_fmt(
			"max depth reached (%s = %d)\n", cv_bt_max_depth.cvar_key, cv_bt_max_depth.data());
	}
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
	auto bt = std::stacktrace::current(cv_bt_ignore_skip.data() == 0 ? (skip + 1) : 0);
	skip = 0; // skip++; I figured out <stacktrace> has it's own skip too late...
	int i = 0;
	int trimmed_frame_start = 0;
	for(auto& frame : bt)
	{
		// NOTE: I think it would be cool if instead of truncating the trace, I also print the very
		// last 10~ frames
		if(i > cv_bt_max_depth.data())
		{
			break;
		}

		if(cv_bt_trim_stacktrace.data() == 0 || g_trim_return_address == nullptr ||
		   trimmed_frame_start == -1 || i < trimmed_frame_start)
		{
			if(i >= skip || cv_bt_ignore_skip.data() == 1)
			{
				std::string name = frame.description();
				std::string file = frame.source_file();
				int line = frame.source_line();

				if(cv_bt_full_paths.data() == 0)
				{
					file = remove_file_path(file.c_str());
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

	trim_stacktrace_print_helper(printer, trimmed_frame_start, i);

#if 0
	tick2 = timer_now();
	slogf("bt time = %f\n", timer_delta_ms(tick1, tick2));
#endif
	return success;
}

#endif

MY_NOINLINE bool debug_stacktrace(debug_stacktrace_observer& printer, int skip)
{
	// TODO: I should break AFTER printing a stacktrace...
#if defined(_WIN32)
	if(cv_bt_trap.data() == 1 || (cv_bt_trap.data() == 2 && (IsDebuggerPresent() != 0)))
#else
	if(cv_bt_trap.data() == 1)
#endif
	{
		MY_BREAKPOINT;
	}

#ifdef HAS_STACKTRACE_PROBABLY
	// this is not great... this would be easier to understand if I didn't use a switch.
	// maybe... goto...
	bool success = true;
	bool print_once = true;
	std::optional<bool> ret;
	switch(static_cast<BT_TYPES>(cv_bt_stacktrace_override.data()))
	{
	case BT_TYPES::ALL: print_once = false; [[fallthrough]];
	case BT_TYPES::DEFAULT:
		// note that the order is sets the default.
	case BT_TYPES::LIBBACKTRACE:
		if(cv_has_stacktrace_libbacktrace.data() != 0)
		{
#ifdef USE_LIBBACKTRACE
			if(!print_once) printer.print_string("\nlibbacktrace:\n");
			ret = write_libbacktrace_stacktrace(printer, skip + 1);
			if(print_once)
			{
				return *ret;
			}
			success = success && ret;
#else
			abort();
#endif
		}
		[[fallthrough]];
	case BT_TYPES::LIBBACKTRACE_MINGW:
#ifdef __MINGW32__
		if(cv_has_stacktrace_libbacktrace.data() != 0)
		{
#ifdef USE_LIBBACKTRACE
			if(!print_once) printer.print_string("\nlibbacktrace mingw:\n");
			ret = write_libbacktrace_mingw_stacktrace(printer, skip + 1);
			if(print_once)
			{
				return *ret;
			}
			success = success && ret;
#else
			abort();
#endif
		}
#endif
		[[fallthrough]];
	case BT_TYPES::WIN32_DBGHELP:
		if(cv_has_stacktrace_dbghelp.data() != 0)
		{
#ifdef USE_WIN32_DEBUG_INFO
			if(!print_once) printer.print_string("\ndbghelp:\n");
			ret = write_win32_stacktrace(printer, skip + 1);
			if(print_once)
			{
				return *ret;
			}
			success = success && ret;
#else
			abort();
#endif
		}
		[[fallthrough]];

	case BT_TYPES::CPP_STACKTRACE:
		if(cv_has_stacktrace_cpp23.data() != 0)
		{
#if __cpp_lib_stacktrace
			if(!print_once) printer.print_string("\nC++ <stacktrace>:\n");
			ret = write_cpp23_stacktrace(printer, skip + 1);
			if(print_once)
			{
				return *ret;
			}
			success = success && ret;
#else
			abort();
#endif
		}
	}
	if(!ret.has_value())
	{
		printer.print_string_fmt(
			"failed to print a stacktrace somehow... (%s = %d)\n",
			cv_bt_stacktrace_override.cvar_key,
			cv_bt_stacktrace_override.data());
		return false;
	}
	return success;
#else
	printer.print_string(
		"debug_stacktrace unimplemented... (build with USE_CPP_STACKTRACE or USE_WIN32_DEBUG_INFO or USE_LIBBACKTRACE)\n");
	return false;
#endif
}

// if you wanted to print the name of a function
bool debug_write_function_info(debug_stacktrace_observer& printer, void* address)
{
#ifdef HAS_DEBUG_FUNCTION_INFO
	bool success = true;
	bool print_once = true;
	std::optional<bool> ret;
	switch(static_cast<BT_TYPES>(cv_bt_stacktrace_override.data()))
	{
	case BT_TYPES::CPP_STACKTRACE: abort();
	case BT_TYPES::ALL: print_once = false; [[fallthrough]];
	case BT_TYPES::DEFAULT:
	case BT_TYPES::LIBBACKTRACE_MINGW:
	case BT_TYPES::LIBBACKTRACE:
		if(cv_has_stacktrace_libbacktrace.data() != 0)
		{
#ifdef USE_LIBBACKTRACE
			if(!print_once) printer.print_string("libbacktrace:\n");
			ret = write_libbacktrace_function_detail(reinterpret_cast<uintptr_t>(address), printer);
			if(print_once)
			{
				return *ret;
			}
			success = success && ret;
#else
			abort();
#endif
		}
		[[fallthrough]];
	case BT_TYPES::WIN32_DBGHELP:
		if(cv_has_stacktrace_dbghelp.data() != 0)
		{
#ifdef USE_WIN32_DEBUG_INFO
			if(!print_once) printer.print_string("\ndbghelp:\n");
			ret = debug_win32_write_function_detail(reinterpret_cast<uintptr_t>(address), printer);
			if(print_once)
			{
				return *ret;
			}
			success = success && ret;
#else
			abort();
#endif
		}
	}
	if(!ret.has_value())
	{
		printer.print_string_fmt(
			"error: no result from debug_write_function_info (%s = %d)\n",
			cv_bt_stacktrace_override.cvar_key,
			cv_bt_stacktrace_override.data());
		return false;
	}
	return success;
#else
	printer.print_string(
		"debug_write_function_info unimplemented... (build with USE_WIN32_DEBUG_INFO or USE_LIBBACKTRACE)\n");
	return false;
#endif
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