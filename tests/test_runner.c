#include "build/build.h"
#include "cache/cache.h"
#include "cbox.h"
#include "linker/linker.h"
#include "model/module_graph.h"
#include "platform/filesystem.h"
#include "platform/process.h"
#include "platform/threads.h"
#include "toolchain/toolchain.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <process.h>
#define cbox_test_process_id _getpid
#else
#include <unistd.h>
#define cbox_test_process_id getpid
#endif

static void test_output_path_for_linker(char *buffer, size_t capacity,
                                        cbox_linker_kind linker_kind,
                                        const char *compiler,
                                        const char *target, const char *profile,
                                        const char *target_kind, const char *name) {
    const char *selected_compiler = cbox_resolve_compiler(compiler, true);
    char *target_directory = selected_compiler != NULL
        ? cbox_build_directory_name(selected_compiler, target) : NULL;
    char *resolved_name = cbox_linker_target_output(
        linker_kind, target_kind, name, target);
    if (target_directory == NULL || resolved_name == NULL) {
        if (capacity > 0U) buffer[0] = '\0';
        free(target_directory);
        free(resolved_name);
        return;
    }
#ifdef _WIN32
    (void)snprintf(buffer, capacity, "CBoxBuild\\%s\\%s\\bin\\%s",
                   target_directory, profile, resolved_name);
#else
    (void)snprintf(buffer, capacity, "CBoxBuild/%s/%s/bin/%s",
                   target_directory, profile, resolved_name);
#endif
    free(target_directory);
    free(resolved_name);
}

static void test_output_path_for_target(char *buffer, size_t capacity,
                                        const char *compiler,
                                        const char *target, const char *profile,
                                        const char *target_kind, const char *name) {
    const char *selected_compiler = cbox_resolve_compiler(compiler, true);
    cbox_linker_kind linker_kind = selected_compiler != NULL
        && strcmp(selected_compiler, "msvc") == 0
        ? CBOX_LINKER_MSVC : CBOX_LINKER_GNU;
    test_output_path_for_linker(buffer, capacity, linker_kind, selected_compiler,
                                target, profile, target_kind, name);
}

static void test_output_path(char *buffer, size_t capacity, const char *compiler,
                             const char *target,
                             const char *target_kind, const char *name) {
    test_output_path_for_target(buffer, capacity, compiler, target, "debug",
                                target_kind, name);
}

static bool test_compiler_build_directories(void) {
    char *gcc_directory = cbox_build_directory_name("gcc", "directory-test");
    char *clang_directory = cbox_build_directory_name("clang", "directory-test");
    bool success = gcc_directory != NULL && clang_directory != NULL
        && strncmp(gcc_directory, "gcc/", 4U) == 0
        && strncmp(clang_directory, "clang/", 6U) == 0
        && strcmp(gcc_directory, clang_directory) != 0;

    free(gcc_directory);
    free(clang_directory);
#if !defined(_WIN32) && !defined(_WIN64)
    cbox_target target = {0};
    if (cbox_resolve_compiler("msvc", false) != NULL
        || cbox_build_target(&target, NULL, "msvc")) {
        success = false;
    }
#endif
    if (!success) {
        fprintf(stderr, "CBox: compiler-specific build directory test failed\n");
    }
    return success;
}

static bool test_module_graph_planning(void) {
    static const char *const sum_imports[] = {"cbox.base"};
    static const char *const external_imports[] = {"std"};
    static const char *const cycle_a_imports[] = {"cycle.b"};
    static const char *const cycle_b_imports[] = {"cycle.a"};
    static const char *const partition_imports[] = {":detail"};
    static const char *const duplicate_imports[] = {"duplicate"};
    const cbox_module_graph_node nodes[] = {
        {"cbox.sum", sum_imports, 1U},
        {"cbox.base", NULL, 0U},
        {"cbox.independent", external_imports, 1U}
    };
    const cbox_module_graph_node cycle_nodes[] = {
        {"cycle.a", cycle_a_imports, 1U},
        {"cycle.b", cycle_b_imports, 1U}
    };
    const cbox_module_graph_node partition_nodes[] = {
        {"cbox:api", partition_imports, 1U},
        {"cbox:detail", NULL, 0U}
    };
    const cbox_module_graph_node duplicate_nodes[] = {
        {"duplicate", NULL, 0U},
        {"duplicate", duplicate_imports, 1U}
    };
    cbox_module_graph_plan plan = {0};
    char error[160];
    bool success = cbox_module_graph_create_plan(nodes, 3U, &plan,
                                                  error, sizeof(error));

    success = success && plan.module_count == 3U && plan.level_count == 2U
        && plan.level_offsets[0] == 0U && plan.level_offsets[1] == 2U
        && plan.level_offsets[2] == 3U
        && plan.module_indices[0] == 1U && plan.module_indices[1] == 2U
        && plan.module_indices[2] == 0U;
    cbox_module_graph_free_plan(&plan);
    if (success) {
        success = !cbox_module_graph_create_plan(cycle_nodes, 2U, &plan,
                                                  error, sizeof(error))
            && strstr(error, "cycle") != NULL;
    }
    cbox_module_graph_free_plan(&plan);
    if (success) {
        success = cbox_module_graph_create_plan(partition_nodes, 2U, &plan,
                                                 error, sizeof(error))
            && plan.module_indices[0] == 1U && plan.module_indices[1] == 0U;
    }
    cbox_module_graph_free_plan(&plan);
    if (success) {
        success = !cbox_module_graph_create_plan(duplicate_nodes, 2U, &plan,
                                                  error, sizeof(error))
            && strstr(error, "duplicate") != NULL;
    }
    cbox_module_graph_free_plan(&plan);
    if (!success) {
        fprintf(stderr, "CBox: C++ module dependency graph test failed\n");
    }
    return success;
}

static bool test_copy_file(const char *source_path, const char *destination_path) {
    unsigned char buffer[8192];
    FILE *source = fopen(source_path, "rb");
    FILE *destination;
    size_t amount;
    bool success = source != NULL;

    if (!success) return false;
    destination = fopen(destination_path, "wb");
    if (destination == NULL) {
        fclose(source);
        return false;
    }
    while ((amount = fread(buffer, 1U, sizeof(buffer), source)) > 0U) {
        if (fwrite(buffer, 1U, amount, destination) != amount) {
            success = false;
            break;
        }
    }
    if (ferror(source)) success = false;
    if (fclose(source) != 0) success = false;
    if (fclose(destination) != 0) success = false;
    if (!success) (void)remove(destination_path);
    return success;
}

static bool test_write_text_file(const char *path, const char *contents) {
    FILE *file = fopen(path, "wb");
    size_t length = strlen(contents);
    bool success;

    if (file == NULL) return false;
    success = fwrite(contents, 1U, length, file) == length;
    if (fclose(file) != 0) success = false;
    return success;
}

static bool test_output_extension_resolution(void) {
    char *output;
    bool success = true;

#if defined(_WIN32) || defined(_WIN64)
    const char *static_extension = ".lib";
    const char *shared_extension = ".dll";
#elif defined(__APPLE__) && defined(__MACH__)
    const char *static_extension = ".a";
    const char *shared_extension = ".dylib";
#else
    const char *static_extension = ".a";
    const char *shared_extension = ".so";
#endif

    output = cbox_linker_target_output(CBOX_LINKER_GNU, "executable",
                                       "program", "fallback");
#if defined(_WIN32) || defined(_WIN64)
    success = output != NULL && strcmp(output, "program.exe") == 0;
#else
    success = output != NULL && strcmp(output, "program") == 0;
#endif
    free(output);
    if (!success) goto failure;

    output = cbox_linker_target_output(CBOX_LINKER_GNU, "executable",
                                       NULL, "fallback");
#if defined(_WIN32) || defined(_WIN64)
    success = output != NULL && strcmp(output, "fallback.exe") == 0;
#else
    success = output != NULL && strcmp(output, "fallback") == 0;
#endif
    free(output);
    if (!success) goto failure;

    output = cbox_linker_target_output(CBOX_LINKER_GNU, "executable",
                                       "program.EXE", "fallback");
    success = output != NULL;
#if defined(_WIN32) || defined(_WIN64)
    success = success && strcmp(output, "program.exe") == 0;
#else
    success = success && strcmp(output, "program") == 0;
#endif
    free(output);
    if (!success) goto failure;

    output = cbox_linker_target_output(CBOX_LINKER_GNU, "executable",
                                       "program.custom", "fallback");
    success = output != NULL && strcmp(output, "program.custom") == 0;
    free(output);
    if (!success) goto failure;

    output = cbox_linker_target_output(CBOX_LINKER_GNU, "static",
                                       "library", "fallback");
    success = output != NULL
        && strcmp(output, "library") != 0
        && strlen(output) >= strlen(static_extension)
        && strcmp(output + strlen(output) - strlen(static_extension),
                   static_extension) == 0;
    free(output);
    if (!success) goto failure;

    output = cbox_linker_target_output(CBOX_LINKER_GNU, "static",
                                       "library.a", "fallback");
    success = output != NULL
        && strlen(output) >= strlen(static_extension)
        && strcmp(output + strlen(output) - strlen(static_extension),
                   static_extension) == 0;
    free(output);
    if (!success) goto failure;

    output = cbox_linker_target_output(CBOX_LINKER_GNU, "shared",
                                       "library.dll", "fallback");
    success = output != NULL
        && strlen(output) >= strlen(shared_extension)
        && strcmp(output + strlen(output) - strlen(shared_extension),
                   shared_extension) == 0;
    free(output);
    if (!success) goto failure;

    output = cbox_linker_target_output(CBOX_LINKER_GNU, "shared",
                                       "library.custom", "fallback");
    success = output != NULL && strcmp(output, "library.custom") == 0;
    free(output);
    if (!success) goto failure;
    return true;

failure:
    fprintf(stderr, "CBox: output extension resolution test failed\n");
    return false;
}

static bool test_linker_library_arguments(void) {
    char *gnu_libraries[] = {
        "m", "pthread", "-lcustom", "vendor/library.a", "custom.dll.a"
    };
    char *msvc_libraries[] = {"m", "kernel32", "vendor\\custom.lib"};
    char *invalid_libraries[] = {"valid", ""};
    char **arguments = NULL;
    size_t argument_count = 0U;
    bool success;

    success = cbox_linker_resolve_libraries(
        CBOX_LINKER_GNU, gnu_libraries,
        sizeof(gnu_libraries) / sizeof(gnu_libraries[0]),
        &arguments, &argument_count);
    if (!success || argument_count != 5U
        || strcmp(arguments[0], "-lm") != 0
        || strcmp(arguments[1], "-lpthread") != 0
        || strcmp(arguments[2], "-lcustom") != 0
        || strcmp(arguments[3], "vendor/library.a") != 0
        || strcmp(arguments[4], "custom.dll.a") != 0) {
        cbox_linker_free_arguments(arguments, argument_count);
        goto failure;
    }
    cbox_linker_free_arguments(arguments, argument_count);
    arguments = NULL;
    argument_count = 0U;

    success = cbox_linker_resolve_libraries(
        CBOX_LINKER_MSVC, msvc_libraries,
        sizeof(msvc_libraries) / sizeof(msvc_libraries[0]),
        &arguments, &argument_count);
    if (!success || argument_count != 2U
        || strcmp(arguments[0], "kernel32.lib") != 0
        || strcmp(arguments[1], "vendor\\custom.lib") != 0) {
        cbox_linker_free_arguments(arguments, argument_count);
        goto failure;
    }
    cbox_linker_free_arguments(arguments, argument_count);
    if (cbox_linker_resolve_libraries(
            CBOX_LINKER_GNU, invalid_libraries,
            sizeof(invalid_libraries) / sizeof(invalid_libraries[0]),
            &arguments, &argument_count)
        || arguments != NULL || argument_count != 0U) {
        cbox_linker_free_arguments(arguments, argument_count);
        goto failure;
    }
    return true;

failure:
    fprintf(stderr, "CBox: linker library argument resolution test failed\n");
    return false;
}

typedef struct {
    unsigned int *visited;
    size_t count;
    size_t failed_index;
} parallel_test_context;

static bool parallel_test_job(size_t index, void *context) {
    parallel_test_context *test = context;
    if (index >= test->count) return false;
    test->visited[index] = 1U;
    return index != test->failed_index;
}

static bool test_parallel_worker_dispatch(void) {
    unsigned int visited[64] = {0U};
    parallel_test_context context;
    size_t index;
    bool success;

    context.visited = visited;
    context.count = sizeof(visited) / sizeof(visited[0]);
    context.failed_index = context.count;
    success = cbox_parallel_for(context.count, context.count,
                                parallel_test_job, &context);
    for (index = 0U; index < context.count; ++index) {
        if (visited[index] != 1U) success = false;
        visited[index] = 0U;
    }
    context.failed_index = context.count / 2U;
    if (cbox_parallel_for(context.count, context.count,
                          parallel_test_job, &context)) {
        success = false;
    }
    for (index = 0U; index < context.count; ++index) {
        if (visited[index] != 1U) success = false;
        visited[index] = 0U;
    }
    context.failed_index = context.count;
    if (!cbox_parallel_for(context.count, 1U, parallel_test_job, &context)) {
        success = false;
    }
    for (index = 0U; index < context.count; ++index) {
        if (visited[index] != 1U) success = false;
    }
    if (!success) {
        fprintf(stderr, "CBox: persistent worker dispatch test failed\n");
    }
    return success;
}

static bool test_parallel_target_dependencies(void) {
    cbox_project project = {0};
    cbox_project cycle_project = {0};
    cbox_linker_kind linker_kind = CBOX_LINKER_GNU;
    char *first_output = NULL;
    char *second_output = NULL;
    char *app_output = NULL;
    char first_path[384];
    char second_path[384];
    char app_path[384];
    char executable[448];
    char changed_source_path[128];
    char *original_library_source = NULL;
    char *changed_library_source = NULL;
    const char *arguments[2];
    FILE *file;
    bool source_written;
    bool success = false;

    first_path[0] = '\0';
    second_path[0] = '\0';
    app_path[0] = '\0';
    changed_source_path[0] = '\0';
#if defined(_WIN32) || defined(_WIN64)
    if (cbox_msvc_available()) linker_kind = CBOX_LINKER_MSVC;
#endif
    first_output = cbox_linker_static_library_output(linker_kind, "dag-first");
    second_output = cbox_linker_static_library_output(linker_kind, "dag-second");
    app_output = cbox_linker_target_output(linker_kind, "executable",
                                           NULL, "dag-app");
    if (first_output == NULL || second_output == NULL || app_output == NULL) {
        goto cleanup;
    }
    test_output_path_for_linker(first_path, sizeof(first_path), linker_kind,
                                linker_kind == CBOX_LINKER_MSVC ? "msvc" : "gcc",
                                "dag-first", "default", "static", first_output);
    test_output_path_for_linker(second_path, sizeof(second_path), linker_kind,
                                linker_kind == CBOX_LINKER_MSVC ? "msvc" : "gcc",
                                "dag-second", "default", "static", second_output);
    test_output_path_for_linker(app_path, sizeof(app_path), linker_kind,
                                linker_kind == CBOX_LINKER_MSVC ? "msvc" : "gcc",
                                "dag-app", "default", "executable", app_output);
    (void)remove(first_path);
    (void)remove(second_path);
    (void)remove(app_path);

    cbox_project_init(&project);
    if (!cbox_load_project("tests/target_dependencies.toml", &project)
        || project.target_count != 3U
        || project.targets[2].dependency_count != 1U
        || strcmp(project.targets[2].depends_on[0], "dag-second") != 0
        || project.targets[2].link_target_count != 1U
        || strcmp(project.targets[2].link_targets[0], "dag-first") != 0
        || !cbox_run_task(&project, "build")) {
        goto cleanup;
    }
    file = fopen(first_path, "rb");
    if (file == NULL) goto cleanup;
    fclose(file);
    file = fopen(second_path, "rb");
    if (file == NULL) goto cleanup;
    fclose(file);
    file = fopen(app_path, "rb");
    if (file == NULL) goto cleanup;
    fclose(file);
#ifdef _WIN32
    (void)snprintf(executable, sizeof(executable), ".\\%s", app_path);
#else
    (void)snprintf(executable, sizeof(executable), "./%s", app_path);
#endif
    arguments[0] = executable;
    arguments[1] = NULL;
    success = cbox_run_process(arguments) == 0;
    if (!success) goto cleanup;

    (void)snprintf(changed_source_path, sizeof(changed_source_path),
                   "tests/.target-link-value-%lu.c",
                   (unsigned long)cbox_test_process_id());
    file = fopen(changed_source_path, "wb");
    if (file == NULL) goto cleanup;
    source_written = fputs("int cbox_target_link_value(void) { return 43; }\n",
                           file) >= 0;
    if (fclose(file) != 0) source_written = false;
    if (!source_written) {
        (void)remove(changed_source_path);
        goto cleanup;
    }
    original_library_source = project.targets[0].c_sources[0];
    changed_library_source = malloc(strlen(changed_source_path) + 1U);
    if (changed_library_source == NULL) goto cleanup;
    memcpy(changed_library_source, changed_source_path,
           strlen(changed_source_path) + 1U);
    project.targets[0].c_sources[0] = changed_library_source;
    if (!cbox_run_task(&project, "build")
        || cbox_run_process(arguments) != 1) {
        goto cleanup;
    }
    project.targets[0].c_sources[0] = original_library_source;
    free(changed_library_source);
    changed_library_source = NULL;
    original_library_source = NULL;
    if (!cbox_run_task(&project, "build")
        || cbox_run_process(arguments) != 0) {
        goto cleanup;
    }
    success = true;

    cbox_project_init(&cycle_project);
    if (!cbox_load_project("tests/target_dependency_cycle.toml",
                           &cycle_project)
        || cbox_run_task(&cycle_project, "build")) {
        success = false;
    }
    cbox_project_free(&cycle_project);

cleanup:
    if (changed_library_source != NULL) {
        project.targets[0].c_sources[0] = original_library_source;
        free(changed_library_source);
    }
    cbox_project_free(&project);
    cbox_project_free(&cycle_project);
    if (first_path[0] != '\0') (void)remove(first_path);
    if (second_path[0] != '\0') (void)remove(second_path);
    if (app_path[0] != '\0') (void)remove(app_path);
    if (changed_source_path[0] != '\0') (void)remove(changed_source_path);
    free(first_output);
    free(second_output);
    free(app_output);
    if (!success) {
        fprintf(stderr, "CBox: target dependency scheduling test failed\n");
    }
    return success;
}

static bool write_parallel_fixture_source(const char *path, const char *body) {
    FILE *file = fopen(path, "wb");
    size_t index;
    bool success = file != NULL;
    if (!success) return false;
    for (index = 0U; index < 600U; ++index) {
        if (fputs("/* padding for CBox parallel compilation test */\n", file) == EOF) {
            success = false;
            break;
        }
    }
    if (success && fputs(body, file) == EOF) success = false;
    if (fclose(file) != 0) success = false;
    if (!success) (void)remove(path);
    return success;
}

static bool test_parallel_compilation_threshold(void) {
    char *sources[] = {
        "tests/parallel_fixture_a.c",
        "tests/parallel_fixture_b.c",
        "tests/parallel_fixture_main.c"
    };
    char output_path[384];
    char executable[448];
    char target_directory[256];
    char artifact_path[512];
    char *reordered_sources[] = {
        "tests/parallel_fixture_main.c",
        "tests/parallel_fixture_a.c",
        "tests/parallel_fixture_b.c"
    };
    const char *arguments[2];
    cbox_target target = {0};
    cbox_profile profile = {0};
    char **object_paths = NULL;
    bool success = false;

    target_directory[0] = '\0';
    if (!write_parallel_fixture_source(sources[0], "int parallel_a(void) { return 20; }\n")
        || !write_parallel_fixture_source(sources[1], "int parallel_b(void) { return 22; }\n")
        || !write_parallel_fixture_source(
            sources[2],
            "int parallel_a(void);\n"
            "int parallel_b(void);\n"
            "int main(void) { return parallel_a() + parallel_b() == 42 ? 0 : 1; }\n")) {
        goto cleanup;
    }
    target.name = "parallel-threshold-test";
    target.kind = "executable";
    target.output = "parallel-threshold-test";
    target.c_sources = sources;
    target.c_source_count = sizeof(sources) / sizeof(sources[0]);
    target.c = "c99";
    profile.name = "parallel-test";
    profile.optimization = "debug";
    {
        char *directory = cbox_build_directory_name("gcc", target.name);
        char **reordered_paths = NULL;
        size_t index;
        if (directory == NULL) goto cleanup;
        (void)snprintf(target_directory, sizeof(target_directory), "%s", directory);
        free(directory);
        (void)snprintf(artifact_path, sizeof(artifact_path),
                       "CBoxBuild/%s/%s/CBoxCache.bin",
                       target_directory, profile.name);
        (void)remove(artifact_path);
        if (!cbox_prepare_object_paths(target_directory,
                                       (const char *const *)sources,
                                       target.c_source_count, profile.name,
                                       ".o", &object_paths)
            || !cbox_prepare_object_paths(target_directory,
                                          (const char *const *)reordered_sources,
                                          target.c_source_count, profile.name,
                                          ".o", &reordered_paths)) {
            cbox_discard_object_paths(object_paths, target.c_source_count);
            cbox_discard_object_paths(reordered_paths, target.c_source_count);
            object_paths = NULL;
            goto cleanup;
        }
        for (index = 0U; index < target.c_source_count; ++index) {
            size_t reordered_index;
            bool matched = false;
            for (reordered_index = 0U; reordered_index < target.c_source_count;
                 ++reordered_index) {
                if (strcmp(sources[index], reordered_sources[reordered_index]) == 0) {
                    matched = strcmp(object_paths[index],
                                     reordered_paths[reordered_index]) == 0;
                    break;
                }
            }
            if (!matched) {
                cbox_discard_object_paths(object_paths, target.c_source_count);
                cbox_discard_object_paths(reordered_paths, target.c_source_count);
                object_paths = NULL;
                goto cleanup;
            }
        }
        cbox_discard_object_paths(reordered_paths, target.c_source_count);
        for (index = 0U; index < target.c_source_count; ++index) {
            int required = snprintf(artifact_path, sizeof(artifact_path),
                                   "%s.deps", object_paths[index]);
            (void)remove(object_paths[index]);
            if (required >= 0 && required < (int)sizeof(artifact_path)) {
                (void)remove(artifact_path);
            }
        }
        cbox_discard_object_paths(object_paths, target.c_source_count);
        object_paths = NULL;
    }
    if (!cbox_build_target(&target, &profile, "gcc")) goto cleanup;
    test_output_path_for_target(output_path, sizeof(output_path),
                                "gcc",
                                target.name, profile.name,
                                target.kind, target.output);
#ifdef _WIN32
    (void)snprintf(executable, sizeof(executable), ".\\%s", output_path);
#else
    (void)snprintf(executable, sizeof(executable), "./%s", output_path);
#endif
    arguments[0] = executable;
    arguments[1] = NULL;
    success = cbox_run_process(arguments) == 0;

cleanup:
    cbox_discard_object_paths(object_paths, target.c_source_count);
    object_paths = NULL;
    {
        size_t index;
        for (index = 0U; index < sizeof(sources) / sizeof(sources[0]); ++index) {
            (void)remove(sources[index]);
        }
    }
    test_output_path_for_target(output_path, sizeof(output_path),
                                "gcc",
                                "parallel-threshold-test", "parallel-test",
                                "executable", "parallel-threshold-test");
    (void)remove(output_path);
    if (target_directory[0] != '\0') {
        size_t index;
        if (cbox_prepare_object_paths(target_directory,
                                      (const char *const *)sources,
                                      sizeof(sources) / sizeof(sources[0]),
                                      "parallel-test", ".o", &object_paths)) {
            for (index = 0U; index < sizeof(sources) / sizeof(sources[0]); ++index) {
                int required = snprintf(artifact_path, sizeof(artifact_path),
                                       "%s.deps", object_paths[index]);
                (void)remove(object_paths[index]);
                if (required >= 0 && required < (int)sizeof(artifact_path)) {
                    (void)remove(artifact_path);
                }
            }
            cbox_discard_object_paths(object_paths,
                                      sizeof(sources) / sizeof(sources[0]));
            object_paths = NULL;
        }
        (void)snprintf(artifact_path, sizeof(artifact_path),
                       "CBoxBuild/%s/parallel-test/CBoxCache.bin",
                       target_directory);
        (void)remove(artifact_path);
    }
    if (!success) {
        fprintf(stderr, "CBox: parallel compilation threshold test failed\n");
    }
    return success;
}

static bool test_delete_globs(void) {
    char root[96];
    char nested[128];
    char file_path[160];
    char root_marker_path[160];
    char nested_pattern[104];
    char external_directory[128];
    char external_marker[160];
    char external_link[160];
    char external_link_target[192];
    const char *patterns[2];
    FILE *file;
    bool success = false;

    (void)snprintf(root, sizeof(root), "tests/.cbox-delete-test-%lu",
                   (unsigned long)cbox_test_process_id());
    (void)snprintf(nested, sizeof(nested), "%s/nested", root);
    (void)snprintf(file_path, sizeof(file_path), "%s/file.tmp", nested);
    (void)snprintf(root_marker_path, sizeof(root_marker_path), "%s/marker.tmp", root);
    (void)snprintf(nested_pattern, sizeof(nested_pattern), "%s/*", root);
    (void)snprintf(external_directory, sizeof(external_directory),
                   "../.cbox-delete-outside-%lu",
                   (unsigned long)cbox_test_process_id());
    (void)snprintf(external_marker, sizeof(external_marker), "%s/marker.tmp",
                   external_directory);
    (void)snprintf(external_link, sizeof(external_link), "%s/external", root);
    (void)snprintf(external_link_target, sizeof(external_link_target),
                   "../../../.cbox-delete-outside-%lu/marker.tmp",
                   (unsigned long)cbox_test_process_id());
    if (!cbox_create_directory(root) || !cbox_create_directory(nested)) {
        goto cleanup;
    }
    file = fopen(file_path, "wb");
    if (file == NULL) {
        goto cleanup;
    }
    if (fclose(file) != 0) {
        goto cleanup;
    }
    patterns[0] = nested_pattern;
    if (!cbox_delete_globs(patterns, 1U)) {
        goto cleanup;
    }
#ifndef _WIN32
    if (!cbox_create_directory(external_directory)
        || !test_write_text_file(external_marker, "outside project\n")
        || symlink(external_link_target, external_link) != 0) {
        goto cleanup;
    }
    patterns[0] = external_link;
    if (cbox_delete_globs(patterns, 1U)) goto cleanup;
    file = fopen(external_marker, "rb");
    if (file == NULL) goto cleanup;
    fclose(file);
#endif
    file = fopen(file_path, "rb");
    if (file != NULL) {
        fclose(file);
        goto cleanup;
    }
    file = fopen(root_marker_path, "wb");
    if (file == NULL) goto cleanup;
    if (fclose(file) != 0) goto cleanup;
    patterns[0] = root_marker_path;
    patterns[1] = "*/*";
    if (cbox_delete_globs(patterns, 2U)) {
        goto cleanup;
    }
    file = fopen(root_marker_path, "rb");
    if (file == NULL) goto cleanup;
    fclose(file);
    patterns[0] = "*";
    if (cbox_delete_globs(patterns, 1U)) goto cleanup;
    patterns[0] = "CBox.toml";
    if (cbox_delete_globs(patterns, 1U)) goto cleanup;
    patterns[0] = ".git/*";
    if (cbox_delete_globs(patterns, 1U)) goto cleanup;
    patterns[0] = "tests/../CBox.toml";
    if (cbox_delete_globs(patterns, 1U)) goto cleanup;
    patterns[0] = "tests/.cbox-delete-pattern-with-no-matches-*";
    if (!cbox_delete_globs(patterns, 1U)) {
        goto cleanup;
    }
    file = fopen("CBox.toml", "rb");
    if (file == NULL) {
        goto cleanup;
    }
    fclose(file);
    success = true;

cleanup:
#ifndef _WIN32
    (void)remove(external_link);
    (void)remove(external_marker);
    (void)rmdir(external_directory);
#endif
    patterns[0] = root;
    (void)cbox_delete_globs(patterns, 1U);
    if (!success) {
        fprintf(stderr, "CBox: delete-pattern integration test failed\n");
    }
    return success;
}

static bool test_source_glob_deduplication(void) {
    char source[112];
    char wildcard_pattern[112];
    const char *patterns[2];
    cbox_path_list paths = {0};
    bool success;

    (void)snprintf(source, sizeof(source),
                   "CBoxBuild/source-glob-dedup-%lu.c",
                   (unsigned long)cbox_test_process_id());
    (void)snprintf(wildcard_pattern, sizeof(wildcard_pattern),
                   "CBoxBuild/source-glob-dedup-*.c");
    patterns[0] = wildcard_pattern;
    patterns[1] = source;
    success = test_write_text_file(source, "int fixture(void) { return 0; }\n")
        && cbox_expand_globs(patterns, 2U, &paths)
        && paths.count == 1U;

    cbox_path_list_free(&paths);
    (void)remove(source);
    if (!success) {
        fprintf(stderr, "CBox: source glob duplicate elimination test failed\n");
    }
    return success;
}

static bool test_delete_task_from_toml(void) {
    char root[96];
    char nested[128];
    char marker[160];
    cbox_project project = {0};
    FILE *file;
    bool success = false;

    (void)snprintf(root, sizeof(root), "tests/.cbox-delete-task-%lu",
                   (unsigned long)cbox_test_process_id());
    (void)snprintf(nested, sizeof(nested), "%s/nested", root);
    (void)snprintf(marker, sizeof(marker), "%s/marker.tmp", nested);
    if (!cbox_create_directory(root) || !cbox_create_directory(nested)) {
        goto cleanup;
    }
    file = fopen(marker, "wb");
    if (file == NULL) {
        goto cleanup;
    }
    if (fclose(file) != 0) {
        goto cleanup;
    }
    cbox_project_init(&project);
    if (cbox_load_project("tests/delete_task.toml", &project)
        && cbox_run_task(&project, "clean")) {
        file = fopen(marker, "rb");
        if (file == NULL) {
            success = true;
        } else {
            fclose(file);
        }
    }
    cbox_project_free(&project);
    cbox_project_init(&project);
    if (cbox_load_project("tests/delete_task_invalid.toml", &project)) {
        cbox_project_free(&project);
        goto cleanup;
    }
    cbox_project_free(&project);
    success = true;

cleanup:
    {
        const char *patterns[] = {root};
        (void)cbox_delete_globs(patterns, 1U);
    }
    if (!success) {
        fprintf(stderr, "CBox: TOML delete-task integration test failed\n");
    }
    return success;
}

typedef struct {
    const char *path;
    cbox_cache_change_reason reason;
    size_t count;
} cache_change_observation;

static void observe_cache_change(const char *path,
                                 cbox_cache_change_reason reason,
                                 void *context) {
    cache_change_observation *observation = context;
    observation->path = path;
    observation->reason = reason;
    ++observation->count;
}

static bool test_cache_format_and_hashes(void) {
    static const unsigned char empty_sha256[32] = {
        0xe3, 0xb0, 0xc4, 0x42, 0x98, 0xfc, 0x1c, 0x14,
        0x9a, 0xfb, 0xf4, 0xc8, 0x99, 0x6f, 0xb9, 0x24,
        0x27, 0xae, 0x41, 0xe4, 0x64, 0x9b, 0x93, 0x4c,
        0xa4, 0x95, 0x99, 0x1b, 0x78, 0x52, 0xb8, 0x55
    };
    const char *dependencies[] = {"CBoxBuild/cache-test-input.txt"};
    const char *output_path = "CBoxBuild/cache-test-object.bin";
    const char *stale_output_path = "CBoxBuild/stale-cache-test-object.bin";
    const char *retained_keys[] = {"CBoxBuild/cache-test-object.bin"};
    const char *cache_path = "CBoxBuild/cache-test.bin";
    static const char input[] = "cache dependency";
    static const char changed[] = "changed dependency";
    static const char object_contents[] = "cached object";
    unsigned char fingerprint[32];
    unsigned char digest[32];
    unsigned char header[12];
    cbox_cache *cache = NULL;
    FILE *file = NULL;
    bool success = false;
    bool io_success;
    long unpruned_size;
    long pruned_size;
    cache_change_observation observation = {0};

    cbox_sha256_buffer("", 0U, digest);
    if (memcmp(digest, empty_sha256, sizeof(digest)) != 0
        || !cbox_create_directories("CBoxBuild")) {
        goto cleanup;
    }
    file = fopen(dependencies[0], "wb");
    if (file == NULL) goto cleanup;
    io_success = fwrite(input, 1U, sizeof(input) - 1U, file) == sizeof(input) - 1U;
    if (fclose(file) != 0) io_success = false;
    file = NULL;
    if (!io_success) goto cleanup;
    file = fopen(output_path, "wb");
    if (file == NULL) goto cleanup;
    io_success = fwrite(object_contents, 1U, sizeof(object_contents) - 1U, file)
        == sizeof(object_contents) - 1U;
    if (fclose(file) != 0) io_success = false;
    file = NULL;
    if (!io_success) goto cleanup;
    file = fopen(stale_output_path, "wb");
    if (file == NULL) goto cleanup;
    io_success = fwrite(object_contents, 1U, sizeof(object_contents) - 1U, file)
        == sizeof(object_contents) - 1U;
    if (fclose(file) != 0) io_success = false;
    file = NULL;
    if (!io_success) goto cleanup;
    cbox_sha256_buffer("compile settings", sizeof("compile settings") - 1U,
                       fingerprint);
    cache = cbox_cache_open(cache_path);
    if (cache == NULL
        || !cbox_cache_store(cache, output_path, fingerprint,
                             dependencies, 1U)
        || !cbox_cache_save(cache, cache_path)) {
        goto cleanup;
    }
    cbox_cache_free(cache);
    cache = NULL;
    file = fopen(cache_path, "rb");
    if (file == NULL) goto cleanup;
    io_success = fread(header, 1U, sizeof(header), file) == sizeof(header);
    if (fclose(file) != 0) io_success = false;
    file = NULL;
    if (!io_success) goto cleanup;
    if (memcmp(header, "CBOX", 4U) != 0 || header[4] != 2U
        || header[5] != 0U || header[6] != 0U || header[7] != 0U
        || header[8] != 1U || header[9] != 0U
        || header[10] != 0U || header[11] != 0U) {
        goto cleanup;
    }
    cache = cbox_cache_open(cache_path);
    if (cache == NULL
        || !cbox_cache_is_current(cache, output_path, fingerprint)) {
        goto cleanup;
    }
    file = fopen(cache_path, "ab");
    if (file == NULL || fputc(0, file) == EOF) {
        if (file != NULL) fclose(file);
        file = NULL;
        goto cleanup;
    }
    if (fclose(file) != 0) {
        file = NULL;
        goto cleanup;
    }
    file = NULL;
    cbox_cache_free(cache);
    cache = cbox_cache_open(cache_path);
    if (cache == NULL || cbox_cache_is_current(cache, output_path, fingerprint)
        || !cbox_cache_store(cache, output_path, fingerprint, dependencies, 1U)
        || !cbox_cache_save(cache, cache_path)) {
        goto cleanup;
    }
    {
        FILE *size_file;
        if (!cbox_cache_store(cache, stale_output_path, fingerprint, dependencies, 1U)
            || !cbox_cache_save(cache, cache_path)) {
            goto cleanup;
        }
        size_file = fopen(cache_path, "rb");
        if (size_file == NULL || fseek(size_file, 0L, SEEK_END) != 0
            || (unpruned_size = ftell(size_file)) < 0L) {
            if (size_file != NULL) fclose(size_file);
            goto cleanup;
        }
        if (fclose(size_file) != 0
            || !cbox_cache_prune(cache, retained_keys, 1U)
            || !cbox_cache_save(cache, cache_path)) {
            goto cleanup;
        }
        size_file = fopen(cache_path, "rb");
        if (size_file == NULL || fseek(size_file, 0L, SEEK_END) != 0
            || (pruned_size = ftell(size_file)) < 0L) {
            if (size_file != NULL) fclose(size_file);
            goto cleanup;
        }
        if (fclose(size_file) != 0 || pruned_size >= unpruned_size
            || cbox_cache_is_current(cache, stale_output_path, fingerprint)
            || !cbox_cache_is_current(cache, output_path, fingerprint)) {
            goto cleanup;
        }
    }
    cbox_cache_free(cache);
    cache = NULL;
    cache = cbox_cache_open(cache_path);
    if (cache == NULL
        || !cbox_cache_is_current(cache, output_path, fingerprint)
        || cbox_cache_is_current(cache, stale_output_path, fingerprint)) {
        goto cleanup;
    }
    cbox_cache_free(cache);
    cache = NULL;
    file = fopen(output_path, "wb");
    if (file == NULL) goto cleanup;
    io_success = fwrite("damaged object", 1U, sizeof("damaged object") - 1U, file)
        == sizeof("damaged object") - 1U;
    if (fclose(file) != 0) io_success = false;
    file = NULL;
    if (!io_success) goto cleanup;
    cache = cbox_cache_open(cache_path);
    if (cache == NULL
        || cbox_cache_check(cache, output_path, fingerprint,
                            observe_cache_change, &observation)
        || observation.count == 0U || observation.path == NULL
        || strcmp(observation.path, output_path) != 0
        || observation.reason != CBOX_CACHE_CHANGE_OUTPUT) {
        goto cleanup;
    }
    cbox_cache_free(cache);
    cache = NULL;
    file = fopen(output_path, "wb");
    if (file == NULL) goto cleanup;
    io_success = fwrite(object_contents, 1U, sizeof(object_contents) - 1U, file)
        == sizeof(object_contents) - 1U;
    if (fclose(file) != 0) io_success = false;
    file = NULL;
    if (!io_success) goto cleanup;
    file = fopen(dependencies[0], "wb");
    if (file == NULL) goto cleanup;
    io_success = fwrite(changed, 1U, sizeof(changed) - 1U, file)
        == sizeof(changed) - 1U;
    if (fclose(file) != 0) io_success = false;
    file = NULL;
    if (!io_success) goto cleanup;
    cache = cbox_cache_open(cache_path);
    memset(&observation, 0, sizeof(observation));
    success = cache != NULL
        && !cbox_cache_check(cache, output_path, fingerprint,
                             observe_cache_change, &observation)
        && observation.count != 0U
        && strcmp(observation.path, dependencies[0]) == 0
        && observation.reason == CBOX_CACHE_CHANGE_DEPENDENCY;
    cbox_cache_free(cache);
    cache = NULL;
    if (success) {
        (void)remove(dependencies[0]);
        cache = cbox_cache_open(cache_path);
        success = cache != NULL
            && !cbox_cache_is_current(cache, output_path, fingerprint);
    }

cleanup:
    if (file != NULL) fclose(file);
    cbox_cache_free(cache);
    (void)remove(dependencies[0]);
    (void)remove(output_path);
    (void)remove(stale_output_path);
    (void)remove(cache_path);
    (void)remove("CBoxBuild/cache-test.bin.tmp");
    if (!success) fprintf(stderr, "CBox: cache format or SHA-256 test failed\n");
    return success;
}

typedef struct {
    size_t dependency_changes[2];
    size_t settings_changes[2];
} cache_batch_observation;

static void observe_cache_batch_change(size_t request_index, const char *path,
                                       cbox_cache_change_reason reason,
                                       void *context) {
    cache_batch_observation *observation = context;
    (void)path;
    if (request_index < 2U && reason == CBOX_CACHE_CHANGE_DEPENDENCY) {
        ++observation->dependency_changes[request_index];
    }
    if (request_index < 2U && reason == CBOX_CACHE_CHANGE_SETTINGS) {
        ++observation->settings_changes[request_index];
    }
}

static bool test_cache_batch_hashing(void) {
    static const char *const shared_path = "CBoxBuild/cache-batch-shared.h";
    static const char *const output_paths[] = {
        "CBoxBuild/cache-batch-one.o",
        "CBoxBuild/cache-batch-two.o"
    };
    static const char *const original_content = "shared header v1\n";
    static const char *const changed_content = "shared header v2\n";
    static const char *const object_content = "object\n";
    unsigned char fingerprint[32];
    unsigned char changed_fingerprint[32];
    bool results[2] = {false, false};
    const char *dependencies[] = {shared_path};
    cbox_cache_check_request requests[2];
    cache_batch_observation observation = {{0U, 0U}, {0U, 0U}};
    cbox_cache *cache = NULL;
    size_t index;
    bool success = false;

    (void)remove("CBoxBuild/cache-batch-test.bin");
    if (!cbox_create_directories("CBoxBuild")
        || !test_write_text_file(shared_path, original_content)) goto cleanup;
    cbox_sha256_buffer("cache batch settings", sizeof("cache batch settings") - 1U,
                       fingerprint);
    cache = cbox_cache_open("CBoxBuild/cache-batch-test.bin");
    if (cache == NULL) goto cleanup;
    for (index = 0U; index < 2U; ++index) {
        if (!test_write_text_file(output_paths[index], object_content)
            || !cbox_cache_store(cache, output_paths[index], fingerprint,
                              dependencies, 1U)) {
            goto cleanup;
        }
        requests[index].output_path = output_paths[index];
        requests[index].fingerprint = fingerprint;
    }
    if (!cbox_cache_check_batch(cache, requests, 2U, results, NULL, NULL)
        || !results[0] || !results[1]) {
        goto cleanup;
    }

    if (!test_write_text_file(shared_path, changed_content)) goto cleanup;
    if (!cbox_cache_check_batch(cache, requests, 2U, results,
                                observe_cache_batch_change, &observation)
        || results[0] || results[1]
        || observation.dependency_changes[0] != 1U
        || observation.dependency_changes[1] != 1U) {
        goto cleanup;
    }

    cbox_sha256_buffer("changed cache batch settings",
                       sizeof("changed cache batch settings") - 1U,
                       changed_fingerprint);
    if (!test_write_text_file(shared_path, original_content)
        || remove(shared_path) != 0) {
        goto cleanup;
    }
    requests[0].fingerprint = changed_fingerprint;
    requests[1].fingerprint = changed_fingerprint;
    memset(&observation, 0, sizeof(observation));
    if (!cbox_cache_check_batch(cache, requests, 2U, results,
                                observe_cache_batch_change, &observation)
        || results[0] || results[1]
        || observation.settings_changes[0] != 1U
        || observation.settings_changes[1] != 1U
        || observation.dependency_changes[0] != 0U
        || observation.dependency_changes[1] != 0U) {
        goto cleanup;
    }
    success = true;

cleanup:
    cbox_cache_free(cache);
    (void)remove(shared_path);
    (void)remove(output_paths[0]);
    (void)remove(output_paths[1]);
    (void)remove("CBoxBuild/cache-batch-test.bin");
    (void)remove("CBoxBuild/cache-batch-test.bin.tmp");
    if (!success) fprintf(stderr, "CBox: batched dependency hashing test failed\n");
    return success;
}

static bool test_cache_u32_record_ids(void) {
    enum { RECORD_COUNT = 128 };
    static const char cache_path[] = "CBoxBuild/cache-u32-ids.bin";
    static const char shared_path[] = "CBoxBuild/cache-u32-shared.h";
    char output_paths[RECORD_COUNT][80];
    const char *dependencies[] = {shared_path};
    const char *kept_keys[RECORD_COUNT / 2U];
    cbox_cache_check_request requests[RECORD_COUNT];
    bool results[RECORD_COUNT] = {false};
    unsigned char fingerprint[32];
    cbox_cache *cache = NULL;
    size_t index;
    bool success = false;

    (void)remove(cache_path);
    if (!cbox_create_directories("CBoxBuild")
        || !test_write_text_file(shared_path, "shared cache dependency\n")) {
        goto cleanup;
    }
    cbox_sha256_buffer("32-bit cache record IDs",
                       sizeof("32-bit cache record IDs") - 1U, fingerprint);
    cache = cbox_cache_open(cache_path);
    if (cache == NULL) goto cleanup;
    for (index = 0U; index < RECORD_COUNT; ++index) {
        if (snprintf(output_paths[index], sizeof(output_paths[index]),
                     "CBoxBuild/cache-u32-%zu.obj", index)
                >= (int)sizeof(output_paths[index])
            || !test_write_text_file(output_paths[index], "cache object\n")
            || !cbox_cache_store(cache, output_paths[index], fingerprint,
                                 dependencies, 1U)) {
            goto cleanup;
        }
        requests[index].output_path = output_paths[index];
        requests[index].fingerprint = fingerprint;
    }
    if (!cbox_cache_save(cache, cache_path)) goto cleanup;
    cbox_cache_free(cache);
    cache = cbox_cache_open(cache_path);
    if (cache == NULL
        || !cbox_cache_check_batch(cache, requests, RECORD_COUNT, results,
                                   NULL, NULL)) {
        goto cleanup;
    }
    for (index = 0U; index < RECORD_COUNT; ++index) {
        if (!results[index]) goto cleanup;
        if ((index & 1U) == 0U) kept_keys[index / 2U] = output_paths[index];
    }
    if (!cbox_cache_prune(cache, kept_keys, RECORD_COUNT / 2U)
        || !cbox_cache_save(cache, cache_path)) {
        goto cleanup;
    }
    cbox_cache_free(cache);
    cache = cbox_cache_open(cache_path);
    if (cache == NULL
        || !cbox_cache_check_batch(cache, requests, RECORD_COUNT, results,
                                   NULL, NULL)) {
        goto cleanup;
    }
    for (index = 0U; index < RECORD_COUNT; ++index) {
        if (results[index] != ((index & 1U) == 0U)) goto cleanup;
    }
    success = true;

cleanup:
    cbox_cache_free(cache);
    (void)remove(cache_path);
    (void)remove(shared_path);
    (void)remove("CBoxBuild/cache-u32-ids.bin.tmp");
    for (index = 0U; index < RECORD_COUNT; ++index) {
        (void)snprintf(output_paths[index], sizeof(output_paths[index]),
                       "CBoxBuild/cache-u32-%zu.obj", index);
        (void)remove(output_paths[index]);
    }
    if (!success) {
        fprintf(stderr, "CBox: 32-bit cache record ID test failed\n");
    }
    return success;
}

static bool test_assembly_file_contains(const char *path, const char *text) {
    FILE *file = fopen(path, "rb");
    char buffer[32768];
    size_t amount;
    bool found = false;

    if (file == NULL) return false;
    amount = fread(buffer, 1U, sizeof(buffer) - 1U, file);
    buffer[amount] = '\0';
    found = strstr(buffer, text) != NULL;
    if (fclose(file) != 0) return false;
    return found;
}

static bool test_assembly_object_outputs(const char *target_directory,
                                         const char *const sources[],
                                         size_t source_count,
                                         const char *profile_name) {
    char **object_paths = NULL;
    size_t index;
    bool success = cbox_prepare_object_paths(
        target_directory, sources, source_count, profile_name, ".obj",
        &object_paths);

    if (object_paths != NULL) {
        for (index = 0U; index < source_count; ++index) {
            FILE *object = fopen(object_paths[index], "rb");
            if (object == NULL) {
                success = false;
            } else if (fclose(object) != 0) {
                success = false;
            }
            (void)remove(object_paths[index]);
        }
    }
    cbox_discard_object_paths(object_paths, source_count);
    return success;
}

static bool test_assembly_generation(void) {
    static const char *const compilers[] = {"gcc", "clang", "auto"};
    static const char *const c_source_paths[] = {
        "CBoxBuild/assembly-fixture-a.c",
        "CBoxBuild/assembly-fixture-b.c"
    };
    static const char *const cpp_source_paths[] = {
        "CBoxBuild/assembly-fixture-c.cpp"
    };
    static const char *const source_text[] = {
        "int assembly_fixture_a(void) { return 41; }\n",
        "int assembly_fixture_b(void) { return 42; }\n",
        "int assembly_fixture_cpp(void) { return 43; }\n"
    };
    size_t compiler_index;
    size_t tested = 0U;
    bool success = cbox_create_directories("CBoxBuild")
        && test_write_text_file(c_source_paths[0], source_text[0])
        && test_write_text_file(c_source_paths[1], source_text[1])
        && test_write_text_file(cpp_source_paths[0], source_text[2]);

    for (compiler_index = 0U;
         success && compiler_index < sizeof(compilers) / sizeof(compilers[0]);
         ++compiler_index) {
        const char *compiler = compilers[compiler_index];
        const char *selected = cbox_resolve_compiler(compiler, false);
        const char *extension;
        char *directory;
        char profile_directory[256];
        char path[768];
        char combined_path[768];
        unsigned char digest[32];
        char identity[33];
        const char *all_sources[] = {
            c_source_paths[0], c_source_paths[1], cpp_source_paths[0]
        };
        cbox_target target = {0};
        cbox_profile profile = {0};
        size_t source_index;

        if (selected == NULL
            || (strcmp(compiler, "gcc") == 0
                && !cbox_program_exists("gcc"))
            || (strcmp(compiler, "clang") == 0
                && !cbox_program_exists("clang"))) {
            continue;
        }
        extension = strcmp(selected, "msvc") == 0 ? ".asm" : ".s";
        target.name = "assembly-output-test";
        target.kind = "executable";
        target.output = "custom-assembly-name.out";
        target.c = "c17";
        target.cpp = "c++20";
        target.c_sources = (char **)c_source_paths;
        target.c_source_count = 2U;
        target.cpp_sources = (char **)cpp_source_paths;
        target.cpp_source_count = 1U;
        profile.name = "assembly-test";
        profile.optimization = "debug";
        combined_path[0] = '\0';
        directory = cbox_build_directory_name(selected, target.name);
        if (directory == NULL) {
            success = false;
            break;
        }
        (void)snprintf(profile_directory, sizeof(profile_directory), "%s",
                       profile.name);
        if (!cbox_build_target_assembly(&target, &profile, compiler, false)) {
            free(directory);
            success = false;
            break;
        }
        if (strcmp(selected, "msvc") == 0
            && !test_assembly_object_outputs(directory, all_sources, 3U,
                                             profile.name)) {
            success = false;
        }
        for (source_index = 0U; source_index < 3U; ++source_index) {
            const char *source = all_sources[source_index];
            const char *basename = strrchr(source, '/');
            const char *dot;
            size_t stem_length;
            size_t byte_index;
            basename = basename == NULL ? source : basename + 1;
            dot = strrchr(basename, '.');
            stem_length = dot == NULL ? strlen(basename)
                : (size_t)(dot - basename);
            cbox_sha256_buffer(source, strlen(source), digest);
            for (byte_index = 0U; byte_index < 16U; ++byte_index) {
                (void)snprintf(identity + byte_index * 2U,
                               sizeof(identity) - byte_index * 2U,
                               "%02x", digest[byte_index]);
            }
            identity[32] = '\0';
            if (snprintf(path, sizeof(path),
                         "CBoxBuild/%s/%s/asm/%.*s-%s%s",
                         directory, profile_directory, (int)stem_length,
                         basename, identity, extension) >= (int)sizeof(path)
                || !test_assembly_file_contains(path, "assembly_fixture_")) {
                success = false;
                break;
            }
            (void)remove(path);
        }
        if (success
            && !cbox_build_target_assembly(&target, &profile, compiler, true)) {
            success = false;
        }
        if (success && strcmp(selected, "msvc") == 0
            && !test_assembly_object_outputs(directory, all_sources, 3U,
                                             profile.name)) {
            success = false;
        }
        if (success
            && (snprintf(combined_path, sizeof(combined_path),
                         "CBoxBuild/%s/%s/asm/custom-assembly-name%s",
                         directory, profile_directory, extension)
                    >= (int)sizeof(combined_path)
                || !test_assembly_file_contains(combined_path,
                                                "assembly_fixture_a")
                || !test_assembly_file_contains(combined_path,
                                                "assembly_fixture_b"))) {
            success = false;
        }
        if (success
            && !test_assembly_file_contains(combined_path, "assembly_fixture_cpp")) {
            success = false;
        }
        if (combined_path[0] != '\0') (void)remove(combined_path);
        free(directory);
        if (!success) break;
        ++tested;
    }
    (void)remove(c_source_paths[0]);
    (void)remove(c_source_paths[1]);
    (void)remove(cpp_source_paths[0]);
    if (tested == 0U) success = false;
    if (!success) fprintf(stderr, "CBox: assembly generation test failed\n");
    return success;
}

static bool test_syntax_check(void) {
    static const char *const compilers[] = {"gcc", "clang", "auto"};
    static const char *const c_sources[] = {
        "CBoxBuild/syntax-check-valid.c"
    };
    static const char *const cpp_sources[] = {
        "CBoxBuild/syntax-check-valid.cpp"
    };
    static const char *const invalid_sources[] = {
        "CBoxBuild/syntax-check-invalid.c"
    };
    size_t compiler_index;
    size_t tested = 0U;
    bool success = cbox_create_directories("CBoxBuild")
        && test_write_text_file(c_sources[0],
                                "int syntax_check_c(void) { return 0; }\n")
        && test_write_text_file(cpp_sources[0],
                                "int syntax_check_cpp() { return 0; }\n")
        && test_write_text_file(invalid_sources[0],
                                "int syntax_check_invalid( {\n");

    for (compiler_index = 0U;
         success && compiler_index < sizeof(compilers) / sizeof(compilers[0]);
         ++compiler_index) {
        const char *compiler = compilers[compiler_index];
        cbox_target target = {0};
        cbox_profile profile = {0};

        if (cbox_resolve_compiler(compiler, true) == NULL) continue;
        target.name = "syntax-check-test";
        target.kind = "executable";
        target.c = "c17";
        target.cpp = "c++20";
        target.c_sources = (char **)c_sources;
        target.c_source_count = 1U;
        target.cpp_sources = (char **)cpp_sources;
        target.cpp_source_count = 1U;
        profile.name = "syntax-check-test";
        profile.optimization = "debug";
        profile.warnings = "strict";
        if (!cbox_check_target(&target, &profile, compiler)) {
            success = false;
            break;
        }
        target.c_sources = (char **)invalid_sources;
        target.cpp_sources = NULL;
        target.cpp_source_count = 0U;
        if (cbox_check_target(&target, &profile, compiler)) {
            success = false;
            break;
        }
        ++tested;
    }
    (void)remove(c_sources[0]);
    (void)remove(cpp_sources[0]);
    (void)remove(invalid_sources[0]);
    if (tested == 0U) success = false;
    if (!success) fprintf(stderr, "CBox: syntax-check test failed\n");
    return success;
}

static bool write_cache_incremental_fixture(const char *header_value) {
    FILE *header = fopen("tests/cache_incremental_tmp.h", "wb");
    FILE *source = fopen("tests/cache_incremental_tmp.c", "wb");
    bool success;

    if (header == NULL || source == NULL) {
        if (header != NULL) fclose(header);
        if (source != NULL) fclose(source);
        return false;
    }
    success = fprintf(header, "#define CBOX_CACHE_INCREMENTAL_VALUE %s\n",
                      header_value) > 0
        && fprintf(source,
                   "#include \"cache_incremental_tmp.h\"\n"
                   "int main(void) { return CBOX_CACHE_INCREMENTAL_VALUE; }\n") > 0;
    if (fclose(header) != 0) success = false;
    if (fclose(source) != 0) success = false;
    return success;
}

static bool run_cache_incremental_fixture(int expected_result) {
    char *sources[] = {"tests/cache_incremental_tmp.c"};
    char output_path[320];
    char executable[384];
    const char *arguments[2];
    cbox_target target = {0};
    cbox_profile profile = {0};

    target.name = "cache-incremental";
    target.kind = "executable";
    target.output = "cache-incremental.exe";
    target.c_sources = sources;
    target.c_source_count = 1U;
    target.c = "c99";
    profile.name = "cache-test";
    profile.optimization = "debug";
    if (!cbox_build_target(&target, &profile, "gcc")) return false;
    test_output_path_for_target(output_path, sizeof(output_path),
                                "gcc",
                                "cache-incremental", "cache-test",
                                "executable", target.output);
#ifdef _WIN32
    (void)snprintf(executable, sizeof(executable), ".\\%s", output_path);
#else
    (void)snprintf(executable, sizeof(executable), "./%s", output_path);
#endif
    arguments[0] = executable;
    arguments[1] = NULL;
    return cbox_run_process(arguments) == expected_result;
}

static bool test_incremental_header_rebuild(void) {
    char output_path[320];
    FILE *output_file;
    bool success = false;

    if (!write_cache_incremental_fixture("42")
        || !run_cache_incremental_fixture(42)
        || !run_cache_incremental_fixture(42)) {
        goto cleanup;
    }
    test_output_path_for_target(output_path, sizeof(output_path),
                                "gcc",
                                "cache-incremental", "cache-test",
                                "executable",
                                "cache-incremental.exe");
    output_file = fopen(output_path, "wb");
    if (output_file == NULL) goto cleanup;
    success = fwrite("damaged output", 1U, sizeof("damaged output") - 1U,
                     output_file) == sizeof("damaged output") - 1U;
    if (fclose(output_file) != 0) success = false;
    if (!success) goto cleanup;
    success = false;
    if (!run_cache_incremental_fixture(42)
        || !write_cache_incremental_fixture("43")
        || !run_cache_incremental_fixture(43)) {
        goto cleanup;
    }
    success = true;

cleanup:
    (void)remove("tests/cache_incremental_tmp.c");
    (void)remove("tests/cache_incremental_tmp.h");
    test_output_path_for_target(output_path, sizeof(output_path),
                                "gcc",
                                "cache-incremental", "cache-test",
                                "executable",
                                "cache-incremental.exe");
    (void)remove(output_path);
    if (!success) {
        fprintf(stderr, "CBox: incremental header rebuild test failed\n");
    }
    return success;
}

static bool run_cache_glob_fixture(int expected_result) {
    char *sources[] = {"tests/cache_glob_tmp/**/*.c"};
    char output_path[320];
    char executable[384];
    const char *arguments[2];
    cbox_target target = {0};
    cbox_profile profile = {0};

    target.name = "cache-source-graph";
    target.kind = "executable";
    target.output = "cache-source-graph.exe";
    target.c_sources = sources;
    target.c_source_count = 1U;
    target.c = "c99";
    profile.name = "cache-test";
    profile.optimization = "debug";
    if (!cbox_build_target(&target, &profile, "gcc")) return false;
    test_output_path_for_target(output_path, sizeof(output_path),
                                "gcc",
                                target.name, profile.name, target.kind,
                                target.output);
#ifdef _WIN32
    (void)snprintf(executable, sizeof(executable), ".\\%s", output_path);
#else
    (void)snprintf(executable, sizeof(executable), "./%s", output_path);
#endif
    arguments[0] = executable;
    arguments[1] = NULL;
    return cbox_run_process(arguments) == expected_result;
}

static bool test_link_dependency_graph_rebuild(void) {
    static const char main_path[] = "tests/cache_glob_tmp/main.c";
    static const char extra_path[] = "tests/cache_glob_tmp/nested/extra.c";
    FILE *main_source;
    FILE *extra_source;
    bool success = false;

    if (!cbox_create_directories("tests/cache_glob_tmp/nested")) goto cleanup;
    main_source = fopen(main_path, "wb");
    if (main_source == NULL) goto cleanup;
    success = fprintf(main_source,
                      "extern int cbox_cache_optional(void) __attribute__((weak));\n"
                      "int main(void) {\n"
                      "    return cbox_cache_optional != 0\n"
                      "        ? cbox_cache_optional() : 0;\n"
                      "}\n") > 0;
    if (fclose(main_source) != 0) success = false;
    if (!success || !run_cache_glob_fixture(0)) goto cleanup;

    extra_source = fopen(extra_path, "wb");
    if (extra_source == NULL) goto cleanup;
    success = fprintf(extra_source,
                      "int cbox_cache_optional(void) { return 42; }\n") > 0;
    if (fclose(extra_source) != 0) success = false;
    if (!success || !run_cache_glob_fixture(42)) goto cleanup;

    if (remove(extra_path) != 0 || !run_cache_glob_fixture(0)) {
        success = false;
        goto cleanup;
    }
    success = true;

cleanup:
    (void)remove(extra_path);
    (void)remove(main_path);
    {
        char output_path[320];
        test_output_path_for_target(output_path, sizeof(output_path),
                                    "gcc",
                                    "cache-source-graph", "cache-test",
                                    "executable", "cache-source-graph.exe");
        (void)remove(output_path);
    }
    {
        const char *patterns[] = {"tests/cache_glob_tmp"};
        (void)cbox_delete_globs(patterns, 1U);
    }
    if (!success) {
        fprintf(stderr, "CBox: recursive glob source-set rebuild test failed\n");
    }
    return success;
}

static bool test_config_versions_and_shell_tasks(void) {
    cbox_project project = {0};
    bool success = false;

    cbox_project_init(&project);
    if (!cbox_load_project("tests/config_v1_0.toml", &project)) {
        goto cleanup;
    }
    if (cbox_load_project("tests/config_v1_1.toml", &project)) {
        goto cleanup;
    }
    if (!cbox_load_project("tests/delete_task.toml", &project)
        || !cbox_run_task(&project, "shell_check")
        || !cbox_run_task(&project, "native_check")) {
        goto cleanup;
    }
    if (cbox_program_exists("pwsh") || cbox_program_exists("powershell.exe")) {
        if (!cbox_run_task(&project, "powershell_check")) {
            goto cleanup;
        }
    }
    success = true;

cleanup:
    cbox_project_free(&project);
    if (!success) {
        fprintf(stderr, "CBox: config version or shell task integration test failed\n");
    }
    return success;
}

static bool test_rejects_config_text(const char *path, const char *contents) {
    cbox_project project = {0};
    bool rejected;

    if (!test_write_text_file(path, contents)) {
        (void)remove(path);
        return false;
    }
    cbox_project_init(&project);
    rejected = !cbox_load_project(path, &project);
    cbox_project_free(&project);
    (void)remove(path);
    return rejected;
}

static bool build_and_run(const char *compiler, const char *target_name,
                          const char *output, bool cpp_only) {
    char *c_sources[] = {"tests/cpp/mixed.c"};
    char *mixed_sources[] = {"tests/cpp/mixed.cc"};
    char *only_sources[] = {"tests/cpp/only.cxx"};
    cbox_target target = {0};
    cbox_profile profile = {0};
    char executable[384];
    char output_path[320];
    const char *arguments[2];

    target.name = (char *)target_name;
    target.kind = "executable";
    target.output = (char *)output;
    target.c = "c17";
    target.cpp = "c++20";
    target.c_sources = cpp_only ? NULL : c_sources;
    target.c_source_count = cpp_only ? 0U : 1U;
    target.cpp_sources = cpp_only ? only_sources : mixed_sources;
    target.cpp_source_count = 1U;
    profile.optimization = "debug";
    profile.warnings = "all";
    profile.debug_symbols = strcmp(compiler, "auto") == 0;
    profile.name = "debug";

    if (cbox_build_target(&target, &profile, compiler)) {
        test_output_path(output_path, sizeof(output_path), compiler, target_name,
                         "executable", output);
#ifdef _WIN32
        (void)snprintf(executable, sizeof(executable), ".\\%s", output_path);
#else
        (void)snprintf(executable, sizeof(executable), "./%s", output_path);
#endif
        arguments[0] = executable;
        arguments[1] = NULL;
        if (cbox_run_process(arguments) == 0) {
            (void)remove(output_path);
            return true;
        }
    }
    test_output_path(output_path, sizeof(output_path), compiler, target_name,
                     "executable", output);
    (void)remove(output_path);
    return false;
}

static bool build_cpp_modules_and_run(const char *compiler, const char *suffix) {
    char *module_sources[] = {
        "tests/cpp/modules/sum.ixx",
        "tests/cpp/modules/base.cppm"
    };
    char *cpp_sources[] = {"tests/cpp/modules/main.cpp"};
    char target_name[80];
    char output[96];
    char executable[384];
    char output_path[320];
    const char *arguments[2];
    cbox_target target = {0};
    cbox_profile profile = {0};

    (void)snprintf(target_name, sizeof(target_name), "cpp-modules-%s", suffix);
    (void)snprintf(output, sizeof(output), "cpp-modules-%s.exe", suffix);
    target.name = target_name;
    target.kind = "executable";
    target.output = output;
    target.cpp = "c++20";
    target.cpp_modules = module_sources;
    target.cpp_module_count = 2U;
    target.cpp_sources = cpp_sources;
    target.cpp_source_count = 1U;
    profile.optimization = "debug";
    profile.name = "debug";

    if (!cbox_build_target(&target, &profile, compiler)) {
        test_output_path(output_path, sizeof(output_path), compiler, target_name,
                         "executable", output);
        (void)remove(output_path);
        return false;
    }
    test_output_path(output_path, sizeof(output_path), compiler, target_name,
                     "executable", output);
#ifdef _WIN32
    (void)snprintf(executable, sizeof(executable), ".\\%s", output_path);
#else
    (void)snprintf(executable, sizeof(executable), "./%s", output_path);
#endif
    arguments[0] = executable;
    arguments[1] = NULL;
    {
        bool success = cbox_run_process(arguments) == 0;
        (void)remove(output_path);
        return success;
    }
}

static bool test_cpp_modules_config(void) {
    cbox_project project = {0};
    bool success;

    cbox_project_init(&project);
    success = false;
    if (cbox_load_project("tests/cpp_modules.toml", &project)) {
        success = project.target_count == 1U
            && project.targets[0].cpp_module_count == 2U
            && strcmp(project.targets[0].cpp_modules[0],
                      "tests/cpp/modules/base.cppm") == 0
            && strcmp(project.targets[0].cpp_modules[1],
                      "tests/cpp/modules/sum.ixx") == 0;
    }
    cbox_project_free(&project);
    if (!success) {
        fprintf(stderr, "CBox: C++ module config integration test failed\n");
        return false;
    }

    cbox_project_init(&project);
    success = !cbox_load_project("tests/cpp_modules_pre20.toml", &project);
    cbox_project_free(&project);
    if (!success) {
        fprintf(stderr, "CBox: C++ module config integration test failed\n");
    }
    return success;
}

static bool test_project_metadata_config(void) {
    cbox_project project = {0};
    char executable[512];
    const char *invalid_array_config = "CBoxBuild/invalid-string-array.toml";
    char *run_arguments[2];
    bool success;

    cbox_project_init(&project);
    success = cbox_load_project("tests/project_metadata.toml", &project)
        && strcmp(project.config_format_version, "1.0") == 0
        && strcmp(project.name, "metadata-test") == 0
        && strcmp(project.version, "2.4.1") == 0
        && strcmp(project.description, "Project metadata summary fixture") == 0
        && project.author_count == 2U
        && strcmp(project.authors[0], "Ada Example") == 0
        && strcmp(project.authors[1], "Grace Example") == 0
        && strcmp(project.license, "Apache 2.0") == 0
        && project.profile_count == 4U
        && strcmp(project.profiles[0].warnings, "basic") == 0
        && strcmp(project.profiles[1].warnings, "extra") == 0
        && strcmp(project.profiles[2].warnings, "strict") == 0
        && strcmp(project.profiles[3].warnings, "all") == 0
        && project.target_count == 7U
        && strcmp(project.targets[0].c, "c99") == 0
        && strcmp(project.targets[0].cpp, "c++11") == 0
        && project.targets[0].link_target_count == 1U
        && strcmp(project.targets[0].link_targets[0],
                  "metadata-library") == 0
        && strcmp(project.targets[1].kind, "static") == 0
        && strcmp(project.targets[2].kind, "shared") == 0
        && project.targets[3].freestanding_configured
        && project.targets[3].freestanding
        && strcmp(project.targets[3].entry_point, "_start") == 0
        && project.targets[4].freestanding_configured
        && !project.targets[4].freestanding
        && !project.targets[0].freestanding_configured
        && strcmp(project.targets[5].name, "metadata-library") == 0
        && strcmp(project.targets[6].name,
                  "metadata-library__base") == 0
        && project.targets[5].link_target_count == 1U
        && strcmp(project.targets[5].link_targets[0],
                  "metadata-library__base") == 0;
    if (success) {
        success = !cbox_run_task_freestanding(&project, "freestanding-disabled")
            && cbox_run_task_freestanding(&project, "freestanding-enabled")
            && cbox_run_task_freestanding(&project, "freestanding-undeclared")
            && cbox_run_task(&project, "imported-library-build");
    }
    if (success) {
        test_output_path_for_target(executable, sizeof(executable), "gcc",
                                    "metadata-test", "checked", "executable",
                                    "metadata-test");
        run_arguments[0] = executable;
        run_arguments[1] = NULL;
        success = executable[0] != '\0'
            && cbox_run_process((const char *const *)run_arguments) == 0;
    }
    cbox_project_free(&project);
    if (success) {
        cbox_project_init(&project);
        success = !cbox_load_project("tests/imports/cycle-a/CBox.toml",
                                     &project);
        cbox_project_free(&project);
    }
    if (success) {
        cbox_project_init(&project);
        success = !cbox_load_project("tests/profile_invalid_warnings.toml",
                                     &project);
        cbox_project_free(&project);
    }
    if (success) {
        static const char invalid_array_contents[] =
            "[cbox]\nversion = \"1.0\"\n"
            "[[target]]\nname = \"invalid-array\"\n"
            "c_sources = [\"valid.c\", 42]\n";
        static const char invalid_table_contents[] =
            "[cbox]\nversion = \"1.0\"\n"
            "[task]\nrelease = \"not-a-task-table\"\n";
        success = test_rejects_config_text(invalid_array_config,
                                           invalid_array_contents)
            && test_rejects_config_text(invalid_array_config,
                                        invalid_table_contents);
    }
    if (success) {
        char long_value[128];
        cbox_project long_value_project = {0};
        char *expanded;
        memset(long_value, 'x', sizeof(long_value) - 1U);
        long_value[sizeof(long_value) - 1U] = '\0';
        long_value_project.name = long_value;
        expanded = cbox_expand_template(&long_value_project, NULL, NULL, NULL,
                                        "{project.name}");
        success = expanded != NULL;
        if (expanded != NULL) {
            success = success
                && strlen(expanded) == sizeof(long_value) - 1U
                && strcmp(expanded, long_value) == 0;
        }
        free(expanded);
        success = success
            && cbox_expand_template(NULL, NULL, NULL, NULL,
                                    "unfinished {template") == NULL;
    }
    if (!success) {
        fprintf(stderr, "CBox: project metadata config integration test failed\n");
    }
    return success;
}

static bool test_freestanding_build(const char *compiler) {
    char *sources[] = {"tests/freestanding_entry.c"};
    cbox_target target = {0};
    bool success;

    target.name = "freestanding-flag-test";
    target.kind = "executable";
    target.entry_point = "cbox_freestanding_entry";
    target.c_sources = sources;
    target.c_source_count = 1U;
    target.c = "c17";
    success = cbox_build_target_freestanding(&target, NULL, compiler);
    if (!success) {
        fprintf(stderr, "CBox: freestanding build test failed with %s\n",
                compiler);
    }
    return success;
}

static bool test_legacy_static_kind_rejected(void) {
    cbox_project project = {0};
    bool success;

    cbox_project_init(&project);
    success = !cbox_load_project("tests/config_legacy_static_kind.toml", &project);
    cbox_project_free(&project);
    if (!success) {
        fprintf(stderr, "CBox: legacy static target kind was unexpectedly accepted\n");
    }
    return success;
}

static bool build_static_math_and_run(const char *compiler, const char *suffix) {
    char *library_sources[] = {"tests/static_math.c"};
    char *program_sources[] = {"tests/static_math_main.c"};
    char target_name[80];
    char *archive_output;
    char archive_argument[320];
    char target_output_path[320];
    char program_output[80];
    char archive_path[320];
    char program_path[320];
    char executable[384];
    char *link_libraries[2];
    const char *arguments[2];
    cbox_target library = {0};
    cbox_target program = {0};
    cbox_profile profile = {0};
    bool success = false;

    (void)snprintf(target_name, sizeof(target_name), "cbox-math-%s", suffix);
#if defined(_WIN32)
    {
        cbox_linker_kind linker_kind = strcmp(compiler, "gcc") == 0
            ? CBOX_LINKER_GNU : CBOX_LINKER_MSVC;
        archive_output = cbox_linker_static_library_output(linker_kind, target_name);
    }
#else
    archive_output = cbox_linker_static_library_output(CBOX_LINKER_GNU, target_name);
#endif
    if (archive_output == NULL) {
        return false;
    }
    test_output_path(target_output_path, sizeof(target_output_path),
                     compiler, target_name, "static", archive_output);
    (void)snprintf(archive_argument, sizeof(archive_argument), "%s",
                   target_output_path);
    (void)snprintf(program_output, sizeof(program_output), "cbox-math-%s.exe", suffix);

    library.name = target_name;
    library.kind = "static";
    library.c_sources = library_sources;
    library.c_source_count = 1U;
    library.c = "c17";
    library.cpp = "c++20";

    program.name = "cbox-math-check";
    program.kind = "executable";
    program.output = program_output;
    program.c_sources = program_sources;
    program.c_source_count = 1U;
    program.c = "c17";
    program.cpp = "c++20";
    link_libraries[0] = archive_argument;
    link_libraries[1] = "m";
    program.link_libraries = link_libraries;
    program.link_library_count = 2U;
    profile.optimization = "debug";
    profile.name = "debug";

    if (!cbox_build_target(&library, &profile, compiler)
        || !cbox_build_target(&program, &profile, compiler)) {
        goto cleanup;
    }
#ifdef _WIN32
    test_output_path(program_path, sizeof(program_path),
                     compiler, "cbox-math-check", "executable", program_output);
    (void)snprintf(executable, sizeof(executable), ".\\%s", program_path);
#else
    test_output_path(program_path, sizeof(program_path),
                     compiler, "cbox-math-check", "executable", program_output);
    (void)snprintf(executable, sizeof(executable), "./%s", program_path);
#endif
    test_output_path(archive_path, sizeof(archive_path), compiler, target_name,
                     "static", archive_output);
    arguments[0] = executable;
    arguments[1] = NULL;
    success = cbox_run_process(arguments) == 0;

cleanup:
    test_output_path(program_path, sizeof(program_path),
                     compiler, "cbox-math-check", "executable", program_output);
    test_output_path(archive_path, sizeof(archive_path), compiler, target_name,
                     "static", archive_output);
    (void)remove(program_path);
    (void)remove(archive_path);
    free(archive_output);
    return success;
}

static bool build_shared_library_and_run(const char *compiler, const char *suffix) {
    char *shared_sources[] = {"tests/shared_value.c"};
    char *program_sources[] = {"tests/shared_value_main.c"};
    char target_name[80];
    char program_name[96];
    char executable[384];
    char shared_path[320];
    char import_path[320];
    char program_path[320];
    char deployed_shared_path[320];
#if defined(_WIN32) || defined(_WIN64)
    char export_output[320];
#endif
    char *shared_output;
    char *import_output;
    char *library_directory;
    char *link_libraries[1];
#if !defined(_WIN32) && !defined(_WIN64)
    char *link_args[1];
    char runtime_argument[512];
#endif
    const char *arguments[2];
    cbox_linker_kind linker_kind;
    cbox_target library = {0};
    cbox_target program = {0};
    cbox_profile profile = {0};
    bool success = false;

#ifdef _WIN32
    linker_kind = strcmp(compiler, "gcc") == 0
        ? CBOX_LINKER_GNU : CBOX_LINKER_MSVC;
#else
    linker_kind = CBOX_LINKER_GNU;
#endif
    (void)snprintf(target_name, sizeof(target_name), "cbox-shared-%s", suffix);
    (void)snprintf(program_name, sizeof(program_name), "cbox-shared-check-%s.exe", suffix);
    library_directory = cbox_build_directory_name(compiler, target_name);
    if (library_directory == NULL) return false;
    shared_output = cbox_linker_shared_library_output(linker_kind, target_name);
    if (shared_output == NULL) {
        free(library_directory);
        return false;
    }
    import_output = cbox_linker_shared_import_output(linker_kind, shared_output);
#if defined(_WIN32) || defined(_WIN64)
    if (import_output == NULL) {
        free(shared_output);
        free(library_directory);
        return false;
    }
    test_output_path(shared_path, sizeof(shared_path), compiler, target_name,
                     "shared", shared_output);
    test_output_path(import_path, sizeof(import_path), compiler, target_name,
                     "shared", import_output);
#else
    import_output = shared_output;
    test_output_path(shared_path, sizeof(shared_path), compiler, target_name,
                     "shared", shared_output);
    (void)snprintf(import_path, sizeof(import_path), "%s", shared_path);
#endif

    library.name = target_name;
    library.kind = "shared";
    library.c_sources = shared_sources;
    library.c_source_count = 1U;
    library.c = "c99";

    program.name = "cbox-shared-check";
    program.kind = "executable";
    program.output = program_name;
    program.c_sources = program_sources;
    program.c_source_count = 1U;
    program.c = "c99";
    link_libraries[0] = import_output;
    program.link_libraries = link_libraries;
    program.link_library_count = 1U;
#if defined(_WIN32) || defined(_WIN64)
    program.link_args = NULL;
#elif defined(__APPLE__) && defined(__MACH__)
    (void)snprintf(runtime_argument, sizeof(runtime_argument),
                   "-Wl,-rpath,CBoxBuild/%s/debug/bin",
                   library_directory);
    link_args[0] = runtime_argument;
    program.link_args = link_args;
    program.link_arg_count = 1U;
#else
    (void)snprintf(runtime_argument, sizeof(runtime_argument),
                   "-Wl,-rpath,CBoxBuild/%s/debug/bin",
                   library_directory);
    link_args[0] = runtime_argument;
    program.link_args = link_args;
    program.link_arg_count = 1U;
#endif
    profile.optimization = "debug";
    profile.name = "debug";
    deployed_shared_path[0] = '\0';
    link_libraries[0] = import_path;
    if (!cbox_build_target(&library, &profile, compiler)
        || !cbox_build_target(&program, &profile, compiler)) {
        goto cleanup;
    }
#if defined(_WIN32) || defined(_WIN64)
    test_output_path(deployed_shared_path, sizeof(deployed_shared_path),
                     compiler, "cbox-shared-check", "shared", shared_output);
    if (!test_copy_file(shared_path, deployed_shared_path)) goto cleanup;
#endif
#ifdef _WIN32
    test_output_path(program_path, sizeof(program_path),
                     compiler, "cbox-shared-check", "executable", program_name);
    (void)snprintf(executable, sizeof(executable), ".\\%s", program_path);
#else
    test_output_path(program_path, sizeof(program_path),
                     compiler, "cbox-shared-check", "executable", program_name);
    (void)snprintf(executable, sizeof(executable), "./%s", program_path);
#endif
    arguments[0] = executable;
    arguments[1] = NULL;
    success = cbox_run_process(arguments) == 0;

cleanup:
    test_output_path(program_path, sizeof(program_path),
                     compiler, "cbox-shared-check", "executable", program_name);
    (void)remove(program_path);
    (void)remove(shared_path);
#if defined(_WIN32) || defined(_WIN64)
    if (deployed_shared_path[0] != '\0') (void)remove(deployed_shared_path);
#endif
#if defined(_WIN32) || defined(_WIN64)
    if (linker_kind == CBOX_LINKER_MSVC) {
        char export_name[144];
        (void)snprintf(export_name, sizeof(export_name), "%s.exp", target_name);
        test_output_path(export_output, sizeof(export_output),
                         compiler, target_name, "shared", export_name);
        (void)remove(export_output);
    }
#endif
    if (import_output != shared_output) {
        (void)remove(import_path);
        free(import_output);
    }
    free(shared_output);
    free(library_directory);
    return success;
}

int main(void) {
    static const struct {
        const char *name;
        const char *cpp_driver;
    } compilers[] = {
        {"gcc", "g++"},
        {"clang", "clang++"}
    };
    size_t index;
    size_t tested = 0U;

    for (index = 0; index < sizeof(compilers) / sizeof(compilers[0]); ++index) {
        char mixed_output[64];
        char cpp_output[64];
        if (!cbox_program_exists(compilers[index].name)
            || !cbox_program_exists(compilers[index].cpp_driver)) {
            continue;
        }
        (void)snprintf(mixed_output, sizeof(mixed_output), "cpp-mixed-%s.exe",
                       compilers[index].name);
        (void)snprintf(cpp_output, sizeof(cpp_output), "cpp-only-%s.exe",
                       compilers[index].name);
        if (!build_and_run(compilers[index].name, mixed_output, mixed_output, false)
            || !build_and_run(compilers[index].name, cpp_output, cpp_output, true)
            || !test_freestanding_build(compilers[index].name)
            || !build_cpp_modules_and_run(compilers[index].name,
                                          compilers[index].name)
            || !build_shared_library_and_run(compilers[index].name,
                                              compilers[index].name)
            || !build_static_math_and_run(compilers[index].name, compilers[index].name)) {
            fprintf(stderr, "CBox: C++ integration test failed with %s\n",
                    compilers[index].name);
            return 1;
        }
        ++tested;
    }
    if (tested == 0U) {
        fprintf(stderr, "CBox: no GCC or Clang C++ toolchain available for tests\n");
        return 1;
    }
    if (cbox_msvc_available() && !test_freestanding_build("msvc")) return 1;
    if (!test_project_metadata_config()) return 1;
    if (!test_assembly_generation() || !test_syntax_check()) return 1;
    if (!test_source_glob_deduplication()) return 1;
    if (!build_and_run("auto", "cpp-auto-mixed.exe", "cpp-auto-mixed.exe", false)
        || !build_and_run("auto", "cpp-auto-only.exe", "cpp-auto-only.exe", true)
        || !build_cpp_modules_and_run("auto", "auto")
        || !build_shared_library_and_run("auto", "auto")
        || !build_static_math_and_run("auto", "auto")
        || !test_delete_globs()
        || !test_cache_format_and_hashes()
        || !test_cache_batch_hashing()
        || !test_cache_u32_record_ids()
        || !test_incremental_header_rebuild()
        || !test_link_dependency_graph_rebuild()
        || !test_delete_task_from_toml()
        || !test_config_versions_and_shell_tasks()
        || !test_cpp_modules_config()
        || !test_output_extension_resolution()
        || !test_linker_library_arguments()
        || !test_parallel_worker_dispatch()
        || !test_parallel_target_dependencies()
        || !test_parallel_compilation_threshold()
        || !test_compiler_build_directories()
        || !test_module_graph_planning()
        || !test_legacy_static_kind_rejected()) {
        fprintf(stderr, "CBox: C++ integration test failed with automatic compiler selection\n");
        return 1;
    }
    printf("CBox: C++ integration tests passed with %zu toolchain(s)\n", tested);
    return 0;
}