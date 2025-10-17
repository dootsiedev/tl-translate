#pragma once

// Note I think my GL header requires Windows.h without WIN32_LEAN_AND_MEAN,
// and I think without WIN32_LEAN_AND_MEAN it sets ERROR as a macro, which clashes with enums...
// so make sure you include "breakpoint.h" after the opengl include, not before.
// TODO: I should stop putting gl.h and <windows.h> into the global scope

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <Windows.h>
#include <intrin.h> // for __debugbreak
#ifdef ERROR
#undef ERROR
#endif
#define MY_BREAKPOINT __debugbreak()
#elif defined(__EMSCRIPTEN__)
// abort is [[noreturn]], so the compiler will optimize away the code needed to continue,
// but I don't know how to step/use a debugger with wasm...
#include <stdlib.h>
#define MY_BREAKPOINT abort()
#else
// assume linux and has SIGTRAP
#include <signal.h>
#define MY_BREAKPOINT raise(SIGTRAP)
#endif