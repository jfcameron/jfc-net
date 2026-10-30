# © Joseph Cameron - MIT License

find_program(JFC_NET_NODE NAMES node nodejs)
if (JFC_NET_NODE)
    add_test(NAME jfcnet_webrtc_contract
        COMMAND "${JFC_NET_NODE}" "${CMAKE_CURRENT_SOURCE_DIR}/contract.cjs")
    if (JFC_NET_TEST_REAL_BROWSERS)
        if (TARGET jfcnet_webrtc_probe)
            set(_jfcnet_probe "$<TARGET_FILE:jfcnet_webrtc_probe>")
        else()
            set(_jfcnet_probe "")
        endif()
        add_test(NAME jfcnet_webrtc_real
            COMMAND "${JFC_NET_NODE}" "${CMAKE_CURRENT_SOURCE_DIR}/real.cjs" "${_jfcnet_probe}")
        set_tests_properties(jfcnet_webrtc_real PROPERTIES TIMEOUT 240)
    endif()
elseif (JFC_NET_TEST_REAL_BROWSERS OR EMSCRIPTEN)
    message(FATAL_ERROR "Node is required for the WebRTC tests")
endif()

if (EMSCRIPTEN)
    add_test(NAME jfcnet_webrtc_install
        COMMAND "${CMAKE_COMMAND}"
            "-DJFC_NET_BUILD_DIR=${PROJECT_BINARY_DIR}"
            "-DJFC_NET_FIXTURE=${CMAKE_CURRENT_SOURCE_DIR}/fixture.js"
            "-DJFC_NET_TOOLCHAIN=${CMAKE_TOOLCHAIN_FILE}"
            "-DJFC_NET_GENERATOR=${CMAKE_GENERATOR}"
            "-DJFC_NET_NODE=${JFC_NET_NODE}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/install.cmake")
    jfc_add_tests(C++_STANDARD 20
        TEST_SOURCE_FILES "${CMAKE_CURRENT_SOURCE_DIR}/webrtc_host_test.cpp"
        LIBRARIES jfcnet_webrtc)
    target_link_options(${JFC_TEST_TARGET} PRIVATE
        "SHELL:--pre-js \"${CMAKE_CURRENT_SOURCE_DIR}/fixture.js\"" -sALLOW_MEMORY_GROWTH)
    set_property(TARGET ${JFC_TEST_TARGET} APPEND PROPERTY LINK_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/fixture.js")
    if (JFC_NET_NODE AND TARGET jfcnet_webrtc_probe)
        add_test(NAME jfcnet_webrtc_probe_fixture COMMAND "${JFC_NET_NODE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/run_probe.cjs" "$<TARGET_FILE:jfcnet_webrtc_probe>")
    endif()
endif()
