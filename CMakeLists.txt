# This is for bootstrapping. I hate CMake an Make, but I need it to make the firts bin of CBox.
# After that, CBox will build itself and CMake will not be needed anymore.

cmake_minimum_required(VERSION 3.20)

project(CBox VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Threads REQUIRED)

set(CBOX_CORE_C_SOURCES
    src/config/config.c
    src/build/build.c
    src/cache/cache.c
    src/toolchain/toolchain.c
    src/toolchain/driver.c
    src/linker/linker.c
    src/toolchain/gcc.c
    src/toolchain/clang.c
    src/toolchain/msvc.c
    src/platform/process.c
    src/platform/threads.c
    src/platform/filesystem.c
    src/util/terminal.c
    third_party/tomlc17/tomlc17.c
)

set(CBOX_CORE_CXX_SOURCES
    src/model/target_graph.cpp
    src/model/module_graph.cpp
    src/util/linker_arguments.cpp
)

set(CBOX_INCLUDE_DIRECTORIES
    include
    src
    libs
    third_party
    third_party/tomlc17
)

add_executable(cbox
    src/main.c
    ${CBOX_CORE_C_SOURCES}
    ${CBOX_CORE_CXX_SOURCES}
)
target_include_directories(cbox PRIVATE ${CBOX_INCLUDE_DIRECTORIES})
target_link_libraries(cbox PRIVATE Threads::Threads)

if(MSVC)
    target_compile_options(cbox PRIVATE /W4)
else()
    target_compile_options(cbox PRIVATE
        $<$<COMPILE_LANGUAGE:C>:-Wall;-Wextra;-Wpedantic>
        $<$<COMPILE_LANGUAGE:CXX>:-Wall;-Wextra;-Wpedantic>
    )
endif()

install(TARGETS cbox RUNTIME DESTINATION bin)

option(CBOX_BUILD_TESTS "Build and register the CBox integration test runner" OFF)
option(CBOX_BUILD_BENCHMARKS "Build the cache benchmark executable" OFF)
option(CBOX_ENABLE_CLANG_TIDY "Add a clang-tidy analysis target when available" OFF)

if(CBOX_BUILD_TESTS)
    enable_testing()
    add_executable(cbox_test_runner
        tests/test_runner.c
        ${CBOX_CORE_C_SOURCES}
        ${CBOX_CORE_CXX_SOURCES}
    )
    target_include_directories(cbox_test_runner PRIVATE ${CBOX_INCLUDE_DIRECTORIES})
    add_dependencies(cbox_test_runner cbox)
    target_link_libraries(cbox_test_runner PRIVATE Threads::Threads)
    if(MSVC)
        target_compile_options(cbox_test_runner PRIVATE /W4)
    else()
        target_compile_options(cbox_test_runner PRIVATE
            $<$<COMPILE_LANGUAGE:C>:-Wall;-Wextra;-Wpedantic>
            $<$<COMPILE_LANGUAGE:CXX>:-Wall;-Wextra;-Wpedantic>
        )
    endif()
    add_test(NAME cbox_integration COMMAND cbox_test_runner)
    set_tests_properties(cbox_integration PROPERTIES
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    )
    add_test(NAME cbox_version COMMAND $<TARGET_FILE:cbox> --version)
    set_tests_properties(cbox_version PROPERTIES
        PASS_REGULAR_EXPRESSION "CBox [0-9]+\\.[0-9]+"
        WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    )
endif()

if(CBOX_BUILD_BENCHMARKS)
    add_executable(cbox_cache_benchmark
        tests/cache_benchmark.c
        src/cache/cache.c
        src/platform/threads.c
        src/platform/filesystem.c
        src/util/terminal.c
    )
    target_include_directories(cbox_cache_benchmark PRIVATE
        ${CBOX_INCLUDE_DIRECTORIES}
    )
    target_link_libraries(cbox_cache_benchmark PRIVATE Threads::Threads)
    if(MSVC)
        target_compile_options(cbox_cache_benchmark PRIVATE /W4)
    else()
        target_compile_options(cbox_cache_benchmark PRIVATE
            -Wall -Wextra -Wpedantic
        )
    endif()
endif()

if(CBOX_ENABLE_CLANG_TIDY)
    find_program(CLANG_TIDY_EXECUTABLE NAMES clang-tidy REQUIRED)
    set(CBOX_TIDY_C_SOURCES
        src/main.c
        ${CBOX_CORE_C_SOURCES}
    )
    list(REMOVE_ITEM CBOX_TIDY_C_SOURCES third_party/tomlc17/tomlc17.c)
    add_custom_target(cbox_lint
        COMMAND "${CLANG_TIDY_EXECUTABLE}"
            -checks=clang-analyzer-*,-clang-analyzer-security.insecureAPI.DeprecatedOrUnsafeBufferHandling
            ${CBOX_TIDY_C_SOURCES}
            --
            -Iinclude -Isrc -Ilibs -Ithird_party -Ithird_party/tomlc17
            -std=c17
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        VERBATIM
    )
endif()
