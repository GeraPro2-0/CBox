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

#ifndef CBOX_PROCESS_H
#define CBOX_PROCESS_H

#include <stdbool.h>

int cbox_run_process(const char *const arguments[]);
int cbox_run_shell_process(const char *command);
bool cbox_run_shell_command(const char *shell, const char *command);
bool cbox_program_exists(const char *program);

#endif