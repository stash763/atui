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
    "${ATUI_SRC_DIR}/core/string.cpp"
    "${ATUI_SRC_DIR}/core/ai.cpp"
)
if(ATUI_BUILD_TESTS)
    list(APPEND ATUI_SOURCES
        "${ATUI_TESTS_DIR}/runner.cpp"
        "${ATUI_TESTS_DIR}/selftest.cpp"
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
