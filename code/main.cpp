// This is an independent project of an individual developer. Dear PVS-Studio, please check it.
// PVS-Studio Static Code Analyzer for C, C++, C#, and Java: https://pvs-studio.com
#include "core/global.h"
#include "translate.h"
#include "translation_context.h"

#include "core/cvar.h"

#include "core/asan_helper.h"

#if defined(_WIN32) && !defined(DISABLE_WIN32_TERM)
// for SetConsoleOutputCP
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#ifdef ERROR
#undef ERROR
#endif

static REGISTER_CVAR_INT(
	cv_win_unicode_hack,
	1,
	"0 = just print utf8 to stdout (this should just work), 1 = SetConsoleOutputCP (clion does not print unicode in run, unless I set system wide locale to experimental utf8)",
	CVAR_T::STARTUP);

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

#if defined(_WIN32) && !defined(DISABLE_WIN32_TERM)
	if(cv_win_unicode_hack.data() == 1)
	{
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