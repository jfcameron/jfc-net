# © Joseph Cameron - MIT License

cmake_minimum_required(VERSION 3.21 FATAL_ERROR)

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/jfc-cmake/jfclib.cmake")

project(jfcnet
    VERSION 0.1
    DESCRIPTION "jfc networking: polled loopback, ENet/UDP and browser WebRTC hosts, simulated network impairments and a bounded binary codec")

include(GNUInstallDirs)

option(JFC_BUILD_TESTS "Build unit tests" OFF)
option(JFC_BUILD_DOCS "Build documentation" OFF)
option(JFC_NET_TEST_REAL_BROWSERS "Test WebRTC with installed Playwright browsers (requires sockets)" OFF)

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
        "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>")

    target_compile_features(jfcnet PUBLIC cxx_std_20)

    if (EMSCRIPTEN)
        add_library(jfcnet_webrtc STATIC src/webrtc_host.cpp)
        add_library(jfc::net_webrtc ALIAS jfcnet_webrtc)
        set_target_properties(jfcnet_webrtc PROPERTIES
            EXPORT_NAME net_webrtc
            CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
        target_link_libraries(jfcnet_webrtc PUBLIC jfcnet)
        set_property(TARGET jfcnet_webrtc PROPERTY INTERFACE_LINK_OPTIONS
            "$<BUILD_INTERFACE:SHELL:--js-library \"${CMAKE_CURRENT_SOURCE_DIR}/src/webrtc.js\">"
            "$<INSTALL_INTERFACE:SHELL:--js-library \"\${_IMPORT_PREFIX}/${CMAKE_INSTALL_DATADIR}/jfcnet/webrtc.js\">")
        set_property(TARGET jfcnet_webrtc PROPERTY INTERFACE_LINK_DEPENDS
            "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src/webrtc.js>"
            "$<INSTALL_INTERFACE:${CMAKE_INSTALL_DATADIR}/jfcnet/webrtc.js>")
    endif()

    if (JFC_BUILD_TESTS)
        set(_jfcnet_core_tests
            "${CMAKE_CURRENT_SOURCE_DIR}/test/codec_test.cpp"
            "${CMAKE_CURRENT_SOURCE_DIR}/test/impaired_host_test.cpp")
        if (NOT EMSCRIPTEN)
            list(APPEND _jfcnet_core_tests "${CMAKE_CURRENT_SOURCE_DIR}/test/loopback_test.cpp")
        endif()
        jfc_add_tests(
            C++_STANDARD 20

            TEST_SOURCE_FILES
                ${_jfcnet_core_tests}

            LIBRARIES
                jfcnet
        )
    endif()

    set(_jfcnet_owns_enet OFF)

    if (NOT EMSCRIPTEN)
        if (NOT TARGET enet)
            set(_jfcnet_owns_enet ON)

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

        set_target_properties(jfcnet_enet PROPERTIES EXPORT_NAME net_enet)

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
                    enet
            )
        endif()
    endif()

    if (JFC_BUILD_FUZZERS)
        add_subdirectory(fuzz)
    endif()

    option(JFC_NET_BUILD_WEBRTC_PROBE "Build the standalone browser WebRTC connection probe" OFF)
    if (EMSCRIPTEN AND JFC_NET_BUILD_WEBRTC_PROBE)
        add_executable(jfcnet_webrtc_probe test/browser/probe.cpp)
        set_target_properties(jfcnet_webrtc_probe PROPERTIES SUFFIX ".html"
            CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
        target_link_libraries(jfcnet_webrtc_probe PRIVATE jfcnet_webrtc)
        target_link_options(jfcnet_webrtc_probe PRIVATE -sALLOW_MEMORY_GROWTH -sEXIT_RUNTIME=1)
    endif()

    if (JFC_BUILD_TESTS)
        add_subdirectory(test/browser)
    endif()

    include(CMakePackageConfigHelpers)

    set(_jfcnet_installed jfcnet)

    if (TARGET jfcnet_webrtc)
        list(APPEND _jfcnet_installed jfcnet_webrtc)
        install(FILES src/webrtc.js DESTINATION "${CMAKE_INSTALL_DATADIR}/jfcnet")
    endif()

    if (_jfcnet_owns_enet)
        list(APPEND _jfcnet_installed jfcnet_enet enet)
    endif()

    install(TARGETS ${_jfcnet_installed}
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

if (JFC_BUILD_DOCS)
    add_subdirectory(docs)
endif()
