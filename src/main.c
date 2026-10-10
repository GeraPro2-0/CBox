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
#include "util/terminal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool is_flag(const char *arg, const char *name) {
    return arg != NULL && strcmp(arg, name) == 0;
}

static bool starts_with(const char *text, const char *prefix) {
    size_t prefix_len = strlen(prefix);
    return text != NULL && strncmp(text, prefix, prefix_len) == 0;
}

int main(int argc, char **argv) {
    cbox_project project = {0};
    const char *task_name = NULL;
    bool assembly = false;
    bool one_assembly = false;
    bool syntax_check = false;
    bool freestanding = false;
    int i;

    // Keep the version command independent of a project manifest.
    if (argc == 2 && (is_flag(argv[1], "--version")
                      || is_flag(argv[1], "-V"))) {
        if (printf("CBox %s\n", cbox_version()) < 0 || fflush(stdout) == EOF) {
            return 1;
        }
        return 0;
    }

    cbox_terminal_init();
    (void)cbox_printf(CBOX_BOLD | CBOX_CYAN,
                      "CBox: ");
    (void)cbox_printf(CBOX_BOLD,
                      "The TOML-based build system for C/C++\n\n");

    cbox_project_init(&project);

    // Help and --list come from the manifest too, so loading it is not optional here.
    if (!cbox_load_project("CBox.toml", &project)) {
        (void)cbox_terminal_printf(stderr, CBOX_TERMINAL_RED,
                                   "failed to load CBox.toml\n");
        cbox_project_free(&project);
        return 1;
    }

    if (argc == 1) {
        cbox_print_project_summary(&project);
        cbox_project_free(&project);
        return 0;
    }

    // Flags can sit on either side of the task name; decide what to run after parsing all of them.
    for (i = 1; i < argc; ++i) {
        const char *arg = argv[i];

        if (is_flag(arg, "--help") || is_flag(arg, "-h")) {
            cbox_print_usage();
            cbox_project_free(&project);
            return 0;
        }

        if (is_flag(arg, "--list") || is_flag(arg, "-l")) {
            cbox_print_task_list(&project);
            cbox_project_free(&project);
            return 0;
        }

        if (is_flag(arg, "--asm")) {
            assembly = true;
            continue;
        }
        if (is_flag(arg, "--one-asm")) {
            one_assembly = true;
            continue;
        }
        if (is_flag(arg, "--check")) {
            syntax_check = true;
            continue;
        }
        if (is_flag(arg, "--freestanding")) {
            freestanding = true;
            continue;
        }

        // A misspelled --option should not quietly turn into a mystery task name.
        if (starts_with(arg, "--")) {
            (void)cbox_terminal_printf(stderr, CBOX_TERMINAL_RED,
                "built-in commands use --; project tasks are invoked as positional commands, e.g. cbox release\n");
            cbox_project_free(&project);
            return 1;
        }

        if (cbox_find_task(&project, arg) != NULL) {
            if (task_name != NULL) {
                (void)cbox_terminal_printf(stderr, CBOX_TERMINAL_RED,
                    "specify only one project task per invocation\n");
                cbox_project_free(&project);
                return 1;
            }
            task_name = arg;
        } else {
            (void)cbox_terminal_printf(stderr, CBOX_TERMINAL_RED,
                "no matching task named '%s'. Try: cbox --help\n", arg);
            cbox_project_free(&project);
            return 1;
        }
    }

    // These modes don't compose; guessing what their combination means would be worse.
    if ((assembly && one_assembly)
        || (syntax_check && (assembly || one_assembly))
        || (freestanding && (syntax_check || assembly || one_assembly))) {
        (void)cbox_terminal_printf(stderr, CBOX_TERMINAL_RED,
            "--check, --asm, --one-asm, and --freestanding are mutually exclusive\n");
        cbox_project_free(&project);
        return 1;
    }
    if (task_name != NULL) {
        // Argument order is settled now; choose exactly one build path.
        bool success = freestanding
            ? cbox_run_task_freestanding(&project, task_name)
            : syntax_check
                ? cbox_run_task_check(&project, task_name)
                : assembly || one_assembly
                    ? cbox_run_task_assembly(&project, task_name, one_assembly)
                    : cbox_run_task(&project, task_name);
        cbox_project_free(&project);
        return success ? 0 : 1;
    }

    // Llegamos hasta acá sin tarea; mejor fallar claro que fingir que hubo trabajo.
    (void)cbox_terminal_printf(stderr, CBOX_TERMINAL_RED,
                               "no matching task found. Try: cbox --help\n");
    cbox_project_free(&project);
    return 1;
}