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

#ifndef CBOX_THREADS_H
#define CBOX_THREADS_H

#include <stdbool.h>
#include <stddef.h>

typedef bool (*cbox_parallel_job_fn)(size_t index, void *context);
typedef struct cbox_mutex cbox_mutex;

/* Jobs receive stable indices; false marks that job as failed, not "cancel the rest." */
size_t cbox_processor_count(void);
cbox_mutex *cbox_mutex_create(void);
void cbox_mutex_destroy(cbox_mutex *mutex);
void cbox_mutex_lock(cbox_mutex *mutex);
void cbox_mutex_unlock(cbox_mutex *mutex);
bool cbox_parallel_for(size_t count, size_t worker_count,
                       cbox_parallel_job_fn job, void *context);

#endif
