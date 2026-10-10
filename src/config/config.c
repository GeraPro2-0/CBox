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

#include "cbox.h"
#include "build/build.h"
#include "platform/filesystem.h"
#include "platform/process.h"
#include "platform/threads.h"
#include "linker/linker.h"
#include "model/target_graph.h"
#include "toolchain/toolchain.h"
#include "util/terminal.h"

#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CBOX_CONFIG_FORMAT_VERSION "1.0"
#define CBOX_CONFIG_FORMAT_MAJOR 1U
#define CBOX_CONFIG_FORMAT_MINOR 0U

static char *dup_string_n(const char *text, size_t length) {
    char *copy;
    if (text == NULL) {
        return NULL;
    }
    copy = (char *)malloc(length + 1U);
    if (copy == NULL) {
        return NULL;
    }
    if (length > 0U) {
        memcpy(copy, text, length);
    }
    copy[length] = '\0';
    return copy;
}

static char *dup_string(const char *text) {
    if (text == NULL) {
        return NULL;
    }
    return dup_string_n(text, strlen(text));
}

static char *dup_datum_string(const toml_datum_t *datum) {
    if (datum == NULL || datum->type != TOML_STRING) {
        return NULL;
    }
    return dup_string_n(datum->u.str.ptr, (size_t)datum->u.str.len);
}

static void free_string_array(char **items, size_t count) {
    size_t i;
    if (items == NULL) {
        return;
    }
    for (i = 0; i < count; ++i) {
        free(items[i]);
    }
    free(items);
}

static void free_task(cbox_task *task) {
    if (task == NULL) {
        return;
    }
    free(task->name);
    free(task->kind);
    free(task->target);
    free(task->profile);
    free(task->print);
    free(task->command);
    free(task->shell);
    free(task->windows_command.shell);
    free(task->windows_command.command);
    free(task->linux_command.shell);
    free(task->linux_command.command);
    free(task->macos_command.shell);
    free(task->macos_command.command);
    free_string_array(task->delete_patterns, task->delete_pattern_count);
    task->name = NULL;
    task->kind = NULL;
    task->target = NULL;
    task->profile = NULL;
    task->print = NULL;
    task->command = NULL;
    task->shell = NULL;
    memset(&task->windows_command, 0, sizeof(task->windows_command));
    memset(&task->linux_command, 0, sizeof(task->linux_command));
    memset(&task->macos_command, 0, sizeof(task->macos_command));
    task->delete_patterns = NULL;
    task->delete_pattern_count = 0U;
}

static void free_target(cbox_target *target) {
    if (target == NULL) {
        return;
    }
    free(target->name);
    free(target->kind);
    free(target->output);
    free(target->entry_point);
    free(target->source_root);
    free(target->c);
    free(target->cpp);
    free_string_array(target->c_sources, target->c_source_count);
    free_string_array(target->cpp_sources, target->cpp_source_count);
    free_string_array(target->cpp_modules, target->cpp_module_count);
    free_string_array(target->depends_on, target->dependency_count);
    free_string_array(target->link_targets, target->link_target_count);
    free_string_array(target->link_target_paths, target->link_target_path_count);
    free_string_array(target->include_dirs, target->include_dir_count);
    free_string_array(target->link_libraries, target->link_library_count);
    free_string_array(target->compile_args, target->compile_arg_count);
    free_string_array(target->link_args, target->link_arg_count);
    target->name = NULL;
    target->kind = NULL;
    target->output = NULL;
    target->entry_point = NULL;
    target->source_root = NULL;
    target->c = NULL;
    target->cpp = NULL;
    target->c_sources = NULL;
    target->cpp_sources = NULL;
    target->cpp_modules = NULL;
    target->depends_on = NULL;
    target->link_targets = NULL;
    target->link_target_paths = NULL;
    target->include_dirs = NULL;
    target->link_libraries = NULL;
    target->compile_args = NULL;
    target->link_args = NULL;
    target->c_source_count = 0U;
    target->cpp_source_count = 0U;
    target->cpp_module_count = 0U;
    target->dependency_count = 0U;
    target->link_target_count = 0U;
    target->link_target_path_count = 0U;
    target->include_dir_count = 0U;
    target->link_library_count = 0U;
    target->compile_arg_count = 0U;
    target->link_arg_count = 0U;
}

static void free_profile(cbox_profile *profile) {
    if (profile == NULL) {
        return;
    }
    free(profile->name);
    free(profile->optimization);
    free(profile->warnings);
    free(profile->print);
    profile->name = NULL;
    profile->optimization = NULL;
    profile->warnings = NULL;
    profile->debug_symbols = false;
    profile->print = NULL;
}

static bool parse_string_value(const toml_datum_t *table, const char *key,
                               char **out) {
    toml_datum_t value;
    if (table == NULL || key == NULL || out == NULL) return false;
    *out = NULL;
    value = toml_get(*table, key);
    if (value.type == TOML_UNKNOWN) return true;
    if (value.type != TOML_STRING) {
        fprintf(stderr, "CBox: setting '%s' must be a string\n", key);
        return false;
    }
    *out = dup_datum_string(&value);
    return *out != NULL;
}

static bool parse_string_array(const toml_datum_t *table, const char *key, char ***out_items,
                              size_t *out_count) {
    toml_datum_t value;
    size_t i;
    char **items = NULL;
    size_t count = 0U;
    size_t cap = 0U;

    if (table == NULL || key == NULL || out_items == NULL || out_count == NULL) {
        return false;
    }

    *out_items = NULL;
    *out_count = 0U;

    value = toml_get(*table, key);
    if (value.type == TOML_UNKNOWN) {
        return true;
    }
    if (value.type != TOML_ARRAY || value.u.arr.size < 0
        || (value.u.arr.size > 0 && value.u.arr.elem == NULL)) {
        fprintf(stderr, "CBox: setting '%s' must be an array of strings\n", key);
        return false;
    }

    for (i = 0; i < (size_t)value.u.arr.size; ++i) {
        toml_datum_t elem = value.u.arr.elem[i];
        char *copy;
        if (elem.type != TOML_STRING) {
            fprintf(stderr, "CBox: setting '%s' must contain only strings\n", key);
            free_string_array(items, count);
            return false;
        }
        copy = dup_datum_string(&elem);
        if (copy == NULL) {
            free_string_array(items, count);
            return false;
        }

        if (count == cap) {
            size_t new_cap;
            char **new_items;
            if (cap == 0U) {
                new_cap = 4U;
            } else if (cap > SIZE_MAX / 2U) {
                free(copy);
                free_string_array(items, count);
                return false;
            } else {
                new_cap = cap * 2U;
            }
            if (new_cap > SIZE_MAX / sizeof(*items)) {
                free(copy);
                free_string_array(items, count);
                return false;
            }
            new_items = realloc(items, new_cap * sizeof(*items));
            if (new_items == NULL) {
                free(copy);
                free_string_array(items, count);
                return false;
            }
            items = new_items;
            cap = new_cap;
        }
        items[count++] = copy;
    }

    *out_items = items;
    *out_count = count;
    return true;
}

static bool parse_delete_patterns(const toml_datum_t *table, cbox_task *task) {
    toml_datum_t value = toml_get(*table, "delete");
    size_t index;

    if (value.type == TOML_UNKNOWN) return true;
    if (value.type != TOML_ARRAY
        || !parse_string_array(table, "delete", &task->delete_patterns,
                               &task->delete_pattern_count)
        || task->delete_pattern_count != (size_t)value.u.arr.size) {
        fprintf(stderr, "CBox: task '%s' delete must be an array of non-empty strings\n",
                task->name != NULL ? task->name : "(unnamed)");
        free_string_array(task->delete_patterns, task->delete_pattern_count);
        task->delete_patterns = NULL;
        task->delete_pattern_count = 0U;
        return false;
    }
    for (index = 0U; index < task->delete_pattern_count; ++index) {
        if (task->delete_patterns[index][0] == '\0') {
            fprintf(stderr, "CBox: task '%s' has an empty delete pattern\n",
                    task->name != NULL ? task->name : "(unnamed)");
            free_string_array(task->delete_patterns, task->delete_pattern_count);
            task->delete_patterns = NULL;
            task->delete_pattern_count = 0U;
            return false;
        }
    }
    return true;
}

static bool is_supported_standard(const char *standard, bool cplusplus) {
    static const char *c_standards[] = {
        "c89", "c90", "ansi c", "ansic", "ansi", "c95", "c99", "c11", "c17", "c18", "c23"
    };
    static const char *cpp_standards[] = {
        "c++98", "c++03", "c++11", "c++14", "c++17", "c++20",
        "c++23", "c++26"
    };
    const char *const *standards = cplusplus ? cpp_standards : c_standards;
    size_t standard_count = cplusplus
        ? sizeof(cpp_standards) / sizeof(cpp_standards[0])
        : sizeof(c_standards) / sizeof(c_standards[0]);
    size_t index;

    for (index = 0; index < standard_count; ++index) {
        if (strcmp(standard, standards[index]) == 0) {
            return true;
        }
    }
    return false;
}

static bool is_supported_shell(const char *shell) {
    return strcmp(shell, "native") == 0 || strcmp(shell, "cmd") == 0
        || strcmp(shell, "sh") == 0 || strcmp(shell, "bash") == 0
        || strcmp(shell, "zsh") == 0 || strcmp(shell, "powershell") == 0;
}

static bool parse_platform_command(const toml_datum_t *table,
                                   cbox_platform_command *platform_command) {
    if (!parse_string_value(table, "shell", &platform_command->shell)
        || !parse_string_value(table, "command", &platform_command->command)
        || platform_command->command == NULL
        || platform_command->command[0] == '\0'
        || (platform_command->shell != NULL
            && !is_supported_shell(platform_command->shell))) {
        return false;
    }
    return true;
}

static bool parse_platform_commands(const toml_datum_t *table, cbox_task *task) {
    toml_datum_t commands = toml_get(*table, "platform_commands");
    size_t index;

    if (commands.type == TOML_UNKNOWN) {
        return true;
    }
    if (commands.type != TOML_TABLE) {
        return false;
    }
    for (index = 0U; index < (size_t)commands.u.tab.size; ++index) {
        toml_datum_t platform_table = commands.u.tab.value[index];
        cbox_platform_command *platform_command;
        const char *platform = commands.u.tab.key[index];

        if (strcmp(platform, "windows") == 0) {
            platform_command = &task->windows_command;
        } else if (strcmp(platform, "linux") == 0) {
            platform_command = &task->linux_command;
        } else if (strcmp(platform, "macos") == 0) {
            platform_command = &task->macos_command;
        } else {
            fprintf(stderr, "CBox: unsupported task command platform '%s'\n", platform);
            return false;
        }
        if (platform_table.type != TOML_TABLE
            || !parse_platform_command(&platform_table, platform_command)) {
            fprintf(stderr, "CBox: invalid command definition for platform '%s'\n",
                    platform);
            return false;
        }
    }
    return true;
}

static bool parse_task_table(const toml_datum_t *table, const char *task_name, cbox_task *task) {
    memset(task, 0, sizeof(*task));
    task->name = dup_string(task_name);
    task->kind = dup_string("task");
    if (task->name == NULL) {
        free_task(task);
        return false;
    }

    if (!parse_string_value(table, "target", &task->target)
        || !parse_string_value(table, "profile", &task->profile)
        || !parse_string_value(table, "print", &task->print)
        || !parse_string_value(table, "command", &task->command)
        || !parse_string_value(table, "shell", &task->shell)) {
        free_task(task);
        return false;
    }
    if (task->shell == NULL) {
        task->shell = dup_string("native");
    }
    if (task->shell == NULL || !is_supported_shell(task->shell)
        || !parse_platform_commands(table, task)) {
        fprintf(stderr, "CBox: invalid shell or platform command in task '%s'\n",
                task_name);
        free_task(task);
        return false;
    }
    if (!parse_delete_patterns(table, task)) {
        free_task(task);
        return false;
    }
    if (task->print == NULL && task->command == NULL) {
        task->print = dup_string("Running task");
        if (task->print == NULL) {
            free_task(task);
            return false;
        }
    }
    return true;
}

static bool parse_target_table(const toml_datum_t *table, cbox_target *target) {
    toml_datum_t freestanding_setting;
    toml_datum_t entry_point_setting;

    memset(target, 0, sizeof(*target));
    freestanding_setting = toml_get(*table, "freestanding");
    if (freestanding_setting.type != TOML_UNKNOWN
        && freestanding_setting.type != TOML_BOOLEAN) {
        fprintf(stderr, "CBox: target setting 'freestanding' must be a boolean\n");
        return false;
    }
    entry_point_setting = toml_get(*table, "entry_point");
    if (entry_point_setting.type != TOML_UNKNOWN
        && entry_point_setting.type != TOML_STRING) {
        fprintf(stderr, "CBox: target setting 'entry_point' must be a string\n");
        return false;
    }
    if (freestanding_setting.type == TOML_BOOLEAN) {
        target->freestanding_configured = true;
        target->freestanding = freestanding_setting.u.boolean;
    }
    if (!parse_string_value(table, "name", &target->name)
        || !parse_string_value(table, "kind", &target->kind)
        || !parse_string_value(table, "output", &target->output)
        || !parse_string_value(table, "entry_point", &target->entry_point)
        || !parse_string_value(table, "c", &target->c)
        || !parse_string_value(table, "cpp", &target->cpp)) {
        free_target(target);
        return false;
    }

    if (target->name == NULL) {
        target->name = dup_string("unnamed-target");
    }
    if (target->kind == NULL) {
        target->kind = dup_string("executable");
    }
    if (target->kind == NULL
        || (strcmp(target->kind, "executable") != 0
            && strcmp(target->kind, "static") != 0
            && strcmp(target->kind, "shared") != 0)) {
        fprintf(stderr, "CBox: unsupported target kind '%s'\n",
                target->kind != NULL ? target->kind : "(out of memory)");
        free_target(target);
        return false;
    }
    if (target->c == NULL) {
        target->c = dup_string("c99");
    }
    if (target->cpp == NULL) {
        target->cpp = dup_string("c++11");
    }
    if (target->c == NULL || !is_supported_standard(target->c, false)) {
        fprintf(stderr, "CBox: unsupported C standard '%s'\n",
                target->c != NULL ? target->c : "(out of memory)");
        free_target(target);
        return false;
    }
    if (target->cpp == NULL || !is_supported_standard(target->cpp, true)) {
        fprintf(stderr, "CBox: unsupported C++ standard '%s'\n",
                target->cpp != NULL ? target->cpp : "(out of memory)");
        free_target(target);
        return false;
    }
    if (entry_point_setting.type == TOML_STRING
        && (target->entry_point == NULL || target->entry_point[0] == '\0')) {
        fprintf(stderr, "CBox: target setting 'entry_point' must not be empty\n");
        free_target(target);
        return false;
    }

    if (!parse_string_array(table, "c_sources", &target->c_sources, &target->c_source_count)) {
        free_target(target);
        return false;
    }
    if (!parse_string_array(table, "cpp_sources", &target->cpp_sources, &target->cpp_source_count)) {
        free_target(target);
        return false;
    }
    if (!parse_string_array(table, "cpp_modules", &target->cpp_modules,
                            &target->cpp_module_count)) {
        free_target(target);
        return false;
    }
    if (!parse_string_array(table, "depends_on", &target->depends_on,
                            &target->dependency_count)) {
        free_target(target);
        return false;
    }
    if (!parse_string_array(table, "link_targets", &target->link_targets,
                            &target->link_target_count)) {
        free_target(target);
        return false;
    }
    if (target->cpp_module_count > 0U
        && strcmp(target->cpp, "c++20") != 0
        && strcmp(target->cpp, "c++23") != 0
        && strcmp(target->cpp, "c++26") != 0) {
        fprintf(stderr, "CBox: C++ modules require the C++20 standard or later\n");
        free_target(target);
        return false;
    }
    if (!parse_string_array(table, "include_dirs", &target->include_dirs, &target->include_dir_count)) {
        free_target(target);
        return false;
    }
    if (!parse_string_array(table, "link_libraries", &target->link_libraries,
                            &target->link_library_count)) {
        free_target(target);
        return false;
    }
    if (!parse_string_array(table, "compile_args", &target->compile_args,
                            &target->compile_arg_count)) {
        free_target(target);
        return false;
    }
    if (!parse_string_array(table, "link_args", &target->link_args,
                            &target->link_arg_count)) {
        free_target(target);
        return false;
    }
    if (strcmp(target->kind, "static") == 0
        && (target->link_library_count > 0U || target->link_arg_count > 0U)) {
        fprintf(stderr, "CBox: static targets cannot use link_libraries or link_args\n");
        free_target(target);
        return false;
    }

    return true;
}

static bool parse_profile_table(const toml_datum_t *table, const char *profile_name, cbox_profile *profile) {
    toml_datum_t warnings_setting;

    memset(profile, 0, sizeof(*profile));
    profile->name = dup_string(profile_name);
    if (profile->name == NULL) {
        return false;
    }

    warnings_setting = toml_get(*table, "warnings");
    if (warnings_setting.type != TOML_UNKNOWN
        && warnings_setting.type != TOML_STRING) {
        fprintf(stderr, "CBox: profile '%s' setting 'warnings' must be a string\n",
                profile_name);
        free_profile(profile);
        return false;
    }
    if (!parse_string_value(table, "optimization", &profile->optimization)
        || !parse_string_value(table, "warnings", &profile->warnings)
        || !parse_string_value(table, "print", &profile->print)) {
        free_profile(profile);
        return false;
    }

    if (profile->optimization == NULL) {
        profile->optimization = dup_string("debug");
    }
    if (profile->warnings == NULL) {
        profile->warnings = dup_string("none");
    }
    if (profile->print == NULL) {
        profile->print = dup_string("Profile: {profile.name}");
    }

    if (profile->optimization == NULL || profile->warnings == NULL
        || profile->print == NULL) {
        free_profile(profile);
        return false;
    }
    if (strcmp(profile->warnings, "none") != 0
        && strcmp(profile->warnings, "basic") != 0
        && strcmp(profile->warnings, "extra") != 0
        && strcmp(profile->warnings, "strict") != 0
        && strcmp(profile->warnings, "all") != 0) {
        fprintf(stderr, "CBox: unsupported warnings profile '%s' "
                "(expected 'none', 'basic', 'extra', 'strict', or 'all')\n",
                profile->warnings);
        free_profile(profile);
        return false;
    }

    {
        toml_datum_t debug_symbols = toml_get(*table, "debug_symbols");
        if (debug_symbols.type != TOML_UNKNOWN
            && debug_symbols.type != TOML_BOOLEAN) {
            fprintf(stderr, "CBox: profile '%s' setting 'debug_symbols' must be a boolean\n",
                    profile_name);
            free_profile(profile);
            return false;
        }
        if (debug_symbols.type == TOML_BOOLEAN) {
            profile->debug_symbols = debug_symbols.u.boolean;
        }
    }

    return true;
}

static bool append_task(cbox_project *project, cbox_task *task) {
    cbox_task *new_tasks;
    if (project == NULL || task == NULL
        || project->task_count >= SIZE_MAX / sizeof(*new_tasks)) {
        return false;
    }
    if (project->task_count == 0U) {
        new_tasks = (cbox_task *)malloc(sizeof(cbox_task));
    } else {
        new_tasks = (cbox_task *)realloc(project->tasks, (project->task_count + 1U) * sizeof(cbox_task));
    }
    if (new_tasks == NULL) {
        return false;
    }
    project->tasks = new_tasks;
    project->tasks[project->task_count++] = *task;
    return true;
}

static bool append_target(cbox_project *project, cbox_target *target) {
    cbox_target *new_targets;
    if (project == NULL || target == NULL
        || project->target_count >= SIZE_MAX / sizeof(*new_targets)) {
        return false;
    }
    if (project->target_count == 0U) {
        new_targets = (cbox_target *)malloc(sizeof(cbox_target));
    } else {
        new_targets = (cbox_target *)realloc(project->targets, (project->target_count + 1U) * sizeof(cbox_target));
    }
    if (new_targets == NULL) {
        return false;
    }
    project->targets = new_targets;
    project->targets[project->target_count++] = *target;
    return true;
}

static bool append_profile(cbox_project *project, cbox_profile *profile) {
    cbox_profile *new_profiles;
    if (project == NULL || profile == NULL
        || project->profile_count >= SIZE_MAX / sizeof(*new_profiles)) {
        return false;
    }
    if (project->profile_count == 0U) {
        new_profiles = (cbox_profile *)malloc(sizeof(cbox_profile));
    } else {
        new_profiles = (cbox_profile *)realloc(project->profiles, (project->profile_count + 1U) * sizeof(cbox_profile));
    }
    if (new_profiles == NULL) {
        return false;
    }
    project->profiles = new_profiles;
    project->profiles[project->profile_count++] = *profile;
    return true;
}

void cbox_project_init(cbox_project *project) {
    if (project == NULL) {
        return;
    }
    memset(project, 0, sizeof(*project));
}

void cbox_project_free(cbox_project *project) {
    size_t i;
    if (project == NULL) {
        return;
    }

    for (i = 0; i < project->task_count; ++i) {
        free_task(&project->tasks[i]);
    }
    free(project->tasks);
    project->tasks = NULL;
    project->task_count = 0U;

    for (i = 0; i < project->profile_count; ++i) {
        free_profile(&project->profiles[i]);
    }
    free(project->profiles);
    project->profiles = NULL;
    project->profile_count = 0U;

    for (i = 0; i < project->target_count; ++i) {
        free_target(&project->targets[i]);
    }
    free(project->targets);
    project->targets = NULL;
    project->target_count = 0U;

    free(project->name);
    project->name = NULL;
    free(project->version);
    project->version = NULL;
    free(project->description);
    project->description = NULL;
    free_string_array(project->authors, project->author_count);
    project->authors = NULL;
    project->author_count = 0U;
    free(project->license);
    project->license = NULL;
    free(project->config_format_version);
    project->config_format_version = NULL;
    free(project->compiler);
    project->compiler = NULL;
}

const cbox_task *cbox_find_task(const cbox_project *project, const char *name) {
    size_t i;
    if (project == NULL || name == NULL) {
        return NULL;
    }
    for (i = 0; i < project->task_count; ++i) {
        if (project->tasks[i].name != NULL && strcmp(project->tasks[i].name, name) == 0) {
            return &project->tasks[i];
        }
    }
    return NULL;
}

const cbox_profile *cbox_find_profile(const cbox_project *project, const char *name) {
    size_t i;
    if (project == NULL || name == NULL) {
        return NULL;
    }
    for (i = 0; i < project->profile_count; ++i) {
        if (project->profiles[i].name != NULL && strcmp(project->profiles[i].name, name) == 0) {
            return &project->profiles[i];
        }
    }
    return NULL;
}

const cbox_target *cbox_find_target(const cbox_project *project, const char *name) {
    size_t i;
    if (project == NULL || name == NULL) {
        return NULL;
    }
    for (i = 0; i < project->target_count; ++i) {
        if (project->targets[i].name != NULL && strcmp(project->targets[i].name, name) == 0) {
            return &project->targets[i];
        }
    }
    return NULL;
}

static bool parse_config_version(const toml_datum_t *version,
                                 unsigned int *major, unsigned int *minor) {
    size_t index = 0U;
    unsigned int value = 0U;
    bool has_major_digit = false;

    if (version->u.str.ptr == NULL || version->u.str.len <= 0) {
        return false;
    }
    while (index < (size_t)version->u.str.len
           && version->u.str.ptr[index] >= '0' && version->u.str.ptr[index] <= '9') {
        unsigned int digit = (unsigned int)(version->u.str.ptr[index] - '0');
        if (value > (UINT_MAX - digit) / 10U) return false;
        value = value * 10U + digit;
        has_major_digit = true;
        ++index;
    }
    if (!has_major_digit || index >= (size_t)version->u.str.len
        || version->u.str.ptr[index++] != '.') {
        return false;
    }
    *major = value;
    value = 0U;
    has_major_digit = false;
    while (index < (size_t)version->u.str.len
           && version->u.str.ptr[index] >= '0' && version->u.str.ptr[index] <= '9') {
        unsigned int digit = (unsigned int)(version->u.str.ptr[index] - '0');
        if (value > (UINT_MAX - digit) / 10U) return false;
        value = value * 10U + digit;
        has_major_digit = true;
        ++index;
    }
    if (!has_major_digit || index != (size_t)version->u.str.len) {
        return false;
    }
    *minor = value;
    return true;
}

static bool validate_config_format(const toml_datum_t *root) {
    toml_datum_t cbox_table = toml_get(*root, "cbox");
    toml_datum_t version;
    unsigned int major;
    unsigned int minor;

    if (cbox_table.type != TOML_TABLE) {
        fprintf(stderr, "CBox: missing [cbox] table with version = \"%s\"\n",
                CBOX_CONFIG_FORMAT_VERSION);
        return false;
    }

    version = toml_get(cbox_table, "version");
    if (version.type != TOML_STRING) {
        fprintf(stderr, "CBox: [cbox].version must be a string (supported: \"%s\")\n",
                CBOX_CONFIG_FORMAT_VERSION);
        return false;
    }

    if (!parse_config_version(&version, &major, &minor)
        || major != CBOX_CONFIG_FORMAT_MAJOR || minor != CBOX_CONFIG_FORMAT_MINOR) {
        fprintf(stderr, "CBox: unsupported CBox.toml format version '%.*s' (supported: %s)\n",
                (int)version.u.str.len, version.u.str.ptr, CBOX_CONFIG_FORMAT_VERSION);
        return false;
    }

    return true;
}

typedef struct {
    const char *paths[64];
    size_t count;
} cbox_project_load_context;

static bool cbox_load_project_internal(const char *config_path,
                                       cbox_project *project,
                                       bool imported,
                                       cbox_project_load_context *context);

static bool path_is_absolute(const char *path) {
    return path != NULL
        && (path[0] == '/' || path[0] == '\\'
            || (path[0] != '\0' && path[1] == ':'));
}

static char *path_directory(const char *path) {
    const char *separator;
    const char *backslash;
    size_t length;

    if (path == NULL) return NULL;
    separator = strrchr(path, '/');
    backslash = strrchr(path, '\\');
    if (separator == NULL || (backslash != NULL && backslash > separator)) {
        separator = backslash;
    }
    if (separator == NULL) return dup_string(".");
    length = (size_t)(separator - path);
#ifdef _WIN32
    if (length == 2U && path[1] == ':') ++length;
#endif
    return dup_string_n(path, length);
}

static char *join_path(const char *directory, const char *path) {
    size_t directory_length;
    size_t path_length;
    char separator = '\\';
    char *joined;

    if (path == NULL) return NULL;
    if (path_is_absolute(path)) return dup_string(path);
    if (directory == NULL) return dup_string(path);
    directory_length = strlen(directory);
    path_length = strlen(path);
#ifndef _WIN32
    separator = '/';
#endif
    if (directory_length > SIZE_MAX - path_length - 2U) return NULL;
    joined = malloc(directory_length + path_length + 2U);
    if (joined == NULL) return NULL;
    memcpy(joined, directory, directory_length);
    if (directory_length > 0U
        && directory[directory_length - 1U] != '/'
        && directory[directory_length - 1U] != '\\') {
        joined[directory_length++] = separator;
    }
    memcpy(joined + directory_length, path, path_length + 1U);
    return joined;
}

static bool root_target_paths(char ***items, size_t count,
                              const char *source_root) {
    char **rooted;
    size_t index;

    if (count == 0U) return true;
    if (*items == NULL || count > SIZE_MAX / sizeof(*rooted)) return false;
    rooted = calloc(count, sizeof(*rooted));
    if (rooted == NULL) return false;
    for (index = 0U; index < count; ++index) {
        rooted[index] = join_path(source_root, (*items)[index]);
        if (rooted[index] == NULL) {
            free_string_array(rooted, index);
            return false;
        }
    }
    free_string_array(*items, count);
    *items = rooted;
    return true;
}

static bool rename_target_references(char **names, size_t count,
                                     const cbox_project *imported_project,
                                     char *const mapped_names[]) {
    size_t name_index;
    for (name_index = 0U; name_index < count; ++name_index) {
        size_t target_index;
        const char *original_name = names[name_index];
        char *replacement = NULL;
        bool found = false;
        for (target_index = 0U;
             target_index < imported_project->target_count; ++target_index) {
            if (imported_project->targets[target_index].name != NULL
                && original_name != NULL
                && strcmp(original_name,
                          imported_project->targets[target_index].name) == 0) {
                if (found) {
                    fprintf(stderr,
                            "CBox: imported target reference '%s' is ambiguous\n",
                            original_name);
                    free(replacement);
                    return false;
                }
                replacement = dup_string(mapped_names[target_index]);
                if (replacement == NULL) return false;
                found = true;
            }
        }
        if (!found) {
            fprintf(stderr,
                    "CBox: imported target dependency '%s' is not defined "
                    "by its package\n",
                    original_name != NULL ? original_name : "(null)");
            return false;
        }
        free(names[name_index]);
        names[name_index] = replacement;
    }
    return true;
}

static bool load_project_dependencies(const toml_datum_t *root,
                                      const char *manifest_identity,
                                      cbox_project *project,
                                      cbox_project_load_context *context) {
    toml_datum_t dependencies = toml_get(*root, "dependency");
    char *manifest_directory = path_directory(manifest_identity);
    size_t dependency_index;
    bool success = false;

    if (manifest_directory == NULL) return false;
    if (dependencies.type == TOML_UNKNOWN) {
        success = true;
        goto cleanup;
    }
    if (dependencies.type != TOML_ARRAY) {
        fprintf(stderr, "CBox: top-level 'dependency' must be an array of tables\n");
        goto cleanup;
    }
    if (context->count == sizeof(context->paths) / sizeof(context->paths[0])) {
        fprintf(stderr, "CBox: local dependency nesting exceeds the limit of %zu\n",
                sizeof(context->paths) / sizeof(context->paths[0]));
        goto cleanup;
    }
    for (dependency_index = 0U; dependency_index < context->count;
         ++dependency_index) {
        // This is the active import stack, not a global "seen" set: a library may
        // be referenced by two separate branches without forming a cycle.
        if (strcmp(context->paths[dependency_index], manifest_identity) == 0) {
            fprintf(stderr, "CBox: local dependency cycle includes '%s'\n",
                    manifest_identity);
            goto cleanup;
        }
    }
    context->paths[context->count++] = manifest_identity;
    success = true;

    for (dependency_index = 0U;
         dependency_index < (size_t)dependencies.u.arr.size;
         ++dependency_index) {
        toml_datum_t dependency = dependencies.u.arr.elem[dependency_index];
        toml_datum_t name_value;
        toml_datum_t path_value;
        toml_datum_t target_value;
        char *alias = NULL;
        char *manifest_path = NULL;
        char *selected_target_name = NULL;
        cbox_project imported_project = {0};
        char **mapped_names = NULL;
        size_t selected_index = SIZE_MAX;
        size_t target_index;
        bool dependency_loaded = false;
        bool dependency_success = false;

        if (dependency.type != TOML_TABLE) {
            fprintf(stderr, "CBox: each [[dependency]] entry must be a table\n");
            goto dependency_cleanup;
        }
        name_value = toml_get(dependency, "name");
        path_value = toml_get(dependency, "path");
        target_value = toml_get(dependency, "target");
        if (name_value.type != TOML_STRING || name_value.u.str.len <= 0
            || path_value.type != TOML_STRING || path_value.u.str.len <= 0
            || target_value.type != TOML_STRING || target_value.u.str.len <= 0) {
            fprintf(stderr,
                    "CBox: each [[dependency]] requires non-empty string "
                    "fields 'name', 'path', and 'target'\n");
            goto dependency_cleanup;
        }
        alias = dup_datum_string(&name_value);
        selected_target_name = dup_datum_string(&target_value);
        {
            char *relative_path = dup_datum_string(&path_value);
            if (relative_path != NULL) {
                manifest_path = join_path(manifest_directory, relative_path);
            }
            free(relative_path);
        }
        if (alias == NULL || selected_target_name == NULL
            || manifest_path == NULL) {
            fprintf(stderr, "CBox: failed to resolve local dependency path\n");
            goto dependency_cleanup;
        }
        cbox_project_init(&imported_project);
        dependency_loaded = cbox_load_project_internal(
            manifest_path, &imported_project, true, context);
        if (!dependency_loaded) goto dependency_cleanup;

        for (target_index = 0U;
             target_index < imported_project.target_count; ++target_index) {
            cbox_target *candidate = &imported_project.targets[target_index];
            if (candidate->name != NULL
                && strcmp(candidate->name, selected_target_name) == 0) {
                if (selected_index != SIZE_MAX) {
                    fprintf(stderr,
                            "CBox: dependency target '%.*s' is ambiguous in '%s'\n",
                            (int)target_value.u.str.len, target_value.u.str.ptr,
                            manifest_path);
                    goto dependency_cleanup;
                }
                selected_index = target_index;
            }
        }
        if (selected_index == SIZE_MAX) {
            fprintf(stderr,
                    "CBox: dependency target '%.*s' was not found in '%s'\n",
                    (int)target_value.u.str.len, selected_target_name,
                    manifest_path);
            goto dependency_cleanup;
        }
        if (imported_project.targets[selected_index].kind == NULL
            || strcmp(imported_project.targets[selected_index].kind, "static") != 0) {
            fprintf(stderr,
                    "CBox: dependency target '%s' in '%s' must be static\n",
                    imported_project.targets[selected_index].name, manifest_path);
            goto dependency_cleanup;
        }
        if (imported_project.target_count > SIZE_MAX / sizeof(*mapped_names)) {
            goto dependency_cleanup;
        }
        mapped_names = calloc(imported_project.target_count,
                              sizeof(*mapped_names));
        if (mapped_names == NULL) goto dependency_cleanup;
        for (target_index = 0U;
             target_index < imported_project.target_count; ++target_index) {
            cbox_target *candidate = &imported_project.targets[target_index];
            size_t name_length;
            size_t alias_length = strlen(alias);
            if (target_index == selected_index) {
                mapped_names[target_index] = dup_string(alias);
            } else if (candidate->name != NULL
                       && alias_length <= SIZE_MAX - strlen(candidate->name) - 3U) {
                name_length = alias_length + strlen(candidate->name) + 3U;
                mapped_names[target_index] = malloc(name_length);
                if (mapped_names[target_index] != NULL) {
                    (void)snprintf(mapped_names[target_index], name_length,
                                   "%s__%s", alias, candidate->name);
                }
            }
            if (mapped_names[target_index] == NULL) goto dependency_cleanup;
            {
                size_t prior_index;
                for (prior_index = 0U; prior_index < target_index; ++prior_index) {
                    if (strcmp(mapped_names[prior_index],
                               mapped_names[target_index]) == 0) {
                        fprintf(stderr,
                                "CBox: imported target name '%s' is ambiguous\n",
                                mapped_names[target_index]);
                        goto dependency_cleanup;
                    }
                }
            }
            for (size_t existing_index = 0U;
                 existing_index < project->target_count; ++existing_index) {
                if (project->targets[existing_index].name != NULL
                    && strcmp(project->targets[existing_index].name,
                              mapped_names[target_index]) == 0) {
                    fprintf(stderr, "CBox: imported target name '%s' conflicts\n",
                            mapped_names[target_index]);
                    goto dependency_cleanup;
                }
            }
        }
        for (target_index = 0U;
             target_index < imported_project.target_count; ++target_index) {
            cbox_target *imported_target =
                &imported_project.targets[target_index];
            if ((imported_target->source_root != NULL
                    && (!root_target_paths(&imported_target->c_sources,
                                           imported_target->c_source_count,
                                           imported_target->source_root)
                        || !root_target_paths(&imported_target->cpp_sources,
                                              imported_target->cpp_source_count,
                                              imported_target->source_root)
                        || !root_target_paths(&imported_target->cpp_modules,
                                              imported_target->cpp_module_count,
                                              imported_target->source_root)
                        || !root_target_paths(&imported_target->include_dirs,
                                              imported_target->include_dir_count,
                                              imported_target->source_root)))
                || !rename_target_references(imported_target->depends_on,
                                             imported_target->dependency_count,
                                             &imported_project, mapped_names)
                || !rename_target_references(imported_target->link_targets,
                                             imported_target->link_target_count,
                                             &imported_project, mapped_names)) {
                goto dependency_cleanup;
            }
            free(imported_target->source_root);
            imported_target->source_root = NULL;
            free(imported_target->name);
            imported_target->name = mapped_names[target_index];
            mapped_names[target_index] = NULL;
            if (!append_target(project, imported_target)) goto dependency_cleanup;
            memset(imported_target, 0, sizeof(*imported_target));
        }
        dependency_success = true;

dependency_cleanup:
        if (mapped_names != NULL) {
            for (target_index = 0U;
                 target_index < imported_project.target_count; ++target_index) {
                free(mapped_names[target_index]);
            }
        }
        free(mapped_names);
        free(alias);
        free(selected_target_name);
        free(manifest_path);
        cbox_project_free(&imported_project);
        if (!dependency_success) {
            success = false;
            --context->count;
            goto cleanup;
        }
        continue;
    }
    --context->count;

cleanup:
    free(manifest_directory);
    return success;
}

static bool cbox_load_project_internal(const char *config_path,
                                       cbox_project *project,
                                       bool imported,
                                       cbox_project_load_context *context) {
    toml_result_t result;
    toml_datum_t root;
    toml_datum_t cbox_table;
    toml_datum_t project_table;
    toml_datum_t toolchain_table;
    toml_datum_t tasks_table;
    toml_datum_t profiles_table;
    toml_datum_t targets_table;
    char *manifest_identity = NULL;
    char *manifest_directory = NULL;
    size_t i;

    if (project == NULL || config_path == NULL) {
        return false;
    }

    cbox_project_free(project);
    cbox_project_init(project);

    result = toml_parse_file_ex(config_path);
    if (!result.ok) {
        fprintf(stderr, "CBox: failed to parse %s: %s\n", config_path, result.errmsg);
        toml_free(result);
        return false;
    }

    root = result.toptab;
    if (!validate_config_format(&root)) {
        toml_free(result);
        return false;
    }

    cbox_table = toml_get(root, "cbox");
    if (!parse_string_value(&cbox_table, "version",
                            &project->config_format_version)) {
        toml_free(result);
        cbox_project_free(project);
        return false;
    }

    project_table = toml_get(root, "project");
    if (project_table.type != TOML_UNKNOWN
        && project_table.type != TOML_TABLE) {
        fprintf(stderr, "CBox: [project] must be a table\n");
        toml_free(result);
        cbox_project_free(project);
        return false;
    }
    if (project_table.type == TOML_TABLE) {
        if (!parse_string_value(&project_table, "name", &project->name)
            || !parse_string_value(&project_table, "version", &project->version)
            || !parse_string_value(&project_table, "description",
                                   &project->description)
            || !parse_string_value(&project_table, "license", &project->license)
            || !parse_string_array(&project_table, "authors", &project->authors,
                                &project->author_count)) {
            toml_free(result);
            cbox_project_free(project);
            return false;
        }
        if (project->name == NULL) {
            project->name = dup_string("CBox");
        }
    }

    toolchain_table = toml_get(root, "toolchain");
    if (toolchain_table.type != TOML_UNKNOWN
        && toolchain_table.type != TOML_TABLE) {
        fprintf(stderr, "CBox: [toolchain] must be a table\n");
        toml_free(result);
        cbox_project_free(project);
        return false;
    }
    if (toolchain_table.type == TOML_TABLE) {
        if (!parse_string_value(&toolchain_table, "compiler",
                                &project->compiler)) {
            toml_free(result);
            cbox_project_free(project);
            return false;
        }
    }
    if (project->compiler == NULL) {
        project->compiler = dup_string("auto");
    }

    tasks_table = toml_get(root, "task");
    if (tasks_table.type != TOML_UNKNOWN
        && tasks_table.type != TOML_TABLE) {
        fprintf(stderr, "[task.*] entries must be tables\n");
        toml_free(result);
        cbox_project_free(project);
        return false;
    }
    if (tasks_table.type == TOML_TABLE) {
        if (tasks_table.u.tab.size < 0
            || (tasks_table.u.tab.size > 0
                && (tasks_table.u.tab.key == NULL
                    || tasks_table.u.tab.value == NULL))) {
            fprintf(stderr, "CBox: invalid task table data\n");
            toml_free(result);
            cbox_project_free(project);
            return false;
        }
        for (i = 0; i < (size_t)tasks_table.u.tab.size; ++i) {
            cbox_task task;
            toml_datum_t task_value = tasks_table.u.tab.value[i];
            if (task_value.type != TOML_TABLE) {
                fprintf(stderr, "CBox: task '%s' must be a table\n",
                        tasks_table.u.tab.key[i]);
                toml_free(result);
                cbox_project_free(project);
                return false;
            }
            if (!parse_task_table(&task_value, tasks_table.u.tab.key[i], &task)) {
                toml_free(result);
                cbox_project_free(project);
                return false;
            }
            if (!append_task(project, &task)) {
                free_task(&task);
                toml_free(result);
                cbox_project_free(project);
                return false;
            }
        }
    }

    profiles_table = toml_get(root, "profile");
    if (profiles_table.type != TOML_UNKNOWN
        && profiles_table.type != TOML_TABLE) {
        fprintf(stderr, "[profile.*] entries must be tables\n");
        toml_free(result);
        cbox_project_free(project);
        return false;
    }
    if (profiles_table.type == TOML_TABLE) {
        if (profiles_table.u.tab.size < 0
            || (profiles_table.u.tab.size > 0
                && (profiles_table.u.tab.key == NULL
                    || profiles_table.u.tab.value == NULL))) {
            fprintf(stderr, "CBox: invalid profile table data\n");
            toml_free(result);
            cbox_project_free(project);
            return false;
        }
        for (i = 0; i < (size_t)profiles_table.u.tab.size; ++i) {
            cbox_profile profile;
            toml_datum_t profile_value = profiles_table.u.tab.value[i];
            if (profile_value.type != TOML_TABLE) {
                fprintf(stderr, "CBox: profile '%s' must be a table\n",
                        profiles_table.u.tab.key[i]);
                toml_free(result);
                cbox_project_free(project);
                return false;
            }
            if (!parse_profile_table(&profile_value, profiles_table.u.tab.key[i], &profile)) {
                toml_free(result);
                cbox_project_free(project);
                return false;
            }
            if (!append_profile(project, &profile)) {
                free_profile(&profile);
                toml_free(result);
                cbox_project_free(project);
                return false;
            }
        }
    }

    targets_table = toml_get(root, "target");
    if (targets_table.type != TOML_UNKNOWN
        && targets_table.type != TOML_ARRAY) {
        fprintf(stderr, "[[target]] entries must be an array of tables\n");
        toml_free(result);
        cbox_project_free(project);
        return false;
    }
    if (targets_table.type == TOML_ARRAY) {
        if (targets_table.u.arr.size < 0
            || (targets_table.u.arr.size > 0
                && targets_table.u.arr.elem == NULL)) {
            fprintf(stderr, "CBox: invalid target array data\n");
            toml_free(result);
            cbox_project_free(project);
            return false;
        }
        for (i = 0; i < (size_t)targets_table.u.arr.size; ++i) {
            cbox_target target;
            toml_datum_t target_value = targets_table.u.arr.elem[i];
            if (target_value.type != TOML_TABLE) {
                fprintf(stderr, "CBox: each [[target]] entry must be a table\n");
                toml_free(result);
                cbox_project_free(project);
                return false;
            }
            if (!parse_target_table(&target_value, &target)) {
                toml_free(result);
                cbox_project_free(project);
                return false;
            }
            if (imported) {
                manifest_identity = cbox_path_identity(config_path);
                if (manifest_identity == NULL) {
                    fprintf(stderr,
                            "CBox: failed to resolve imported manifest path '%s'\n",
                            config_path);
                } else {
                    manifest_directory = path_directory(manifest_identity);
                }
                free(manifest_identity);
                manifest_identity = NULL;
                if (manifest_directory == NULL) {
                    free_target(&target);
                    toml_free(result);
                    cbox_project_free(project);
                    return false;
                }
                target.source_root = dup_string(manifest_directory);
                if (target.source_root == NULL) {
                    free_target(&target);
                    free(manifest_directory);
                    toml_free(result);
                    cbox_project_free(project);
                    return false;
                }
                free(manifest_directory);
                manifest_directory = NULL;
            }
            if (!append_target(project, &target)) {
                free_target(&target);
                toml_free(result);
                cbox_project_free(project);
                return false;
            }
        }
    }

    manifest_identity = cbox_path_identity(config_path);
    if (manifest_identity == NULL) {
        fprintf(stderr, "CBox: failed to resolve configuration path '%s'\n",
                config_path);
        toml_free(result);
        cbox_project_free(project);
        return false;
    }
    if (!load_project_dependencies(&root, manifest_identity, project, context)) {
        free(manifest_identity);
        toml_free(result);
        cbox_project_free(project);
        return false;
    }
    free(manifest_identity);
    toml_free(result);

    if (project->task_count == 0U && project->target_count == 0U) {
        fprintf(stderr, "CBox: no valid project data found in %s\n", config_path);
        cbox_project_free(project);
        return false;
    }

    return true;
}

const char *cbox_version(void) {
    return CBOX_CONFIG_FORMAT_VERSION;
}

bool cbox_load_project(const char *config_path, cbox_project *project) {
    cbox_project_load_context context = {0};
    return cbox_load_project_internal(config_path, project, false, &context);
}

void cbox_print_usage(void) {
    (void)cbox_printf(CBOX_BOLD | CBOX_GREEN, "Usage:");
    (void)cbox_printf(CBOX_ITALIC,
                      "\n"
                      "  cbox --help\n"
                      "  cbox --version\n"
                      "  cbox --list\n"
                      "  cbox <task-name>\n"
                      "  cbox <task-name> --asm\n"
                      "  cbox <task-name> --one-asm\n"
                      "  cbox <task-name> --check\n"
                      "  cbox <task-name> --freestanding\n\n");
    (void)cbox_printf(CBOX_BOLD | CBOX_GREEN, "Built-in commands:");
    (void)cbox_printf(CBOX_ITALIC,
                      "\n"
                      "  --help, -h         Show help\n"
                      "  --version, -V      Show CBox version\n"
                      "  --list, -l         List available tasks from the TOML file\n"
                      "  --asm              Generate assembly output for each target source\n"
                      "  --one-asm          Combine target sources into one assembly file\n"
                      "  --check            Check source syntax without producing outputs\n"
                      "  --freestanding     Build with freestanding compiler/linker options\n\n");
    (void)cbox_printf(CBOX_BOLD | CBOX_GREEN,
                      "Project tasks examples (from [task.*]):");
    (void)cbox_printf(CBOX_ITALIC,
                      "\n"
                      "  cbox release\n"
                      "  cbox debug\n"
                      "  cbox clean\n"
                      "  cbox test\n"
                      "  cbox parangaricutirimicuaro\n\n");
}

void cbox_print_project_summary(const cbox_project *project) {
    size_t i;
    if (project == NULL) {
        return;
    }
    (void)cbox_printf(CBOX_BOLD, "CBox configuration:");
    printf("\n");
    printf("  Format version: %s\n",
           project->config_format_version ? project->config_format_version : "(unknown)");
    (void)cbox_printf(CBOX_BOLD, "Project:");
    printf("\n");
    printf("  Name: %s\n", project->name ? project->name : "(unnamed)");
    if (project->version != NULL) {
        printf("  Version: %s\n", project->version);
    }
    if (project->description != NULL) {
        printf("  Description: %s\n", project->description);
    }
    if (project->author_count > 0U) {
        printf("  Authors: ");
        for (i = 0U; i < project->author_count; ++i) {
            printf("%s%s", i == 0U ? "" : ", ", project->authors[i]);
        }
        printf("\n");
    }
    if (project->license != NULL) {
        printf("  License: %s\n", project->license);
    }
    printf("Tasks: %zu\n", project->task_count);
    for (i = 0; i < project->task_count; ++i) {
        printf("  - %s\n", project->tasks[i].name ? project->tasks[i].name : "(unnamed)");
    }
    printf("Targets: %zu\n", project->target_count);
    for (i = 0; i < project->target_count; ++i) {
        printf("  - %s (%s)\n",
               project->targets[i].name ? project->targets[i].name : "(unnamed)",
               project->targets[i].kind ? project->targets[i].kind : "unknown");
    }
}

void cbox_print_task_list(const cbox_project *project) {
    size_t i;
    if (project == NULL) {
        return;
    }
    (void)cbox_printf(CBOX_BOLD, "Available tasks:");
    printf("\n");
    for (i = 0; i < project->task_count; ++i) {
        printf("  %s", project->tasks[i].name ? project->tasks[i].name : "(unnamed)");
        if (project->tasks[i].profile != NULL) {
            printf(" ");
            (void)cbox_printf(CBOX_ITALIC, "[profile: %s]",
                              project->tasks[i].profile);
        }
        printf("\n");
    }
    if (project->profile_count > 0U) {
        (void)cbox_printf(CBOX_BOLD, "Available profiles:");
        printf("\n");
        for (i = 0; i < project->profile_count; ++i) {
            printf("  %s", project->profiles[i].name ? project->profiles[i].name : "(unnamed)");
            printf(" (optimization: %s, warnings: %s, debug_symbols: %s)\n",
                   project->profiles[i].optimization ? project->profiles[i].optimization : "debug",
                   project->profiles[i].warnings ? project->profiles[i].warnings : "none",
                   project->profiles[i].debug_symbols ? "true" : "false");
        }
    }
}

static bool contains_target_placeholder(const char *text) {
    if (text == NULL) {
        return false;
    }
    while (*text != '\0') {
        if (*text == '{') {
            const char *end = strchr(text, '}');
            if (end != NULL) {
                size_t len = (size_t)(end - text - 1U);
                if (len >= 7U && strncmp(text + 1, "target.", 7) == 0) {
                    return true;
                }
            }
        }
        ++text;
    }
    return false;
}

static bool reserve_template_buffer(char **buffer, size_t *capacity,
                                    size_t required) {
    size_t new_capacity;
    char *resized;

    if (buffer == NULL || *buffer == NULL || capacity == NULL) return false;
    if (required <= *capacity) return true;
    new_capacity = *capacity;
    while (new_capacity < required) {
        if (new_capacity > SIZE_MAX / 2U) {
            new_capacity = required;
            break;
        }
        new_capacity *= 2U;
    }
    resized = realloc(*buffer, new_capacity);
    if (resized == NULL) return false;
    *buffer = resized;
    *capacity = new_capacity;
    return true;
}

char *cbox_expand_template(const cbox_project *project, const cbox_task *task,
                          const cbox_profile *profile, const cbox_target *target,
                          const char *input) {
    const char *src;
    char *out;
    size_t out_len = 0U;
    size_t cap = 32U;
    const char *value;

    if (input == NULL) {
        return NULL;
    }

    out = (char *)malloc(cap);
    if (out == NULL) {
        return NULL;
    }

    src = input;
    while (*src != '\0') {
        if (*src == '{') {
            const char *end = strchr(src, '}');
            char *key;
            size_t key_len;
            if (end == NULL) {
                free(out);
                return NULL;
            }
            key_len = (size_t)(end - src - 1U);
            if (key_len == SIZE_MAX) {
                free(out);
                return NULL;
            }
            key = (char *)malloc(key_len + 1U);
            if (key == NULL) {
                free(out);
                return NULL;
            }
            memcpy(key, src + 1, key_len);
            key[key_len] = '\0';

            value = NULL;
            if (target != NULL && strcmp(key, "target.name") == 0) {
                value = target->name;
            } else if (target != NULL && strcmp(key, "target.kind") == 0) {
                value = target->kind;
            } else if (profile != NULL && strcmp(key, "profile.name") == 0) {
                value = profile->name;
            } else if (profile != NULL && strcmp(key, "profile.optimization") == 0) {
                value = profile->optimization;
            } else if (profile != NULL && strcmp(key, "profile.warnings") == 0) {
                value = profile->warnings;
            } else if (profile != NULL && strcmp(key, "profile.debug_symbols") == 0) {
                value = profile->debug_symbols ? "true" : "false";
            } else if (project != NULL && strcmp(key, "project.name") == 0) {
                value = project->name;
            } else if (task != NULL && strcmp(key, "task.name") == 0) {
                value = task->name;
            }

            if (value == NULL) {
                value = "";
            }

            {
                size_t value_length = strlen(value);
                if (out_len == SIZE_MAX
                    || value_length > SIZE_MAX - out_len - 1U
                    || !reserve_template_buffer(
                        &out, &cap, out_len + value_length + 1U)) {
                    free(key);
                    free(out);
                    return NULL;
                }
                memcpy(out + out_len, value, value_length);
                out_len += value_length;
            }
            src = end + 1;
            free(key);
            continue;
        }

        if (out_len > SIZE_MAX - 2U
            || !reserve_template_buffer(&out, &cap, out_len + 2U)) {
            free(out);
            return NULL;
        }
        out[out_len++] = *src++;
    }

    out[out_len] = '\0';
    return out;
}

static const cbox_platform_command *current_platform_command(const cbox_task *task) {
#ifdef _WIN32
    return &task->windows_command;
#elif defined(__APPLE__) && defined(__MACH__)
    return &task->macos_command;
#elif defined(__linux__)
    return &task->linux_command;
#else
    return NULL;
#endif
}

typedef struct {
    const cbox_project *project;
    const cbox_profile *profile;
    const char *compiler;
    const size_t *target_indices;
    bool *results;
    bool freestanding;
} cbox_target_build_batch;

static cbox_linker_kind project_linker_kind(const char *compiler) {
    if (compiler != NULL && strcmp(compiler, "msvc") == 0) {
        return CBOX_LINKER_MSVC;
    }
    return CBOX_LINKER_GNU;
}

static bool find_project_target_index(const cbox_project *project,
                                      const char *name, size_t *target_index) {
    size_t index;
    bool found = false;

    for (index = 0U; index < project->target_count; ++index) {
        if (project->targets[index].name != NULL
            && strcmp(project->targets[index].name, name) == 0) {
            if (found) {
                fprintf(stderr, "CBox: target name '%s' is ambiguous\n", name);
                return false;
            }
            *target_index = index;
            found = true;
        }
    }
    return found;
}

static bool collect_link_target_indices(const cbox_project *project,
                                        size_t target_index,
                                        size_t indices[], size_t *count,
                                        bool visited[]) {
    const cbox_target *target = &project->targets[target_index];
    size_t link_index;

    for (link_index = 0U; link_index < target->link_target_count; ++link_index) {
        size_t dependency_index;
        const cbox_target *dependency;
        if (!find_project_target_index(project, target->link_targets[link_index],
                                       &dependency_index)) {
            fprintf(stderr, "CBox: linked target '%s' is unknown or ambiguous\n",
                    target->link_targets[link_index]);
            return false;
        }
        dependency = &project->targets[dependency_index];
        if (dependency->kind == NULL || strcmp(dependency->kind, "static") != 0) {
            fprintf(stderr,
                    "CBox: link_targets entry '%s' must name a static target\n",
                    target->link_targets[link_index]);
            return false;
        }
        if (visited[dependency_index]) continue;
        visited[dependency_index] = true;
        indices[(*count)++] = dependency_index;
        if (!collect_link_target_indices(project, dependency_index, indices,
                                         count, visited)) {
            return false;
        }
    }
    return true;
}

static bool build_project_target(const cbox_project *project,
                                 size_t target_index,
                                 const cbox_profile *profile,
                                 const char *compiler,
                                 bool freestanding) {
    const cbox_target *target = &project->targets[target_index];
    cbox_target resolved = *target;
    char **libraries = NULL;
    char **target_paths = NULL;
    char **include_dirs = NULL;
    size_t *linked_indices = NULL;
    bool *visited = NULL;
    size_t linked_count = 0U;
    size_t library_count;
    size_t include_capacity;
    size_t include_count;
    size_t index;
    bool success = false;

    if (target->link_target_count == 0U) {
        return freestanding
            ? cbox_build_target_freestanding(target, profile, compiler)
            : cbox_build_target(target, profile, compiler);
    }
    if (project->target_count > SIZE_MAX / sizeof(*linked_indices)
        || project->target_count > SIZE_MAX / sizeof(*visited)) {
        return false;
    }
    linked_indices = malloc(project->target_count * sizeof(*linked_indices));
    visited = calloc(project->target_count, sizeof(*visited));
    if (linked_indices == NULL || visited == NULL
        || !collect_link_target_indices(project, target_index, linked_indices,
                                        &linked_count, visited)
        || target->link_library_count > SIZE_MAX - linked_count) {
        goto cleanup;
    }
    library_count = target->link_library_count + linked_count;
    if (library_count > SIZE_MAX / sizeof(*libraries)
        || linked_count > SIZE_MAX / sizeof(*target_paths)) {
        goto cleanup;
    }
    libraries = malloc(library_count * sizeof(*libraries));
    target_paths = calloc(linked_count, sizeof(*target_paths));
    include_capacity = target->include_dir_count;
    for (index = 0U; index < linked_count; ++index) {
        size_t dependency_include_count =
            project->targets[linked_indices[index]].include_dir_count;
        if (dependency_include_count > SIZE_MAX - include_capacity) {
            goto cleanup;
        }
        include_capacity += dependency_include_count;
    }
    if (include_capacity > SIZE_MAX / sizeof(*include_dirs)) goto cleanup;
    include_dirs = malloc((include_capacity == 0U ? 1U : include_capacity)
                          * sizeof(*include_dirs));
    if (libraries == NULL || (linked_count > 0U && target_paths == NULL)
        || include_dirs == NULL) {
        goto cleanup;
    }
    for (index = 0U; index < target->link_library_count; ++index) {
        libraries[index] = target->link_libraries[index];
    }
    include_count = 0U;
    for (index = 0U; index < target->include_dir_count; ++index) {
        include_dirs[include_count++] = target->include_dirs[index];
    }
    for (index = 0U; index < linked_count; ++index) {
        size_t dependency_index = linked_indices[index];
        const cbox_target *dependency;
        const char *dependency_compiler;
        size_t include_index;
        size_t existing_index;
        dependency = &project->targets[dependency_index];
        dependency_compiler = cbox_resolve_compiler(
            compiler,
            dependency->cpp_source_count > 0U || dependency->cpp_module_count > 0U);
        if (dependency_compiler == NULL) {
            fprintf(stderr, "CBox: cannot select a compiler for linked target '%s'\n",
                    dependency->name != NULL ? dependency->name : "(unnamed)");
            goto cleanup;
        }
        target_paths[index] = cbox_target_artifact_path(
            project_linker_kind(dependency_compiler), dependency_compiler,
            dependency, profile);
        if (target_paths[index] == NULL) {
            fprintf(stderr, "CBox: failed to resolve output for linked target '%s'\n",
                    dependency->name != NULL ? dependency->name : "(unnamed)");
            goto cleanup;
        }
        libraries[target->link_library_count + index] = target_paths[index];
        for (include_index = 0U; include_index < dependency->include_dir_count;
             ++include_index) {
            for (existing_index = 0U; existing_index < include_count;
                 ++existing_index) {
                if (strcmp(include_dirs[existing_index],
                           dependency->include_dirs[include_index]) == 0) {
                    break;
                }
            }
            if (existing_index == include_count) {
                include_dirs[include_count++] =
                    dependency->include_dirs[include_index];
            }
        }
    }
    resolved.link_libraries = libraries;
    resolved.link_library_count = library_count;
    resolved.link_target_paths = target_paths;
    resolved.link_target_path_count = linked_count;
    resolved.include_dirs = include_dirs;
    resolved.include_dir_count = include_count;
    success = freestanding
        ? cbox_build_target_freestanding(&resolved, profile, compiler)
        : cbox_build_target(&resolved, profile, compiler);

cleanup:
    for (index = 0U; index < linked_count; ++index) {
        free(target_paths != NULL ? target_paths[index] : NULL);
    }
    free(target_paths);
    free(libraries);
    free(include_dirs);
    free(linked_indices);
    free(visited);
    return success;
}

static bool build_target_batch_job(size_t job_index, void *context) {
    cbox_target_build_batch *batch = context;
    size_t target_index = batch->target_indices[job_index];

    batch->results[job_index] = build_project_target(
        batch->project, target_index, batch->profile, batch->compiler,
        batch->freestanding);
    return batch->results[job_index];
}

static bool cbox_build_target_graph(const cbox_project *project,
                                    const cbox_target *target,
                                    const cbox_profile *profile,
                                    const char *compiler,
                                    bool freestanding) {
    cbox_target_graph_plan plan = {0};
    bool *results = NULL;
    size_t target_count = project->target_count;
    size_t level;
    char error_message[256];
    bool success = false;

    if (!cbox_target_graph_create_plan(project, target, &plan,
                                       error_message, sizeof(error_message))) {
        fprintf(stderr, "CBox: cannot plan target graph: %s\n",
                error_message[0] != '\0' ? error_message : "unknown error");
        return false;
    }
    if (freestanding) {
        size_t plan_index;
        for (plan_index = 0U; plan_index < plan.target_count; ++plan_index) {
            const cbox_target *planned_target =
                &project->targets[plan.target_indices[plan_index]];
            if (planned_target->freestanding_configured
                && !planned_target->freestanding) {
                (void)cbox_printf(CBOX_BOLD | CBOX_RED, "CBox: ");
                fprintf(stderr,
                        "--freestanding rejected: target '%s' declares "
                        "freestanding = false\n",
                        planned_target->name != NULL ? planned_target->name
                                                     : "(unnamed)");
                goto cleanup;
            }
            if (!planned_target->freestanding_configured) {
                fprintf(stderr,
                        "CBox: --freestanding: target '%s' has no freestanding "
                        "declaration; proceeding without verification\n",
                        planned_target->name != NULL ? planned_target->name
                                                     : "(unnamed)");
            }
        }
    }
    results = calloc(target_count, sizeof(*results));
    if (results == NULL) {
        fprintf(stderr, "CBox: out of memory preparing target build results\n");
        goto cleanup;
    }
    for (level = 0U; level < plan.level_count; ++level) {
        const size_t level_start = plan.level_offsets[level];
        const size_t level_end = plan.level_offsets[level + 1U];
        const size_t ready_count = level_end - level_start;
        cbox_target_build_batch batch;

        if (ready_count == 1U) {
            if (!build_project_target(project, plan.target_indices[level_start],
                                      profile, compiler, freestanding)) {
                goto cleanup;
            }
            continue;
        }

        memset(results, 0, ready_count * sizeof(*results));
        batch.project = project;
        batch.profile = profile;
        batch.compiler = compiler;
        batch.target_indices = plan.target_indices + level_start;
        batch.results = results;
        batch.freestanding = freestanding;
        printf("CBox: scheduling %zu independent ready targets\n", ready_count);
        (void)cbox_parallel_for(ready_count, cbox_processor_count(),
                                build_target_batch_job, &batch);
        for (size_t index = 0U; index < ready_count; ++index) {
            if (!results[index]) {
                fprintf(stderr, "CBox: target '%s' failed; dependent targets will not be built\n",
                        project->targets[plan.target_indices[level_start + index]].name != NULL
                            ? project->targets[plan.target_indices[level_start + index]].name
                            : "(unnamed)");
                goto cleanup;
            }
        }
    }
    success = true;

cleanup:
    free(results);
    cbox_target_graph_free_plan(&plan);
    return success;
}

static bool run_task(const cbox_project *project, const char *name,
                     bool assembly, bool one_assembly, bool syntax_check,
                     bool freestanding) {
    const cbox_task *task;
    const cbox_profile *profile;
    const cbox_target *target = NULL;
    const cbox_platform_command *platform_command;
    const char *command;
    const char *shell;
    size_t i;
    bool needs_target_loop = false;

    if (project == NULL || name == NULL) {
        return false;
    }

    task = cbox_find_task(project, name);
    if (task == NULL) {
        return false;
    }
    if ((assembly || syntax_check || freestanding) && task->target == NULL) {
        fprintf(stderr, "CBox: compiler mode requires a task with a target\n");
        return false;
    }

    command = task->command;
    shell = task->shell != NULL ? task->shell : "native";
    platform_command = current_platform_command(task);
    if (platform_command != NULL && platform_command->command != NULL) {
        command = platform_command->command;
        if (platform_command->shell != NULL) {
            shell = platform_command->shell;
        }
    }

    profile = NULL;
    if (task->profile != NULL) {
        profile = cbox_find_profile(project, task->profile);
        if (profile == NULL) {
            fprintf(stderr, "CBox: task '%s' refers to unknown profile '%s'\n", name, task->profile);
            return false;
        }
    }

    if (task->target != NULL) {
        target = cbox_find_target(project, task->target);
        if (target == NULL) {
            fprintf(stderr, "CBox: task '%s' refers to unknown target '%s'\n", name, task->target);
            return false;
        }
        needs_target_loop = false;
    } else if (contains_target_placeholder(task->print) || contains_target_placeholder(command)) {
        needs_target_loop = true;
    }

    if (task->delete_pattern_count > 0U
        && !cbox_delete_globs((const char *const *)task->delete_patterns,
                              task->delete_pattern_count)) {
        return false;
    }

    if (needs_target_loop) {
        if (project->target_count == 0U) {
            fprintf(stderr, "CBox: task '%s' requires a target context, but no targets were defined\n", name);
            return false;
        }
        for (i = 0; i < project->target_count; ++i) {
            char *expanded = cbox_expand_template(project, task, profile, &project->targets[i],
                                                 task->print != NULL ? task->print : command);
            if (expanded == NULL) {
                fprintf(stderr, "CBox: failed to expand task '%s' for target '%s'\n", name, project->targets[i].name);
                return false;
            }
            printf("%s\n", expanded);
            free(expanded);
        }
        return true;
    }

    if (target != NULL) {
        if (freestanding) {
            cbox_terminal_init();
            (void)cbox_printf(CBOX_BOLD | CBOX_GREEN, "CBox: ");
            fprintf(stderr,
                    "--freestanding enables freestanding compiler/linker "
                    "options for the selected target graph\n");
        }
        bool built = syntax_check
            ? cbox_check_target(target, profile, project->compiler)
            : assembly
                ? cbox_build_target_assembly(target, profile, project->compiler,
                                             one_assembly)
                : cbox_build_target_graph(project, target, profile,
                                          project->compiler, freestanding);
        if (!built) {
            return false;
        }
        if (assembly) {
            printf("Generated assembly for task '%s'.\n", name);
            return true;
        }
        if (syntax_check) return true;
        if (freestanding) return true;
        char *expanded = cbox_expand_template(project, task, profile, target,
                                             task->print != NULL ? task->print : command);
        if (expanded == NULL) {
            fprintf(stderr, "CBox: failed to expand task '%s' for target '%s'\n", name, target->name);
            return false;
        }
        printf("%s\n", expanded);
        free(expanded);
        return true;
    }

    if (command != NULL && command[0] != '\0') {
        printf("Running: %s\n", command);
        return cbox_run_shell_command(shell, command);
    }

    if (task->print != NULL) {
        char *expanded = cbox_expand_template(project, task, profile, NULL, task->print);
        if (expanded == NULL) {
            fprintf(stderr, "CBox: failed to expand task '%s'\n", name);
            return false;
        }
        printf("%s\n", expanded);
        free(expanded);
    }
    return true;
}

bool cbox_run_task(const cbox_project *project, const char *name) {
    return run_task(project, name, false, false, false, false);
}

bool cbox_run_task_assembly(const cbox_project *project, const char *name,
                            bool one_assembly) {
    return run_task(project, name, true, one_assembly, false, false);
}

bool cbox_run_task_check(const cbox_project *project, const char *name) {
    return run_task(project, name, false, false, true, false);
}

bool cbox_run_task_freestanding(const cbox_project *project, const char *name) {
    return run_task(project, name, false, false, false, true);
}
