# © Joseph Cameron - MIT License

set(_jfcnet_tests
    "${CMAKE_CURRENT_LIST_DIR}/codec_test.cpp"
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
