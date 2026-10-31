# © Joseph Cameron - MIT License

cmake_minimum_required(VERSION 3.21 FATAL_ERROR)

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/jfc-cmake/jfclib.cmake")

project(jfcnet
    VERSION 0.1
    DESCRIPTION "jfc networking: a polled transport interface, a loopback, simulated network impairments and a bounded binary codec")

include(GNUInstallDirs)

option(JFCNET_BUILD_TESTS "Build unit tests" ${PROJECT_IS_TOP_LEVEL})
option(JFCNET_BUILD_DOCS "Build documentation" OFF)

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
        ${CMAKE_CURRENT_SOURCE_DIR}/src/loopback.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/reader.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/writer.cpp)

    add_library(jfc::net ALIAS jfcnet)

    set_target_properties(jfcnet PROPERTIES EXPORT_NAME net)

    set_target_properties(jfcnet PROPERTIES PREFIX "lib")

    target_include_directories(jfcnet PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
        "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>")

    target_compile_features(jfcnet PUBLIC cxx_std_20)

    add_library(jfcnet_impaired_host STATIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src/ext/impaired_host.cpp)

    add_library(jfc::net_impaired_host ALIAS jfcnet_impaired_host)

    set_target_properties(jfcnet_impaired_host PROPERTIES EXPORT_NAME net_impaired_host PREFIX "lib")

    target_compile_features(jfcnet_impaired_host PUBLIC cxx_std_20)

    target_link_libraries(jfcnet_impaired_host PUBLIC jfcnet)

    add_library(jfcnet_conformance INTERFACE)

    target_sources(jfcnet_conformance INTERFACE
        "${CMAKE_CURRENT_SOURCE_DIR}/test/conformance/transport_conformance.cpp")

    target_include_directories(jfcnet_conformance INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/test/conformance")

    target_link_libraries(jfcnet_conformance INTERFACE jfcnet)

    if (JFCNET_BUILD_TESTS)
        add_subdirectory(test)
    endif()

    if (JFC_BUILD_FUZZERS)
        add_subdirectory(fuzz)
    endif()

    include(CMakePackageConfigHelpers)

    install(TARGETS jfcnet jfcnet_impaired_host
        EXPORT jfcnet-targets
        ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}")

    install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/include/"
        DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")

    install(EXPORT jfcnet-targets
        FILE jfcnet-targets.cmake
        NAMESPACE jfc::
        DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/jfcnet")

    write_basic_package_version_file(
        "${PROJECT_BINARY_DIR}/jfcnet-config-version.cmake"
        VERSION ${PROJECT_VERSION}
        COMPATIBILITY SameMajorVersion)

    file(WRITE "${PROJECT_BINARY_DIR}/jfcnet-config.cmake"
"include(\"\${CMAKE_CURRENT_LIST_DIR}/jfcnet-targets.cmake\")
")

    install(FILES
        "${PROJECT_BINARY_DIR}/jfcnet-config.cmake"
        "${PROJECT_BINARY_DIR}/jfcnet-config-version.cmake"
        DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/jfcnet")
endif()

if (JFCNET_BUILD_DOCS)
    jfc_generate_documentation_doxygen(
        README_PATH      README.md
        OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/docs

        INCLUDE_DIRECTORIES
            include/
    )
endif()
