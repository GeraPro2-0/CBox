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

#include "build/build.h"
#include "toolchain/toolchain.h"
#include "platform/process.h"
#include "platform/filesystem.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static const char *detect_compiler(bool needs_cpp) {
#ifdef _WIN32
    static const char *candidates[] = {"cl", "clang", "gcc"};
#elif defined(__APPLE__)
    static const char *candidates[] = {"clang", "gcc"};
#elif defined(__linux__)
    static const char *candidates[] = {"gcc", "clang"};
#else
    static const char *candidates[] = {"gcc", "clang"};
#endif
    size_t index;

    for (index = 0; index < sizeof(candidates) / sizeof(candidates[0]); ++index) {
#ifdef _WIN32
        if (strcmp(candidates[index], "cl") == 0) {
            if (cbox_msvc_available()) {
                return "msvc";
            }
            continue;
        }
#endif
        if (cbox_program_exists(candidates[index])) {
            if (needs_cpp && strcmp(candidates[index], "gcc") == 0
                && !cbox_program_exists("g++")) {
                continue;
            }
            if (needs_cpp && strcmp(candidates[index], "clang") == 0
                && !cbox_program_exists("clang++")) {
                continue;
            }
            return candidates[index];
        }
    }
    return NULL;
}

const char *cbox_resolve_compiler(const char *compiler, bool needs_cpp) {
    const char *selected = compiler;

    if (selected == NULL || strcmp(selected, "auto") == 0) {
        return detect_compiler(needs_cpp);
    }
#ifdef _WIN32
    if (strcmp(selected, "cl") == 0) return "msvc";
#else
    if (strcmp(selected, "cl") == 0 || strcmp(selected, "msvc") == 0) {
        return NULL;
    }
#endif
    if (strcmp(selected, "msvc") == 0
        || strcmp(selected, "gcc") == 0
        || strcmp(selected, "clang") == 0) {
        return selected;
    }
    return NULL;
}

static bool build_target(const cbox_target *target, const cbox_profile *profile,
                         const char *compiler, bool assembly, bool syntax_check,
                         bool one_assembly, bool freestanding) {
    const char *selected;
    cbox_path_list source_paths = {0};
    cbox_path_list cpp_source_paths = {0};
    cbox_path_list cpp_module_paths = {0};
    cbox_target resolved_target;
    char **freestanding_compile_args = NULL;
    char **freestanding_link_args = NULL;
    char *entry_point_argument = NULL;
    bool uses_msvc_linker;
    bool success = false;

    if (target == NULL) {
        fprintf(stderr, "CBox: cannot build a null target\n");
        return false;
    }
    selected = cbox_resolve_compiler(
        compiler, target->cpp_source_count > 0U || target->cpp_module_count > 0U);
    if (selected == NULL) {
        if (compiler != NULL
            && (strcmp(compiler, "msvc") == 0 || strcmp(compiler, "cl") == 0)) {
            fprintf(stderr, "CBox: MSVC is only supported on Windows\n");
        } else if (compiler == NULL || strcmp(compiler, "auto") == 0) {
            fprintf(stderr, "CBox: no supported C/C++ compiler found in PATH\n");
        } else {
            fprintf(stderr, "CBox: unsupported compiler '%s' (supported: auto, msvc, clang, gcc)\n",
                    compiler);
        }
        return false;
    }
    uses_msvc_linker = strcmp(selected, "msvc") == 0;
#if defined(_WIN32) || defined(_WIN64)
    if (strcmp(selected, "clang") == 0) uses_msvc_linker = true;
#endif
    if (!cbox_expand_globs((const char *const *)target->c_sources,
                           target->c_source_count, &source_paths)) {
        return false;
    }
    // Resolve patterns before handing sources to a driver so compilation, dependency
    // planning, and cache keys all see the same concrete file list.
    if (!cbox_expand_globs((const char *const *)target->cpp_sources,
                           target->cpp_source_count, &cpp_source_paths)) {
        cbox_path_list_free(&source_paths);
        return false;
    }
    if (!cbox_expand_globs((const char *const *)target->cpp_modules,
                           target->cpp_module_count, &cpp_module_paths)) {
        cbox_path_list_free(&source_paths);
        cbox_path_list_free(&cpp_source_paths);
        return false;
    }
    resolved_target = *target;
    resolved_target.c_sources = source_paths.items;
    resolved_target.c_source_count = source_paths.count;
    resolved_target.cpp_sources = cpp_source_paths.items;
    resolved_target.cpp_source_count = cpp_source_paths.count;
    resolved_target.cpp_modules = cpp_module_paths.items;
    resolved_target.cpp_module_count = cpp_module_paths.count;
    if (freestanding) {
        // Keep manifest-owned argument arrays untouched; these temporary arrays add
        // only the flags needed for this invocation.
        char *compile_option = strcmp(selected, "msvc") == 0
            ? "/X" : "-ffreestanding";
        const char *entry_prefix = strcmp(selected, "msvc") == 0
            ? "/ENTRY:"
            : uses_msvc_linker ? "-Wl,/ENTRY:" : "-Wl,-e,";
        size_t index;

        if (target->compile_arg_count == SIZE_MAX
            || target->compile_arg_count + 1U
                > SIZE_MAX / sizeof(*freestanding_compile_args)) {
            goto cleanup;
        }
        freestanding_compile_args = malloc(
            (target->compile_arg_count + 1U)
                * sizeof(*freestanding_compile_args));
        if (freestanding_compile_args == NULL) goto cleanup;
        for (index = 0U; index < target->compile_arg_count; ++index) {
            freestanding_compile_args[index] = target->compile_args[index];
        }
        freestanding_compile_args[target->compile_arg_count] = compile_option;
        resolved_target.compile_args = freestanding_compile_args;
        resolved_target.compile_arg_count = target->compile_arg_count + 1U;

        if (strcmp(target->kind, "static") != 0) {
            size_t extra_count = target->link_arg_count;
            size_t output_index = target->link_arg_count;

            if ((strcmp(selected, "msvc") == 0
                    && extra_count == SIZE_MAX)
                || (target->entry_point != NULL
                    && extra_count
                        > SIZE_MAX - (strcmp(selected, "msvc") == 0 ? 1U : 0U) - 1U)) {
                goto cleanup;
            }
            extra_count += strcmp(selected, "msvc") == 0 ? 1U : 0U;
            extra_count += target->entry_point != NULL ? 1U : 0U;
            if (extra_count > SIZE_MAX / sizeof(*freestanding_link_args)) {
                goto cleanup;
            }
            freestanding_link_args = malloc(
                (extra_count == 0U ? 1U : extra_count)
                    * sizeof(*freestanding_link_args));
            if (freestanding_link_args == NULL) goto cleanup;
            for (index = 0U; index < target->link_arg_count; ++index) {
                freestanding_link_args[index] = target->link_args[index];
            }
            if (strcmp(selected, "msvc") == 0) {
                freestanding_link_args[output_index++] = "/NODEFAULTLIB";
            }
            if (target->entry_point != NULL) {
                size_t prefix_length = strlen(entry_prefix);
                size_t entry_length = strlen(target->entry_point);
                if (entry_length > SIZE_MAX - prefix_length - 1U) goto cleanup;
                entry_point_argument = malloc(prefix_length + entry_length + 1U);
                if (entry_point_argument == NULL) goto cleanup;
                memcpy(entry_point_argument, entry_prefix, prefix_length);
                memcpy(entry_point_argument + prefix_length, target->entry_point,
                       entry_length + 1U);
                freestanding_link_args[output_index++] = entry_point_argument;
            }
            resolved_target.link_args = freestanding_link_args;
            resolved_target.link_arg_count = output_index;
        }
    }

    if (strcmp(selected, "gcc") == 0) {
        if (!cbox_program_exists(selected)) {
            fprintf(stderr, "CBox: GCC was selected but not found in PATH\n");
            goto cleanup;
        }
        success = syntax_check
            ? cbox_toolchain_check(cbox_gcc_driver(), &resolved_target, profile)
            : assembly
                ? cbox_toolchain_assemble(cbox_gcc_driver(), &resolved_target,
                                          profile, one_assembly)
                : cbox_toolchain_build(cbox_gcc_driver(), &resolved_target, profile);
        goto cleanup;
    }
    if (strcmp(selected, "clang") == 0) {
        if (!cbox_program_exists(selected)) {
            fprintf(stderr, "CBox: Clang was selected but not found in PATH\n");
            goto cleanup;
        }
        success = syntax_check
            ? cbox_toolchain_check(cbox_clang_driver(), &resolved_target, profile)
            : assembly
                ? cbox_toolchain_assemble(cbox_clang_driver(), &resolved_target,
                                          profile, one_assembly)
                : cbox_toolchain_build(cbox_clang_driver(), &resolved_target, profile);
        goto cleanup;
    }
    if (strcmp(selected, "msvc") == 0) {
        if (!cbox_msvc_available()) {
            fprintf(stderr, "CBox: MSVC was selected but its build tools were not found\n");
            goto cleanup;
        }
        success = syntax_check
            ? cbox_toolchain_check(cbox_msvc_driver(), &resolved_target, profile)
            : assembly
                ? cbox_toolchain_assemble(cbox_msvc_driver(), &resolved_target,
                                          profile, one_assembly)
                : cbox_toolchain_build(cbox_msvc_driver(), &resolved_target, profile);
        goto cleanup;
    }
    fprintf(stderr, "CBox: unsupported compiler '%s' (supported: auto, msvc, clang, gcc)\n",
            selected);
cleanup:
    free(entry_point_argument);
    free(freestanding_link_args);
    free(freestanding_compile_args);
    cbox_path_list_free(&source_paths);
    cbox_path_list_free(&cpp_source_paths);
    cbox_path_list_free(&cpp_module_paths);
    return success;
}

bool cbox_build_target(const cbox_target *target, const cbox_profile *profile,
                       const char *compiler) {
    return build_target(target, profile, compiler, false, false, false, false);
}

bool cbox_build_target_freestanding(const cbox_target *target,
                                    const cbox_profile *profile,
                                    const char *compiler) {
    return build_target(target, profile, compiler, false, false, false, true);
}

bool cbox_build_target_assembly(const cbox_target *target,
                                const cbox_profile *profile,
                                const char *compiler, bool one_assembly) {
    return build_target(target, profile, compiler, true, false, one_assembly,
                        false);
}

bool cbox_check_target(const cbox_target *target, const cbox_profile *profile,
                       const char *compiler) {
    return build_target(target, profile, compiler, false, true, false, false);
}