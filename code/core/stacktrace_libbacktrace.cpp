// This is an independent project of an individual developer. Dear PVS-Studio, please check it.
// PVS-Studio Static Code Analyzer for C, C++, C#, and Java: https://pvs-studio.com

#include "global_pch.h"
#include "global.h"

#include "stacktrace.h"

#include "cvar.h"

#include <cinttypes>
#include <cstdio>

// because I don't include global.h
#include <cassert>
#ifndef ASSERT
#define ASSERT assert
#endif

#include <memory>

static int has_libbacktrace
#if defined(USE_LIBBACKTRACE)
	= 1;
#else
	= 0;
#endif
static REGISTER_CVAR_INT(
	cv_has_stacktrace_libbacktrace, has_libbacktrace, "0 = not found, 1 = found", CVAR_T::READONLY);


#ifndef USE_WIN32_DEBUG_INFO
#ifndef __EMSCRIPTEN__

// I haven't tested it, but I think msys could work with libbacktrace.
#ifdef USE_LIBBACKTRACE
#include <backtrace.h>

#if defined(__linux__)
#include <dlfcn.h> // for dladdr
#endif

#ifdef __GNUG__
#include <cxxabi.h>
#endif




struct bt_payload
{
	debug_stacktrace_callback call = NULL;
	int idx = 0;
	void* ud = NULL;
	// nested syminfo call...
	backtrace_state* state;
	// I don't want to get the module again when I use syminfo.
	const char* syminfo_module = NULL;
};

static void bt_error_callback(void* vdata, const char* msg, int errnum)
{
	(void)errnum;
	ASSERT(vdata != NULL);
	bt_payload* payload = static_cast<bt_payload*>(vdata);
	ASSERT(payload->call != NULL);

	if(payload == NULL || payload->call == NULL)
	{
		fprintf(stderr, "libbacktrace error: %s\n", (msg != NULL ? msg : "no error?"));
		return;
	}

	payload->call(NULL, (msg != NULL ? msg : "no error?"), payload->ud);
}

static void bt_syminfo_callback(
	void* vdata, uintptr_t pc, const char* function, uintptr_t symval, uintptr_t symsize)
{
	bt_payload* payload = static_cast<bt_payload*>(vdata);
	(void)symval;
	(void)symsize;
#ifdef __GNUG__
	auto free_del = [](void* ptr) { free(ptr); };
	std::unique_ptr<char, decltype(free_del)> demangler{NULL, free_del};
	if(cv_bt_demangle.data != 0 && function != NULL)
	{
		int status = 0;
		demangler.reset(abi::__cxa_demangle(function, NULL, NULL, &status));
		if(status == 0)
		{
			function = demangler.get();
		}
	}
#endif
	debug_stacktrace_info info{payload->idx, pc, payload->syminfo_module, function, NULL, 0};

	payload->call(&info, NULL, payload->ud);
}

static int bt_full_callback(
	void* vdata, uintptr_t pc, const char* filename, int lineno, const char* function)
{
	bt_payload* payload = static_cast<bt_payload*>(vdata);

	// don't know why this is always at the bottom of the stack in libbacktrace
	if(pc == static_cast<uintptr_t>(-1))
	{
		return 0;
	}

	// TODO: I don't use cv_bt_max_depth, it would be nice if I did.
	++payload->idx;

	const char* module_name = NULL;

	// TODO: on windows I could get the module name without windbg
	// 	by referencing whereami
	// something like: GetModuleHandleEx(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS
	// | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT
	// then use wai getModulePath_
#if defined(__linux__)

	// gotta memcpy because it's possible uintptr_t and void* have incompatible alignment
	// clang-tidy will nag me if I cast it...
	void* addr;
	memcpy(&addr, &pc, sizeof(void*));

	Dl_info dinfo;
	if(dladdr(addr, &dinfo) != 0)
	{
		module_name = dinfo.dli_fname;
		if(cv_bt_full_paths.data == 0)
		{
			const char* temp_module_name = strrchr(dinfo.dli_fname, '/');
			if(temp_module_name != NULL)
			{
				module_name = temp_module_name + 1;
			}
		}

		// a fallback, this only works because the functions are public
		// this won't work if you use -fvisibility=hidden, or inline optimizations.
		if(function == NULL)
		{
			function = dinfo.dli_sname;
		}
	}

	if(function == NULL)
	{
		// backtrace_syminfo requires the symbol table but does not require the debug info
		// technically this should give the same result as dinfo.dli_sname, but maybe it does something else?
		// this works with mingw, but you should use StalkWalk + Dbghelp to handle both mingw and msvc debug info.
		payload->syminfo_module = module_name;
		if(backtrace_syminfo(payload->state, pc, bt_syminfo_callback, bt_error_callback, vdata) ==
		   1)
		{
			return 0;
		}
	}
#endif

#ifdef __GNUG__
	auto free_del = [](void* ptr) { free(ptr); };
	std::unique_ptr<char, decltype(free_del)> demangler{NULL, free_del};
	if(cv_bt_demangle.data != 0 && function != NULL)
	{
		int status = 0;
		demangler.reset(abi::__cxa_demangle(function, NULL, NULL, &status));
		if(status == 0)
		{
			function = demangler.get();
		}
	}
#endif

	if(filename != NULL)
	{
		if(cv_bt_full_paths.data == 0)
		{
			const char* temp_filename = strrchr(filename, '/');
			if(temp_filename != NULL)
			{
				filename = temp_filename + 1;
			}
		}
	}

	debug_stacktrace_info info{payload->idx, pc, module_name, function, filename, lineno};

	// NOTE: can an error end the stack walk? I probably don't want that.
	return payload->call(&info, NULL, payload->ud);
}

// wrapper because static initialization of a constructor is thread safe.
struct bt_state_wrapper
{
	backtrace_state* state;
	explicit bt_state_wrapper(bt_payload& info)
	: state(backtrace_create_state(NULL, 1, bt_error_callback, &info))
	{
	}
};

__attribute__((noinline)) bool
	write_libbacktrace_stacktrace(debug_stacktrace_callback callback, void* ud, int skip)
{
	bt_payload info;
	info.call = callback;
	info.ud = ud;
	info.idx = 0;
	info.syminfo_module = NULL;

	static bt_state_wrapper state(info);
	info.state = state.state;
	if(state.state == NULL)
	{
		return false;
	}
#if 0
	TIMER_U tick1;
	TIMER_U tick2;
	tick1 = timer_now();
#endif
	// resolving the debug info is very slow, but you could offload the overhead by using
	// backtrace_simple and in another thread you can resolving the debug info from backtrace_pcinfo
	int ret = backtrace_full(state.state, skip + 1, bt_full_callback, bt_error_callback, &info);

#if 0
	tick2 = timer_now();
	slogf("bt time = %f\n", timer_delta_ms(tick1, tick2));
#endif
	return ret == 0;
}

#endif

#endif // __EMSCRIPTEN__
#endif // USE_WIN32_DEBUG_INFO