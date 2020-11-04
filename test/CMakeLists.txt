cmake_minimum_required(VERSION 3.5)

project(supercan LANGUAGES C CXX)
set (CMAKE_C_STANDARD 11)
set (CMAKE_CXX_STANDARD 14)


function(SetMaxWarningLevel compilerFlagsVar)
    #message(STATUS "var: ${compilerFlagsVar}")
    if(MSVC)
        # Force to always compile with W4
        if(${compilerFlagsVar} MATCHES "/W[0-4]")
            string(REGEX REPLACE "/W[0-4]" "/W4" _Temp "${${compilerFlagsVar}}")
            set(${compilerFlagsVar} "${_Temp}" PARENT_SCOPE)
        else()
            set(${compilerFlagsVar} "${${compilerFlagsVar}} /W4" PARENT_SCOPE)
        endif()
    elseif(CMAKE_COMPILER_IS_GNUCC OR CMAKE_COMPILER_IS_GNUCXX)
        set(${compilerFlagsVar} "${${compilerFlagsVar}} -Wall -Wextra -Wformat -Wc++-compat -Wno-strict-aliasing" PARENT_SCOPE)
    endif()
endfunction()

set(CMAKE_VERBOSE_MAKEFILE ON)

SetMaxWarningLevel("CMAKE_C_FLAGS")
SetMaxWarningLevel("CMAKE_CXX_FLAGS")

set(LIB_SRC_LIST
    ../src/usnprintf.c
    ../src/can_bit_timing.c
)

set(TEST_SRC_LIST
    main.cpp
    test_usnprintf.cpp
    test_can_bit_timing.cpp
    test_chunky.cpp
    test_dev_time_tracker.cpp
)

# CppUnitLite2 static lib
aux_source_directory(../3rd-party/CppUnitLite2/src CPPUNITLITE2_SRC_LIST)
add_library(CppUnitLite2 STATIC ${CPPUNITLITE2_SRC_LIST})
if(MSVC)
    target_compile_definitions(CppUnitLite2 PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

# Test
include_directories(
    ../3rd-party/CppUnitLite2/src
    ../src
)

add_executable(supercan-test ${TEST_SRC_LIST} ${LIB_SRC_LIST})
target_link_libraries(supercan-test CppUnitLite2)
