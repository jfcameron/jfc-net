# © Joseph Cameron - MIT License

set(_jfcnet_tests
    "${CMAKE_CURRENT_LIST_DIR}/codec_test.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/exception_test.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/loopback_conformance.cpp")

if (NOT EMSCRIPTEN)
    list(APPEND _jfcnet_tests "${CMAKE_CURRENT_LIST_DIR}/loopback_test.cpp")
endif()

jfc_add_tests(
    C++_STANDARD 20

    TEST_SOURCE_FILES
        ${_jfcnet_tests}

    LIBRARIES
        jfcnet
        jfcnet_conformance
)

jfc_add_tests(
    C++_STANDARD 20

    TEST_SOURCE_FILES
        "${CMAKE_CURRENT_LIST_DIR}/ext/impaired_host_conformance.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/ext/impaired_host_test.cpp"

    LIBRARIES
        jfcnet_impaired_host
        jfcnet_conformance
)

if (NOT CMAKE_CROSSCOMPILING)
    add_test(NAME jfc-net_install_and_consume
        COMMAND "${CMAKE_COMMAND}"
            "-DBUILD_DIR=${PROJECT_BINARY_DIR}"
            "-DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/install_and_consume"
            "-DCONFIG=$<CONFIG>"
            "-DGENERATOR=${CMAKE_GENERATOR}"
            "-DPLATFORM=${CMAKE_GENERATOR_PLATFORM}"
            "-DTOOLSET=${CMAKE_GENERATOR_TOOLSET}"
            "-DCXX_COMPILER=${CMAKE_CXX_COMPILER}"
            "-DCXX_FLAGS=${CMAKE_CXX_FLAGS}"
            "-DEXE_LINKER_FLAGS=${CMAKE_EXE_LINKER_FLAGS}"
            -P "${CMAKE_CURRENT_LIST_DIR}/install/install_and_consume.cmake")
endif()
