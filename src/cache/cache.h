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

#ifndef CBOX_CACHE_H
#define CBOX_CACHE_H

#include <stdbool.h>
#include <stddef.h>

typedef struct cbox_cache cbox_cache;

typedef struct {
    const char *output_path;
    const unsigned char *fingerprint;
} cbox_cache_check_request;

typedef enum {
    CBOX_CACHE_CHANGE_UNCACHED,
    CBOX_CACHE_CHANGE_SETTINGS,
    CBOX_CACHE_CHANGE_OUTPUT,
    CBOX_CACHE_CHANGE_DEPENDENCY
} cbox_cache_change_reason;

typedef void (*cbox_cache_change_callback)(const char *path,
                                          cbox_cache_change_reason reason,
                                          void *context);
typedef void (*cbox_cache_batch_change_callback)(
    size_t request_index, const char *path, cbox_cache_change_reason reason,
    void *context);

/* A "current" result also requires the output and every recorded dependency to match. */
cbox_cache *cbox_cache_open(const char *path);
bool cbox_cache_is_current(const cbox_cache *cache, const char *output_path,
                           const unsigned char fingerprint[32]);
bool cbox_cache_check(const cbox_cache *cache, const char *output_path,
                      const unsigned char fingerprint[32],
                      cbox_cache_change_callback on_change, void *context);
bool cbox_cache_check_batch(
    const cbox_cache *cache, const cbox_cache_check_request requests[],
    size_t request_count, bool results[],
    cbox_cache_batch_change_callback on_change, void *context);
bool cbox_cache_store(cbox_cache *cache, const char *output_path,
                      const unsigned char fingerprint[32],
                      const char *const dependencies[], size_t dependency_count);
bool cbox_cache_prune(cbox_cache *cache, const char *const keys[], size_t key_count);
bool cbox_cache_save(const cbox_cache *cache, const char *path);
void cbox_cache_free(cbox_cache *cache);
void cbox_sha256_buffer(const void *data, size_t length, unsigned char digest[32]);
bool cbox_sha256_file(const char *path, unsigned char digest[32]);

#endif
