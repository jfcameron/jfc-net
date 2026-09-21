# © Joseph Cameron - All Rights Reserved

cmake_minimum_required(VERSION 3.21 FATAL_ERROR)

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/jfc-cmake/jfclib.cmake")

project(jfcnet
    VERSION 0.1
    DESCRIPTION "jfc networking: a polled transport interface, its loopback, and a bounded binary codec")

option(JFC_BUILD_TESTS "Build unit tests" OFF)

set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if (MSVC)
    add_compile_options($<$<COMPILE_LANGUAGE:CXX>:/permissive->)
else()
    if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-pedantic>)
    endif()

    include(CheckCXXCompilerFlag)

    foreach (_strict_flag "-Werror=c++20-extensions" "-Werror=c++23-extensions")
        string(MAKE_C_IDENTIFIER "HAS${_strict_flag}" _strict_flag_symbol)

        check_cxx_compiler_flag("${_strict_flag}" ${_strict_flag_symbol})

        if (${_strict_flag_symbol})
            add_compile_options($<$<COMPILE_LANGUAGE:CXX>:${_strict_flag}>)
        endif()
    endforeach()
endif()

if (NOT TARGET jfcnet)
    add_library(jfcnet STATIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src/impaired_host.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/loopback.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/reader.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/writer.cpp)

    add_library(jfc::net ALIAS jfcnet)

    set_target_properties(jfcnet PROPERTIES EXPORT_NAME net)

    set_target_properties(jfcnet PROPERTIES PREFIX "lib")

    target_include_directories(jfcnet PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
        "$<INSTALL_INTERFACE:include>")

    target_compile_features(jfcnet PUBLIC cxx_std_20)

    if (JFC_BUILD_TESTS)
        jfc_add_tests(
            C++_STANDARD 20

            TEST_SOURCE_FILES
                "${CMAKE_CURRENT_SOURCE_DIR}/test/codec_test.cpp"
                "${CMAKE_CURRENT_SOURCE_DIR}/test/impaired_host_test.cpp"
                "${CMAKE_CURRENT_SOURCE_DIR}/test/loopback_test.cpp"

            LIBRARIES
                jfcnet
        )
    endif()

    if (NOT TARGET enet)
        set(_jfc_outer_build_tests "${JFC_BUILD_TESTS}")

        add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/extern/enet" "${CMAKE_CURRENT_BINARY_DIR}/extern/enet"
            EXCLUDE_FROM_ALL)

        set(JFC_BUILD_TESTS "${_jfc_outer_build_tests}")

        target_include_directories(enet PUBLIC "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/extern/enet/include>")

        if (WIN32)
            target_link_libraries(enet PUBLIC ws2_32 winmm)
        endif()
    endif()

    add_library(jfcnet_enet STATIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src/enet_host.cpp)

    add_library(jfc::net_enet ALIAS jfcnet_enet)

    set_target_properties(jfcnet_enet PROPERTIES PREFIX "lib")

    target_link_libraries(jfcnet_enet
        PUBLIC
            jfcnet
        PRIVATE
            enet)

    target_compile_features(jfcnet_enet PUBLIC cxx_std_20)

    if (JFC_BUILD_TESTS)
        jfc_add_tests(
            C++_STANDARD 20

            TEST_SOURCE_FILES
                "${CMAKE_CURRENT_SOURCE_DIR}/test/enet_host_test.cpp"

            LIBRARIES
                jfcnet_enet
        )
    endif()

    if (JFC_BUILD_FUZZERS)
        add_subdirectory(fuzz)
    endif()
endif()
