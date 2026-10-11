cmake_minimum_required(VERSION 3.16)

project(atui
        LANGUAGES CXX
        VERSION 0.0.0
)

if(NOT CMAKE_CXX_STANDARD)
    set(CMAKE_CXX_STANDARD 14)
endif()
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Feature gates.
#  Network (libcurl + SSE) is enabled for v1 Linux but the code is added
#  in milestone M2; the gate is wired up now so the build stays clean.
option(ATUI_HAVE_NETWORK "Use libcurl for the network transport" OFF)

option(ATUI_BUILD_TESTS "Build the self-test suite" ON)
option(ATUI_DEBUG_KEYS "Log key events to /tmp/atui_keylog.txt" OFF)

# tvision is a git submodule.
add_subdirectory(tvision)

if(ATUI_DEBUG_KEYS)
    add_compile_definitions(ATUI_DEBUG_KEYS)
endif()

set(ATUI_SRC_DIR   "${CMAKE_CURRENT_SOURCE_DIR}/src")
set(ATUI_TESTS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/tests")

set(ATUI_SOURCES
    "${ATUI_SRC_DIR}/main.cpp"
    "${ATUI_SRC_DIR}/tui/app.cpp"
    "${ATUI_SRC_DIR}/tui/theme.cpp"
    # Markdown + syntax highlighting for the transcript. Network-independent.
    "${ATUI_SRC_DIR}/tui/markdown.cpp"
    "${ATUI_SRC_DIR}/tui/syntax.cpp"
    # Reasoning text: prose/code segmentation (code gets the treatment above).
    "${ATUI_SRC_DIR}/tui/thinking.cpp"
    # The wrapping rule shared by the transcript, the approval box and thinking.
    "${ATUI_SRC_DIR}/tui/wrap.cpp"
    "${ATUI_SRC_DIR}/core/string.cpp"
    "${ATUI_SRC_DIR}/core/ai.cpp"
    # Settings persistence: INI helper + glue. Always built so provider/theme
    # settings survive across runs.
    "${ATUI_SRC_DIR}/settings/ini.cpp"
    "${ATUI_SRC_DIR}/settings/settings.cpp"
    # Tools exposed to the model. Network-independent (pure local functions);
    # the chat model renders them into the request's "tools" array.
    "${ATUI_SRC_DIR}/tools/tool_registry.cpp"
    "${ATUI_SRC_DIR}/tools/json_args.cpp"
    "${ATUI_SRC_DIR}/tools/approval.cpp"
    # Who decides whether a tool call has to be asked about: the policy (pure,
    # so both build configs can be tested) and the agent interface its answer
    # feeds into.
    "${ATUI_SRC_DIR}/tools/permissions.cpp"
    "${ATUI_SRC_DIR}/tools/judge.cpp"
    "${ATUI_SRC_DIR}/tools/time_tool.cpp"
    "${ATUI_SRC_DIR}/tools/edit_file_tool.cpp"
    "${ATUI_SRC_DIR}/tools/run_command_tool.cpp"
    # Provider settings singleton: no libcurl dependency, always built so the
    # Options > Providers dialog works regardless of the network transport.
    "${ATUI_SRC_DIR}/net/provider.cpp"
)
if(ATUI_HAVE_NETWORK)
    list(APPEND ATUI_SOURCES
        "${ATUI_SRC_DIR}/net/oai_client.cpp"
        "${ATUI_SRC_DIR}/net/oai_chat_model.cpp"
        "${ATUI_SRC_DIR}/net/json_extract.cpp"
        "${ATUI_SRC_DIR}/net/token_queue.cpp"
        "${ATUI_SRC_DIR}/net/dbg.cpp"
    )
    add_compile_definitions(ATUI_HAVE_NETWORK=1)
endif()
if(ATUI_BUILD_TESTS)
    list(APPEND ATUI_SOURCES
        "${ATUI_TESTS_DIR}/runner.cpp"
        "${ATUI_TESTS_DIR}/selftest.cpp"
        "${ATUI_TESTS_DIR}/oai_mock_test.cpp"
        "${ATUI_TESTS_DIR}/oai_live_test.cpp"
        "${ATUI_TESTS_DIR}/markdown_test.cpp"
    )
endif()

add_executable(atui ${ATUI_SOURCES})

target_include_directories(atui
    PRIVATE
        "${ATUI_SRC_DIR}"
        "${ATUI_TESTS_DIR}"
)

target_link_libraries(atui
    PRIVATE
        tvision
)

if(ATUI_HAVE_NETWORK)
    find_package(CURL REQUIRED)
    target_link_libraries(atui
        PRIVATE
            CURL::libcurl
    )
endif()

# Version string, consumed via the ATUI_VERSION macro.
add_compile_definitions(ATUI_VERSION="${PROJECT_VERSION}")

install(TARGETS atui
    RUNTIME DESTINATION bin
)

if(ATUI_BUILD_TESTS)
    add_test(NAME selftest
             COMMAND ${CMAKE_BINARY_DIR}/atui --self-test)
endif()
