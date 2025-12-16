// This is an independent project of an individual developer. Dear PVS-Studio, please check it.
// PVS-Studio Static Code Analyzer for C, C++, C#, and Java: https://pvs-studio.com

#include "global.h"

// ASSERT will call debug_stacktrace, do not call ASSERT here.
#undef ASSERT

#include <cassert>

#include "stacktrace.h"

#include "cvar.h"

#include <cinttypes>
#include <cstdio>

#include <memory>

#include "../util/string_tools.h"

static int has_libbacktrace
#if defined(USE_LIBBACKTRACE)
	= 1;
#else
	= 0;
#endif
REGISTER_CVAR_INT(
	cv_has_stacktrace_libbacktrace, has_libbacktrace, "0 = not found, 1 = found", CVAR_T::READONLY);

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

enum
{
	BT_EARLY_EXIT_RETURN = 123456
};

// wrapper because static initialization of a constructor is thread safe.
struct bt_state_wrapper
{
	debug_stacktrace_observer* printer = nullptr;
	backtrace_state* state;

	static void error_callback(void* vdata, const char* msg, int errnum)
	{
		static_cast<bt_state_wrapper*>(vdata)->bt_error_callback(msg, errnum);
	};

	explicit bt_state_wrapper(debug_stacktrace_observer* printer_)
	: printer(printer_)
	, state(backtrace_create_state(NULL, 1, error_callback, this))
	{
		// the printer passed in will be freed, so the printer is also freed.
		printer = nullptr;
	}

	backtrace_state* get() const
	{
		return state;
	}

	void bt_error_callback(const char* msg, int errnum)
	{
		if(printer == nullptr)
		{
			// TODO: if errnum is positive it's an errno
			fprintf(
				stderr,
				"libbacktrace error(%d): %s\n",
				errnum,
				(msg != nullptr ? msg : "no error?"));
			return;
		}

		printer->print_string_fmt(
			"libbacktrace error(%d): %s\n", errnum, (msg != nullptr ? msg : "no error?"));
	}
};

struct bt_payload
{
	explicit bt_payload(bt_state_wrapper& state_, debug_stacktrace_observer& printer_)
	: state(state_)
	, printer(printer_)
	{
	}

	int idx = 0;
	// nested syminfo call...
	bt_state_wrapper& state;
	debug_stacktrace_observer& printer;
	// I don't want to get the module again when I use syminfo...
	const char* syminfo_module = nullptr;

	int trimmed_frame_start = -1;

#if 0
    void bt_error_callback(void *vdata, const char *msg, int errnum) {
        // remember to not use ASSERT
        assert(vdata != NULL);
        bt_payload *payload = static_cast<bt_payload *>(vdata);
        assert(payload->printer != NULL);

        if (payload == NULL || payload->printer == NULL) {
            fprintf(stderr, "libbacktrace error: %s\n", (msg != NULL ? msg : "no error?"));
            return;
        }

        // TODO: if errnum is positive it's an errno
        payload->printer->print_string_fmt("libbacktrace error(%d): %s\n", errnum, (msg != NULL ? msg : "no error?"));
    }
#endif
#if 1
	static void bt_syminfo_callback_wrap(
		void* vdata, uintptr_t pc, const char* function, uintptr_t symval, uintptr_t symsize)
	{
		(void)symval;
		(void)symsize;
		static_cast<bt_payload*>(vdata)->bt_syminfo_callback(pc, function);
	}
	void bt_syminfo_callback(uintptr_t pc, const char* function)
	{
#ifdef __GNUG__
		auto free_del = [](void* ptr) { free(ptr); };
		std::unique_ptr<char, decltype(free_del)> demangler{NULL, free_del};
		if(cv_bt_demangle.data() != 0 && function != NULL)
		{
			int status = 0;
			demangler.reset(abi::__cxa_demangle(function, NULL, NULL, &status));
			if(status == 0)
			{
				function = demangler.get();
			}
		}
#endif
		debug_stacktrace_info info{idx, pc, syminfo_module, function, NULL, 0};
		printer.print_line(&info);
	}
#endif

	static void error_callback_wrap(void* vdata, const char* msg, int errnum)
	{
		static_cast<bt_payload*>(vdata)->error_callback(msg, errnum);
	};

	void error_callback(const char* msg, int errnum)
	{
		++idx;
		printer.print_string_fmt(
			"libbacktrace error(%d): %s\n", errnum, (msg != NULL ? msg : "no error?"));
	}

	static int bt_full_callback_wrap(
		void* vdata, uintptr_t pc, const char* filename, int lineno, const char* function)
	{
		return static_cast<bt_payload*>(vdata)->bt_full_callback(pc, filename, lineno, function);
	}
	// I add +1 because backtrace_full won't match with the __builtin_return_address!
	static int bt_full_callback_plus_one_wrap(
		void* vdata, uintptr_t pc, const char* filename, int lineno, const char* function)
	{
		return static_cast<bt_payload*>(vdata)->bt_full_callback(
			pc + 1, filename, lineno, function);
	}

	int bt_full_callback(uintptr_t pc, const char* filename, int lineno, const char* function)
	{
		// don't know why this is always at the bottom of the stack in libbacktrace
		if(pc == static_cast<uintptr_t>(-1))
		{
			idx++;
			return 0;
		}

		if(idx >= cv_bt_max_depth.data())
		{
			idx++;
			return BT_EARLY_EXIT_RETURN;
		}

		if(trimmed_frame_start != -1 && idx >= trimmed_frame_start)
		{
			// I am counting the frames
			// TODO: it would be faster to count if I used a simple callback!
			//  technically on windows, I use
			idx++;
			return 0;
		}

		const char* module_name = NULL;

#if defined(__linux__)
		// gotta memcpy because it's possible uintptr_t and void* have incompatible alignment
		// clang-tidy will nag me if I cast it...
		void* addr;
		memcpy(&addr, &pc, sizeof(void*));

		Dl_info dinfo;
		if(dladdr(addr, &dinfo) != 0)
		{
			module_name = dinfo.dli_fname;
			if(cv_bt_full_paths.data() == 0)
			{
				module_name = remove_file_path(dinfo.dli_fname);
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
			// backtrace_syminfo does not work. on mingw.
			// I don't know if it works on linux.
			// backtrace_syminfo requires the symbol table but does not require the debug info.
			// I assume on linux this gives the same result as dinfo.dli_sname?
			syminfo_module = module_name;
			if(backtrace_syminfo(
				   state.get(), pc, bt_syminfo_callback_wrap, error_callback_wrap, this) == 1)
			{
				check_trimmed(pc, function);
				idx++;
				return 0;
			}
		}
#else
#ifdef USE_WIN32_DEBUG_INFO
		// if you have USE_LLVM_MINGW_PDB
		if(function == NULL)
		{
			// This will work, if you have both dwarf and -gcodeview data.
			// But debuggers will be confused by the dwarf info
			// (for example, windbg won't seek the source code / inspect variables,
			// BUT it will still find the exception location if you use -v analyze)
			// If you remove the dwarf info, it breaks libbacktrace.
			// So, the conclusion is, don't mix dwarf with codeview.
			// ATM I pass in -Xlinker --strip-debug, and it seems to keep the pdb info.
			if(debug_win32_write_function_detail(pc, printer))
			{
				check_trimmed(pc, function);
				idx++;
				return 0;
			}
		}
#endif

		// I could probably cache the module address
		// and reuse it instead of finding the path for every frame.
		// and I could also avoid some allocations.
		// but I have not benchmarked this.
		void* addr;
		memcpy(&addr, &pc, sizeof(void*));
		int length = WIN32_getModulePath(addr, nullptr, 0, nullptr);
		std::unique_ptr<char[]> path = std::make_unique<char[]>(length + 1);
		if(WIN32_getModulePath(addr, path.get(), length, nullptr) == length)
		{
			path[length] = '\0';
			module_name = path.get();
			if(cv_bt_full_paths.data() == 0)
			{
				module_name = remove_file_path(module_name);
			}
		}
#endif

#ifdef __GNUG__
		auto free_del = [](void* ptr) { free(ptr); };
		std::unique_ptr<char, decltype(free_del)> demangler{NULL, free_del};
		if(cv_bt_demangle.data() != 0 && function != NULL)
		{
			int status = 0;
			demangler.reset(abi::__cxa_demangle(function, NULL, NULL, &status));
			switch(status)
			{
			case 0: function = demangler.get(); break;
			case -1:
				printer.print_string(
					"abi::__cxa_demangle(-1): A memory allocation failure occurred.\n");
				break;
			case -2:
				// this is spammy.
				// payload->printer->print_string(
				//	"abi::__cxa_demangle(-2): mangled_name is not a valid name under the C++ ABI
				// mangling rules.\n");
				break;
			case -3:
				printer.print_string("abi::__cxa_demangle(-3_): One of the arguments is invalid.\n");
				break;
			default: printer.print_string_fmt("abi::__cxa_demangle(%d): unknown status.\n", status);
			}
		}
#endif

		if(filename != NULL)
		{
			if(cv_bt_full_paths.data() == 0)
			{
				filename = remove_file_path(filename);
			}
		}

		debug_stacktrace_info info{idx + 1, pc, module_name, function, filename, lineno};

		// NOTE: I could make an error will stop the stacktrace... do I want that?
		printer.print_line(&info);
		check_trimmed(pc, function);
		idx++;
		return 0;
	}

	void check_trimmed(uintptr_t addr, const char* function)
	{
		// convert the address to the same type
		if(cv_bt_trim_stacktrace.data() != 0 && g_trim_return_address != nullptr)
		{
			uintptr_t address_copy;
			memcpy(&address_copy, &g_trim_return_address, sizeof(address_copy));
			if(addr == address_copy)
			{
				if(trimmed_frame_start != -1)
				{
					printer.print_string_fmt("info: trim function found again at: %d\n", idx);
				}
				else
				{
					// printer->print_string("info: g_trim_return_address WORKS! get rid of the
					// other code!\n");
					//  I start trimming 1 frame below the target.
					trimmed_frame_start = idx + 1;
					return;
				}
			}
			(void)function;
#if 0
			if(function == nullptr)
			{
				return;
			}
			// I think the best way to avoid this is to just use libunwind.
			// But using DbgHelp StackWalk -> backtrace_pcinfo works decently.
			// AND I use DbgHelp for getting the Module name (BUT there are other ways to get it)
			// Instead of using StackWalk, I could use RtlCaptureStackBackTrace to avoid DbgHelp
			// BUT... StackWalk only works because I can access the ReturnAddr...
#define TRIM_FUNCTION_MAP(XX) \
	XX(SDL_Init)              \
	XX(SDL_AppIterate)        \
	XX(SDL_AppEvent)          \
	XX(SDL_AppQuit)
#define XX(x)                                                                          \
	if(strcmp(function, #x) == 0)                                                      \
	{                                                                                  \
		if(trimmed_frame_start != -1)                                                  \
		{                                                                              \
			printer.print_string_fmt("info: trim function found again at: %d\n", idx); \
		}                                                                              \
		else                                                                           \
		{                                                                              \
		}                                                                              \
		return;                                                                        \
	}
			TRIM_FUNCTION_MAP(XX)
#undef XX
#endif
		}
	}
};

bt_state_wrapper& get_bt_state(debug_stacktrace_observer* printer)
{
	static bt_state_wrapper state(printer);
	return state;
}

__attribute__((noinline)) bool
	write_libbacktrace_stacktrace(debug_stacktrace_observer& printer, int skip)
{
	bt_payload info(get_bt_state(&printer), printer);
	if(info.state.get() == nullptr)
	{
		return false;
	}
	// resolving the debug info is very slow, but you could offload the overhead by using
	// backtrace_simple and in another thread you can resolve the debug info from backtrace_pcinfo
	int ret = backtrace_full(
		info.state.get(),
		cv_bt_ignore_skip.data() == 0 ? (skip + 1) : 0,

#ifdef __MINGW32__
		bt_payload::bt_full_callback_plus_one_wrap,
#else
		// TODO: I need to test linux & and building libbacktrace before I remove this.
		bt_payload::bt_full_callback_wrap,
#endif
		bt_payload::error_callback_wrap,
		&info);

	trim_stacktrace_print_helper(printer, info.trimmed_frame_start, info.idx);
	return ret == 0 || ret == BT_EARLY_EXIT_RETURN;
}

#ifdef __MINGW32__

__attribute__((noinline)) bool
	write_libbacktrace_mingw_stacktrace(debug_stacktrace_observer& printer, int skip)
{
	bt_payload info(get_bt_state(&printer), printer);
	if(info.state.get() == nullptr)
	{
		return false;
	}

	// I noticed that using this would not give the optimized address for SDL_AppXXX functions...
	// Also this will print inlined templates (I wonder why libbacktrace excludes it)
	// BUT inlined templates are very incorrect, so I wonder what is going on...
	std::unique_ptr<void*[]> stack_info = std::make_unique<void*[]>(cv_bt_max_depth.data() + 1);

	// apparently this function breaks easily with anything nonstandard.
	// TODO: I think <stacktrace> won't retrieve function name, until you call name()
	//  if that is true, I can just use the address part.
	// TODO: cv_bt_max_depth wont work with -1
	int result = RtlCaptureStackBackTrace(
		cv_bt_ignore_skip.data() == 0 ? (skip + 1) : 0,
		cv_bt_max_depth.data() + 1,
		stack_info.get(),
		nullptr);

	int ret = 0;
	for(int i = 0; i < result && ret != BT_EARLY_EXIT_RETURN; i++)
	{
		uintptr_t address_copy;
		memcpy(&address_copy, &stack_info[i], sizeof(address_copy));
		ret = backtrace_pcinfo(
			info.state.get(),
			address_copy,
			bt_payload::bt_full_callback_wrap,
			bt_payload::error_callback_wrap,
			&info);
	}

	trim_stacktrace_print_helper(printer, info.trimmed_frame_start, info.idx);

	return result > 0;
}
#endif

__attribute__((noinline)) bool
	write_libbacktrace_function_detail(uintptr_t stack_frame, debug_stacktrace_observer& printer)
{
	bt_payload info(get_bt_state(&printer), printer);
	if(info.state.get() == nullptr)
	{
		return false;
	}
	// resolving the debug info is very slow, but you could offload the overhead by using
	// backtrace_simple and in another thread you can resolving the debug info from backtrace_pcinfo
	int ret = backtrace_pcinfo(
		info.state.get(),
		stack_frame,
		bt_payload::bt_full_callback_wrap,
		bt_payload::error_callback_wrap,
		&info);

	return ret == 0;
}

#endif

#endif // __EMSCRIPTEN__
