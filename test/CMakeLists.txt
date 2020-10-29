cmake_minimum_required(VERSION 3.5)

project(supercan LANGUAGES C CXX)
set (CMAKE_C_STANDARD 11)
set (CMAKE_CXX_STANDARD 17)

set(SUPERCAN_FW_DIR ../Boards/examples/device/supercan)

message(STATUS "SUPERCAN_FW_DIR: ${SUPERCAN_FW_DIR}")

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

#SetMaxWarningLevel("CMAKE_C_FLAGS")
#SetMaxWarningLevel("CMAKE_CXX_FLAGS")

set(TEST_SRC_LIST
    main.cpp
    test_usnprintf.cpp
    ../Windows/app/can_bit_timing.c
    test_can_bit_timing.cpp
    ${SUPERCAN_FW_DIR}/src/usnprintf.c

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
    ${SUPERCAN_FW_DIR}/inc
    ../Windows/app)

add_executable(supercan-test ${TEST_SRC_LIST})
target_link_libraries(supercan-test CppUnitLite2)
