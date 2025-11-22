#pragma once

#ifdef _MSC_VER
#define MY_NOINLINE __declspec(noinline)
#else
#define MY_NOINLINE __attribute__((noinline))
#endif

// std::source_location can get the __FILE__/__LINE__ of the caller of member functions
// Most of my asserts just check for NULL at the start of a function,
// because I tend to use init()/destroy() instead of constructors + exceptions.
// which means there is a chance of using the data in the incorrect order,
// and a basic assert offers no info especially if there are multiple locations of use.
// I think it's possible std::source_location could give performance issues,
// It's possible that inlining (breaking stacktraces) wont give any performance.

#if defined(__cpp_lib_source_location) && !defined(DISABLE_CUSTOM_ASSERT)
#define MY_HAS_SOURCE_LOCATION 1
#else
#define MY_HAS_SOURCE_LOCATION 0
#endif

// TODO: PASS_SRC should be replaced by C++26 contract preconditions.
//  and PASS_SRC is annoying because I don't print both locations (but I print a stacktrace)
// BUT... I don't know if I can use contracts yet, it seems like it has a few compile time errors.
#if MY_HAS_SOURCE_LOCATION
#include <source_location>

// for passing the location into deeply nested functions, not needed for a single depth.
#define PASS_SRC_LOC _src_loc_data
#define PASS_SRC_LOC2 , _src_loc_data

// so you would use it like:
// void func(SRC_LOC);
// void func(SRC_LOC_IMPL){ASSERT_SRC_LOC(false);}
#define SRC_LOC const std::source_location PASS_SRC_LOC = std::source_location::current()
#define SRC_LOC_IMPL const std::source_location PASS_SRC_LOC

// if the function already has parameters, adds a comma.
// void func(int i SRC_LOC2);
// void func(int i SRC_LOC2_IMPL){ASSERT_SRC_LOC(false);}
#define SRC_LOC2 , SRC_LOC
#define SRC_LOC2_IMPL , SRC_LOC_IMPL

// I thought about using (__VA_ARGS__ __VA_OPT__(,) SRC_LOC)
// But I don't like the fact that msvc requires a flag to use __VA_OPT__
// usage: void func SRC_LOC(int i)

#else
#define SRC_LOC
#define SRC_LOC2
#define SRC_LOC_IMPL
#define SRC_LOC2_IMPL
#define PASS_SRC_LOC
#define PASS_SRC_LOC2
#endif

// clang-format off

// disabling custom assert + enabling NDEBUG should remove all asserts.
#ifdef DISABLE_CUSTOM_ASSERT

// [[assume(expr)]] will cause UB if an assert failed.
#ifdef FORCE_UNSAFE_ASSERT
// this requires C++23, unreachable might work as well.
#define UNSAFE_ASSERT(expr) [[assume(expr)]]
#define ASSERT(expr) [[assume(expr)]]
#define ASSERT_M(expr, _) [[assume(expr)]]
#define ASSERT_SRC_LOC ASSERT
#define ASSERT_M_SRC_LOC ASSERT_M
#else // FORCE_UNSAFE_ASSERT
#include <cassert>
#define ASSERT assert
#ifdef NDEBUG
#define ASSERT_M(expr, message) assert((expr) && #message)
#else
#define ASSERT_M(expr, message) ((void)((!!(expr)) || (fprintf(stderr, "ASSERT_M(" #expr ", " #message "): `%s`\n", message))), assert(expr))
#endif
#define ASSERT_SRC_LOC ASSERT
#define ASSERT_M_SRC_LOC ASSERT_M
#endif // FORCE_UNSAFE_ASSERT

#else //! DISABLE_CUSTOM_ASSERT

#if MY_HAS_SOURCE_LOCATION
#define ASSERT(expr) (void)((!!(expr)) || (implement_ASSERT(#expr, nullptr), 0))
#define ASSERT_M(expr, message) (void)((!!(expr)) || (implement_ASSERT(#expr, message), 0))
#define ASSERT_SRC_LOC(expr) (void)((!!(expr)) || (implement_ASSERT(#expr, nullptr, PASS_SRC_LOC), 0))
#define ASSERT_M_SRC_LOC(expr, message) (void)((!!(expr)) || (implement_ASSERT(#expr, message, PASS_SRC_LOC), 0))
[[noreturn]] MY_NOINLINE void implement_ASSERT(const char* expr, const char* message, SRC_LOC);
#else
#define ASSERT(expr) (void)((!!(expr)) || (implement_ASSERT(#expr, nullptr, __FILE__, __LINE__), 0))
#define ASSERT_M(expr, message) (void)((!!(expr)) || (implement_ASSERT(#expr, message, __FILE__, __LINE__), 0))
#define ASSERT_SRC_LOC ASSERT
#define ASSERT_M_SRC_LOC ASSERT_M
[[noreturn]] MY_NOINLINE void implement_ASSERT(const char* expr, const char* message, const char* file, int line);
#endif
#endif

// clang-format on

// unsafe assert for [[assume()]] micro optimizations if NDEBUG is defined.
// use DISABLE_CUSTOM_ASSERT + FORCE_UNSAFE_ASSERT if you want all ASSERT's to be unsafe.
#ifndef UNSAFE_ASSERT
// debug builds will use ASSERT
#ifndef NDEBUG
#define UNSAFE_ASSERT ASSERT
#else
// technically I could use unreachable in C++20
#ifdef __has_cpp_attribute
#if __has_cpp_attribute(assume)
#define UNSAFE_ASSERT(expr) [[assume(expr)]]
#endif
#endif

// I use C++17 ATM, so this allows msvc to use it.
#ifndef UNSAFE_ASSERT
#if defined(_MSC_VER) && !defined(__clang__) // MSVC
#define UNSAFE_ASSERT(expr) __assume(expr)
#endif
#endif

// fallback.
#ifndef UNSAFE_ASSERT
#define UNSAFE_ASSERT ASSERT
#endif
#endif
#endif

// this is a vague signal to say "THIS FUNCTION PRINTS TO SERR, CAPTURE IT!"
// if a function return is not for serr, just use [[nodiscard]].
#define NDSERR [[nodiscard]]

// CHECK is a soft assert, where the error is probably the programmers fault so the message is not
// user friendly, but unlike ASSERT the program can still continue (gracefully fail)
// returns the value of the condition.
// check prints into serr, which should be handled by serr_get_error.

// clang-format off
#if MY_HAS_SOURCE_LOCATION
#define CHECK(expr) ((!!(expr)) || (implement_CHECK(#expr, NULL), false))
#define CHECK_SRC_LOC(expr) ((!!(expr)) || (implement_CHECK(#expr, NULL, PASS_SRC_LOC), false))
#define CHECK_M(expr, message) ((!!(expr)) || (implement_CHECK(#expr, message), false))
#define CHECK_M_SRC_LOC(expr, message) ((!!(expr)) || (implement_CHECK(#expr, message, PASS_SRC_LOC), false))
MY_NOINLINE void implement_CHECK(const char* expr, const char* message, SRC_LOC);
#else
#define CHECK(expr) ((!!(expr)) || (implement_CHECK(#expr, NULL, __FILE__, __LINE__), false))
#define CHECK_SRC_LOC CHECK
#define CHECK_M(expr, message) ((!!(expr)) || (implement_CHECK(#expr, message, __FILE__, __LINE__), false))
#define CHECK_M_SRC_LOC CHECK_M
MY_NOINLINE void implement_CHECK(const char* expr, const char* message, const char* file, int line);
#endif
// clang-format on

// this opens a dialog window.
bool show_error(const char* title, const char* message);