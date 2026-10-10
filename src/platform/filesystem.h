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

#ifndef CBOX_FILESYSTEM_H
#define CBOX_FILESYSTEM_H

#include <stdbool.h>
#include <stddef.h>

typedef struct {
    char **items;
    size_t count;
} cbox_path_list;

/* The caller owns the expanded paths and releases them with cbox_path_list_free. */
bool cbox_expand_globs(const char *const patterns[], size_t pattern_count,
                       cbox_path_list *paths);
char *cbox_path_identity(const char *path);
bool cbox_path_identity_equal(const char *left, const char *right);
bool cbox_delete_globs(const char *const patterns[], size_t pattern_count);
void cbox_path_list_free(cbox_path_list *paths);
bool cbox_create_directory(const char *path);
bool cbox_create_directories(const char *path);

#endif