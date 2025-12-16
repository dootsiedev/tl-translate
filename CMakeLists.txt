cmake_minimum_required(VERSION 3.16)

if(${CMAKE_SOURCE_DIR} STREQUAL ${CMAKE_BINARY_DIR})
    message(FATAL_ERROR "Prevented in-tree built. Please create a build directory outside of the source code and call cmake from there")
endif()

project (translate-test CXX)

option(USE_CPP23 "to print stack traces and maybe something else" OFF)

# I personally target c++17, but I use c++23 for <stacktrace> for testing, and I want to try using <source_location>
if(USE_CPP23)
    set(CMAKE_CXX_STANDARD 23)
else()
    set(CMAKE_CXX_STANDARD 17)
endif()

set(CMAKE_CXX_EXTENSIONS ON)

include(GNUInstallDirs)


#This is a hack, uses a weird regex to copy libraries into the /bin dir.
#this is a command that shows the rpath: readelf -d exe |head -30
option(PORTABLE "Helps bundle DLL's when you install, also sets rpath on linux" OFF)

#for clang-cl
#clang-cl.exe acts exactly like MSVC (drag-n-drop replacement),
#but you can use clang++.exe which can generate exactly the same binary,
#but with different command line syntax (and MSVC=FALSE and GNUC=TRUE).
#so MY_CLANGCL means both MSVC and the GNUC.
if("x${CMAKE_CXX_COMPILER_ID}" MATCHES "x.*Clang")
    set(MY_CLANG TRUE)
    if("x${CMAKE_CXX_SIMULATE_ID}" STREQUAL "xMSVC")
        #called MY_ because I don't want it to clash with anything
        set(MY_CLANGCL TRUE)
    endif()
endif()


#
# global modifications, must be done before add_executable
#

#NDEBUG removes assert, I want to keep the checks for release builds with debug info.
#technically I have a custom assert that depends on DISABLE_CUSTOM_ASSERT.
if(MSVC)
    string( REPLACE "/DNDEBUG" "" CMAKE_CXX_FLAGS_RELWITHDEBINFO "${CMAKE_CXX_FLAGS_RELWITHDEBINFO}")
else()
    string( REPLACE "-DNDEBUG" "" CMAKE_CXX_FLAGS_RELWITHDEBINFO "${CMAKE_CXX_FLAGS_RELWITHDEBINFO}")
endif()

option(USE_LTO "Use link time optimizations" OFF)
if(USE_LTO)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION ON)
endif()


option(USE_SIMPLE_HARDEN "hardening that shouldn't affect performance / not worth putting into a option" OFF)

#If you install anywhere other than /usr or the default /usr/local
#the libraries will not be accessible without setting rpath (or manually doing LD_LIBRARY_PATH)
#if(PORTABLE OR NOT WIN32)
#    set(CMAKE_INSTALL_RPATH "$ORIGIN/../${CMAKE_INSTALL_LIBDIR}")
#endif()

find_program(CCACHE_FOUND ccache)
if(CCACHE_FOUND)
    option(USE_CCACHE "faster for rebuilding?" ON)
    if(USE_CCACHE)
        message(STATUS "ccache found")
        set(CMAKE_CXX_COMPILER_LAUNCHER ccache)
    endif()
endif()

#
# flags start
#

# no reason to turn of RTTI, but maybe it makes the binary smaller
option(NO_RTTI "disable run time type information" OFF)
# disable exceptions.
# msvc can still catch exceptions, but everything on the stack will leak.
option(NO_EXCEPTIONS "disable exception unwinding" OFF)

# put all the flags here.
# TODO: I don't put all the flags here.
#  I want to clean up the flag groups, I think I should split it up into 3 parts.
#  options / defines for my code, extensively-strict warnings (warnings + clang's ubsan=implicit-conversion),
#  and common flags used for external libraries (such as by fetchContent or a subdirectory, should match vcpkg flags).
#  (does INTERFACE/PUBLIC/PRIVATE help in this situation?)
set(MY_COMPILER_FLAGS "")

if (MSVC OR MY_CLANGCL)
    # this is missing  for stat instead of _stat.
    # it's ugly that I define _stat for linux, but... I kind of like the style...
	set(MY_COMPILER_FLAGS ${MY_COMPILER_FLAGS} -D_CRT_SECURE_NO_WARNINGS -D_CRT_NONSTDC_NO_DEPRECATE -D_SCL_SECURE_NO_WARNINGS -DNOMINMAX)
    if(USE_SIMPLE_HARDEN)
        set(MY_COMPILER_FLAGS ${MY_COMPILER_FLAGS} -D_MSVC_STL_HARDENING)
    endif()
endif()


if (MSVC)
    #set(MY_COMPILER_FLAGS ${MY_COMPILER_FLAGS} /D_CRT_SECURE_NO_WARNINGS /D_CRT_NONSTDC_NO_DEPRECATE /D_SCL_SECURE_NO_WARNINGS /DNOMINMAX)

    set(MY_COMPILER_FLAGS ${MY_COMPILER_FLAGS} /wd4244) # C4244: 'conversion_type': conversion from 'type1' to 'type2', possible loss of data
    set(MY_COMPILER_FLAGS ${MY_COMPILER_FLAGS} /wd4068) # C4068: unknown pragma

    # treat as error
    set(MY_COMPILER_FLAGS ${MY_COMPILER_FLAGS} /we4473)# warning C4473: 'printf' : not enough arguments passed for format string

    #I never noticed anything with utf8 because I don't use unicode, but why not.
    set(MY_COMPILER_FLAGS ${MY_COMPILER_FLAGS} /utf-8)

    if(MY_CLANGCL)
        # this is required for cfi sanitizer / LTO, but it's untested.
        #set(MY_COMPILER_FLAGS ${MY_COMPILER_FLAGS} -fuse-ld=lld-link)
    endif()

    # I turn this off for MY_CLANGCL because it's slower than msvc, because I think it's because analyzed by msvc.
    # (but I don't use clang-cl.exe, I don't know if it even compiles, I typically just use clang++.exe)
	option(NO_ANALYZE "disable msvc /analyze, required for slogf formatting warnings (but /analyze is slow)" ${MY_CLANGCL})
	if(NOT NO_ANALYZE)
		# will this enable SAL warnings for _Printf_format_string_
		set(MY_COMPILER_FLAGS ${MY_COMPILER_FLAGS} /analyze)
        # treat _Printf_format_string_ as error... This is terrible...
        set(MY_COMPILER_FLAGS ${MY_COMPILER_FLAGS} /we6063) #warning C6063: Missing string argument to 'MyFormat' that corresponds to conversion specifier '1'.
        set(MY_COMPILER_FLAGS ${MY_COMPILER_FLAGS} /we6067) #warning C6067: _Param_(2) in call to 'MyFormat' must be the address of a string. Actual type: 'int'.
        set(MY_COMPILER_FLAGS ${MY_COMPILER_FLAGS} /we6066)
        set(MY_COMPILER_FLAGS ${MY_COMPILER_FLAGS} /we6065)
        set(MY_COMPILER_FLAGS ${MY_COMPILER_FLAGS} /we6064)
        set(MY_COMPILER_FLAGS ${MY_COMPILER_FLAGS} /we6303) # warning C6303: Format string mismatch.
        set(MY_COMPILER_FLAGS ${MY_COMPILER_FLAGS} /we6302) # wchar_t %hs (does not apply to my code)
	endif()


    if(USE_CPP23)
        # for __VA_OPT__ (I don't use it ATM)
        set(MY_COMPILER_FLAGS ${MY_COMPILER_FLAGS} /Zc:preprocessor)
	else()
		# msvc C++17 is permissive by default
		# which means RVO can give different side effects between debug/release
		set(MY_COMPILER_FLAGS ${MY_COMPILER_FLAGS} /permissive-)
    endif()

    if(CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo")
        # frame pointer omission ruins stack traces
        # this only matters for 32 bit binaries
        set(MY_COMPILER_FLAGS ${MY_COMPILER_FLAGS} /Oy-)

        # I want relwithdebinfo to be closer to release
        string( REPLACE "/INCREMENTAL" "/INCREMENTAL:NO" CMAKE_EXE_LINKER_FLAGS_RELWITHDEBINFO "${CMAKE_EXE_LINKER_FLAGS_RELWITHDEBINFO}")
		# much smaller binary.
		set(CMAKE_EXE_LINKER_FLAGS_RELWITHDEBINFO "${CMAKE_EXE_LINKER_FLAGS_RELWITHDEBINFO} /OPT:REF")
    endif()

    if(NO_RTTI)
		string( REPLACE "/GR" "/GR-" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
        #set(MY_COMPILER_FLAGS ${MY_COMPILER_FLAGS} /GR-)
    endif()
    if(NO_EXCEPTIONS)
		string( REPLACE "/EHsc" "/EHs-" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
        set(MY_COMPILER_FLAGS ${MY_COMPILER_FLAGS} /D_HAS_EXCEPTIONS=0)
    endif()

    #silence the warnings for flags that are incompatible with asan
    if(USE_ASAN)
        #string( REPLACE "/INCREMENTAL" "/INCREMENTAL:NO" CMAKE_EXE_LINKER_FLAGS_RELWITHDEBINFO "${CMAKE_EXE_LINKER_FLAGS_RELWITHDEBINFO}")
        string( REPLACE "/INCREMENTAL" "/INCREMENTAL:NO" CMAKE_EXE_LINKER_FLAGS_DEBUG "${CMAKE_EXE_LINKER_FLAGS_DEBUG}")
    endif()
else()

    if(NO_RTTI)
        set(MY_COMPILER_FLAGS ${MY_COMPILER_FLAGS} -fno-rtti)
    endif()
    if(NO_EXCEPTIONS)
        set(MY_COMPILER_FLAGS ${MY_COMPILER_FLAGS} -fno-exceptions)
    endif()

    #this is a big chunk taken from openrtc2, I hope they don't mind, but I like warnings.
    include(CheckCXXCompilerFlag)

    # Check if a flag exists and add it to the list of compiler options
    function (ADD_CHECK_CXX_COMPILER_FLAG _CXXFLAGS _CACHE_VAR _FLAG)
        CHECK_CXX_COMPILER_FLAG("${_FLAG}" "${_CACHE_VAR}")
        if (${_CACHE_VAR})
            set(${_CXXFLAGS} ${${_CXXFLAGS}} ${_FLAG} PARENT_SCOPE)
        else ()
            message(STATUS "Unsupported CXXFLAG: ${_FLAG}")
        endif ()
    endfunction ()

    if(USE_SIMPLE_HARDEN)
        ADD_CHECK_CXX_COMPILER_FLAG(MY_COMPILER_FLAGS CXX_trivial-auto-var-init -ftrivial-auto-var-init=pattern)
    endif()


    if(NOT WIN32)
        include(CheckCXXSymbolExists)

        if(cxx_std_20 IN_LIST CMAKE_CXX_COMPILE_FEATURES)
            set(header version)
        else()
            set(header ciso646)
        endif()

        # I believe I require to build libc++ if I want checked iterators, and it does not support invalidation checks.
        check_cxx_symbol_exists(_LIBCPP_VERSION ${header} LIBCPP)
        if(LIBCPP)
            if(USE_SIMPLE_HARDEN)
                # this makes vector[] into a range check and probably more
                set(MY_COMPILER_FLAGS ${MY_COMPILER_FLAGS} -D_LIBCPP_HARDENING_MODE=_LIBCPP_HARDENING_MODE_FAST)
            elseif(CMAKE_BUILD_TYPE STREQUAL "Debug")
                # this makes the C++ library similar to msvc's /MDd with checked iterators I think?
                set(MY_COMPILER_FLAGS ${MY_COMPILER_FLAGS} -D_LIBCPP_HARDENING_MODE=_LIBCPP_HARDENING_MODE_EXTENSIVE)
            endif()
        endif()

        check_cxx_symbol_exists(__GLIBCXX__ ${header} GLIBCXX)
        if(GLIBCXX)
            if(USE_SIMPLE_HARDEN)
                # this makes vector[] into a range check and probably more
                set(MY_COMPILER_FLAGS ${MY_COMPILER_FLAGS} -D_GLIBCXX_ASSERTIONS)
            elseif(CMAKE_BUILD_TYPE STREQUAL "Debug")
                # this makes the C++ library similar to msvc's /MDd with checked iterators I think?
                set(MY_COMPILER_FLAGS ${MY_COMPILER_FLAGS} -D_GLIBCXX_DEBUG)
            endif()
        endif()

    endif()

    # this doesn't work. you need mingw, and it works without adding this
    if(0)#WIN32)
        # clang diagnostic colors with ninja?
        ADD_CHECK_CXX_COMPILER_FLAG(MY_COMPILER_FLAGS CXX_ANSI_ESCAPE_CODES -fansi-escape-codes)
    endif()

    ADD_CHECK_CXX_COMPILER_FLAG(MY_COMPILER_FLAGS CXX_WARN_SUGGEST_OVERRIDE -Wsuggest-override) #not in gcc
    ADD_CHECK_CXX_COMPILER_FLAG(MY_COMPILER_FLAGS CXX_WARN_DUPLICATED_COND -Wduplicated-cond) #not in clang
    ADD_CHECK_CXX_COMPILER_FLAG(MY_COMPILER_FLAGS CXX_WARN_NON_VIRTUAL_DTOR -Wnon-virtual-dtor) #not in gcc
    ADD_CHECK_CXX_COMPILER_FLAG(MY_COMPILER_FLAGS CXX_WARN_MISSING_VARIABLE_DECLARATIONS -Wmissing-variable-declarations) #not in gcc
    ADD_CHECK_CXX_COMPILER_FLAG(MY_COMPILER_FLAGS CXX_WARN_DUPLICATED_BRANCHES -Wduplicated-branches) #not in clang
    ADD_CHECK_CXX_COMPILER_FLAG(MY_COMPILER_FLAGS CXX_WARN_RESTRICT -Wrestrict) #part of -Wall in gcc, not in clang.
    #ADD_CHECK_CXX_COMPILER_FLAG(MY_COMPILER_FLAGS CXX_WARN_MISSING_FIELD_INITIALIZERS -Wmissing-field-initializers) #enabled by -Wextra on both gcc and clang????
    ADD_CHECK_CXX_COMPILER_FLAG(MY_COMPILER_FLAGS CXX_WARN_UNREACHABLE_CODE_BREAK -Wunreachable-code-aggressive) #not in gcc
    ADD_CHECK_CXX_COMPILER_FLAG(MY_COMPILER_FLAGS CXX_WARN_LOGICAL_OP -Wlogical-op) #not in clang
    ADD_CHECK_CXX_COMPILER_FLAG(MY_COMPILER_FLAGS CXX_WARN_RANGE_LOOP_ANALYSIS -Wloop-analysis) #not in gcc
    ADD_CHECK_CXX_COMPILER_FLAG(MY_COMPILER_FLAGS CXX_WARN_TAUTOLOGICAL_ZERO_COMPARE -Wtautological-constant-in-range-compare) #not in gcc
    ADD_CHECK_CXX_COMPILER_FLAG(MY_COMPILER_FLAGS CXX_WARN_IMPLICIT_FALLTHROUGH -Wimplicit-fallthrough) #not in clang

    ADD_CHECK_CXX_COMPILER_FLAG(MY_COMPILER_FLAGS CXX_WARN_FORMAT -Wformat)
    ADD_CHECK_CXX_COMPILER_FLAG(MY_COMPILER_FLAGS CXX_WARN_FORMAT_SECURITY -Wformat-security)

    # Do not enable for MinGW, as its headers contain redundant declarations of builtin functions (from orct2)
    ADD_CHECK_CXX_COMPILER_FLAG(MY_COMPILER_FLAGS CXX_WARN_REDUNDANT_DECLS -Wredundant-decls) #doesn't do anything on clang

    #these 2 warnings can be tested once in a while, but they can be annoying since final is not neccessary everywhere.
    #ADD_CHECK_CXX_COMPILER_FLAG(MY_COMPILER_FLAGS CXX_WARN_SUGGEST_FINAL_TYPES -Wsuggest-final-types)
    #ADD_CHECK_CXX_COMPILER_FLAG(MY_COMPILER_FLAGS CXX_WARN_SUGGEST_FINAL_METHODS -Wsuggest-final-methods)

    # I really dont want to convert float to double, but it's still annoying when
    ADD_CHECK_CXX_COMPILER_FLAG(MY_COMPILER_FLAGS CXX_WARN_DOUBLE_PROMOTION -Wdouble-promotion)

    # I remove the warning because I don't mind casting double to float.
    #ADD_CHECK_CXX_COMPILER_FLAG(MY_COMPILER_FLAGS CXX_NO_WARN_IMPLICIT_FLOAT_CONVERSION -Wno-implicit-float-conversion)
    # this redundant because it is controlled by above,
    # this has a risk of overflow but clang-tidy and ubsan should catch issues.
    # and explict casting will ruin ubsan from catching anything.
    #ADD_CHECK_CXX_COMPILER_FLAG(MY_COMPILER_FLAGS CXX_NO_WARN_IMPLICIT_INT_FLOAT_CONVERSION -Wno-implicit-int-float-conversion)

    ADD_CHECK_CXX_COMPILER_FLAG(MY_COMPILER_FLAGS CXX_WARN_SHADOW_ALL -Wshadow-all)

    if(NOT CXX_WARN_SHADOW_ALL)
        set(MY_COMPILER_FLAGS ${MY_COMPILER_FLAGS} -Wshadow)
    endif()

    ADD_CHECK_CXX_COMPILER_FLAG(MY_COMPILER_FLAGS CXX_WARN_ENUM_CONVERSION -Wenum-conversion)

    # a cope since it's pretty easy to neglect warnings that lead to crashes.
    ADD_CHECK_CXX_COMPILER_FLAG(MY_COMPILER_FLAGS CXX_WERROR_FORMAT -Werror=format)

    set(MY_COMPILER_FLAGS ${MY_COMPILER_FLAGS} -Wall -Wextra -Wpedantic)
    set(MY_COMPILER_FLAGS ${MY_COMPILER_FLAGS} -Wunused -Wundef -Wmissing-declarations -Wvla)

    # frame pointer omission ruins stack traces
    # this only matters for 32 bit binaries
    if(CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo")
        set(MY_COMPILER_FLAGS ${MY_COMPILER_FLAGS} -fno-omit-frame-pointer)
    endif()

endif()

#
# create ALL_SANITIZERS because you can reuse the settings on potential built-in libraries
#

add_library(ALL_SANITIZERS INTERFACE EXCLUDE_FROM_ALL)

option(USE_ASAN "Use address sanitizer" OFF)
if(USE_ASAN)
    if(MY_CLANGCL)
		#copied partly from clion https://www.jetbrains.com/help/clion/google-sanitizers.html#clang-cl-configure
		#you will need to change this to work with your system because msvc uses the same asan DLL name for clang.

        # change if you update...
        set(USE_CLANG_CL_ASAN_PATH "C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Tools/Llvm/x64/lib/clang/19/lib/windows" CACHE PATH "The clang path to clang_rt.asan_dynamic-x86_64")

		target_compile_options(ALL_SANITIZERS INTERFACE -fsanitize=address)
		target_link_directories(ALL_SANITIZERS INTERFACE ${USE_CLANG_CL_ASAN_PATH})
		#if(FORCE_STATIC_VCRT)
		#    target_link_libraries(ALL_SANITIZERS INTERFACE clang_rt.asan_static-x86_64.lib)
		#    target_link_options(ALL_SANITIZERS INTERFACE /wholearchive:clang_rt.asan_static_runtime_thunk-x86_64.lib)
		#else()
		target_link_libraries(ALL_SANITIZERS INTERFACE clang_rt.asan_dynamic-x86_64 clang_rt.asan_dynamic_runtime_thunk-x86_64)
		if(MSVC)
			target_link_options(ALL_SANITIZERS INTERFACE /wholearchive:clang_rt.asan_dynamic_runtime_thunk-x86_64.lib)
		endif()
	elseif(MSVC)
		#make sure to globally set this if you are running without a debugger:
		#set ASAN_VCASAN_DEBUGGING=1
		#alternatively if you can use a core dump instead (but the dumps are big):
		#set ASAN_SAVE_DUMPS=MyFileName.dmp
		#note the variables do not work in clang-cl because clang-cl doesn't support vcasan atm.
		target_compile_options(ALL_SANITIZERS INTERFACE /fsanitize=address)
	else()
        #to use more aggressive checks make sure to set:
        #ASAN_OPTIONS=strict_string_checks=1:detect_stack_use_after_return=1:check_initialization_order=1:strict_init_order=1
        #or define it through __asan_default_options
		target_compile_options(ALL_SANITIZERS INTERFACE -fsanitize=address)
		target_link_options(ALL_SANITIZERS INTERFACE -fsanitize=address)
	endif()
endif()



# don't forget to use ubsan_options=print_stacktrace=1, and make sure llvm_symbolizer is in PATH.
option(USE_UBSAN "Use undefined behavior sanitizer" OFF)
if(USE_UBSAN)
    if(MSVC AND NOT MY_CLANGCL)
        message(WARNING "ubsan not supported")
    endif()

    # I don't know why but asan + ubsan works fine with non static vcrt (this is probably wrong)
    if(MY_CLANGCL AND CMAKE_MSVC_RUNTIME_LIBRARY STREQUAL "MultiThreadedDLL")
        message(WARNING "USE_UBSAN needs CMAKE_MSVC_RUNTIME_LIBRARY to equal MultiThreaded because ubsan only works with the static runtime ATM")
    endif()

    target_compile_options(ALL_SANITIZERS INTERFACE -fsanitize=undefined)
    target_link_options(ALL_SANITIZERS INTERFACE -fsanitize=undefined)

    option(USE_UBSAN_TRAP "this makes the program stop, and raise the debugger" OFF)
    # (faster?) hardening: -fsanitize-minimal-runtime -fno-sanitize-merge
    if(USE_UBSAN_TRAP)
        if(MY_CLANG)
            target_compile_options(ALL_SANITIZERS INTERFACE -fsanitize-trap=undefined)
            target_link_options(ALL_SANITIZERS INTERFACE -fsanitize-trap=undefined)
        else()
            #gcc doesn't support the same flags
            target_compile_options(ALL_SANITIZERS INTERFACE -fsanitize-undefined-trap-on-error)
            target_link_options(ALL_SANITIZERS INTERFACE -fsanitize-undefined-trap-on-error)
        endif()
    elseif(USE_SIMPLE_HARDEN)
        # makes the application stop with a message.
        # BUT while caught in the debugger, it will not show the message...
        # I could probably allow the annoying clang checks to recover...
        target_compile_options(ALL_SANITIZERS INTERFACE -fno-sanitize-recover=all)
        # this helps with optimized builds?
        target_compile_options(ALL_SANITIZERS INTERFACE -fno-sanitize-merge)

        # fsanitize-minimal-runtime doesn't work with asan
        # And I believe I must add all the ubsan functions... which is terrible... (UNLESS I am using this wrong?)
        # I probably need this if I wanted to work in wasm-wasi.
        # AND if I wanted to print a stacktrace on other people's PC (llvm-symbolizer is 80mb).
        # someone did it here I think?
        # https://cvsweb.netbsd.org/bsdweb.cgi/src/common/lib/libc/misc/ubsan.c?rev=1.12
        #target_compile_options(ALL_SANITIZERS INTERFACE -fsanitize-minimal-runtime -fno-sanitize-merge)
    else()
        # these are clang specific, and these flags cause many false positives (especially implicit-conversion)
        if(MY_CLANG)
            target_compile_options(ALL_SANITIZERS INTERFACE -fsanitize=unsigned-integer-overflow -fsanitize=nullability -fsanitize=implicit-conversion -fsanitize=local-bounds)
        endif()
    endif()
    #else()
    #    if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    #        target_compile_options(ALL_SANITIZERS INTERFACE -fno-sanitize-trap=undefined)
    #        target_link_options(ALL_SANITIZERS INTERFACE -fno-sanitize-trap=undefined)
    #    else()
    #        #gcc doesn't support the same flags
    #        target_compile_options(ALL_SANITIZERS INTERFACE -fno-sanitize-undefined-trap-on-error)
    #        target_link_options(ALL_SANITIZERS INTERFACE -fno-sanitize-undefined-trap-on-error)
    #    endif()

endif ()


# I am unsure if TSAN would work, the FAQ say that C++11 threads are buggy
# or require the libc/libstdc++ standard library to be built with TSAN
option(USE_TSAN "Use thread sanitizer" OFF)
if(USE_TSAN)
    target_compile_options(ALL_SANITIZERS INTERFACE -fsanitize=thread)
    target_link_options(ALL_SANITIZERS INTERFACE -fsanitize=thread)
endif()

#I think MSAN requires all libraries to also be compiled with msan, unlike other sanitizers.
#this includes libstdc++ or libc++ and libraries like SDL
option(USE_MSAN "Use memory sanitizer" OFF)
if(USE_MSAN)
    target_compile_options(ALL_SANITIZERS INTERFACE -fsanitize=memory -fsanitize-memory-track-origins)
    target_link_options(ALL_SANITIZERS INTERFACE -fsanitize=memory -fsanitize-memory-track-origins)
endif()

#TODO: on gcc you have -fhardened, maybe I should emulate it?
# on clang you must enable link time optimizations for it to work
option(USE_FORTIFY "Use _FORTIFY_SOURCE=2 on linux" OFF)
if(USE_FORTIFY)
    target_compile_options(ALL_SANITIZERS INTERFACE -D_FORTIFY_SOURCE=2)
endif()


if(MY_CLANG)

    if(NOT WIN32)
        option(USE_CLANG_SAFESTACK "enable clang's fsanitize=safe-stack (linux only)" ${USE_SIMPLE_HARDEN})
        if(USE_CLANG_SAFESTACK)
            #one problem with safe-stack is that it doesn't work across DSO's (maybe?)
            #and it will not work with LSAN
            target_compile_options(ALL_SANITIZERS INTERFACE -fsanitize=safe-stack)
            target_link_options(ALL_SANITIZERS INTERFACE -fsanitize=safe-stack)
        endif()
    endif()

    # this works with MY_CLANGCL, don't use Clang-CL.exe, use clang++ with ninja
    # It could work with msbuild but it needs lld since LINK.exe does not understand CFI
    option(USE_CFI "Use clang's Control Flow Integrety sanitizer" OFF)
    if(USE_CFI)
        if(NOT USE_LTO)
            message(WARNING "you must enable USE_LTO with USE_CLANG_CFI")
        endif()

        target_compile_options(ALL_SANITIZERS INTERFACE -DMY_FIX_CFI_ICALL)

        # TODO: I probably could check for CFI sanitizer using a macro?
        target_compile_options(ALL_SANITIZERS INTERFACE -DFIX_OPENGL_CFI_ICALL)

        #note I think CMAKE_INTERPROCEDURAL_OPTIMIZATION will actually
        #emit something different than -flto, but I think the llvm IR is all that matters.
        #target_compile_options(ALL_SANITIZERS INTERFACE -flto)
        #target_link_options(ALL_SANITIZERS INTERFACE -flto)

        #visibility hidden is required
        if(NOT WIN32)
            target_compile_options(ALL_SANITIZERS INTERFACE -fvisibility=hidden)
        endif()

        #on my system, std::to_string would cause an indirect call error...
        #maybe that is libstd++'s fault, maybe libc is needed.
        target_compile_options(ALL_SANITIZERS INTERFACE -fsanitize=cfi)# -fno-sanitize=cfi-icall -fsanitize-cfi-icall-generalize-pointers)
        target_link_options(ALL_SANITIZERS INTERFACE -fsanitize=cfi)# -fno-sanitize=cfi-icall -fsanitize-cfi-icall-generalize-pointers)
        #target_compile_options(ALL_SANITIZERS INTERFACE -fsanitize-ignorelist=cfi_ignorelist.txt)
        #target_link_options(ALL_SANITIZERS INTERFACE -fsanitize-ignorelist=cfi_ignorelist.txt)

        # this is supposed to speed up linking with flto=thin, but it doesn't feel it.
        target_link_options(ALL_SANITIZERS INTERFACE -Xclang -funique-source-file-names)

        if(PORTABLE)
            target_compile_options(ALL_SANITIZERS INTERFACE -fsanitize-trap=cfi)
            target_link_options(ALL_SANITIZERS INTERFACE -fsanitize-trap=cfi)
        else()
            target_compile_options(ALL_SANITIZERS INTERFACE -fno-sanitize-trap=cfi)
            target_link_options(ALL_SANITIZERS INTERFACE -fno-sanitize-trap=cfi)
        endif()
    endif()
endif()

if(WIN32)
    option(USE_MSVC_CFG "enable /guard:cf on msvc" OFF)
    if(NOT USE_MSVC_CFG AND USE_SIMPLE_HARDEN AND NOT USE_CFI)
        set(USE_MSVC_CFG ON)
    endif()
    if(USE_MSVC_CFG)
        if(MSVC)
            target_compile_options(ALL_SANITIZERS INTERFACE /guard:cf)
            target_link_options(ALL_SANITIZERS INTERFACE /guard:cf)
        elseif(MY_CLANGCL)
            target_compile_options(ALL_SANITIZERS INTERFACE -Xclang -cfguard)
            target_link_options(ALL_SANITIZERS INTERFACE -Xlinker /guard:cf)
            # TODO: I should reuse this...
        elseif(MY_CLANG)
            target_compile_options(ALL_SANITIZERS INTERFACE -mguard=cf)
            target_link_options(ALL_SANITIZERS INTERFACE -mguard=cf)
        else()
            #I probably could get it working on mingw (at least clang), I just don't know the flag.
            message(WARNING "/guard:cf unsupported")
        endif()
    endif()
endif()

option(USE_CET "enable -fcf-protection=full or /CETCOMPAT, needs newish CPU's" OFF)
if(USE_CET)
    if(MSVC)
        target_compile_options(ALL_SANITIZERS INTERFACE /CETCOMPAT)
        target_link_options(ALL_SANITIZERS INTERFACE /CETCOMPAT)
    else()
        target_compile_options(ALL_SANITIZERS INTERFACE -fcf-protection=full)
        target_link_options(ALL_SANITIZERS INTERFACE -fcf-protection=full)
    endif()
endif()


if(NOT WIN32)
    option(USE_STACK_CLASH_PROTECTION "enable -fstack-clash-protection (linux only)" OFF)
    if(USE_STACK_CLASH_PROTECTION)
        #stack clash protection might already be enabled by default?
        target_compile_options(ALL_SANITIZERS INTERFACE -fstack-clash-protection)
        target_link_options(ALL_SANITIZERS INTERFACE -fstack-clash-protection)
    endif()
endif()

if(NOT MSVC OR MY_CLANG_CL)
    option(USE_STACK_PROTECTOR "enable -fstack-protector-strong" OFF)
    if(USE_STACK_PROTECTOR)
        target_compile_options(ALL_SANITIZERS INTERFACE -fstack-protector-strong)
        target_link_options(ALL_SANITIZERS INTERFACE -fstack-protector-strong)
    endif()

    if(NOT USE_STACK_PROTECTOR AND USE_SIMPLE_HARDEN)
        target_compile_options(ALL_SANITIZERS INTERFACE -fstack-protector)
        target_link_options(ALL_SANITIZERS INTERFACE -fstack-protector)
    endif()
endif()

#
# EMSCRIPTEN
#
if(EMSCRIPTEN)
    #NOTE: for USE_ASAN I need -sINITIAL_MEMORY=159391744, and it only works when I use CMAKE_EXE_LINKER_FLAGS
    #TODO: removing NDEBUG isn't enough to enable asserts for some reason. So don't use RelWithDebInfo, use Debug...
    set(CMAKE_EXECUTABLE_SUFFIX ".html")
    #I need source-maps because I wanted to get EM_LOG_C_STACK working,
    #but all the paths leads to "stdlib_typeinfo.cpp:18:0", and I tried enabling/disabling RTTI
    #requires: emrun --port 9090 --serve_root / .
    #set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -gsource-map -sLOAD_SOURCE_MAP=1 --source-map-base http://localhost:9090/")
    #set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -gsource-map")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} --preload-file ${CMAKE_SOURCE_DIR}/demo_files@/ --shell-file ${CMAKE_SOURCE_DIR}/demo_code/wasm_index.html")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -sEXPORTED_FUNCTIONS=_main,_free,_enter_fullscreen,_paste_clipboard,_copy_clipboard,_cut_clipboard -sEXPORTED_RUNTIME_METHODS=ccall,cwrap")
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        #interesting options: EXCEPTION_DEBUG, DEMANGLE_SUPPORT
        set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -sGL_ASSERTIONS=1")
        set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -sASSERTIONS=1")
    else()
        set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -sDISABLE_EXCEPTION_CATCHING=1")
    endif()
    #set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -sUSE_SDL=2")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -sFULL_ES3")
endif()


#
# start of project
#
add_executable(${PROJECT_NAME}
        $<$<BOOL:${WIN32}>:translate.manifest>

        code/main.cpp

		code/translate.h
        # I should put translate.cpp as far down this list as possible
        # because it will check if every language has the same translation as the reference.
        # but you should first fix your non-existing string asserts before that
        # I wonder if there is a custom cmake rule to only build AFTER other files.
		code/translate.cpp
		code/translation_context.h
		code/translation_context.cpp

        code/core/global_pch.h
        code/core/global.h
        code/core/global.cpp
        code/core/cvar.h
        code/core/cvar.cpp
        code/core/stacktrace.h
        code/core/stacktrace_shared.cpp
        code/core/stacktrace_libbacktrace.cpp
        code/core/stacktrace_win32.cpp
        code/core/RWops.h
        code/core/RWops.cpp
        code/core/assert.h
        code/core/assert.cpp
        code/core/breakpoint.h
        code/core/asan_helper.h
        code/core/asan_helper.cpp

        code/util/fnv1a_hash.h
        code/util/string_tools.h
        code/util/utf8_stuff.h
        code/util/utf8_stuff.cpp

        code/datastructures/BS_stream.h
        code/datastructures/BS_stream.cpp
        code/datastructures/linklist.h

        code/datastructures/event_listener_observer.h
        code/datastructures/event_listener_observer.cpp
)

# put the parser into it's own file so I can remove the debug info from it.
# the only problem is that technically I use the same binary for translate & merge-strings
# and it doesn't share the same serr, which is fine (it shouldn't use it, but it has headers...
add_library(TL_PARSER OBJECT EXCLUDE_FROM_ALL
        code/translation_parser.h
        code/translation_parser.cpp)

# for msvc... I either need to set the target to STATIC instead of OBJECT and use /DEBUG:NONE I think.
# but I worry that it's not gonna do what I want it to do
if(NOT MSVC OR MY_CLANGCL)
    # get rid of that pesky debug info (I can still get annoying stacktraces /sanitization that fills the whole screen)
    target_compile_options(TL_PARSER PRIVATE -g0)
endif()

# add the options
target_link_libraries(TL_PARSER MY_OPTIONS_FLAGS)
if(USE_UBSAN AND MY_CLANG)
    # I copy the sanitizer flags so that I can call fno-sanitize=implicit-conversion AFTER adding sanitizers...
    # there is probably a better way... I think using set() instead of a target would help.
    get_target_property(COPY_ALL_SAN_COMPILE_PROPERTIES ALL_SANITIZERS INTERFACE_COMPILE_OPTIONS)
    get_target_property(COPY_ALL_SAN_LINK_PROPERTIES ALL_SANITIZERS INTERFACE_LINK_OPTIONS)

    target_compile_options(TL_PARSER PRIVATE ${COPY_ALL_SAN_COMPILE_PROPERTIES})
    target_link_options(TL_PARSER PRIVATE ${COPY_ALL_SAN_LINK_PROPERTIES})

    target_compile_options(TL_PARSER PRIVATE -fno-sanitize=implicit-conversion)
else()
    target_link_libraries(TL_PARSER ALL_SANITIZERS)
endif()

#uhh... this should be part of MY_COMPILER_FLAGS, but I chose not...
add_library(MY_OPTIONS_FLAGS INTERFACE EXCLUDE_FROM_ALL)

if(MY_CLANG_CL)
    target_compile_options(MY_OPTIONS_FLAGS INTERFACE -fansi-escape-codes -fcolor-diagnostics)
endif()

# pdb files on mingw work surprisingly well, similar stacktrace results as clang-cl
# one of the problems with mingw is that Release won't give stacktraces for lldb / llvm-symbolizer / libbacktrace (libunwind),
# Unlike linux Release (but it would not include source/line info).
# For an optimized build, msvc tools (like windbg) has buggy stack traces (lldb/etc will print it correctly).
# I think the buggy stack trace is because of the itanium ABI.
if(MINGW)
    option(USE_LLVM_MINGW_PDB "make mingw use codeview pdb files" OFF)
    if(USE_LLVM_MINGW_PDB)
        # -gcodeview will still include some sort of debug info required for libunwind stacktraces (libbacktrace).
        # and that info will break lldb (only prints mingw symbols)
        # you need to add the flag -Xlinker --strip-debug, which will not affect the pdb (llvm-strip also works)
        # BUT libunwind(libbacktrace) can't get addresses from mingw DLL's (mixing DLL's is not good however)
        # I was able to use libunwind to grab a stacktrace and print it with dbghelp.
        if(USE_LIBBACKTRACE)
            message(WARNING "USE_LLVM_MINGW_PDB does not work with USE_LIBBACKTRACE")
        endif()
        target_compile_options(ALL_SANITIZERS INTERFACE -gcodeview)
        target_link_options(ALL_SANITIZERS INTERFACE -Xlinker --strip-debug -Wl,--pdb=)
    endif()
endif()


# flags to make the core library work as a terminal application.
target_compile_definitions(MY_OPTIONS_FLAGS INTERFACE DISABLE_SDL DISABLE_CONSOLE DISABLE_CUSTOM_ASSERT)

target_compile_definitions(MY_OPTIONS_FLAGS INTERFACE TL_ENABLE_FORMAT)

# this should be easier to test first.
option(TL_COMPILE_TIME_TRANSLATION "embed translations into the code, does not require boost parser" ON)

if(TL_COMPILE_TIME_TRANSLATION)
    target_compile_definitions(MY_OPTIONS_FLAGS INTERFACE TL_COMPILE_TIME_TRANSLATION)
else()
    # boost spirit takes up 500mb by default, and that's just for windows-x64...
    #find_package(Boost CONFIG REQUIRED COMPONENTS spirit)
    #target_link_libraries(${PROJECT_NAME} Boost::spirit)

    # boost parser works standalone (but needs boost hana for structs, I use tuples atm, and hana pulls a lot of boost)
    # but unlike boost spirit... boost parser needs C++ exceptions (I don't mind exceptions, clang-cl+asan does).
    target_include_directories(MY_OPTIONS_FLAGS SYSTEM INTERFACE "${CMAKE_SOURCE_DIR}/parser/include")

    # non virtual member function call, something with std::any casting.
    # ideally, I could try to compile ONLY the parser with this flag.
    # note: boost parser will crash llvm-link with cfi in version 19, but it works tested on 21.
    if(USE_CFI)
        target_compile_options(ALL_SANITIZERS INTERFACE -fno-sanitize=cfi-nvcall)
        target_link_options(ALL_SANITIZERS INTERFACE -fno-sanitize=cfi-nvcall)
    endif()
endif()


option(TL_COMPILE_TIME_ASSERTS "check that all _T(\"\") strings are included in translations/english_ref.inl during compile time, does not propagate to other languages ATM" OFF)

if(TL_COMPILE_TIME_ASSERTS)
    target_compile_definitions(MY_OPTIONS_FLAGS INTERFACE TL_COMPILE_TIME_ASSERTS)
endif()

# this gets rid of the assert popup, so instead it just uses plain assert()
#option(DISABLE_CUSTOM_ASSERT "by default a dialog window opens, this will use normal assert()" OFF)
#if(DISABLE_CUSTOM_ASSERT)
#    target_compile_definitions(MY_OPTIONS_FLAGS INTERFACE DISABLE_CUSTOM_ASSERT)
#endif()

option(FORCE_UNSAFE_ASSERT "replace assert with [[assume(x)]] which will lead to optimizations and UB" OFF)
if(FORCE_UNSAFE_ASSERT)
    if(NOT DISABLE_CUSTOM_ASSERT)
        message(ERROR "you must enable DISABLE_CUSTOM_ASSERT with FORCE_UNSAFE_ASSERT")
    endif()
    target_compile_definitions(MY_OPTIONS_FLAGS INTERFACE FORCE_UNSAFE_ASSERT)
endif()

# I don't think this actually helps much
option(USE_PCH "testing precompiled header global_pch.h" OFF)
if(USE_PCH)
    target_precompile_headers(MY_OPTIONS_FLAGS INTERFACE "code/core/global_pch.h")
endif()

#TODO: this should be a cvar...
if(WIN32)
    # it's slow because everything is still utf8, but at the last step I convert it to wchar_t
    # but slogf is much worse, it is vasprintf -> convert to wchar_t -> fwrite(stdout, wchar)
    # this has nothing to do with the utf8 manifest option,
    # for that to work you need to set the experimental utf8 localization in system settings, or set chcp 65001
    # This will probably be replaced with SetConsoleOutputCP(CP_UTF8) which is identical to chcp 65001,
    # but it triggers Address sanitizer when I use conemu... (very specific to me...).
    option(WIN_UNICODE_HACK "this will output utf8 to the console, but it's very SLOW!!!" OFF)
    if(WIN_UNICODE_HACK)
        target_compile_definitions(MY_OPTIONS_FLAGS INTERFACE WIN_UNICODE_HACK)
    endif()
endif()


# enable this just in case (clang on linux doesn't default to PIE)
option(USE_PIE "Enables ASLR in case it's off" OFF)
include(CheckPIESupported)
check_pie_supported(OUTPUT_VARIABLE output LANGUAGES CXX)
set_property(TARGET ${PROJECT_NAME} PROPERTY POSITION_INDEPENDENT_CODE ${USE_PIE} OR ${USE_SIMPLE_HARDEN})
if(USE_PIE AND NOT CMAKE_CXX_LINK_PIE_SUPPORTED)
    message(FATAL_ERROR "PIE is not supported at link time:\n${output}"
            "PIE link options will not be passed to linker.")
endif()

if(NOT WIN32 AND USE_SIMPLE_HARDEN)
# -Wl,-z,relro,-z,now
# OR use -fno-plt which is more aggressive?
    target_link_options(MY_OPTIONS_FLAGS PRIVATE -Wl,-z,relro,-z,now)
endif()

#
# SDL3
#
#find_package(SDL3 CONFIG REQUIRED)
#target_link_libraries(${PROJECT_NAME} SDL3::SDL3)

# use <stacktrace>
# it runs slower compared to dbghelp and libbacktrace (a stutter)
# it lacks the dbghelp thread context feature (watchdog), and function pointer info.
# it also wont work with msvc clang cfi, and ubsan might also print some implicit conversion on msvc.
option(USE_CPP_STACKTRACE "use <stacktrace> to print stack traces, requires USE_CPP23" OFF)
if(USE_CPP_STACKTRACE)
    if(NOT USE_CPP23)
        message(WARNING "USE_CPP23 is required for USE_CPP_STACKTRACE.")
    endif()
    target_compile_definitions(MY_OPTIONS_FLAGS INTERFACE USE_CPP_STACKTRACE)
endif()

option(USE_LIBBACKTRACE "to print stack traces" OFF)
if(USE_LIBBACKTRACE)
    target_compile_definitions(${PROJECT_NAME} PRIVATE USE_LIBBACKTRACE)
    if(LINUX)
        target_link_libraries(${PROJECT_NAME} "-lbacktrace")
        target_link_libraries(${PROJECT_NAME} "-ldl")
    elseif(MINGW)
        # This only works with vcpkg for mingw.
        # ATM I have libbacktrace installed in msys and I am too lazy to remove it.
        # It will say that it wont work on windows, but it will.
        # I use the clang64 msys terminal to install with vcpkg
        # ./vcpkg install --triplet=x64-mingw-dynamic libbacktrace --allow-unsupported
        # You could compile or install it from msys, and it will use less space.
        target_link_libraries(${PROJECT_NAME} "$<PATH:CMAKE_PATH,$ENV{MY_VCPKG_ROOT}/installed/${VCPKG_TARGET_TRIPLET}/lib/libbacktrace.a>")
    else()
        message(FATAL_ERROR "platform does not have libbacktrace")
    endif()
endif()

option(USE_WIN32_DEBUG_INFO "to print stack traces, requires dbghelp.dll to be supplied" OFF)
if(USE_WIN32_DEBUG_INFO)
    # I found a dbghelp package in vcpkg, but it seems impossible to get working because it doesn't have a .cmake file to load from.
    # you need to install windows debugging tools (comes with the old windbg) which includes dbghelp.dll globally.
    # I load dbghelp.dll dynamically during runtime when the stacktrace is requested.
    target_compile_definitions(MY_OPTIONS_FLAGS INTERFACE USE_WIN32_DEBUG_INFO)
endif()

# off by default because it's annoying for debugging,
# because the watchdog will prematurely exit if the timer is hit
# (and also ATM gltest does not use threads so this isn't used)
option(USE_WIN32_THREAD "I can use this to implement a watchdog" OFF)
if(USE_WIN32_THREAD)
    target_compile_definitions(MY_OPTIONS_FLAGS INTERFACE USE_WIN32_THREAD)
endif()

# USE_PTHREAD just raises a trap within the timed out thread,
# unlike windows it will not print a stacktrace, it's technically possible with a signal handler, but it's tricky.
option(USE_PTHREAD "I can use this to implement a watchdog" OFF)
if(USE_PTHREAD)
    target_compile_definitions(MY_OPTIONS_FLAGS INTERFACE USE_WIN32_THREAD)
endif()


target_link_libraries(${PROJECT_NAME} ALL_SANITIZERS MY_OPTIONS_FLAGS)
target_link_libraries(${PROJECT_NAME} TL_PARSER)
target_compile_options(${PROJECT_NAME} PRIVATE ${MY_COMPILER_FLAGS})


#
# install
#

set(EXECUTABLE_INSTALLATION_PATH ${CMAKE_INSTALL_BINDIR})

if(WIN32)
    # copied from https://github.com/retifrav/scraps/blob/master/CMake/index.md#copying-dlls-on-installation
    # that is a sad crutch for installing dependencies DLLs on Windows
    if(NOT EXISTS ${CMAKE_INSTALL_PREFIX}/${EXECUTABLE_INSTALLATION_PATH}) # it doesn't exist before the very first installation yet
        #install(DIRECTORY DESTINATION ${CMAKE_INSTALL_PREFIX}/${EXECUTABLE_INSTALLATION_PATH}) # still too late, because add_custom_command() will execute before that
        file(MAKE_DIRECTORY ${CMAKE_INSTALL_PREFIX}/${EXECUTABLE_INSTALLATION_PATH}) # but at least this one will execute before add_custom_command()
    endif()
endif()

if(APPLE)
    install(TARGETS ${PROJECT_NAME}
            BUNDLE DESTINATION ${CMAKE_INSTALL_BINDIR}
    )
else()
    install(TARGETS ${PROJECT_NAME}
            RUNTIME DESTINATION ${EXECUTABLE_INSTALLATION_PATH}
    )
endif()

if(NOT TL_COMPILE_TIME_TRANSLATION)
    add_custom_command(
            TARGET ${PROJECT_NAME}
            POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different ${PROJECT_SOURCE_DIR}/translations $<TARGET_FILE_DIR:${PROJECT_NAME}>/translations
            COMMENT "copying translation files")
    file(GLOB TRANSLATION_FILES ${PROJECT_SOURCE_DIR}/translations/*.inl)
    install(FILES ${TRANSLATION_FILES} DESTINATION ${EXECUTABLE_INSTALLATION_PATH}/translations )
endif()

if(0)
# I made a mistake by storing the hexfont in bzip2 to save space in the git repo,
# since I will obviously have more assets in the future...
# but I am too lazy to undo it since I know this works...
find_program(MY_TOOL_EXECUTABLE bzip2 HINTS "${VCPKG_ROOT}/installed/${VCPKG_TARGET_TRIPLET}/tools/bzip2" OPTIONAL)
if(MY_TOOL_EXECUTABLE)
    # I don't know if this is what normal cmake projects do.
    # TODO: should I copy all the other files into CMAKE_CURRENT_BINARY_DIR?
    set(HEXFILE_OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/unifont-full.hex)
    add_custom_command(
            OUTPUT ${HEXFILE_OUTPUT}
            COMMAND ${MY_TOOL_EXECUTABLE} -c -k -d ${CMAKE_CURRENT_SOURCE_DIR}/data/unifont-full.hex.bz2 > ${HEXFILE_OUTPUT}
            DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/data/unifont-full.hex.bz2"
            COMMENT "extracting hexfont bzip2"
    )

    install(FILES ${HEXFILE_OUTPUT} DESTINATION ${EXECUTABLE_INSTALLATION_PATH})
else()
    # I could use the mini wasm version.
    message(WARNING "bzip2 not found. using demo_files/unifont-full.hex version.")
    install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/demo_files/unifont-full.hex" DESTINATION ${EXECUTABLE_INSTALLATION_PATH} )
endif()
endif()


if(MINGW)
    # I need msys because I can't figure out how to make vcpkg detect that I am using mingw.
    # I was able to just copy llvm-mingw into the clang64 folder of msys (because I already downloaded it)
    # and it will work perfectly fine with the clang terminal!
    # https://github.com/mstorsjo/llvm-mingw
    # I don't think there is any reason to use this particular build of llvm (msys has the same libc++/libunwind)
    # BUT I want to building llvm from source so that I can try out iterator checks and sanitizers.
    # Not sure if dumping clang into the clang64 folder is safe/correct, it will conflict with pacman packages.
    set(USE_MINGW_BIN_PATH "C:/msys64/clang64/bin" CACHE PATH "The /bin of your mingw, that contains compilers/dll's")
    add_custom_command(
            TARGET ${PROJECT_NAME}
            POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different ${USE_MINGW_BIN_PATH}/libc++.dll ${USE_MINGW_BIN_PATH}/libunwind.dll $<TARGET_FILE_DIR:${PROJECT_NAME}>
            COMMENT "copying libc++ and unwind"
    )
    #install(FILES ${LIBCPP_FILE} ${LIBUNWIND_FILE} DESTINATION ${EXECUTABLE_INSTALLATION_PATH} )
endif()

# copy asan so that it works inside the build directory without an IDE, because vcpkg wont...
if(USE_ASAN AND WIN32)
    if(MSVC)
        # this should be in vcvarsall.bat
        find_file(ASAN_LIBRARY clang_rt.asan_dynamic-x86_64.dll REQUIRED)
    elseif(MY_CLANGCL)
        find_file(ASAN_LIBRARY clang_rt.asan_dynamic-x86_64.dll HINTS ${USE_CLANG_CL_ASAN_PATH} REQUIRED)
    elseif(MINGW)
        find_file(ASAN_LIBRARY libclang_rt.asan_dynamic-x86_64.dll HINTS ${USE_MINGW_BIN_PATH} REQUIRED)
    else()
        message(FATAL_ERROR "unsupported windows asan platform")
    endif()
    if (ASAN_LIBRARY)
        message(STATUS "Found Asan library: ${ASAN_LIBRARY}")
        add_custom_command(
                TARGET ${PROJECT_NAME}
                POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different ${ASAN_LIBRARY} $<TARGET_FILE_DIR:${PROJECT_NAME}>
                COMMENT "copying clang_rt.asan_dynamic-x86_64.dll"
        )
    else()
        message(WARNING "Asan library library not found.")
    endif()
    # I don't need to install it because I already copy it with GET_RUNTIME_DEPENDENCIES
    #install(FILES ${ASAN_LIBRARY} DESTINATION ${EXECUTABLE_INSTALLATION_PATH})
endif()


if(PORTABLE AND USE_UBSAN)
    # I could copy llvm-symbolizer (and I think asan might also use it?)
endif()

# black magic install all shared libraries.
if(PORTABLE OR WIN32)
    #if(EXISTS VCPKG_ROOT)
    #    set(MY_DEPENDENCY_PATHS "${VCPKG_ROOT}/installed/${VCPKG_TARGET_TRIPLET}")
    #else()

    set(MY_DEPENDENCY_PATHS ${CMAKE_SYSTEM_LIBRARY_PATH})

    # when I build from source, I use CMAKE_PREFIX_PATH for loading libraries.
    # but for vcpkg this is pointless.
    foreach(_file ${CMAKE_PREFIX_PATH})
        set(MY_DEPENDENCY_PATHS ${MY_DEPENDENCY_PATHS} "${_file}/${CMAKE_INSTALL_LIBDIR}")
    endforeach()

    message(STATUS "MY_DEPENDENCY_PATHS: ${MY_DEPENDENCY_PATHS}")
    install(CODE "set(MY_DEPENDENCY_PATHS \"${MY_DEPENDENCY_PATHS}\")")

    # Transfer the value of ${MY_DEPENDENCY_PATHS} into the install script
	install(CODE "set(MY_DESTINATION_PATH \"${CMAKE_INSTALL_PREFIX}/${EXECUTABLE_INSTALLATION_PATH}\")")
    install(CODE "set(MY_EXECUTABLE_NAME \"$<TARGET_FILE:${PROJECT_NAME}>\")")
    install(CODE [==[
        # I think I can use RUNTIME_DEPENDENCY_SET, but I don't want to touch this ATM.
        # see https://github.com/Todiq/test_conan/blob/main/beta/CMakeLists.txt
		file(GET_RUNTIME_DEPENDENCIES
			EXECUTABLES ${MY_EXECUTABLE_NAME}
			RESOLVED_DEPENDENCIES_VAR _r_deps
			UNRESOLVED_DEPENDENCIES_VAR _u_deps
			DIRECTORIES ${MY_DEPENDENCY_PATHS}
			PRE_EXCLUDE_REGEXES
                [=[api-ms-]=]
                [=[ext-ms-]=]
                [[kernel32\.dll]]
                [[libc\.so\..*]] [[libgcc_s\.so\..*]] [[libm\.so\..*]] [[libstdc\+\+\.so\..*]]
			POST_EXCLUDE_REGEXES
                [=[.*system32\/.*\.dll]=]
                [=[^\/(lib|usr\/lib|usr\/local\/lib)]=]
		)
		foreach(_file ${_r_deps})
			if(WIN32)
				#this fixes the filename being lowercase.
				get_filename_component(_file "${_file}" ABSOLUTE)
                #NOTE: I could try to replace the extension with pdb and install that too.
			endif()
			file(INSTALL
				DESTINATION ${MY_DESTINATION_PATH}
				TYPE SHARED_LIBRARY
				FOLLOW_SYMLINK_CHAIN
				FILES "${_file}"
			)
		endforeach()
        foreach(_file ${_u_deps})
            message(WARNING "Unresolved dependency: ${_file}")
		endforeach()
    ]==])

    # copy PDB
    if(MSVC OR MY_CLANGCL)
        # copy the exe PDB file.
        install(CODE "set(MY_DEBUG_SYMBOL $<TARGET_PDB_FILE:${PROJECT_NAME}>)")
        install(CODE [[file(INSTALL FILES ${MY_DEBUG_SYMBOL}  DESTINATION ${MY_DESTINATION_PATH} OPTIONAL)]])

        # I could use RUNTIME_DEPENDENCIES to get ALL the PDB files, but I don't want all the PDB files.
        # and if I was doing a full sanitizer build with ubsan, static linking would include all the vcpkg libraries.
        install(CODE "set(MY_RUNTIME_DEPS \"$<TARGET_RUNTIME_DLLS:${PROJECT_NAME}>\")")
        install(CODE [==[
        foreach(_file ${MY_RUNTIME_DEPS})
            #replace the DLL extension with pdb
            string(REGEX REPLACE "[.]dll$" ".pdb" _file ${_file})
            file(INSTALL
				DESTINATION ${MY_DESTINATION_PATH}
				FILES "${_file}" OPTIONAL)
		endforeach()
    ]==])
    endif()

endif()


#
# tests
#
option(BUILD_TESTS "ATM only for the merge-strings tool, I plan on using extract-strings but it's separate because it's LLVM..." OFF)
if(BUILD_TESTS)
    # I want to fetchContent this just for fun, but I don't know if fetchcontent will let me set the build flags.
    add_subdirectory(tools/merge-strings)

    #TODO: use curl to call the openrouter embedding API
    #find_package(CURL REQUIRED)
    #target_link_libraries(merge-strings PRIVATE CURL::libcurl)

    # used for TCP between python.
    find_package(libuv CONFIG REQUIRED)
    target_link_libraries(merge-strings PRIVATE $<IF:$<TARGET_EXISTS:libuv::uv_a>,libuv::uv_a,libuv::uv>)

    target_link_libraries(merge-strings PRIVATE TL_PARSER)
    target_link_libraries(merge-strings PRIVATE ALL_SANITIZERS MY_OPTIONS_FLAGS)
    target_compile_options(merge-strings PRIVATE ${MY_COMPILER_FLAGS})
endif()