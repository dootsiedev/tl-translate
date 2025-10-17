#include "core/global.h"
#include "translate.h"
#include "translation_context.h"

#include "core/cvar.h"

int main(int argc, char** argv)
{
	switch(load_cvar(argc, argv))
	{
	case CVAR_LOAD::SUCCESS: break;
	case CVAR_LOAD::ERROR: return 1;
	case CVAR_LOAD::CLOSE: return 0;
	}

	if(!g_translation_context.init())
	{
		return 1;
	}

	slog(_T("test!\n"));
	slogf(_T("test %s!\n"), "foo");
	slogf(_T("test\nnewline:%s!\n"), "bar");

#if 0 // no way to modify the cvars...
	if(!save_cvar())
	{
		success = false;
	}
#endif

	return 0;
}