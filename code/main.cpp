// This is an independent project of an individual developer. Dear PVS-Studio, please check it.
// PVS-Studio Static Code Analyzer for C, C++, C#, and Java: https://pvs-studio.com
#include "core/global.h"
#include "translate.h"
#include "translation_context.h"

#include "core/cvar.h"

#include "core/asan_helper.h"

#ifdef _WIN32

// for SetConsoleOutputCP
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#ifdef ERROR
#undef ERROR
#endif

// for cv_win_unicode_hack
#include <fcntl.h>
#include <io.h>
#include <stdio.h>

//defined in global.cpp (I really should put this into a header...)
extern cvar_int cv_win_unicode_hack;
extern bool g_win_unicode_trigger;
#endif

int main(int argc, char** argv)
{
#ifdef MY_HAS_ASAN
	init_asan();
#endif

	switch(load_cvar(argc, argv))
	{
	case CVAR_LOAD::SUCCESS: break;
	case CVAR_LOAD::ERROR: show_error("cvar error", serr_get_error().c_str()); return 1;
	case CVAR_LOAD::CLOSE: return 0;
	}

#ifdef _WIN32
	if(cv_win_unicode_hack.data() == 1)
	{
		g_win_unicode_trigger = true;
		_setmode(_fileno(stdout), _O_U8TEXT);
	}
	if(cv_win_unicode_hack.data() == 2)
	{
		// With conemu, I get an address sanitizer error UNLESS I check GetACP
		// it points to <unknown module> at address 0
		// you can just enable the Beta: Use Unicode UTF-8 for worldwide language support
		// or use chcp 65001
		// and I believe that if you had a Japanese Locale, japanese text should print fine.
		if(SetConsoleOutputCP(CP_UTF8) == FALSE)
		{
			slogf(
				"SetConsoleOutputCP(CP_UTF8) error: %s\n",
				WIN_GetFormattedGLE(GetLastError()).c_str());
		}
	}
#endif

	if(!get_translation_context().init())
	{
		if(!serr_check_error())
		{
			slog("init failed without printing to serr.\n");
		}
		// clear the buffer.
		show_error("translation error", serr_get_error().c_str());
		return 1;
	}
	// TRANSLATORS: this is a comment!
	slog(_T("test!\n"));

	/* TRANSLATORS: another comment!*/ slogf_T( "test %s!\n", "foo");

	slogf_T("test\nnewline: %s!\n", // TRANSLATORS: a third comment!
		"bar");

	// slogf(_T("unknown!\n"), "bar");

#if 0 // no way to modify the cvars...
	if(!save_cvar())
	{
		success = false;
	}
#endif

	return 0;
}