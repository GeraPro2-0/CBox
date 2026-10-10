/*
 * Copyright 2026 GeraPro2_0
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif

#include "toolchain/toolchain.h"
#include "linker/linker.h"
#include "platform/process.h"
#include "util/terminal.h"

#define ARENALIB_POOL_BLOCKS 1
#define ARENALIB_POOL_BLOCK_SIZE 65536
#include "arenalib.h"

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#ifdef _WIN32
#include <process.h>
#endif

static void gcc_arena_oom(void *user_data, arenalib_size_t size_requested) {
    const char *purpose = user_data != NULL ? (const char *)user_data : "temporary data";
    fprintf(stderr, "CBox: out of memory allocating %zu bytes for %s\n",
            (size_t)size_requested, purpose);
}

static bool gcc_read_version(const char *program, char *buffer, size_t capacity) {
    char command[64];
    FILE *pipe;
    size_t length;
#ifdef _WIN32
    int result;
#else
    int result;
#endif

    if (program == NULL || buffer == NULL || capacity == 0U
        || snprintf(command, sizeof(command), "%s --version 2>&1", program)
            >= (int)sizeof(command)) return false;
#ifdef _WIN32
    pipe = _popen(command, "r");
#else
    pipe = popen(command, "r");
#endif
    if (pipe == NULL || fgets(buffer, (int)capacity, pipe) == NULL) {
        if (pipe != NULL) {
#ifdef _WIN32
            (void)_pclose(pipe);
#else
            (void)pclose(pipe);
#endif
        }
        return false;
    }
#ifdef _WIN32
    result = _pclose(pipe);
#else
    result = pclose(pipe);
#endif
    if (result != 0) return false;
    length = strcspn(buffer, "\r\n");
    buffer[length] = '\0';
    return length > 0U;
}

static bool gcc_driver_version(char *buffer, size_t capacity) {
    char c_version[128];
    char cpp_version[128];
    int written;
    if (!gcc_read_version("gcc", c_version, sizeof(c_version))
        || !gcc_read_version("g++", cpp_version, sizeof(cpp_version))) {
        fprintf(stderr, "CBox: could not determine GCC/G++ version\n");
        return false;
    }
    written = snprintf(buffer, capacity, "%s\n%s", c_version, cpp_version);
    return written >= 0 && (size_t)written < capacity;
}

static const char *gcc_optimization_flag(const cbox_profile *profile) {
    if (profile == NULL || profile->optimization == NULL
        || strcmp(profile->optimization, "debug") == 0) return "-Og";
    if (strcmp(profile->optimization, "release") == 0) return "-O2";
    if (strcmp(profile->optimization, "fast") == 0) return "-O3";
    if (strcmp(profile->optimization, "small") == 0) return "-Os";
    if (strcmp(profile->optimization, "none") == 0) return "-O0";
    if (strcmp(profile->optimization, "basic") == 0) return "-O1";
    if (strcmp(profile->optimization, "tiny") == 0) return "-Oz";
    fprintf(stderr, "CBox: unsupported GCC optimization profile '%s'\n",
            profile->optimization);
    return NULL;
}

static size_t gcc_warning_flag_count(const cbox_profile *profile) {
    if (profile == NULL || profile->warnings == NULL
        || strcmp(profile->warnings, "none") == 0) return 0U;
    if (strcmp(profile->warnings, "basic") == 0) return 1U;
    if (strcmp(profile->warnings, "extra") == 0) return 2U;
    return 3U;
}

static void gcc_append_warning_flags(const cbox_profile *profile,
                                     const char **arguments, size_t *index) {
    if (gcc_warning_flag_count(profile) >= 1U) {
        arguments[(*index)++] = "-Wall";
    }
    if (gcc_warning_flag_count(profile) >= 2U) {
        arguments[(*index)++] = "-Wextra";
    }
    if (gcc_warning_flag_count(profile) >= 3U) {
        arguments[(*index)++] = "-Wpedantic";
    }
}

static const char *gcc_c_standard(const char *standard) {
    if (strcmp(standard, "c89") == 0 || strcmp(standard, "c90") == 0) return "c90";
    if (strcmp(standard, "c95") == 0) return "iso9899:199409";
    if (strcmp(standard, "c18") == 0) return "c17";
    if (strcmp(standard, "ansi c") == 0 || strcmp(standard, "ansic") == 0 || strcmp(standard, "ansi") == 0) return "c90";
    return standard;
}

static bool gcc_compile_object(const cbox_target *target, const cbox_profile *profile,
                               const char *compiler, const char *standard_name,
                               const char *optimization, const char *language,
                               bool use_modules, const char *module_mapper,
                               const char *source,
                               const char *object, const char *dependency_file) {
    const char **arguments;
    char standard[64];
    arenalib_arena_t argument_arena = {0};
    unsigned char arena_storage[256];
    size_t argument_count;
    size_t index = 0U;
    size_t include_index;
    int result;

    if (target->include_dir_count > (SIZE_MAX - 12U) / 2U
        || target->compile_arg_count > SIZE_MAX - 12U
            - target->include_dir_count * 2U
        || snprintf(standard, sizeof(standard), "-std=%s", standard_name)
            >= (int)sizeof(standard)
        || !arenalib_arena_init(&argument_arena, arena_storage,
                                sizeof(arena_storage), 0U)) {
        return false;
    }
    arenalib_arena_set_oom_callback(&argument_arena, gcc_arena_oom,
                                    "GCC compiler arguments");
    argument_count = 12U + (use_modules ? 2U : 0U)
        + (profile != NULL && profile->debug_symbols ? 1U : 0U)
        + gcc_warning_flag_count(profile)
        + (strcmp(target->kind, "shared") == 0 ? 1U : 0U)
        + target->include_dir_count * 2U + target->compile_arg_count;
    arguments = arenalib_arena_calloc(&argument_arena, argument_count + 1U,
                                      sizeof(*arguments));
    if (arguments == NULL) {
        arenalib_arena_destroy(&argument_arena);
        return false;
    }
    arguments[index++] = compiler;
    arguments[index++] = standard;
    arguments[index++] = optimization;
    if (use_modules) {
        char *mapper_option = arenalib_arena_malloc(&argument_arena,
                                                    strlen(module_mapper) + 18U);
        if (mapper_option == NULL) {
            arenalib_arena_destroy(&argument_arena);
            return false;
        }
        arguments[index++] = "-fmodules";
        (void)snprintf(mapper_option, strlen(module_mapper) + 18U,
                       "-fmodule-mapper=%s", module_mapper);
        arguments[index++] = mapper_option;
    }
#if !defined(_WIN32) && !defined(_WIN64)
    if (strcmp(target->kind, "shared") == 0) arguments[index++] = "-fPIC";
#endif
    if (profile != NULL && profile->debug_symbols) arguments[index++] = "-g";
    gcc_append_warning_flags(profile, arguments, &index);
    for (include_index = 0; include_index < target->compile_arg_count; ++include_index) {
        arguments[index++] = target->compile_args[include_index];
    }
    for (include_index = 0; include_index < target->include_dir_count; ++include_index) {
        arguments[index++] = "-I";
        arguments[index++] = target->include_dirs[include_index];
    }
    arguments[index++] = "-x";
    arguments[index++] = language;
    arguments[index++] = "-MMD";
    // Keep system headers out of the dependency list; compiler/toolchain updates
    // shouldn't make every project source look stale.
    arguments[index++] = "-MF";
    arguments[index++] = dependency_file;
    arguments[index++] = "-c";
    arguments[index++] = source;
    arguments[index++] = "-o";
    arguments[index++] = object;
    arguments[index] = NULL;
    result = cbox_run_process(arguments);
    arenalib_arena_destroy(&argument_arena);
    return result == 0;
}

static bool gcc_assemble_source(const cbox_target *target,
                               const cbox_profile *profile, bool cpp,
                               const char *source, const char *assembly,
                               const char *object) {
    const char *compiler = cpp ? "g++" : "gcc";
    const char *standard_name = cpp
        ? (target->cpp != NULL ? target->cpp : "c++11")
        : gcc_c_standard(target->c != NULL ? target->c : "c99");
    const char *optimization = gcc_optimization_flag(profile);
    const char **arguments;
    char standard[64];
    arenalib_arena_t arena = {0};
    unsigned char storage[256];
    size_t argument_count;
    size_t index = 0U;
    size_t include_index;
    bool success = false;

    (void)object;
    if (optimization == NULL
        || (cpp && !cbox_program_exists(compiler))
        || target->include_dir_count > (SIZE_MAX - 12U) / 2U
        || target->compile_arg_count > SIZE_MAX - 12U
            - target->include_dir_count * 2U
        || snprintf(standard, sizeof(standard), "-std=%s", standard_name)
            >= (int)sizeof(standard)
        || !arenalib_arena_init(&arena, storage, sizeof(storage), 0U)) {
        return false;
    }
    arenalib_arena_set_oom_callback(&arena, gcc_arena_oom,
                                    "GCC assembly arguments");
    argument_count = 7U + gcc_warning_flag_count(profile)
        + (profile != NULL && profile->debug_symbols ? 1U : 0U)
        + target->include_dir_count * 2U + target->compile_arg_count;
    arguments = arenalib_arena_calloc(&arena, argument_count + 1U,
                                      sizeof(*arguments));
    if (arguments == NULL) goto cleanup;
    arguments[index++] = compiler;
    arguments[index++] = standard;
    arguments[index++] = optimization;
    if (profile != NULL && profile->debug_symbols) arguments[index++] = "-g";
    gcc_append_warning_flags(profile, arguments, &index);
    for (include_index = 0U; include_index < target->compile_arg_count;
         ++include_index) {
        arguments[index++] = target->compile_args[include_index];
    }
    for (include_index = 0U; include_index < target->include_dir_count;
         ++include_index) {
        arguments[index++] = "-I";
        arguments[index++] = target->include_dirs[include_index];
    }
    arguments[index++] = "-S";
    arguments[index++] = source;
    arguments[index++] = "-o";
    arguments[index++] = assembly;
    arguments[index] = NULL;
    success = cbox_run_process(arguments) == 0;

cleanup:
    arenalib_arena_destroy(&arena);
    return success;
}

static bool gcc_check_source(const cbox_target *target,
                            const cbox_profile *profile, bool cpp,
                            const char *source) {
    const char *compiler = cpp ? "g++" : "gcc";
    const char *standard_name = cpp
        ? (target->cpp != NULL ? target->cpp : "c++11")
        : gcc_c_standard(target->c != NULL ? target->c : "c99");
    const char *optimization = gcc_optimization_flag(profile);
    const char **arguments;
    char standard[64];
    arenalib_arena_t arena = {0};
    unsigned char storage[256];
    size_t argument_count;
    size_t index = 0U;
    size_t include_index;
    bool success = false;

    if (optimization == NULL
        || (cpp && !cbox_program_exists(compiler))
        || target->include_dir_count > (SIZE_MAX - 8U) / 2U
        || target->compile_arg_count > SIZE_MAX - 8U
            - target->include_dir_count * 2U
        || snprintf(standard, sizeof(standard), "-std=%s", standard_name)
            >= (int)sizeof(standard)
        || !arenalib_arena_init(&arena, storage, sizeof(storage), 0U)) {
        return false;
    }
    arenalib_arena_set_oom_callback(&arena, gcc_arena_oom,
                                    "GCC syntax-check arguments");
    argument_count = 5U + gcc_warning_flag_count(profile)
        + target->include_dir_count * 2U + target->compile_arg_count;
    arguments = arenalib_arena_calloc(&arena, argument_count + 1U,
                                      sizeof(*arguments));
    if (arguments == NULL) goto cleanup;
    arguments[index++] = compiler;
    arguments[index++] = standard;
    arguments[index++] = optimization;
    gcc_append_warning_flags(profile, arguments, &index);
    for (include_index = 0U; include_index < target->compile_arg_count;
         ++include_index) {
        arguments[index++] = target->compile_args[include_index];
    }
    for (include_index = 0U; include_index < target->include_dir_count;
         ++include_index) {
        arguments[index++] = "-I";
        arguments[index++] = target->include_dirs[include_index];
    }
    arguments[index++] = "-fsyntax-only";
    arguments[index++] = source;
    arguments[index] = NULL;
    success = cbox_run_process(arguments) == 0;

cleanup:
    arenalib_arena_destroy(&arena);
    return success;
}

static bool gcc_link_objects(const cbox_target *target, const cbox_profile *profile,
                             const char *optimization, const char *output,
                             char *const object_paths[], size_t object_count,
                             bool shared, const char *import_output) {
    const char **arguments;
    const char *linker = target->cpp_source_count > 0U
        || target->cpp_module_count > 0U ? "g++" : "gcc";
    char **library_arguments = NULL;
    size_t library_argument_count = 0U;
    arenalib_arena_t argument_arena = {0};
    unsigned char arena_storage[256];
    size_t argument_count;
    size_t extra_arguments = (shared ? 1U : 0U)
        + (profile != NULL && profile->debug_symbols ? 1U : 0U);
    size_t index = 0U;
    size_t object_index;
    int result;

#if defined(_WIN32) || defined(_WIN64)
    if (shared) {
        ++extra_arguments;
    }
#endif
    if (!cbox_linker_resolve_libraries(CBOX_LINKER_GNU, target->link_libraries,
                                       target->link_library_count, &library_arguments,
                                       &library_argument_count)
        || library_argument_count > SIZE_MAX - target->link_arg_count
        || library_argument_count + target->link_arg_count > SIZE_MAX - 5U
        || extra_arguments > SIZE_MAX - 5U - library_argument_count
            - target->link_arg_count
        || object_count > SIZE_MAX - 5U - extra_arguments
            - library_argument_count - target->link_arg_count
        || !arenalib_arena_init(&argument_arena, arena_storage,
                                sizeof(arena_storage), 0U)) {
        cbox_linker_free_arguments(library_arguments, library_argument_count);
        return false;
    }
    arenalib_arena_set_oom_callback(&argument_arena, gcc_arena_oom,
                                    "GCC linker arguments");
    argument_count = 4U + object_count + library_argument_count + target->link_arg_count
        + extra_arguments;
    arguments = arenalib_arena_calloc(&argument_arena, argument_count + 1U,
                                      sizeof(*arguments));
    if (arguments == NULL) {
        arenalib_arena_destroy(&argument_arena);
        cbox_linker_free_arguments(library_arguments, library_argument_count);
        return false;
    }
    arguments[index++] = linker;
    arguments[index++] = optimization;
#if defined(_WIN32) || defined(_WIN64)
    if (shared) {
        char *import_option;
        if (import_output == NULL) {
            arenalib_arena_destroy(&argument_arena);
            cbox_linker_free_arguments(library_arguments, library_argument_count);
            return false;
        }
        import_option = arenalib_arena_malloc(&argument_arena,
                                              strlen(import_output) + 24U);
        if (import_option == NULL) {
            arenalib_arena_destroy(&argument_arena);
            cbox_linker_free_arguments(library_arguments, library_argument_count);
            return false;
        }
        (void)snprintf(import_option, strlen(import_output) + 24U,
                       "-Wl,--out-implib,%s", import_output);
        arguments[index++] = import_option;
    }
#else
    (void)import_output;
#endif
    if (shared) arguments[index++] = "-shared";
    if (profile != NULL && profile->debug_symbols) arguments[index++] = "-g";
    for (object_index = 0; object_index < object_count; ++object_index) {
        arguments[index++] = object_paths[object_index];
    }
    for (object_index = 0; object_index < library_argument_count; ++object_index) {
        arguments[index++] = library_arguments[object_index];
    }
    for (object_index = 0; object_index < target->link_arg_count; ++object_index) {
        arguments[index++] = target->link_args[object_index];
    }
    arguments[index++] = "-o";
    arguments[index++] = output;
    arguments[index] = NULL;
        (void)cbox_terminal_printf(stdout, CBOX_TERMINAL_CYAN,
                                   "linking \"%s\" with GCC\n",
                                   target->name != NULL ? target->name : output);
    result = cbox_run_process(arguments);
    arenalib_arena_destroy(&argument_arena);
    cbox_linker_free_arguments(library_arguments, library_argument_count);
    return result == 0;
}

static bool gcc_archive_objects(const cbox_target *target, const char *output,
                                char *const object_paths[], size_t object_count) {
    const char **arguments;
    arenalib_arena_t argument_arena = {0};
    unsigned char arena_storage[256];
    size_t argument_count;
    size_t index = 0U;
    size_t object_index;
    int result;

    if (!cbox_program_exists("ar") || object_count > SIZE_MAX - 3U
        || !arenalib_arena_init(&argument_arena, arena_storage,
                                sizeof(arena_storage), 0U)) {
        fprintf(stderr, "CBox: GNU static libraries require ar in PATH\n");
        return false;
    }
    arenalib_arena_set_oom_callback(&argument_arena, gcc_arena_oom,
                                    "GCC archiver arguments");
    argument_count = 3U + object_count;
    arguments = arenalib_arena_calloc(&argument_arena, argument_count + 1U,
                                      sizeof(*arguments));
    if (arguments == NULL) {
        arenalib_arena_destroy(&argument_arena);
        return false;
    }
    arguments[index++] = "ar";
    arguments[index++] = "rcs";
    arguments[index++] = output;
    for (object_index = 0U; object_index < object_count; ++object_index) {
        arguments[index++] = object_paths[object_index];
    }
    arguments[index] = NULL;
    (void)cbox_terminal_printf(stdout, CBOX_TERMINAL_CYAN,
                               "archiving %s with ar\n",
                               target->name != NULL ? target->name : output);
    result = cbox_run_process(arguments);
    arenalib_arena_destroy(&argument_arena);
    return result == 0;
}

static bool gcc_driver_compile(const cbox_target *target, const cbox_profile *profile,
                               bool cpp, bool module, const char *source,
                               const char *object, const char *module_output,
                               const char *const module_names[],
                               const char *const module_outputs[],
                               size_t module_count, const char *module_mapper,
                               const char *dependency_file) {
    const char *compiler = cpp ? "g++" : "gcc";
    const char *standard = cpp
        ? (target->cpp != NULL ? target->cpp : "c++11")
        : gcc_c_standard(target->c != NULL ? target->c : "c99");
    const char *optimization = gcc_optimization_flag(profile);

    (void)module;
    (void)module_output;
    (void)module_names;
    (void)module_outputs;
    (void)module_count;
    if (optimization == NULL || (cpp && !cbox_program_exists(compiler))) {
        return false;
    }
    return gcc_compile_object(target, profile, compiler, standard, optimization,
                              cpp ? "c++" : "c",
                              cpp && target->cpp_module_count > 0U,
                              module_mapper,
                              source, object, dependency_file);
}

static bool gcc_driver_link(const cbox_target *target, const cbox_profile *profile,
                            const char *output, char *const objects[], size_t count,
                            bool shared, const char *import_output) {
    const char *optimization = gcc_optimization_flag(profile);
    return optimization != NULL
        && gcc_link_objects(target, profile, optimization, output, objects, count,
                            shared, import_output);
}

static const cbox_toolchain_driver gcc_driver = {
    "GCC", ".o", ".gcm", CBOX_LINKER_GNU, gcc_driver_version, gcc_driver_compile,
    gcc_assemble_source, gcc_check_source, gcc_archive_objects, gcc_driver_link
};

const cbox_toolchain_driver *cbox_gcc_driver(void) {
    return &gcc_driver;
}