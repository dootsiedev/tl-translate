cmake_minimum_required(VERSION 3.16)

if(${CMAKE_SOURCE_DIR} STREQUAL ${CMAKE_BINARY_DIR})
    message(FATAL_ERROR "Prevented in-tree built. Please create a build directory outside of the source code and call cmake from there")
endif()

#project(merge-strings CXX)

#set(CMAKE_CXX_STANDARD 17)

#set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

  
add_executable(merge-strings
        $<$<BOOL:${WIN32}>:../../translate.manifest>

        main.cpp

        # Kind of ugly, there could be a better way of organizing this.
        ../../code/translation_parser.h
        ../../code/translation_parser.cpp

        ../../code/util/utf8_stuff.h
        ../../code/util/utf8_stuff.cpp

)
target_compile_definitions(merge-strings PUBLIC MY_DISABLE_GLOBAL_DEP TL_ENABLE_FORMAT)
target_include_directories(merge-strings SYSTEM PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/cxxopts")


enable_testing() # Enable CTest
add_test(NAME MyTest COMMAND merge-strings
        --patch "${CMAKE_SOURCE_DIR}/translations/english_ref.inl"
        --stage-dir "${CMAKE_CURRENT_BINARY_DIR}/test-translations/"
        "${CMAKE_CURRENT_SOURCE_DIR}/test/empty_jp.inl")

option(ENABLE_AI "Use BGE m3 for merging similar strings, you need python3 with FlagEmbedding installed." OFF)
if(ENABLE_AI)
    #I need python for the BGE library
    # based on this
    if(AI_PYTHON_DIR)
        set(Python3_ROOT_DIR "${AI_PYTHON_DIR}")
        set(Python3_FIND_VIRTUALENV FIRST)
        ## update the environment with VIRTUAL_ENV variable (mimic the activate script)
        # NOTE: I have not tested if this actually makes find_python find python in the folder.
    endif()
    find_package(Python3 REQUIRED COMPONENTS Interpreter)

    # I will install flag embedding if either I create venv, or if venv already exists but it is missing FlagEmbedding
    function(install_flag_embedding)
        if(NOT AI_PYTHON_DIR)
            message(FATAL_ERROR "AI_PYTHON_DIR not set while trying to install FlagEmbedding!")
        endif()
        #
        cmake_path(IS_PREFIX AI_PYTHON_DIR ${Python3_EXECUTABLE} _python_is_in_parent)
        if(_python_is_in_parent)
            # I could find a python script to check if the PC has a GPU with cuda support
            # BUT... my 1660 TI took 30 seconds (15s GPU busy) but my CPU 12100f took 40 seconds
            # for 100 queries and 100 passages (for 2 x 2, they both take 10 seconds).
            # And CUDA is a big dependency I think, it's hard to say it's worth it.
            message(STATUS "Installing FlagEmbedding to AI_PYTHON_DIR = ${AI_PYTHON_DIR}")
            message(STATUS "WARNING this will install torch without cuda (from my testing, not worth it)")
            message(STATUS "this worked for me (in AI_PYTHON_DIR/Scripts): pip3 install torch torchvision torchaudio --index-url https://download.pytorch.org/whl/cu126 --upgrade --force-reinstall")
            execute_process(COMMAND ${Python3_EXECUTABLE} -m pip install -U FlagEmbedding COMMAND_ERROR_IS_FATAL ANY)
        else()
            message(FATAL_ERROR
                    "python not found in AI_PYTHON_DIR while trying to install FlagEmbedding!\n"
                    "AI_PYTHON_DIR = ${AI_PYTHON_DIR}\n"
                    "Python = ${Python3_EXECUTABLE}\n")
        endif()
    endfunction()

    if(AI_PYTHON_DIR)
        # I use find_path to normalize to an absolute path, but I could normalize it with cmake_path I think
        find_path(AI_PYTHON_DIR PATHS ${AI_PYTHON_DIR} REQUIRED)
        cmake_path(IS_PREFIX AI_PYTHON_DIR ${Python3_EXECUTABLE} _python_is_in_parent)

        if(_python_is_in_parent)
            message(STATUS "Using the AI_PYTHON_DIR = ${AI_PYTHON_DIR}")
        else()
            message(STATUS "Python not in AI_PYTHON_DIR.")
            message(STATUS "AI_PYTHON_DIR = ${AI_PYTHON_DIR}")
            message(STATUS "Python = ${Python3_EXECUTABLE}")

            message(STATUS "Attempting to create venv.")

            # https://discourse.cmake.org/t/possible-to-create-a-python-virtual-env-from-cmake-and-then-find-it-with-findpython3/1132
            execute_process(COMMAND "${Python3_EXECUTABLE}" -m venv "${AI_PYTHON_DIR}" COMMAND_ERROR_IS_FATAL ANY)
            ## update the environment with VIRTUAL_ENV variable (mimic the activate script)
            set(ENV{VIRTUAL_ENV} "${AI_PYTHON_DIR}")
            ## change the context of the search
            set(Python3_FIND_VIRTUALENV ONLY)
            ## unset Python3_EXECUTABLE because it is also an input variable (see documentation, Artifacts Specification section)
            unset(Python3_EXECUTABLE)
            # this is just in case you don't have a global Python dir, and manually set Python3_ROOT_DIR
            # Not sure if this works, or necessary.
            set(Python3_ROOT_DIR "${AI_PYTHON_DIR}" CACHE PATH FORCE)
            ## Launch a new search
            find_package(Python3 REQUIRED COMPONENTS Interpreter)

            install_flag_embedding()
        endif()
    endif()

    # find FlagEmbedding
    # TODO: I don't use python, I think there are a lot of python package managers... Does this work with anaconda?
    #  I think I need to just ONLY check pip if I have AI_PYTHON_DIR?
    execute_process(COMMAND ${Python3_EXECUTABLE} -m pip show FlagEmbedding
            RESULT_VARIABLE pip_result
            OUTPUT_VARIABLE pip_output
            ERROR_VARIABLE pip_error)

    if(pip_result EQUAL 0)
        message(STATUS "FlagEmbedding was found! ${pip_output}")
    else()
        if(AI_PYTHON_DIR)
            install_flag_embedding()
        else()
            message(FATAL_ERROR "FlagEmbedding was not found: ${pip_error}!\n"
                    "use AI_PYTHON_DIR to create a venv folder if you want a to install FlagEmbedding in cmake")
        endif()
    endif()
    add_test(NAME AI_Test COMMAND ${CMAKE_COMMAND} -E env HF_HUB_CACHE=${AI_CACHE_DIR} --
            ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/BGE-m3-Embedding.py)
endif()