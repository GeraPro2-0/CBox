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

#ifndef CBOX_H
#define CBOX_H

#include "tomlc17.h"

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char *shell;
    char *command;
} cbox_platform_command;

typedef struct {
    char *name;
    char *kind;
    char *target;
    char *profile;
    char *print;
    char *command;
    char *shell;
    cbox_platform_command windows_command;
    cbox_platform_command linux_command;
    cbox_platform_command macos_command;
    char **delete_patterns;
    size_t delete_pattern_count;
} cbox_task;

typedef struct {
    char *name;
    char *optimization;
    char *warnings;
    bool debug_symbols;
    char *print;
} cbox_profile;

typedef struct {
    char *name;
    char *kind;
    char *output;
    char *entry_point;
    char *source_root;
    bool freestanding;
    bool freestanding_configured;
    char **c_sources;
    size_t c_source_count;
    char **cpp_sources;
    size_t cpp_source_count;
    char **cpp_modules;
    size_t cpp_module_count;
    char **depends_on;
    size_t dependency_count;
    char **link_targets;
    size_t link_target_count;
    char **link_target_paths;
    size_t link_target_path_count;
    char **include_dirs;
    size_t include_dir_count;
    char **link_libraries;
    size_t link_library_count;
    char **compile_args;
    size_t compile_arg_count;
    char **link_args;
    size_t link_arg_count;
    char *c;
    char *cpp;
} cbox_target;

typedef struct {
    char *name;
    char *version;
    char *description;
    char **authors;
    size_t author_count;
    char *license;
    char *config_format_version;
    char *compiler;
    cbox_task *tasks;
    size_t task_count;
    cbox_profile *profiles;
    size_t profile_count;
    cbox_target *targets;
    size_t target_count;
} cbox_project;

void cbox_project_init(cbox_project *project);
void cbox_project_free(cbox_project *project);
const char *cbox_version(void);
bool cbox_load_project(const char *config_path, cbox_project *project);
const cbox_task *cbox_find_task(const cbox_project *project, const char *name);
const cbox_profile *cbox_find_profile(const cbox_project *project, const char *name);
const cbox_target *cbox_find_target(const cbox_project *project, const char *name);
char *cbox_expand_template(const cbox_project *project, const cbox_task *task,
                          const cbox_profile *profile, const cbox_target *target,
                          const char *input);
void cbox_print_usage(void);
void cbox_print_project_summary(const cbox_project *project);
void cbox_print_task_list(const cbox_project *project);
bool cbox_run_task(const cbox_project *project, const char *name);
bool cbox_run_task_assembly(const cbox_project *project, const char *name,
							bool one_assembly);
bool cbox_run_task_check(const cbox_project *project, const char *name);
bool cbox_run_task_freestanding(const cbox_project *project, const char *name);

#ifdef __cplusplus
}
#endif

#endif /* CBOX_H */

/* Un día la maestra dijo "Tarea para la casa"...







































































































































































































y Tarea se fue.
JAJAJAJAAJJALKJASKJASJASKJAJS */
