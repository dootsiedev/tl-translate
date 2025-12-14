#pragma once

#include "global.h"
#include "cvar.h"

extern cvar_int cv_has_stacktrace;
extern cvar_int cv_bt_trap;
extern cvar_int cv_bt_demangle;
extern cvar_int cv_bt_full_paths;
extern cvar_int cv_bt_max_depth;
extern cvar_int cv_bt_ignore_skip;
extern cvar_int cv_bt_trim_stacktrace;
extern cvar_int cv_has_stacktrace_libbacktrace;
extern cvar_int cv_has_stacktrace_dbghelp;

#ifdef _WIN32
// stolen from whereami.h
/*
	int length = WIN32_getModulePath(addr, nullptr, 0, nullptr);
	// I don't think I need the +1 but I am unsure
	std::unique_ptr<char[]> path = std::make_unique<char[]>(length + 1);
	if(WIN32_getModulePath(addr, path.get(), length, nullptr) == length)
 */
int WIN32_getModulePath(void* address, char* out, int capacity, int* dirname_length);
// I could add in WIN32_getProgramPath if I needed it.
// int WIN32_getModulePath_(HMODULE* hmodule, char* out, int capacity, int* dirname_length);
#endif

#if defined(__EMSCRIPTEN__)

class debug_stacktrace_string_printer
{
public:
	std::string& str;
	debug_stacktrace_string_printer(std::string& str_) // NOLINT(*-explicit-constructor)
	: str(str_)
	{
	}
};

// if not optimized, I think you won't get any info without dwarf info (-g).
// technically NDEBUG is not a good variable to use because relwithdebinfo will exclude it
// but I remove NDEBUG from relwithdebinfo so it's not a problem.
#if !defined(NDEBUG)
#define HAS_STACKTRACE_PROBABLY
#endif

#include <emscripten.h>
// skip is ignored...
inline bool debug_stacktrace(debug_stacktrace_string_printer& out, int)
{
	// the problem with this is that there is no way to "skip" calls.
	char buffer[10000];
	// no error code, includes size of null terminator.
	// there is no way to detect if truncation occurred.
	int flags = 0;
	// #ifdef HAS_STACKTRACE_PROBABLY
	flags |= EM_LOG_C_STACK;
	// #endif
	int ret = emscripten_get_callstack(flags, buffer, sizeof(buffer));
	out.str.append(buffer, ret - 1);
	out.str += '\n';
	return true;
}
#else // __EMSCRIPTEN__

struct debug_stacktrace_info
{
	int index;
	uintptr_t addr;
	const char* module;
	const char* function;
	const char* file;
	int line;
};

// If I include the delegate header with CFI icall on clang-cl llvm 19, it crashes the linker.
#if 0
#include "3rdparty/CppDelegates/Delegate.h"

typedef SA::delegate<bool(debug_stacktrace_info* data, const char* error)> debug_stacktrace_cb_type;

struct debug_stacktrace_string_printer
{
	std::string& str;
	debug_stacktrace_string_printer(std::string& str_) // NOLINT(*-explicit-constructor)
	: str(str_)
	{
	}
	bool operator()(debug_stacktrace_info* data, const char* error);
};
#endif

class debug_stacktrace_observer
{
public:
	virtual bool print_line(debug_stacktrace_info* data) = 0;
	virtual void print_string(const char* message) = 0;
	virtual void print_string_fmt(MY_MSVC_PRINTF const char* fmt, ...)
		__attribute__((format(printf, 2, 3))) = 0;
	// not used but needed to avoid warnings.
	virtual ~debug_stacktrace_observer() = default;
};

class debug_stacktrace_string_printer : public debug_stacktrace_observer
{
	std::string& str;

public:
	explicit debug_stacktrace_string_printer(std::string& str_)
	: str(str_)
	{
	}
	bool print_line(debug_stacktrace_info* data) override;
	void print_string(const char* message) override
	{
		str.append(message);
	}
	void print_string_fmt(const char* fmt, ...) override;
};

// skip will remove function frames from the top of the stacktrace,
// you should use MY_NOINLINE if you are skipping a frame.
MY_NOINLINE bool debug_stacktrace(debug_stacktrace_observer& printer, int skip = 0);

// if you wanted to print the name of a function pointer
bool debug_write_function_info(debug_stacktrace_observer& printer, void* address);

// <stacktrace> C++23
// I am too lazy to replace __cpp_lib_stacktrace with USE_CPP_STACKTRACE
// I just undef it.
#ifndef USE_CPP_STACKTRACE
#if defined(__cpp_lib_stacktrace) && __cpp_lib_stacktrace
#undef __cpp_lib_stacktrace
#endif
#endif

#ifndef __cpp_lib_stacktrace
#define __cpp_lib_stacktrace 0
#endif

#if __cpp_lib_stacktrace && !defined(HAS_STACKTRACE_PROBABLY)
// Disable C++23 stacktrace if I am using CFI sanitizer.
#ifdef MY_FIX_CFI_ICALL
#undef __cpp_lib_stacktrace
#define __cpp_lib_stacktrace 0
#else
#define HAS_STACKTRACE_PROBABLY
#endif
#endif

// you need dbghelp which comes with windbg (not preview) I think, or I could just copy it.
#ifdef USE_WIN32_DEBUG_INFO

#if !defined(HAS_STACKTRACE_PROBABLY)
#define HAS_STACKTRACE_PROBABLY
#endif

#if !defined(HAS_DEBUG_FUNCTION_INFO)
#define HAS_DEBUG_FUNCTION_INFO
#endif

__declspec(noinline) bool write_win32_stacktrace(debug_stacktrace_observer& printer, int skip);

// exposed because debug_thread uses it to dump the trace
__declspec(noinline) bool
	debug_raw_win32_stacktrace(void* ctx, debug_stacktrace_observer& printer, int skip);

// Write the details of one function to the log file
NDSERR bool
	debug_win32_write_function_detail(uintptr_t stack_frame, debug_stacktrace_observer& printer);
#endif

#ifdef USE_LIBBACKTRACE

#if !defined(HAS_STACKTRACE_PROBABLY)
#define HAS_STACKTRACE_PROBABLY
#endif

#if !defined(HAS_DEBUG_FUNCTION_INFO)
#define HAS_DEBUG_FUNCTION_INFO
#endif

__attribute__((noinline)) bool
	write_libbacktrace_stacktrace(debug_stacktrace_observer& printer, int skip);
#ifdef __MINGW32__
__attribute__((noinline)) bool
	write_libbacktrace_mingw_stacktrace(debug_stacktrace_observer& printer, int skip);
#endif
__attribute__((noinline)) bool
	write_libbacktrace_function_detail(uintptr_t stack_frame, debug_stacktrace_observer& printer);
#endif

#ifdef HAS_STACKTRACE_PROBABLY

// internal use only.
// returns false if snprintf failed, serr is not written.
void trim_stacktrace_print_helper(
	debug_stacktrace_observer& printer, int trim_start, int total_frames);

extern thread_local void* g_trim_return_address;

// get the address of the current function using the return address.
// I could get the address of the function directly (explicitly) but I worry about inlining.
#ifndef __has_builtin
#define __has_builtin(x) 0 /* for non-clang compilers */
#endif
#if __has_builtin(__builtin_return_address)
#define TRIM_STACKTRACE_START \
	g_trim_return_address = __builtin_extract_return_addr(__builtin_return_address(0))
#elif defined(_MSC_VER)
#include <intrin.h>
#pragma intrinsic(_ReturnAddress)
#define TRIM_STACKTRACE_START g_trim_return_address = _ReturnAddress()
#endif
#define TRIM_STACKTRACE_END g_trim_return_address = nullptr

#endif // HAS_STACKTRACE_PROBABLY
#endif // __EMSCRIPTEN__

#ifndef TRIM_STACKTRACE_START
#define TRIM_STACKTRACE_START
#endif

#ifndef TRIM_STACKTRACE_END
#define TRIM_STACKTRACE_END
#endif

struct trim_stacktrace_end_raii
{
	~trim_stacktrace_end_raii()
	{
		TRIM_STACKTRACE_END;
	}
};

// convenience macro
#ifndef TRIM_STACKTRACE
#define TRIM_STACKTRACE    \
	TRIM_STACKTRACE_START; \
	trim_stacktrace_end_raii _trim_end_raii
#endif
