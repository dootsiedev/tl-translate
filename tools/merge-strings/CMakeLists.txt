cmake_minimum_required(VERSION 3.16)

if(${CMAKE_SOURCE_DIR} STREQUAL ${CMAKE_BINARY_DIR})
    message(FATAL_ERROR "Prevented in-tree built. Please create a build directory outside of the source code and call cmake from there")
endif()

project(merge-strings CXX)

set(CMAKE_CXX_STANDARD 17)
  
add_executable(merge-strings main.cpp

        # I could set this with add_subdirectory (with all build flags) and more folder organization...
        ../../code/translation_parser.h
        ../../code/translation_parser.cpp

        ../../code/core/global_pch.h
        ../../code/core/global.h
        ../../code/core/global.cpp
        ../../code/core/cvar.h
        ../../code/core/cvar.cpp
        ../../code/core/stacktrace.h
        ../../code/core/stacktrace_shared.cpp
        ../../code/core/stacktrace_libbacktrace.cpp
        ../../code/core/stacktrace_win32.cpp
        ../../code/core/RWops.h
        ../../code/core/RWops.cpp
        ../../code/core/assert.h
        ../../code/core/assert.cpp
        ../../code/core/breakpoint.h

        ../../code/util/utf8_stuff.h
        ../../code/util/utf8_stuff.cpp

        ../../code/datastructures/BS_stream.h
        ../../code/datastructures/BS_stream.cpp
)
