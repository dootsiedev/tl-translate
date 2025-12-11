// This is an independent project of an individual developer. Dear PVS-Studio, please check it.
// PVS-Studio Static Code Analyzer for C, C++, C#, and Java: https://pvs-studio.com
#include "global.h"

#include "assert.h"

#ifndef DISABLE_SDL
// this shouldn't be here, but I need it for the window for show_error
#include "../app.h"
#endif

#include "stacktrace.h"
#include "breakpoint.h"
#include "../util/string_tools.h"

#ifdef DISABLE_CUSTOM_ASSERT
static int has_custom_assert = 0;
#else
static int has_custom_assert = 2;
#endif
static REGISTER_CVAR_INT(
	cv_assert_mode,
	has_custom_assert,
	"0 = just print to stderr, 1 = breakpoint, 2 = show dialog",
	CVAR_T::RUNTIME);

static REGISTER_CVAR_INT(
	cv_check_mode,
	0,
	"like Asserts, but not fatal (only enable this if you need to debug the check). 0 = print to serr, 1 = breakpoint, 2 = show window",
	CVAR_T::RUNTIME);

static std::string print_assert_explanation(
	const char* assert_name,
	const char* expr,
	const char* extra,
#if MY_HAS_SOURCE_LOCATION
	const std::source_location _src_loc_data
#else
	const char* file,
	int line
#endif
)
{
	std::string formatted_message;
	str_asprintf(
		formatted_message,
		"\n%s failed\n"
#if MY_HAS_SOURCE_LOCATION
		"File: %s, Line %u\n"
		"Func: %s\n"
		"Expression: `%s`\n"
		"%s",
		assert_name,
		_src_loc_data.file_name(),
		_src_loc_data.line(),
		_src_loc_data.function_name(),
#else
		"File: %s, Line %d\n"
		"Expression: `%s`\n"
		"%s",
		assert_name,
		file,
		line,
#endif
		expr,
		extra != NULL ? extra : "");

	return formatted_message;
}

#ifndef DISABLE_SDL
// reduce a bit of copy paste.
static void exit_fullscreen()
{
	if(g_app.window == nullptr)
	{
		return;
	}
	if(cv_fullscreen.data() == 0)
	{
		return;
	}
	if(!SDL_IsMainThread())
	{
		// TODO: if I had proper event queues, maybe post a callback that does the job?
		//  could I post a custom event into the SDL queue?
		return;
	}
	if(!SDL_SetWindowFullscreen(g_app.window, false))
	{
		fprintf(stderr, "SDL_SetWindowFullscreen Warning: %s\n", SDL_GetError());
	}
	// I'm not sure if this helps.
	if(!SDL_ShowWindow(g_app.window))
	{
		fprintf(stderr, "SDL_ShowWindow Warning: %s\n", SDL_GetError());
	}
	// I'm not sure if this helps.
	if(!SDL_SyncWindow(g_app.window))
	{
		fprintf(stderr, "SDL_SyncWindow Warning: %s\n", SDL_GetError());
	}
}
#endif

// uses SDL2 to present a window with a trimmed error.
// false does not return serr, use SDL_GetError()
// (because show_error is used in assert, serr uses assert...)
bool show_error(const char* title, const char* message)
{
#ifndef DISABLE_SDL
	// TODO: try to avoid showing more than 1 window at a time.
	// I use this in ASSERT dummy!
	// ASSERT(title != NULL);
	// ASSERT(message != NULL);

	// it doesn't need to be thread local, but why not.
	static
#ifndef __EMSCRIPTEN__
		thread_local
#endif
		bool ignore = false;
	if(ignore)
	{
		// ignore.
		return true;
	}

	// dialog boxes are not ideal in fullscreen.
	exit_fullscreen();

	// clion isn't line buffered on /subsystem:windows so it's nice to have text updated.
	fflush(stdout);

	// I am not 100% happy with how fragile this looks,
	// but I would rather have the WHOLE message here.
	char message_buf[10000];
	const char* cur = message;
	const char* line_start = message;
	const char* end = message + std::min(strlen(message), sizeof(message_buf) - 1);
	char* buf_cur = message_buf;
	int line_count = 0;
	for(; cur != end; ++cur)
	{
		if(*cur == '\n')
		{
			// maybe instead of truncating the bottom, I should truncate the middle?
			// and maybe I should care about utf8?
			++line_count;
			if(line_count > 50)
			{
				*buf_cur++ = '~';
				break;
			}
			*buf_cur++ = *cur;
			line_start = cur + 1;
			continue;
		}
		if((cur - line_start) < 200)
		{
			*buf_cur++ = *cur;
		}
	}
	// trim the newline.
	if(buf_cur <= end && *(buf_cur - 1) == '\n')
	{
		*(buf_cur - 1) = '\n';
	}
	*buf_cur = '\0';

	// open a menu with an ignore button if you get stuck in an infinite loop of errors.
	// not a good behavior because it will ignore ALL show_error's.
	// Also, if the message contains a stacktrace, the FPS would dip hard.
	// (since the message will still be generated and formatted).
	// BUT this is better than nothing if you can't close the menu because of the popup,
	// because the popup is modal, you can't close the parent window in any way.
	// I could technically pass the return address into the function to ignore only a short list.
	// in that case it would be fine to show the ignore button always, because it's selective.
	static
#ifndef __EMSCRIPTEN__
		thread_local
#endif
		Uint64 timer_start = SDL_GetTicks(),
			   open_count = 0;
	Uint64 time_now = SDL_GetTicks();
	// if you open 5 menus in under 10 seconds
	if(time_now - timer_start < 10000)
	{
		++open_count;
		if(open_count >= 5)
		{
			// I can't use slog because show_error is used in ASSERT.
			fprintf(stderr, "Spam Detected in %s: %s\n", __func__, title);
			enum
			{
				POP_SPAM_CONTINUE,
				POP_SPAM_IGNORE,
				POP_SPAM_TERMINATE,
				MAX_POP_SPAM
			};
			SDL_MessageBoxButtonData msg_buttons[MAX_POP_SPAM];
			memset(&msg_buttons, 0, sizeof(msg_buttons));
			msg_buttons[POP_SPAM_CONTINUE].text = "Ok";
			msg_buttons[POP_SPAM_CONTINUE].buttonID = POP_SPAM_CONTINUE;
			msg_buttons[POP_SPAM_CONTINUE].flags = SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT;

			msg_buttons[POP_SPAM_IGNORE].text = "Ignore All";
			msg_buttons[POP_SPAM_IGNORE].buttonID = POP_SPAM_IGNORE;
			msg_buttons[POP_SPAM_IGNORE].flags = SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT;

			msg_buttons[POP_SPAM_TERMINATE].text = "Terminate";
			msg_buttons[POP_SPAM_TERMINATE].buttonID = POP_SPAM_TERMINATE;

			SDL_MessageBoxData msg_data;
			memset(&msg_data, 0, sizeof(msg_data));
			msg_data.title = title;
			msg_data.flags = SDL_MESSAGEBOX_BUTTONS_RIGHT_TO_LEFT;
			msg_data.message = message_buf;
			msg_data.buttons = msg_buttons;
			msg_data.numbuttons = std::size(msg_buttons);
			if(SDL_IsMainThread())
			{
				msg_data.window = g_app.window;
			}

			int result = -1;
			if(!SDL_ShowMessageBox(&msg_data, &result))
			{
				return false;
			}
			switch(result)
			{
			case POP_SPAM_CONTINUE: break;
			case POP_SPAM_IGNORE: ignore = true; break;
			case POP_SPAM_TERMINATE:
				// I probably should attempt to gracefully quit (save settings).
				// but this option exists because maybe you can't.
				// I should use an exception if I caught them.
				std::quick_exit(EXIT_FAILURE);
			default:
				fprintf(stderr, "Unknown result from SDL_ShowMessageBox (%s: %s)\n", __func__, title);
				MY_BREAKPOINT;
			}
		}
	}
	else
	{
		open_count = 0;
	}
	timer_start = time_now;

	if(g_app.window == nullptr || !SDL_IsMainThread())
	{
		// TODO: this is annoying, because the ASSERT thread will kill the whole process.
		//  and this ASSERT is probably inside of a mutex that will cause a deadlock.
		//  so the best option is to send an event to the main thread to destroy the window.
		//  or display the popup on the main thread + quick_exit (if I have a watchdog).
		//  The most graceful solution is to replace ASSERT with a C++ exception,
		//  so that the mutexes can be unlocked with RAII
		//  (but... I use ASSERT inside of my destructors... it calls terminate which is fine...),
		//  thankfully I have no threads ATM.
		return SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, title, message_buf, nullptr);
	}
	return SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, title, message_buf, g_app.window);
#else // DISABLE_SDL
	return true;
#endif
}

#ifndef DISABLE_CUSTOM_ASSERT
// not the right location to say this,
// but my problem with ASSERT_SRC_LOC is that
// it is misleading for the location to be up 1 stack frame (it feels like less information).
// but it's still useful in the right context (functions that don't do anything, setters/getters).
// TODO: I think I need 2 Assert functions, one default, and one with 2 src_loc locations.
#if MY_HAS_SOURCE_LOCATION
[[noreturn]] MY_NOINLINE void implement_ASSERT(const char* expr, const char* message, SRC_LOC_IMPL)
#else
[[noreturn]] MY_NOINLINE void
	implement_ASSERT(const char* expr, const char* message, const char* file, int line)
#endif
{
	static
#ifndef __EMSCRIPTEN__
		thread_local
#endif
		int nested_assert = 0;

	if(nested_assert == 1)
	{
		fputs("Nested Assert\n", stderr);
		MY_BREAKPOINT;
	}
	else if(nested_assert >= 3)
	{
		fputs("Nested Assert (3), exiting.\n", stderr);
		quick_exit(EXIT_FAILURE);
	}
	nested_assert += 1;

	// stdout will print after stderr without this.
	// (actually, in my ide it still prints before... on /subsystem:windows (I think?),
	// should I use debugoutput on windows?)
	// everything in ASSERT should be stderr.
	// and slog/serr can't be used because of recursion due to ASSERT.
	fflush(stdout);

	if(cv_assert_mode.data() != 0)
	{
		// this helps with IDE's, fullscreen, etc.
		exit_fullscreen();
	}

	// normally I would just print it without checking the cvar
	// in case stacktrace is set to trap, but we are already trapping.
	std::string stack_message;
	if(message != nullptr)
	{
		str_asprintf(stack_message, "Message: %s\n", message);
	}

	if(cv_has_stacktrace.data() != 0)
	{
		stack_message += "\nStacktrace:\n";
		debug_stacktrace_string_printer handler(stack_message);
		debug_stacktrace(handler, 1);
	}

	std::string formatted_message = print_assert_explanation(
		"Assert",
		expr,
		stack_message.c_str(),
#if MY_HAS_SOURCE_LOCATION
		PASS_SRC_LOC
#else
		file,
		line
#endif
	);

	if(serr_check_error())
	{
		fputs("Serr before Assert\n", stderr);
	}

	fwrite(formatted_message.c_str(), 1, formatted_message.size(), stderr);

	if(cv_assert_mode.data() == 1)
	{
		MY_BREAKPOINT;
	}
	else if(cv_assert_mode.data() >= 2)
	{
#ifdef _WIN32
		if(cv_assert_mode.data() != 3 && IsDebuggerPresent() != 0)
		{
			// I don't want to look at a dialog box in an IDE, disable with = 3
			fputs("debugger detected, enter breakpoint (disable with cv_assert_mode = 3)\n", stderr);
			MY_BREAKPOINT;
		}
#endif

		// better print it since we are exiting & it's most likely related.
		// and combining this into one message box would be too big due to cv_serr_bt
		if(serr_check_error())
		{
			show_error("Serr before Assert (press OK to view Assert next)", serr_get_error().c_str());
		}
		show_error("Assert Failed", formatted_message.c_str());
	}

	// TODO: make show_error give buttons for copy to clipboard + debug + continue + ignore
	//  (but the problem is that I need [[noreturn]] to remove annoying null warnings...
	//  And [[noreturn]] is going to delete unreachable code, I think?)

	// I don't use exit() because it will call atexit
	// which calls C++ destructors, which could trigger ASSERT due to bad state.
	// which is due to my poorly designed code... (should be avoided)
	// but assert normally does abort() which won't trigger atexit()
	// I don't use abort because I already printed a stacktrace + dialog + break on debugger,
	// abort will open a "debug" dialog on windows (because i disabled WER with the registry).
	// If I had a crash reporter, I should abort, but it probably has a "create crash" function.
	quick_exit(EXIT_FAILURE);
}
#endif
#if MY_HAS_SOURCE_LOCATION
MY_NOINLINE void implement_CHECK(const char* expr, const char* message, SRC_LOC_IMPL)
#else
MY_NOINLINE void implement_CHECK(const char* expr, const char* message, const char* file, int line)
#endif
{
	if(cv_check_mode.data() != 0)
	{
		fflush(stdout);
#ifndef DISABLE_SDL
		exit_fullscreen();
#endif
	}

	// serr should print a trace for me, I don't want to print 2 stacktraces into serr.
	// Also, CHECK will still include the file / line, so that's fine.
	std::string stacktrace;
	bool trace_already_printed =
		cv_has_stacktrace.data() != 0 && serr_check_error() /* cv_serr_bt.data() == 1 */;
	if(cv_has_stacktrace.data() != 0 && (trace_already_printed || cv_check_mode.data() == 2))
	{
		stacktrace += "\nStacktrace:\n";
		debug_stacktrace_string_printer handler(stacktrace);
		debug_stacktrace(handler, 1);
	}

	std::string extra;
	if(message != nullptr)
	{
		str_asprintf(extra, "Message: %s\n", message);
	}
	if(trace_already_printed)
	{
		extra += stacktrace;
	}
	std::string formatted_message = print_assert_explanation(
		"Check",
		expr,
		extra.c_str(),
#if MY_HAS_SOURCE_LOCATION
		PASS_SRC_LOC
#else
		file,
		line
#endif
	);

	// this will create a stacktrace if !trace_already_printed
	// it will also include THIS frame (I would skip it), but that's fine.
	serr_raw(formatted_message.c_str(), formatted_message.size());

	if(cv_check_mode.data() == 1)
	{
		MY_BREAKPOINT;
	}
	else if(cv_check_mode.data() >= 2)
	{
#ifdef _WIN32
		if(cv_check_mode.data() != 3 && IsDebuggerPresent() != 0)
		{
			// I don't want to look at a dialog box in an IDE, disable with = 3
			fputs("debugger detected, enter breakpoint (disable with cv_check_mode = 3)\n", stderr);
			MY_BREAKPOINT;
		}
#endif
		if(serr_check_error())
		{
			show_error("Serr before Check (press OK to view Check next)", serr_get_error().c_str());
		}

		if(cv_has_stacktrace.data() != 0)
		{
			// add the trace in if it's not already added.
			if(!trace_already_printed)
			{
				formatted_message += stacktrace;
			}
		}
		show_error("Check Failed", formatted_message.c_str());
	}
}