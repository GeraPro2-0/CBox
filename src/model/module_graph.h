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

#ifndef CBOX_MODEL_MODULE_GRAPH_H
#define CBOX_MODEL_MODULE_GRAPH_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char *name;
    const char *const *imports;
    size_t import_count;
} cbox_module_graph_node;

typedef struct {
    size_t *module_indices;
    /* Half-open ranges in module_indices; imports always appear in earlier levels. */
    size_t *level_offsets;
    size_t module_count;
    size_t level_count;
} cbox_module_graph_plan;

bool cbox_module_graph_create_plan(const cbox_module_graph_node nodes[],
                                   size_t node_count,
                                   cbox_module_graph_plan *plan,
                                   char *error_message,
                                   size_t error_capacity);
void cbox_module_graph_free_plan(cbox_module_graph_plan *plan);

#ifdef __cplusplus
}
#endif

#endif
