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

#ifndef CBOX_LINKER_H
#define CBOX_LINKER_H

#include <stdbool.h>
#include <stddef.h>

typedef enum {
    CBOX_LINKER_GNU,
    CBOX_LINKER_MSVC
} cbox_linker_kind;

#ifdef __cplusplus
extern "C" {
#endif

bool cbox_linker_resolve_libraries(cbox_linker_kind kind,
                                   char *const libraries[], size_t library_count,
                                   char ***arguments, size_t *argument_count);
void cbox_linker_free_arguments(char **arguments, size_t argument_count);
char *cbox_linker_target_output(cbox_linker_kind kind, const char *target_kind,
                                const char *configured_output,
                                const char *target_name);
char *cbox_linker_static_library_output(cbox_linker_kind kind,
                                        const char *target_name);
char *cbox_linker_shared_library_output(cbox_linker_kind kind,
                                        const char *target_name);
char *cbox_linker_shared_import_output(cbox_linker_kind kind,
                                       const char *shared_output);

#ifdef __cplusplus
}
#endif

#endif