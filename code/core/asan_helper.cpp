// This is an independent project of an individual developer. Dear PVS-Studio, please check it.
// PVS-Studio Static Code Analyzer for C, C++, C#, and Java: https://pvs-studio.com
#include "global.h"
#include "asan_helper.h"

#ifdef MY_HAS_ASAN

#include <stdlib.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#ifdef ERROR
#undef ERROR
#endif
#endif

// clang asan does not have this function.
#if defined(_MSC_VER) && !defined(__clang__)
#define USE_ASAN_COE
inline int setenv(const char* name, const char* value, int overwrite)
{
	int errcode = 0;
	if(!overwrite)
	{
		size_t envsize = 0;
		errcode = getenv_s(&envsize, NULL, 0, name);
		if(errcode || envsize) return errcode;
	}
	return _putenv_s(name, value);
}
extern "C" const char* __asan_default_options()
{
	if(IsDebuggerPresent() == 0)
	{
#ifdef USE_ASAN_COE
		// I don't need continue on error
		// but asan on windows does not play with JIT debugging well enough.
		// even if JIT gets fixed, I like this more than opening VS studio.
		// and this config is better for releasing to people without debuggers (if it's possible).
		// I know I can make coredumps, but it's like 200-500mb uncompressed.
		// TODO: I should also disable this behavior, maybe a macro, or check the arguments.
		//  or use include_if_exists
#ifndef DISABLE_SDL
		if(SDL_setenv_unsafe("COE_LOG_FILE", "ASAN_REPORT.log", 0) != 0)
		{
			fprintf(stderr, "SDL_setenv_unsafe: %s\n", SDL_GetError());
		}
#elif 0
		// I don't know why but this triggers address sanitizer with a stack overflow in LLDB.
		// this would not happen in SDL
		if(SetEnvironmentVariable("COE_LOG_FILE", "ASAN_REPORT.log") == FALSE)
		{
			fprintf(
				stderr,
				"SetEnvironmentVariableW: %s\n",
				WIN_GetFormattedGLE(GetLastError()).c_str());
		}
#else
		int ret = setenv("COE_LOG_FILE", "ASAN_REPORT.log", 0);
		if(ret != 0)
		{
			fprintf(stderr, "setenv: %s\n", strerror(ret));
		}
#endif
		// Fast fail won't override continue on error, but not all asan errors can be continued.
		// So in that case, I need fastfail so that it makes the error debuggable.
		// (I assume the reason why it's not fastfail by default is because
		// fastfail will create a redundant stacktrace inside of a CI test???)
		// Technically ASAN_VCASAN_DEBUGGING should do the job of debugging,
		// but ATM VS jit debugger cannot inspect any info from a crash that can't be continued
		// (but dumping with task manager works, but VS jit debug will close it...)
		// I personally just use WER to dump mini-dumps into a folder.
		return "windows_fast_fail_on_error=1:continue_on_error=1";
#endif
		// I like fastfail because it reports to WER (asan just calls _exit() I believe).
		// to use WER you need to set windows registry values to create local dumps,
		// BUT if I started using a tool like sentry, fastfail would prevent callbacks from working.
		// (should I collect info inside the asan callback if I had sentry?
		// should I allow asan to run outside the dev environment?
		// Can I send a stream to wer during a fastfail?)
		return "windows_fast_fail_on_error=1";
	}
	return "";
}
#else
extern "C" const char* __asan_default_options()
{
	return "strict_string_checks=1:detect_stack_use_after_return=1:check_initialization_order=1:strict_init_order=1";
}
#endif

void my_asan_handler(const char* msg)
{
#ifdef USE_ASAN_COE
	// print the message in Continue-on-error
	// because it is written to a file, not stdout/stderr
	// (if slog started printing to a file without redirection, add that here)
	fflush(stdout);
	fputs(msg, stderr);
	// on windows it wont flush on crash if it's piped?
	fflush(stderr);
#endif
	{
		// I like the idea of opening a dialog box,
		// since terminals are annoying.
		// but it's not great because the stacktraces are HUGE
		// so it will easily truncate.
		show_error("Address Sanitizer (see ASAN_REPORT.log)", msg);
	}
	// clang cl does not support windows_fast_fail_on_error or abort_on_error, so I do it here.
	// technically it says "aborting" but it doesn't open the abort dialog / appear in WER.
	// (probably because abort() depends on the CRT which I bet the asan.dll does not link to)
#if defined(_WIN32) && defined(__clang__)
	// 71 is copied from windows_fast_fail_on_error
	//__fastfail(71);
	// I feel like this SHOULD move the exception address to the address you pass in...
	// but it wont....
#if 1
	EXCEPTION_RECORD record;
	memset(&record, 0, sizeof(record));
	record.ExceptionAddress = __asan_get_report_pc();
	record.ExceptionCode = 71;
	record.NumberParameters = 2;
	record.ExceptionInformation[0] = reinterpret_cast<uintptr_t>(__asan_get_report_pc());
	record.ExceptionInformation[0] = reinterpret_cast<uintptr_t>(__asan_get_report_address());
	RaiseFailFastException(&record, NULL, FAIL_FAST_GENERATE_EXCEPTION_ADDRESS);
#endif
#endif
}
void init_asan()
{
#ifdef _WIN32
	if(IsDebuggerPresent() == 0)
#endif
	{
		__asan_set_error_report_callback(my_asan_handler);
	}
}
#endif