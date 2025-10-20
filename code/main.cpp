#include "core/global.h"
#include "translate.h"
#include "translation_context.h"

#include "core/cvar.h"

int main(int argc, char** argv)
{
	switch(load_cvar(argc, argv))
	{
	case CVAR_LOAD::SUCCESS: break;
	case CVAR_LOAD::ERROR: show_error("cvar error", serr_get_error().c_str()); return 1;
	case CVAR_LOAD::CLOSE: return 0;
	}

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

	slog(_T("test!\n"));
	slogf(_T("test %s!\n"), "foo");
	slogf(_T("test\nnewline: %s!\n"), "bar");

	//slogf(_T("unknown!\n"), "bar");

#if 0 // no way to modify the cvars...
	if(!save_cvar())
	{
		success = false;
	}
#endif

	return 0;
}