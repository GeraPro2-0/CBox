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

#define _POSIX_C_SOURCE 200809L

#include "cache/cache.h"
#include "platform/filesystem.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#include <direct.h>
#include <windows.h>
#include <process.h>
#define cbox_benchmark_process_id _getpid
#define cbox_benchmark_remove_directory _rmdir
#else
#include <sys/stat.h>
#include <unistd.h>
#define cbox_benchmark_process_id getpid
#define cbox_benchmark_remove_directory rmdir
#endif

#define CBOX_BENCHMARK_DEPENDENCIES 8U
#define CBOX_BENCHMARK_SHARED_FILES 16U
#define CBOX_BENCHMARK_ITERATIONS 8U

static double benchmark_time_seconds(void) {
#ifdef _WIN32
    LARGE_INTEGER counter;
    LARGE_INTEGER frequency;
    if (!QueryPerformanceFrequency(&frequency)
        || !QueryPerformanceCounter(&counter)) {
        return -1.0;
    }
    return (double)counter.QuadPart / (double)frequency.QuadPart;
#else
    struct timespec value;
    if (clock_gettime(CLOCK_MONOTONIC, &value) != 0) return -1.0;
    return (double)value.tv_sec + (double)value.tv_nsec / 1000000000.0;
#endif
}

static bool write_sized_file(const char *path, size_t size,
                             unsigned char seed) {
    unsigned char buffer[4096];
    FILE *file = fopen(path, "wb");
    size_t index;
    size_t remaining = size;
    bool success = file != NULL;

    if (!success) return false;
    for (index = 0U; index < sizeof(buffer); ++index) {
        buffer[index] = (unsigned char)(seed + index * 31U);
    }
    while (remaining > 0U) {
        size_t amount = remaining < sizeof(buffer) ? remaining : sizeof(buffer);
        if (fwrite(buffer, 1U, amount, file) != amount) {
            success = false;
            break;
        }
        remaining -= amount;
    }
    if (fclose(file) != 0) success = false;
    return success;
}

static void cleanup_benchmark_files(const char *root, size_t entry_count) {
    size_t index;
    char path[256];

    for (index = 0U; index < entry_count; ++index) {
        (void)snprintf(path, sizeof(path), "%s/output-%zu.bin", root, index);
        (void)remove(path);
    }
    for (index = 0U; index < CBOX_BENCHMARK_SHARED_FILES; ++index) {
        (void)snprintf(path, sizeof(path), "%s/shared-%zu.h", root, index);
        (void)remove(path);
    }
    (void)snprintf(path, sizeof(path), "%s/cache.bin", root);
    (void)remove(path);
    (void)cbox_benchmark_remove_directory(root);
}

static bool benchmark_entry_count(size_t entry_count, unsigned long process_id) {
    static const size_t output_size = 4096U;
    static const size_t shared_file_size = 16384U;
    char root[128];
    char cache_path[160];
    char (*output_paths)[256] = NULL;
    char (*shared_paths)[256] = NULL;
    const char *dependencies[CBOX_BENCHMARK_DEPENDENCIES];
    cbox_cache_check_request *requests = NULL;
    bool *results = NULL;
    cbox_cache *cache = NULL;
    unsigned char fingerprint[32];
    size_t index;
    size_t iteration;
    double scalar_start;
    double scalar_seconds;
    double batch_start;
    double batch_seconds;
    size_t unique_paths;
    bool success = false;

    (void)snprintf(root, sizeof(root), "tests/.cbox-cache-bench-%lu-%zu",
                   process_id, entry_count);
    (void)snprintf(cache_path, sizeof(cache_path), "%s/cache.bin", root);
    if (entry_count > SIZE_MAX / sizeof(*output_paths)
        || entry_count > SIZE_MAX / sizeof(*requests)
        || entry_count > SIZE_MAX / sizeof(*results)) {
        return false;
    }
    output_paths = calloc(entry_count, sizeof(*output_paths));
    shared_paths = calloc(CBOX_BENCHMARK_SHARED_FILES, sizeof(*shared_paths));
    requests = calloc(entry_count, sizeof(*requests));
    results = calloc(entry_count, sizeof(*results));
    if (output_paths == NULL || shared_paths == NULL || requests == NULL
        || results == NULL || !cbox_create_directories(root)) {
        goto cleanup;
    }
    for (index = 0U; index < CBOX_BENCHMARK_SHARED_FILES; ++index) {
        (void)snprintf(shared_paths[index], sizeof(shared_paths[index]),
                       "%s/shared-%zu.h", root, index);
        if (!write_sized_file(shared_paths[index], shared_file_size,
                              (unsigned char)index)) {
            goto cleanup;
        }
    }
    cbox_sha256_buffer("cache benchmark settings",
                       sizeof("cache benchmark settings") - 1U, fingerprint);
    cache = cbox_cache_open(cache_path);
    if (cache == NULL) goto cleanup;
    for (index = 0U; index < entry_count; ++index) {
        size_t dependency_index;
        (void)snprintf(output_paths[index], sizeof(output_paths[index]),
                       "%s/output-%zu.bin", root, index);
        if (!write_sized_file(output_paths[index], output_size,
                              (unsigned char)(index * 7U))) {
            goto cleanup;
        }
        for (dependency_index = 0U;
             dependency_index < CBOX_BENCHMARK_DEPENDENCIES;
             ++dependency_index) {
            dependencies[dependency_index] = shared_paths[
                (index + dependency_index) % CBOX_BENCHMARK_SHARED_FILES];
        }
        if (!cbox_cache_store(cache, output_paths[index], fingerprint,
                              dependencies, CBOX_BENCHMARK_DEPENDENCIES)) {
            goto cleanup;
        }
        requests[index].output_path = output_paths[index];
        requests[index].fingerprint = fingerprint;
    }

    for (index = 0U; index < entry_count; ++index) {
        if (!cbox_cache_check(cache, requests[index].output_path,
                              requests[index].fingerprint, NULL, NULL)) {
            goto cleanup;
        }
    }
    if (!cbox_cache_check_batch(cache, requests, entry_count, results,
                                NULL, NULL)) {
        goto cleanup;
    }
    for (index = 0U; index < entry_count; ++index) {
        if (!results[index]) goto cleanup;
    }

    unique_paths = entry_count + CBOX_BENCHMARK_SHARED_FILES;
    scalar_start = benchmark_time_seconds();
    if (scalar_start < 0.0) goto cleanup;
    for (iteration = 0U; iteration < CBOX_BENCHMARK_ITERATIONS; ++iteration) {
        for (index = 0U; index < entry_count; ++index) {
            if (!cbox_cache_check(cache, requests[index].output_path,
                                  requests[index].fingerprint, NULL, NULL)) {
                goto cleanup;
            }
        }
    }
    scalar_seconds = benchmark_time_seconds() - scalar_start;

    batch_start = benchmark_time_seconds();
    if (batch_start < 0.0) goto cleanup;
    for (iteration = 0U; iteration < CBOX_BENCHMARK_ITERATIONS; ++iteration) {
        if (!cbox_cache_check_batch(cache, requests, entry_count, results,
                                    NULL, NULL)) {
            goto cleanup;
        }
        for (index = 0U; index < entry_count; ++index) {
            if (!results[index]) goto cleanup;
        }
    }
    batch_seconds = benchmark_time_seconds() - batch_start;
    if (scalar_seconds <= 0.0 || batch_seconds <= 0.0) goto cleanup;

    printf("entries=%zu unique_files=%zu repeats=%zu scalar_ms=%.3f "
           "batched_ms=%.3f speedup=%.2fx scalar_file_hashes=%zu "
           "batched_file_hashes=%zu\n",
           entry_count, unique_paths, (size_t)CBOX_BENCHMARK_ITERATIONS,
           scalar_seconds * 1000.0, batch_seconds * 1000.0,
           scalar_seconds / batch_seconds,
           CBOX_BENCHMARK_ITERATIONS * entry_count
               * (CBOX_BENCHMARK_DEPENDENCIES + 1U),
           CBOX_BENCHMARK_ITERATIONS * unique_paths);
    success = true;

cleanup:
    cbox_cache_free(cache);
    cleanup_benchmark_files(root, entry_count);
    free(output_paths);
    free(shared_paths);
    free(requests);
    free(results);
    return success;
}

int main(void) {
    unsigned long process_id = (unsigned long)cbox_benchmark_process_id();

    printf("Warm incremental cache validation benchmark "
           "(scalar per-entry checks vs deduplicated batch checks)\n");
    if (!benchmark_entry_count(16U, process_id)
        || !benchmark_entry_count(128U, process_id)) {
        fprintf(stderr, "CBox: cache benchmark failed\n");
        return 1;
    }
    return 0;
}
