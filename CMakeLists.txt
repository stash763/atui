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
#  Network is the hand-rolled HTTP/SSE transport in src/net -- sockets, our own
#  framing, no third-party HTTP library.  The build without it is the test harness:
#  atui needs a server, so that configuration does not offer a chat at all.
option(ATUI_HAVE_NETWORK "Enable the network transport (plain HTTP to an OpenAI-compatible server)" OFF)

option(ATUI_BUILD_TESTS "Build the self-test suite" ON)
option(ATUI_DEBUG_KEYS "Log key events to /tmp/atui_keylog.txt" OFF)
# Builds the no-thread configuration on a host that has threads, so the fallback every
# platform without them depends on (DOS above all) is exercised by the test suite
# rather than assumed.  See core/thread.h.
option(ATUI_NO_THREADS "Build without thread support, to test the fallback" OFF)

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
    # Transcript-visible text is ASCII; this is where it becomes so.
    "${ATUI_SRC_DIR}/tui/ascii_fold.cpp"
    "${ATUI_SRC_DIR}/core/string.cpp"
    # Text and map helpers for the standard library Watcom 1.9 ships: no
    # std::string::compare, no to_string, no std::ostringstream, no std::getline,
    # no const map::find.  See the headers for what each one replaces.
    "${ATUI_SRC_DIR}/core/strutil.cpp"
    "${ATUI_SRC_DIR}/core/lines.cpp"
    # gmtime_r()/localtime_r() are POSIX; this is the one place that knows.
    "${ATUI_SRC_DIR}/core/clock.cpp"
    "${ATUI_SRC_DIR}/core/ai.cpp"
    # Where atui is installed, and the undo journal that lives there. Network
    # independent: it is local file handling, and it has to work before any
    # request is made, since what it protects is the moment one fails.
    "${ATUI_SRC_DIR}/core/paths.cpp"
    "${ATUI_SRC_DIR}/core/childenv.cpp"
    "${ATUI_SRC_DIR}/core/journal.cpp"
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
    # Provider settings singleton: it holds strings and reads a file, so it has no
    # transport dependency and is always built -- which is what lets the endpoint
    # field be validated before anything tries to connect.
    # The endpoint splitter goes with it: the dialog has to be able to say "that
    # is not a URL" about the line being typed, which is the same question the
    # transport asks before opening a socket.
    "${ATUI_SRC_DIR}/net/provider.cpp"
    "${ATUI_SRC_DIR}/net/url.cpp"
    # The thread seam (core/thread.h): one interface, one file per platform, each
    # empty on the platforms it is not for.  Built whether or not this build has
    # threads: thread_none.cpp is what makes "no threads" a working answer instead of
    # a link error, and the tests assert the refusal as well as the real thing.
    "${ATUI_SRC_DIR}/core/thread.cpp"
    "${ATUI_SRC_DIR}/core/thread_posix.cpp"
    "${ATUI_SRC_DIR}/core/thread_os2.cpp"
    "${ATUI_SRC_DIR}/core/thread_win.cpp"
    "${ATUI_SRC_DIR}/core/thread_none.cpp"
)
if(ATUI_HAVE_NETWORK)
    list(APPEND ATUI_SOURCES
        # The transport: HTTP/1.1 as a state machine over a socket layer small
        # enough to hold the differences between the platforms.  All three socket
        # files are listed -- each is empty on the platforms it is not for -- so
        # the set of sources does not depend on the host, which is what keeps a
        # cross build from finding one file missing.
        "${ATUI_SRC_DIR}/net/http.cpp"
        "${ATUI_SRC_DIR}/net/sse.cpp"
        "${ATUI_SRC_DIR}/net/oai_body.cpp"
        "${ATUI_SRC_DIR}/net/chat_request.cpp"
        "${ATUI_SRC_DIR}/net/chat_turn.cpp"
        "${ATUI_SRC_DIR}/net/http_simple.cpp"
        "${ATUI_SRC_DIR}/net/models.cpp"
        "${ATUI_SRC_DIR}/net/sock_posix.cpp"
        "${ATUI_SRC_DIR}/net/sock_os2.cpp"
        "${ATUI_SRC_DIR}/net/sock_win.cpp"
        "${ATUI_SRC_DIR}/net/permission_judge.cpp"
        "${ATUI_SRC_DIR}/net/json_extract.cpp"
        "${ATUI_SRC_DIR}/net/dbg.cpp"
    )
    add_compile_definitions(ATUI_HAVE_NETWORK=1)
    # And nothing is linked for it: the transport is sockets and string handling,
    # the same code the Open Watcom build compiles.  An HTTP library would be one
    # more dependency to find on every platform, and the reason it was worth it --
    # TLS -- is the one thing atui does not do.
endif()
if(ATUI_BUILD_TESTS)
    list(APPEND ATUI_SOURCES
        "${ATUI_TESTS_DIR}/runner.cpp"
        "${ATUI_TESTS_DIR}/selftest.cpp"
        "${ATUI_TESTS_DIR}/http_test.cpp"
        "${ATUI_TESTS_DIR}/chat_request_test.cpp"
        "${ATUI_TESTS_DIR}/chat_turn_test.cpp"
        "${ATUI_TESTS_DIR}/judge_transport_test.cpp"
        "${ATUI_TESTS_DIR}/url_test.cpp"
        "${ATUI_TESTS_DIR}/thread_test.cpp"
        "${ATUI_TESTS_DIR}/turn_queue_test.cpp"
        "${ATUI_TESTS_DIR}/live_test.cpp"
        "${ATUI_TESTS_DIR}/markdown_test.cpp"
        "${ATUI_TESTS_DIR}/ascii_fold_test.cpp"
    )
endif()

add_executable(atui ${ATUI_SOURCES})

if(ATUI_BUILD_TESTS)
    # The suite is compiled into the same binary as the program (that is how it
    # tests static functions), so `--self-test` has to know whether it is there:
    # 200 KB of test code is a large share of a binary whose targets count bytes.
    target_compile_definitions(atui PRIVATE ATUI_SELFTEST=1)
endif()

target_include_directories(atui
    PRIVATE
        "${ATUI_SRC_DIR}"
        "${ATUI_TESTS_DIR}"
)

target_link_libraries(atui
    PRIVATE
        tvision
)

# Threads, where the platform has them: core/thread_posix.cpp needs pthreads and
# nothing else here does.  Found rather than hardcoded, because the flag differs by
# platform (it is nothing at all on macOS, where pthreads is in libSystem), and
# because the OS/2 build is not made with CMake: there the answer is `-bm`, with
# ATUI_HAVE_OS2_THREADS defined to say so, since a compiler macro cannot tell a
# single-threaded C runtime from a multithreaded one (core/thread.h).
if(NOT CMAKE_SYSTEM_NAME STREQUAL "OS2")
    find_package(Threads)
    if(Threads_FOUND)
        target_link_libraries(atui PRIVATE Threads::Threads)
    endif()
endif()

if(ATUI_HAVE_NETWORK)
    if(WIN32)
        # The other platforms have sockets in the C library; Windows keeps them in
        # ws2_32, and net/sock_win.cpp is the only thing that wants it.
        target_link_libraries(atui
            PRIVATE
                ws2_32
        )
    endif()
endif()

# Version string, consumed via the ATUI_VERSION macro, and the source root so a
# self-test can read the permission corpus that ships with the tree.
if(ATUI_NO_THREADS)
    add_compile_definitions(ATUI_NO_THREADS=1)
endif()

add_compile_definitions(ATUI_VERSION="${PROJECT_VERSION}")
add_compile_definitions(ATUI_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
# Where the binary ends up, used when argv[0] is a bare name found on PATH: it is
# the one place the undo journal can be put that the packager knows about.
add_compile_definitions(ATUI_INSTALL_DIR="${CMAKE_INSTALL_PREFIX}/bin")

install(TARGETS atui
    RUNTIME DESTINATION bin
)

if(ATUI_BUILD_TESTS)
    add_test(NAME selftest
             COMMAND ${CMAKE_BINARY_DIR}/atui --self-test)
endif()
