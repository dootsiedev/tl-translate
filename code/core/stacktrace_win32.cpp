// This is an independent project of an individual developer. Dear PVS-Studio, please check it.
// PVS-Studio Static Code Analyzer for C, C++, C#, and Java: https://pvs-studio.com

#include "global_pch.h"
#include "global.h"

#include "stacktrace.h"

// for remove_file_path
#include "../util/string_tools.h"

#ifdef USE_WIN32_DEBUG_INFO
static int has_stacktrace_dbghelp = 1;
static CVAR_T disable_cvar_if_no_dbghelp = CVAR_T::RUNTIME;
#else
static int has_stacktrace_dbghelp = 0;
static CVAR_T disable_cvar_if_no_dbghelp = CVAR_T::DISABLED;
#endif
REGISTER_CVAR_INT(
	cv_has_stacktrace_dbghelp,
	has_stacktrace_dbghelp,
	"0 = not found, 1 = found",
	CVAR_T::READONLY);

// this exists because of boost spirit, and maybe
static REGISTER_CVAR_INT(
	cv_bt_show_inlined_functions,
	has_stacktrace_dbghelp,
	"0 = hide, 1 = show (only for dbghelp)",
	disable_cvar_if_no_dbghelp);

#ifdef USE_WIN32_DEBUG_INFO

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <DbgHelp.h>

// this code was based on some apache crashrpt library.
// but it's modified to an unrecognizable point.

// dbghelp is dynamically loaded, because it should be optional.
// And since this is thread local, this has the added benefit that I don't need to have mutexes.
#define DBGHELP_DLL "dbghelp.dll"

// public functions in dbghelp.dll
/*
typedef BOOL(WINAPI* MINIDUMPWRITEDUMP)(
	HANDLE hProcess,
	DWORD ProcessId,
	HANDLE hFile,
	MINIDUMP_TYPE DumpType,
	CONST PMINIDUMP_EXCEPTION_INFORMATION ExceptionParam,
	CONST PMINIDUMP_USER_STREAM_INFORMATION UserStreamParam,
	CONST PMINIDUMP_CALLBACK_INFORMATION CallbackParam);
	*/
typedef BOOL(WINAPI* SYMINITIALIZE)(HANDLE hProcess, PSTR UserSearchPath, BOOL fInvadeProcess);
typedef DWORD(WINAPI* SYMSETOPTIONS)(DWORD SymOptions);
typedef DWORD(WINAPI* SYMGETOPTIONS)(VOID);
typedef BOOL(WINAPI* SYMCLEANUP)(HANDLE hProcess);
typedef BOOL(WINAPI* SYMGETLINEFROMADDR64)(
	HANDLE hProcess, DWORD64 dwAddr, PDWORD pdwDisplacement, PIMAGEHLP_LINE64 Line);
typedef BOOL(WINAPI* SYMFROMADDR)(
	HANDLE hProcess, DWORD64 Address, PDWORD64 Displacement, PSYMBOL_INFO Symbol);
typedef BOOL(WINAPI* STACKWALK64)(
	DWORD MachineType,
	HANDLE hProcess,
	HANDLE hThread,
	LPSTACKFRAME64 StackFrame,
	PVOID ContextRecord,
	PREAD_PROCESS_MEMORY_ROUTINE64 ReadMemoryRoutine,
	PFUNCTION_TABLE_ACCESS_ROUTINE64 FunctionTableAccessRoutine,
	PGET_MODULE_BASE_ROUTINE64 GetModuleBaseRoutine,
	PTRANSLATE_ADDRESS_ROUTINE64 TranslateAddress);
typedef PVOID(WINAPI* SYMFUNCTIONTABLEACCESS64)(HANDLE hProcess, DWORD64 AddrBase);
typedef DWORD64(WINAPI* SYMGETMODULEBASE64)(HANDLE hProcess, DWORD64 dwAddr);
typedef DWORD(WINAPI* UNDECORATESYMBOLNAME)(
	PCSTR name, PSTR outputString, DWORD maxStringLength, DWORD flags);
typedef BOOL (*SYMGETMODULEINFO)(HANDLE hProcess, DWORD64 qwAddr, PIMAGEHLP_MODULE64 ModuleInfo);

typedef DWORD(WINAPI* SYMADDRINCLUDEINLINETRACE)(HANDLE hProcess, DWORD64 Address);
typedef BOOL(WINAPI* SYMQUERYINLINETRACE)(
	HANDLE hProcess,
	DWORD64 StartAddress,
	DWORD StartContext,
	DWORD64 StartRetAddress,
	DWORD64 CurAddress,
	LPDWORD CurContext,
	LPDWORD CurFrameIndex);
typedef BOOL(WINAPI* SYMFROMINLINECONTEXT)(
	HANDLE hProcess,
	DWORD64 Address,
	ULONG InlineContext,
	PDWORD64 Displacement,
	PSYMBOL_INFO Symbol);
typedef BOOL(WINAPI* SYMGETLINEFROMINLINECONTEXT)(
	HANDLE hProcess,
	DWORD64 qwAddr,
	ULONG InlineContext,
	DWORD64 qwModuleBaseAddress,
	PDWORD pdwDisplacement,
	PIMAGEHLP_LINE64 Line64);

#define DBGHELP_FUNC_MAP(XX)                                 \
	XX(SymInitialize, SYMINITIALIZE)                         \
	XX(SymSetOptions, SYMSETOPTIONS)                         \
	XX(SymGetOptions, SYMGETOPTIONS)                         \
	XX(SymCleanup, SYMCLEANUP)                               \
	XX(SymGetLineFromAddr64, SYMGETLINEFROMADDR64)           \
	XX(SymFromAddr, SYMFROMADDR)                             \
	XX(StackWalk64, STACKWALK64)                             \
	XX(SymFunctionTableAccess64, SYMFUNCTIONTABLEACCESS64)   \
	XX(SymGetModuleBase64, SYMGETMODULEBASE64)               \
	XX(UnDecorateSymbolName, UNDECORATESYMBOLNAME)           \
	XX(SymGetModuleInfo, SYMGETMODULEINFO)                   \
	XX(SymAddrIncludeInlineTrace, SYMADDRINCLUDEINLINETRACE) \
	XX(SymQueryInlineTrace, SYMQUERYINLINETRACE)             \
	XX(SymFromInlineContext, SYMFROMINLINECONTEXT)           \
	XX(SymGetLineFromInlineContext, SYMGETLINEFROMINLINECONTEXT)

struct DBGHELP_CONTEXT
{
	HMODULE dbghelp_dll;
	// function pointers
#define XX(name, type) type name##_;
	DBGHELP_FUNC_MAP(XX)
#undef XX
};

static thread_local DBGHELP_CONTEXT dbg_ctx;

// I could probably use a callback to merge the inline and non-inline function...
__attribute__((no_sanitize("cfi-icall"))) void write_inline_function_detail(
	HANDLE proc,
	DWORD64 address,
	DWORD inline_context,
	int nr_of_frame,
	debug_stacktrace_observer& printer)
{
	const char* filename = NULL;
	int lineno = 0;
	const char* function = NULL;
	const char* module_name = NULL;
	char function_buffer[500];

	IMAGEHLP_MODULE moduleInfo;
	::ZeroMemory(&moduleInfo, sizeof(moduleInfo));
	moduleInfo.SizeOfStruct = sizeof(moduleInfo);
	std::unique_ptr<char[]> module_path;
	// gets the fuller path, I should replace dbghelp, but I assume this is slower.
	if(cv_bt_full_paths.data() != 0)
	{
		// NOLINTNEXTLINE(*-no-int-to-ptr)
		int length = WIN32_getModulePath(reinterpret_cast<void*>(address), nullptr, 0, nullptr);
		module_path = std::make_unique<char[]>(length + 1);
		// NOLINTNEXTLINE(*-no-int-to-ptr)
		if(WIN32_getModulePath(
			   reinterpret_cast<void*>(address), module_path.get(), length, nullptr) == length)
		{
			module_name = module_path.get();
		}
	}
	else
	{
		if(dbg_ctx.SymGetModuleInfo_(proc, address, &moduleInfo) != FALSE)
		{
			module_name = moduleInfo.ModuleName;
		}
	}

	ULONG64
	symbolBuffer[(sizeof(SYMBOL_INFO) + MAX_SYM_NAME + sizeof(ULONG64) - 1) / sizeof(ULONG64)];
	PSYMBOL_INFO pIHS = (PSYMBOL_INFO)symbolBuffer;
	pIHS->SizeOfStruct = sizeof(SYMBOL_INFO);
	pIHS->MaxNameLen = MAX_SYM_NAME;

	// I copied this from
	// https://github.com/rogerorr/articles/blob/main/Debugging_Optimised_Code/SimpleStackWalker.cpp
	DWORD64 uDisplacement(0);
	if(dbg_ctx.SymFromInlineContext_(proc, address, inline_context, &uDisplacement, pIHS) != 0)
	{
		function = pIHS->Name;
		LONG_PTR displacement = static_cast<LONG_PTR>(uDisplacement);
		if(snprintf(
			   function_buffer,
			   sizeof(function_buffer),
			   "(inline) %s%+lld (0x%llx)",
			   function,
			   displacement,
			   address) >= 0)
		{
			function = function_buffer;
		}
	}

	// file/line number

	IMAGEHLP_LINE64 ih_line;
	ih_line.SizeOfStruct = sizeof(IMAGEHLP_LINE);

	// I don't know what's the deal with 2 displacements...
	// MAYBE this is the relative displacement,
	// while the above is the displacement from the function.
	DWORD dwDisplacement(0);
	if(dbg_ctx.SymGetLineFromInlineContext_(
		   proc, address, inline_context, 0, &dwDisplacement, &ih_line) != 0)
	{
		filename = ih_line.FileName;
		if(cv_bt_full_paths.data() == 0)
		{
			filename = remove_file_path(ih_line.FileName);
		}
		lineno = ih_line.LineNumber; // NOLINT(*-narrowing-conversions)
	}

	debug_stacktrace_info info{nr_of_frame + 1, address, module_name, function, filename, lineno};

	// TODO: handle errors?
	printer.print_line(&info);
}

// mingw but built with -gcodeview + -Wl,--pdb=
// But it's an optimized build.
// Normally the pdb file should have source + line info which avoids the itanium ABI mangling
// (somehow)
#ifdef __GNUG__
#include <cxxabi.h>
#endif
// Write the details of one function to the log file
__attribute__((no_sanitize("cfi-icall"))) static void
	write_function_detail(DWORD64 address, int nr_of_frame, debug_stacktrace_observer& printer)
{
	const char* filename = NULL;
	int lineno = 0;
	const char* function = NULL;
	const char* module_name = NULL;
	char function_buffer[500];

	HANDLE proc = GetCurrentProcess();

#ifdef __GNUG__
	auto free_del = [](void* ptr) { free(ptr); };
	std::unique_ptr<char, decltype(free_del)> demangler{NULL, free_del};
#endif

	IMAGEHLP_MODULE moduleInfo;
	::ZeroMemory(&moduleInfo, sizeof(moduleInfo));
	moduleInfo.SizeOfStruct = sizeof(moduleInfo);

	// gets the fuller path, I should replace dbghelp, but I assume this is slower.
	std::unique_ptr<char[]> module_path;
	if(cv_bt_full_paths.data() != 0)
	{
		// NOLINTNEXTLINE(*-no-int-to-ptr)
		int length = WIN32_getModulePath(reinterpret_cast<void*>(address), nullptr, 0, nullptr);
		module_path = std::make_unique<char[]>(length + 1);
		// NOLINTNEXTLINE(*-no-int-to-ptr)
		if(WIN32_getModulePath(
			   reinterpret_cast<void*>(address), module_path.get(), length, nullptr) == length)
		{
			module_name = module_path.get();
		}
	}
	else
	{
		if(dbg_ctx.SymGetModuleInfo_(proc, address, &moduleInfo) != FALSE)
		{
			module_name = moduleInfo.ModuleName;
		}
	}

	ULONG64
	symbolBuffer[(sizeof(SYMBOL_INFO) + MAX_SYM_NAME + sizeof(ULONG64) - 1) / sizeof(ULONG64)];
	PSYMBOL_INFO pIHS = (PSYMBOL_INFO)symbolBuffer;
	DWORD64 func_disp = 0;

	// log the function name
	pIHS->SizeOfStruct = sizeof(SYMBOL_INFO);
	pIHS->MaxNameLen = MAX_SYM_NAME;
	if(dbg_ctx.SymFromAddr_(proc, address, &func_disp, pIHS) != FALSE)
	{
		// the name is already undecorated from dbghelp due to SYMOPT_UNDNAME
		function = pIHS->Name;

#ifdef __GNUG__
		if(cv_bt_demangle.data() != 0 && function != NULL)
		{
			int status = 0;
			// I need to add an underscore.
			std::string temp = "_";
			temp += function;
			demangler.reset(abi::__cxa_demangle(temp.c_str(), NULL, NULL, &status));
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

		// the displacement says that this is probably a bogus private function,
		// unless you have internal symbols (not relying only on exports)
		if(func_disp >= 100000 && module_name != nullptr && moduleInfo.GlobalSymbols == FALSE)
		{
			// signal that there is no debug info, and the symbol was found, but ignored.
			// this probably could happen more with lto, but would the info even be helpful?
			if(snprintf(
				   function_buffer,
				   sizeof(function_buffer),
				   "(bad symbol) %s+%llu (0x%llx)",
				   function,
				   func_disp,
				   address) >= 0)
			{
				function = function_buffer;
			}
		}
		else
		{
			// I could also check (pIHS->Flags & SYMFLAG_EXPORT)
			if(moduleInfo.GlobalSymbols == FALSE)
			{
				// print the offset, useful for noticing the offset is huge, and probably wrong.
				if(snprintf(
					   function_buffer, sizeof(function_buffer), "%s+%llu", function, func_disp) >=
				   0)
				{
					function = function_buffer;
				}
			}
		}

		/*char undecorate_buffer[500];//=_T("?");

		if(UnDecorateSymbolName_( pIHS->Name, undecorate_buffer, sizeof(undecorate_buffer),
			UNDNAME_NAME_ONLY ) == 0)
		{
		  function = pIHS->Name;
		} else {
		  function = undecorate_buffer;
		}*/
	}

	IMAGEHLP_LINE64 ih_line;
	DWORD line_disp = 0;

	// find the source line for this function.
	ih_line.SizeOfStruct = sizeof(IMAGEHLP_LINE);
	if(dbg_ctx.SymGetLineFromAddr64_(proc, address, &line_disp, &ih_line) != FALSE)
	{
		filename = ih_line.FileName;
		if(cv_bt_full_paths.data() == 0)
		{
			filename = remove_file_path(filename);
		}
		lineno = ih_line.LineNumber;
	}

	debug_stacktrace_info info{nr_of_frame + 1, address, module_name, function, filename, lineno};

	// TODO: handle errors?
	printer.print_line(&info);
}

// Walk over the stack and log all relevant information to the log file
__attribute__((no_sanitize("cfi-icall"))) static void
	write_stacktrace(CONTEXT* context, debug_stacktrace_observer& printer, int skip = 0)
{
	HANDLE proc = GetCurrentProcess();
	STACKFRAME64 stack_frame;
	DWORD machine;

	// Write the stack trace
	ZeroMemory(&stack_frame, sizeof(STACKFRAME64));
	stack_frame.AddrPC.Mode = AddrModeFlat;
	stack_frame.AddrStack.Mode = AddrModeFlat;
	stack_frame.AddrFrame.Mode = AddrModeFlat;

#if defined(_M_IX86)
	machine = IMAGE_FILE_MACHINE_I386;
	stack_frame.AddrPC.Offset = context->Eip;
	stack_frame.AddrStack.Offset = context->Esp;
	stack_frame.AddrFrame.Offset = context->Ebp;
#elif defined(_M_X64)
	machine = IMAGE_FILE_MACHINE_AMD64;
	stack_frame.AddrPC.Offset = context->Rip;
	stack_frame.AddrStack.Offset = context->Rsp;
	stack_frame.AddrFrame.Offset = context->Rbp;
#else
#error Unknown processortype, please disable USE_WIN32_DEBUG_INFO
#endif

	int trimmed_frame_start = -1;
	bool print_once = false;
	uintptr_t address_copy = reinterpret_cast<uintptr_t>(g_trim_return_address);
	auto if_skip = [&](int index, DWORD64 pc) -> bool {
		// >= because this is before I increment.
		if(index >= cv_bt_max_depth.data() + skip)
		{
			return true;
		}

		// the +1 is to skip THIS function frame.
		if(cv_bt_ignore_skip.data() == 0 && index < skip)
		{
			return true;
		}

		// check if this shuold be trimmed.
		if(cv_bt_trim_stacktrace.data() != 0 && g_trim_return_address != nullptr)
		{
			if(address_copy == pc)
			{
#ifndef __OPTIMIZE__
				// I need to handle inlined functions...
				if(trimmed_frame_start != -1)
				{
					printer.print_string_fmt("info: trim function found again at: %d\n", index);
				}
				else
#endif
				{
					// I start trimming 1 frame below the target.
					trimmed_frame_start = index + 1 - skip;
				}
				// don't trim THIS line.
				return false;
			}
			// below the trim, I don't trim it because it might be weird (setjmp/longjmp?).
			if(trimmed_frame_start == -1 && !print_once && address_copy < stack_frame.AddrPC.Offset)
			{
				print_once = true;
				printer.print_string("(trim found incorrectly)\n");
			}
		}

		if(trimmed_frame_start != -1 && index >= trimmed_frame_start)
		{
			return true;
		}
		return false;
	};

	int i = 0;
	while(true)
	{
		if(dbg_ctx.StackWalk64_(
			   machine,
			   proc,
			   GetCurrentThread(),
			   &stack_frame,
			   context,
			   NULL,
			   dbg_ctx.SymFunctionTableAccess64_,
			   dbg_ctx.SymGetModuleBase64_,
			   NULL) == FALSE)
		{
			break;
		}

		// StackWalk returns TRUE with a frame of zero at the end.
		if(stack_frame.AddrPC.Offset == 0)
		{
			break;
		}

		// this does one more stackwalk, so that I can detect if there is a missing entry.
		// in case cv_bt_max_depth.data() == the size of the stacktrace.
		if(i >= cv_bt_max_depth.data() + skip)
		{
			++i;
			break;
		}

		DWORD64 pc = stack_frame.AddrPC.Offset;

		if(!if_skip(i, pc))
		{
			write_function_detail(stack_frame.AddrPC.Offset, i - skip, printer);
		}
		++i;

		if(cv_bt_show_inlined_functions.data() == 0)
		{
			continue;
		}

		// Expand any inline frames
		DWORD inline_count = dbg_ctx.SymAddrIncludeInlineTrace_(proc, pc);
		if(inline_count > 0)
		{
			DWORD inline_context(0), frameIndex(0);
			if(dbg_ctx.SymQueryInlineTrace_(proc, pc, 0, pc, pc, &inline_context, &frameIndex))
			{
				for(DWORD frame = 0; frame < inline_count; frame++, inline_context++)
				{
					if(!if_skip(i, pc))
					{
						write_inline_function_detail(proc, pc, inline_context, i - skip, printer);
						// showInlineVariablesAt(os, stackFrame, *context, inline_context)
					}
					++i;
				}
			}
		}
	}
	trim_stacktrace_print_helper(printer, trimmed_frame_start, i - skip);
}

// Load the dbghelp.dll file, try to find a version that matches our requirements.
__attribute__((no_sanitize("cfi-icall"))) static bool
	load_dbghelp_dll(debug_stacktrace_observer& printer)
{
	memset(&dbg_ctx, 0, sizeof(dbg_ctx));
	dbg_ctx.dbghelp_dll = LoadLibrary(DBGHELP_DLL);
	if(dbg_ctx.dbghelp_dll == NULL)
	{
		printer.print_string_fmt(
			"failed to load %s: %s\n", DBGHELP_DLL, WIN_GetFormattedGLE().c_str());
		return false;
	}
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wcast-function-type-mismatch"
#endif
#define XX(name, type)                                                                           \
	dbg_ctx.name##_ = (type)GetProcAddress(dbg_ctx.dbghelp_dll, #name);                          \
	if(dbg_ctx.name##_ == nullptr)                                                               \
	{                                                                                            \
		printer.print_string_fmt(                                                                \
			"failed to load %s in %s: %s\n", #name, DBGHELP_DLL, WIN_GetFormattedGLE().c_str()); \
		goto cleanup;                                                                            \
	}
	DBGHELP_FUNC_MAP(XX)
#undef XX
#ifdef __clang__
#pragma clang diagnostic pop
#endif
	DWORD opts;
	// initialize the symbol loading code
	opts = dbg_ctx.SymGetOptions_();

	opts |= SYMOPT_LOAD_LINES;
	opts |= SYMOPT_DEFERRED_LOADS;
	opts |= SYMOPT_OMAP_FIND_NEAREST;

	// turning mangling off has no effect because the symbols are stored unmangled
	// only public symbols are affected (i don't know what that means).
	if(cv_bt_demangle.data() == 0)
	{
		opts &= ~SYMOPT_UNDNAME;
	}

	// Set the 'load lines' option to retrieve line number information;
	//   set the Deferred Loads option to map the debug info in memory only
	//   when needed.
	dbg_ctx.SymSetOptions_(opts);

	// Initialize the debughlp DLL with the default path and automatic
	//   module enumeration (and loading of symbol tables) for this process.

	dbg_ctx.SymInitialize_(GetCurrentProcess(), NULL, TRUE);

	return true;

cleanup:
	if(dbg_ctx.dbghelp_dll != NULL)
	{
		FreeLibrary(dbg_ctx.dbghelp_dll);
		dbg_ctx.dbghelp_dll = NULL;
	}

	return false;
}

// Cleanup the dbghelp.dll library
__attribute__((no_sanitize("cfi-icall"))) static void cleanup_debughelp()
{
	dbg_ctx.SymCleanup_(GetCurrentProcess());

	FreeLibrary(dbg_ctx.dbghelp_dll);

#define XX(name, type) dbg_ctx.name##_ = nullptr;
	DBGHELP_FUNC_MAP(XX)
#undef XX

	dbg_ctx.dbghelp_dll = NULL;
}

__declspec(noinline) bool write_win32_stacktrace(debug_stacktrace_observer& printer, int skip)
{
	CONTEXT ctx;

	ctx.ContextFlags = CONTEXT_FULL;
	// if (!GetThreadContext(GetCurrentThread(), &ctx))
	// I think this is 64 bit only, but GetThreadContext adds in an extra 2 frames (weird)
	RtlCaptureContext(&ctx);
	return debug_raw_win32_stacktrace(&ctx, printer, skip + 1);
}

__declspec(noinline) bool
	debug_raw_win32_stacktrace(void* ctx, debug_stacktrace_observer& printer, int skip)
{
	if(ctx == NULL)
	{
		// I can't use serr due to recursion.
		printer.print_string_fmt("%s: ctx null\n", __func__);
		return false;
	}

	if(!load_dbghelp_dll(printer))
	{
		return false;
	}

	bool ret = true;
	write_stacktrace(static_cast<CONTEXT*>(ctx), printer, skip);

	cleanup_debughelp();

	return ret;
}

//__attribute__((no_sanitize("cfi-icall")))
bool debug_win32_write_function_detail(uintptr_t stack_frame, debug_stacktrace_observer& printer)
{
	if(!load_dbghelp_dll(printer))
	{
		return false;
	}
	write_function_detail(stack_frame, 0, printer);

#if 0
	// (BUT mingw should be using dbghelp if it was using
	if(cv_bt_show_inlined_functions.data() != 0)
	{
		HANDLE proc = GetCurrentProcess();
		DWORD inline_count = dbg_ctx.SymAddrIncludeInlineTrace_(proc, stack_frame);
		if(inline_count > 0)
		{
			DWORD inline_context(0), frameIndex(0);
			if(dbg_ctx.SymQueryInlineTrace_(
				   proc, stack_frame, 0, stack_frame, stack_frame, &inline_context, &frameIndex))
			{
				for(DWORD frame = 0; frame < inline_count; frame++, inline_context++)
				{
					write_inline_function_detail(
						proc, stack_frame, inline_context, frame + 1, printer);
					// showInlineVariablesAt(os, stackFrame, *context, inline_context)
				}
			}
		}
	}
#endif
	cleanup_debughelp();
	return true;
}

#endif