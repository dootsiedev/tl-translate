// This is an independent project of an individual developer. Dear PVS-Studio, please check it.
// PVS-Studio Static Code Analyzer for C, C++, C#, and Java: https://pvs-studio.com

#include "global_pch.h"
#include "global.h"

#include "stacktrace.h"

#ifdef USE_WIN32_DEBUG_INFO
static int has_stacktrace_dbghelp = 1;
#else
static int has_stacktrace_dbghelp = 0;
#endif
static REGISTER_CVAR_INT(
	cv_has_stacktrace_dbghelp,
	has_stacktrace_dbghelp,
	"0 = not found, 1 = found",
	CVAR_T::READONLY);

#ifdef USE_WIN32_DEBUG_INFO

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <DbgHelp.h>

// dbghelp is dynamically loaded, because it should be optional.
// because cmake wont copy dbghelp for you (does windows install with dbghelp?).
#define DBGHELP_DLL "dbghelp.dll"

static HMODULE dbghelp_dll = NULL;

// public functions in dbghelp.dll
typedef BOOL(WINAPI* MINIDUMPWRITEDUMP)(
	HANDLE hProcess,
	DWORD ProcessId,
	HANDLE hFile,
	MINIDUMP_TYPE DumpType,
	CONST PMINIDUMP_EXCEPTION_INFORMATION ExceptionParam,
	CONST PMINIDUMP_USER_STREAM_INFORMATION UserStreamParam,
	CONST PMINIDUMP_CALLBACK_INFORMATION CallbackParam);
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

// function pointers
static MINIDUMPWRITEDUMP MiniDumpWriteDump_;
static SYMINITIALIZE SymInitialize_;
static SYMSETOPTIONS SymSetOptions_;
static SYMGETOPTIONS SymGetOptions_;
static SYMCLEANUP SymCleanup_;
static SYMGETLINEFROMADDR64 SymGetLineFromAddr64_;
static SYMFROMADDR SymFromAddr_;
static STACKWALK64 StackWalk64_;
static SYMFUNCTIONTABLEACCESS64 SymFunctionTableAccess64_;
static SYMGETMODULEBASE64 SymGetModuleBase64_;

static UNDECORATESYMBOLNAME UnDecorateSymbolName_;
static SYMGETMODULEINFO SymGetModuleInfo_;

// Write the details of one function to the log file
__attribute__((no_sanitize("cfi-icall")))
static void
	write_function_detail(DWORD64 stack_frame, int nr_of_frame, debug_stacktrace_observer& printer)
{
	const char* filename = NULL;
	int lineno = 0;
	const char* function = NULL;
	const char* module_name = NULL;
	char function_buffer[500];

	HANDLE proc = GetCurrentProcess();

	IMAGEHLP_MODULE moduleInfo;
	::ZeroMemory(&moduleInfo, sizeof(moduleInfo));
	moduleInfo.SizeOfStruct = sizeof(moduleInfo);

	// If you want to get the absolute path, using GetModuleNameFromAddress + whereami
	// getModulePath_ but printing a full path for every line is very noisey.
	if(SymGetModuleInfo_(proc, stack_frame, &moduleInfo) != FALSE)
	{
		module_name = moduleInfo.ModuleName;
	}

	ULONG64
	symbolBuffer[(sizeof(SYMBOL_INFO) + MAX_SYM_NAME + sizeof(ULONG64) - 1) / sizeof(ULONG64)];
	PSYMBOL_INFO pIHS = (PSYMBOL_INFO)symbolBuffer;
	DWORD64 func_disp = 0;

	// log the function name
	pIHS->SizeOfStruct = sizeof(SYMBOL_INFO);
	pIHS->MaxNameLen = MAX_SYM_NAME;
	if(SymFromAddr_(proc, stack_frame, &func_disp, pIHS) != FALSE)
	{
		// the name is already undecorated from dbghelp due to SYMOPT_UNDNAME
		function = pIHS->Name;

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
				   stack_frame) >= 0)
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
	if(SymGetLineFromAddr64_(proc, stack_frame, &line_disp, &ih_line) != FALSE)
	{
		filename = ih_line.FileName;
		if(cv_bt_full_paths.data() == 0)
		{
			const char* temp_filename = strrchr(filename, '\\');
			if(temp_filename != NULL)
			{
				filename = temp_filename + 1;
			}
		}
		lineno = ih_line.LineNumber;
	}

	debug_stacktrace_info info{
		nr_of_frame + 1, stack_frame, module_name, function, filename, lineno};

	// TODO: handle errors?
	bool ret = printer.print_line(&info);
}

// Walk over the stack and log all relevant information to the log file
__attribute__((no_sanitize("cfi-icall")))
static void write_stacktrace(CONTEXT* context, debug_stacktrace_observer& printer, int skip = 0)
{
	HANDLE proc = GetCurrentProcess();
	STACKFRAME64 stack_frame;
	DWORD machine;
	int i = 0;

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

	int trimmed_frame_start = 0;
	// convert the address to the same type (DWORD64)
	uintptr_t address_copy;
	memcpy(&address_copy, &g_trim_return_address, sizeof(address_copy));

	for(;;)
	{
		if(i > cv_bt_max_depth.data())
		{
			printer.print_string_fmt(
				"max depth reached (%s = %d)\n",
				cv_bt_max_depth.cvar_key,
				cv_bt_max_depth.data());
			break;
		}
		if(!StackWalk64_(
			   machine,
			   proc,
			   GetCurrentThread(),
			   &stack_frame,
			   context,
			   NULL,
			   SymFunctionTableAccess64_,
			   SymGetModuleBase64_,
			   NULL))
		{
			break;
		}
		// Sometimes StackWalk returns TRUE with a frame of zero at the end.
		if(stack_frame.AddrPC.Offset == 0)
		{
			break;
		}

		if(i >= skip || cv_bt_ignore_skip.data() == 1)
		{
			write_function_detail(stack_frame.AddrPC.Offset, i - skip, printer);
		}

		if(cv_bt_trim_stacktrace.data() != 0 && g_trim_return_address != nullptr)
		{
			if(address_copy == stack_frame.AddrPC.Offset)
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


		i++;
	}
	if(cv_bt_trim_stacktrace.data() != 0 && g_trim_return_address != nullptr)
	{
		trim_stacktrace_print_helper(
			printer, trimmed_frame_start == 0 ? -1 : (i - trimmed_frame_start));
	}
}

// Load the dbghelp.dll file, try to find a version that matches our requirements.
__attribute__((no_sanitize("cfi-icall")))
static BOOL load_dbghelp_dll()
{
	dbghelp_dll = (HMODULE)LoadLibrary(DBGHELP_DLL);
	if(dbghelp_dll != NULL)
	{

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wcast-function-type-mismatch"
#endif
		DWORD opts;
		// load the functions
		MiniDumpWriteDump_ = (MINIDUMPWRITEDUMP)GetProcAddress(dbghelp_dll, "MiniDumpWriteDump");
		SymInitialize_ = (SYMINITIALIZE)GetProcAddress(dbghelp_dll, "SymInitialize");
		SymSetOptions_ = (SYMSETOPTIONS)GetProcAddress(dbghelp_dll, "SymSetOptions");
		SymGetOptions_ = (SYMGETOPTIONS)GetProcAddress(dbghelp_dll, "SymGetOptions");
		SymCleanup_ = (SYMCLEANUP)GetProcAddress(dbghelp_dll, "SymCleanup");
		SymGetLineFromAddr64_ =
			(SYMGETLINEFROMADDR64)GetProcAddress(dbghelp_dll, "SymGetLineFromAddr64");
		SymFromAddr_ = (SYMFROMADDR)GetProcAddress(dbghelp_dll, "SymFromAddr");
		StackWalk64_ = (STACKWALK64)GetProcAddress(dbghelp_dll, "StackWalk64");
		SymFunctionTableAccess64_ =
			(SYMFUNCTIONTABLEACCESS64)GetProcAddress(dbghelp_dll, "SymFunctionTableAccess64");
		SymGetModuleBase64_ = (SYMGETMODULEBASE64)GetProcAddress(dbghelp_dll, "SymGetModuleBase64");

		UnDecorateSymbolName_ =
			(UNDECORATESYMBOLNAME)GetProcAddress(dbghelp_dll, "UnDecorateSymbolName");
		SymGetModuleInfo_ = (SYMGETMODULEINFO)GetProcAddress(dbghelp_dll, "SymGetModuleInfo");
#ifdef __clang__
#pragma clang diagnostic pop
#endif

		if(!(MiniDumpWriteDump_ && SymInitialize_ && SymSetOptions_ && SymGetOptions_ &&
			 SymCleanup_ && SymGetLineFromAddr64_ && SymFromAddr_ && SymGetModuleBase64_ &&
			 StackWalk64_ && SymFunctionTableAccess64_ && UnDecorateSymbolName_ &&
			 SymGetModuleInfo_))
			goto cleanup;

		// initialize the symbol loading code
		opts = SymGetOptions_();

		opts |= SYMOPT_LOAD_LINES;
		opts |= SYMOPT_DEFERRED_LOADS;

		// turning mangling off has no effect because the symbols are stored unmangled
		// only public symbols are affected (i don't know what that means).
		if(cv_bt_demangle.data() == 0)
		{
			opts &= ~SYMOPT_UNDNAME;
		}

		// Set the 'load lines' option to retrieve line number information;
		//   set the Deferred Loads option to map the debug info in memory only
		//   when needed.
		SymSetOptions_(opts);

		// Initialize the debughlp DLL with the default path and automatic
		//   module enumeration (and loading of symbol tables) for this process.

		SymInitialize_(GetCurrentProcess(), NULL, TRUE);

		return TRUE;
	}

cleanup:
	if(dbghelp_dll) FreeLibrary(dbghelp_dll);

	return FALSE;
}

// Cleanup the dbghelp.dll library
__attribute__((no_sanitize("cfi-icall")))
static void cleanup_debughlp()
{
	SymCleanup_(GetCurrentProcess());

	FreeLibrary(dbghelp_dll);

	dbghelp_dll = NULL;
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
		fprintf(stderr, "<stderr> %s: ctx null\n", __func__);
		return false;
	}

	if(!load_dbghelp_dll())
	{
		// TODO: should probably go through all the functions and use getlasterror
		fprintf(stderr, "<stderr> %s: failed to load dbghelp\n", __func__);
		return false;
	}

	bool ret = true;
	write_stacktrace(static_cast<CONTEXT*>(ctx), printer, skip);

	cleanup_debughlp();

	return ret;
}

bool debug_win32_write_function_detail(uintptr_t stack_frame, debug_stacktrace_observer& printer)
{
	if(!load_dbghelp_dll())
	{
		// TODO: should probably go through all the functions and use getlasterror
		fprintf(stderr, "<stderr> %s: failed to load dbghelp\n", __func__);
		return false;
	}
	write_function_detail(stack_frame, 0, printer);
	cleanup_debughlp();
	return true;
}

#endif