#pragma once

// this is used for MY_DISABLE_GLOBAL_DEP for external projects that are not GUI's (merge-string)
// this  exists because cvars are not good if you use stdout for more than just logs.

#include <cassert>
#include <cstdio>

#ifndef NDSERR
#define NDSERR [[nodiscard]]
#endif

#ifndef serr
#define serr(msg)           \
	do                      \
	{                       \
		fflush(stdout);     \
		fputs(msg, stderr); \
	} while(0)
#endif
#ifndef serrf
#define serrf(fmt, ...)                    \
	do                                     \
	{                                      \
		fflush(stdout);                    \
		fprintf(stderr, fmt, __VA_ARGS__); \
	} while(0)
#endif
// (*nevermind) I don't want slog because I want to use stdout for formatted output,
// so any accidental leaks to stdout can corrupt the output
// I could replace slog with OutputDebugStringA, but I don't know what to do for linux.
#if 1
#ifndef slog
#define slog(msg) fputs(msg, stdout)
#endif
#ifndef slogf
#define slogf(fmt, ...) fprintf(stdout, fmt, __VA_ARGS__)
#endif
#endif
#ifndef ASSERT
#define ASSERT assert
#define ASSERT_M(expr, message) ((void)((!!(expr)) || (serrf("ASSERT_M(" #expr ", " #message "): `%s`\n", message))), assert(expr))
#endif