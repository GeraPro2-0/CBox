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

#include "cache/cache.h"

#include "mybit.h"

#define ARENALIB_POOL_BLOCKS 1
#define ARENALIB_POOL_BLOCK_SIZE 65536
#include "arenalib.h"
#include "platform/threads.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <errno.h>
#include <sys/stat.h>
#endif

#define CBOX_SHA256_BLOCK_SIZE 64U
#define CBOX_SHA256_DIGEST_SIZE 32U
#define CBOX_CACHE_MAX_RECORDS 100000U
#define CBOX_CACHE_MAX_STRING (1024U * 1024U)
#define CBOX_CACHE_PARALLEL_HASH_THRESHOLD 16U

typedef struct {
    uint32_t state[8];
    uint64_t bit_count;
    unsigned char block[CBOX_SHA256_BLOCK_SIZE];
    size_t block_length;
} cbox_sha256_context;

typedef struct {
    char *key;
    unsigned char fingerprint[CBOX_SHA256_DIGEST_SIZE];
    unsigned char output_hash[CBOX_SHA256_DIGEST_SIZE];
    char **dependency_paths;
    unsigned char *dependency_hashes;
    size_t dependency_count;
} cbox_cache_entry;

typedef struct cbox_cache_fallback_allocation {
    void *memory;
    struct cbox_cache_fallback_allocation *next;
} cbox_cache_fallback_allocation;

struct cbox_cache {
    arenalib_arena_t arena;
    unsigned char arena_storage[65536];
    cbox_cache_entry *entries;
    size_t count;
    uint32_t *key_index;
    size_t key_index_capacity;
    cbox_cache_fallback_allocation *fallback_allocations;
    arenalib_size_t arena_failed_request;
};

typedef struct {
    cbox_cache_change_callback callback;
    void *context;
} cbox_single_change_context;

typedef struct {
    const char *const *paths;
    unsigned char *digests;
    unsigned char *valid;
} cbox_cache_hash_batch;

static uint64_t cbox_cache_key_hash(const char *key);

static void report_single_cache_change(size_t request_index, const char *path,
                                       cbox_cache_change_reason reason,
                                       void *context) {
    cbox_single_change_context *single = context;
    (void)request_index;
    single->callback(path, reason, single->context);
}

static uint32_t cbox_cache_batch_intern_path(const char *path,
                                             const char *paths[],
                                             size_t *path_count,
                                             uint32_t slots[],
                                             size_t slot_capacity) {
    size_t slot = (size_t)cbox_cache_key_hash(path) & (slot_capacity - 1U);

    while (slots[slot] != 0U) {
        uint32_t path_id = slots[slot] - 1U;
        if (strcmp(paths[path_id], path) == 0) return path_id;
        slot = (slot + 1U) & (slot_capacity - 1U);
    }
    if (*path_count >= UINT32_MAX - 1U) return UINT32_MAX;
    paths[*path_count] = path;
    slots[slot] = (uint32_t)(*path_count + 1U);
    return (uint32_t)(*path_count)++;
}

static uint32_t cbox_cache_batch_find_path(const char *path,
                                           const char *paths[],
                                           const uint32_t slots[],
                                           size_t slot_capacity) {
    size_t slot = (size_t)cbox_cache_key_hash(path) & (slot_capacity - 1U);

    while (slots[slot] != 0U) {
        uint32_t path_id = slots[slot] - 1U;
        if (strcmp(paths[path_id], path) == 0) return path_id;
        slot = (slot + 1U) & (slot_capacity - 1U);
    }
    return UINT32_MAX;
}

static bool cbox_cache_hash_batch_job(size_t index, void *context) {
    cbox_cache_hash_batch *batch = context;
    batch->valid[index] = cbox_sha256_file(batch->paths[index],
                                            batch->digests + index * 32U);
    return true;
}

static void cbox_cache_arena_oom(void *user_data,
                                 arenalib_size_t size_requested) {
    cbox_cache *cache = user_data;
    cache->arena_failed_request = size_requested;
}

static const uint32_t cbox_sha256_round_constants[64] = {
    0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U,
    0x3956c25bU, 0x59f111f1U, 0x923f82a4U, 0xab1c5ed5U,
    0xd807aa98U, 0x12835b01U, 0x243185beU, 0x550c7dc3U,
    0x72be5d74U, 0x80deb1feU, 0x9bdc06a7U, 0xc19bf174U,
    0xe49b69c1U, 0xefbe4786U, 0x0fc19dc6U, 0x240ca1ccU,
    0x2de92c6fU, 0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU,
    0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U,
    0xc6e00bf3U, 0xd5a79147U, 0x06ca6351U, 0x14292967U,
    0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU, 0x53380d13U,
    0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U,
    0xa2bfe8a1U, 0xa81a664bU, 0xc24b8b70U, 0xc76c51a3U,
    0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U,
    0x19a4c116U, 0x1e376c08U, 0x2748774cU, 0x34b0bcb5U,
    0x391c0cb3U, 0x4ed8aa4aU, 0x5b9cca4fU, 0x682e6ff3U,
    0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U,
    0x90befffaU, 0xa4506cebU, 0xbef9a3f7U, 0xc67178f2U
};

static uint32_t cbox_sha256_load_word(const unsigned char *bytes) {
    return mybit_load_be32(bytes);
}

static void cbox_sha256_transform(cbox_sha256_context *context,
                                  const unsigned char block[64]) {
    uint32_t words[64];
    uint32_t a, b, c, d, e, f, g, h;
    size_t index;

    for (index = 0U; index < 16U; ++index) {
        words[index] = cbox_sha256_load_word(block + index * 4U);
    }
    for (; index < 64U; ++index) {
        uint32_t x = words[index - 15U];
        uint32_t y = words[index - 2U];
        uint32_t s0 = mybit_rotr32(x, 7U) ^ mybit_rotr32(x, 18U) ^ (x >> 3U);
        uint32_t s1 = mybit_rotr32(y, 17U) ^ mybit_rotr32(y, 19U) ^ (y >> 10U);
        words[index] = words[index - 16U] + s0 + words[index - 7U] + s1;
    }

    a = context->state[0]; b = context->state[1];
    c = context->state[2]; d = context->state[3];
    e = context->state[4]; f = context->state[5];
    g = context->state[6]; h = context->state[7];
    for (index = 0U; index < 64U; ++index) {
        uint32_t sum1 = mybit_rotr32(e, 6U) ^ mybit_rotr32(e, 11U)
            ^ mybit_rotr32(e, 25U);
        uint32_t choice = (e & f) ^ (~e & g);
        uint32_t temporary1 = h + sum1 + choice
            + cbox_sha256_round_constants[index] + words[index];
        uint32_t sum0 = mybit_rotr32(a, 2U) ^ mybit_rotr32(a, 13U)
            ^ mybit_rotr32(a, 22U);
        uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
        uint32_t temporary2 = sum0 + majority;
        h = g; g = f; f = e; e = d + temporary1;
        d = c; c = b; b = a; a = temporary1 + temporary2;
    }
    context->state[0] += a; context->state[1] += b;
    context->state[2] += c; context->state[3] += d;
    context->state[4] += e; context->state[5] += f;
    context->state[6] += g; context->state[7] += h;
}

static void cbox_sha256_init(cbox_sha256_context *context) {
    static const uint32_t initial_state[8] = {
        0x6a09e667U, 0xbb67ae85U, 0x3c6ef372U, 0xa54ff53aU,
        0x510e527fU, 0x9b05688cU, 0x1f83d9abU, 0x5be0cd19U
    };
    memcpy(context->state, initial_state, sizeof(initial_state));
    context->bit_count = 0U;
    context->block_length = 0U;
}

static void cbox_sha256_update(cbox_sha256_context *context,
                               const unsigned char *data, size_t length) {
    while (length > 0U) {
        size_t available = CBOX_SHA256_BLOCK_SIZE - context->block_length;
        size_t amount = length < available ? length : available;
        memcpy(context->block + context->block_length, data, amount);
        context->block_length += amount;
        context->bit_count += (uint64_t)amount * 8U;
        data += amount;
        length -= amount;
        if (context->block_length == CBOX_SHA256_BLOCK_SIZE) {
            cbox_sha256_transform(context, context->block);
            context->block_length = 0U;
        }
    }
}

static void cbox_sha256_finish(cbox_sha256_context *context,
                               unsigned char digest[32]) {
    uint64_t bit_count = context->bit_count;
    size_t index;

    context->block[context->block_length++] = 0x80U;
    if (context->block_length > 56U) {
        memset(context->block + context->block_length, 0,
               CBOX_SHA256_BLOCK_SIZE - context->block_length);
        cbox_sha256_transform(context, context->block);
        context->block_length = 0U;
    }
    memset(context->block + context->block_length, 0, 56U - context->block_length);
    mybit_store_be64(context->block + 56U, bit_count);
    cbox_sha256_transform(context, context->block);
    for (index = 0U; index < 8U; ++index) {
        mybit_store_be32(digest + index * 4U, context->state[index]);
    }
}

void cbox_sha256_buffer(const void *data, size_t length, unsigned char digest[32]) {
    cbox_sha256_context context;
    cbox_sha256_init(&context);
    cbox_sha256_update(&context, data, length);
    cbox_sha256_finish(&context, digest);
}

bool cbox_sha256_file(const char *path, unsigned char digest[32]) {
    unsigned char buffer[8192];
    cbox_sha256_context context;
    FILE *file;
    size_t amount;

    if (path == NULL || digest == NULL) return false;
    file = fopen(path, "rb");
    if (file == NULL) return false;
    cbox_sha256_init(&context);
    while ((amount = fread(buffer, 1U, sizeof(buffer), file)) > 0U) {
        cbox_sha256_update(&context, buffer, amount);
    }
    {
        bool success = !ferror(file);
        if (fclose(file) != 0) success = false;
        if (!success) return false;
    }
    cbox_sha256_finish(&context, digest);
    return true;
}

static cbox_cache *cbox_cache_create(void) {
    cbox_cache *cache = calloc(1U, sizeof(*cache));
    if (cache == NULL) {
        fprintf(stderr, "CBox: out of memory allocating build cache\n");
        return NULL;
    }
    if (!arenalib_arena_init(&cache->arena, cache->arena_storage,
                             sizeof(cache->arena_storage), 0U)) {
        fprintf(stderr, "CBox: failed to initialize build-cache arena\n");
        free(cache);
        return NULL;
    }
    arenalib_arena_set_oom_callback(&cache->arena, cbox_cache_arena_oom, cache);
    return cache;
}

static void *cbox_cache_allocate(cbox_cache *cache, size_t size) {
    void *memory;
    cbox_cache_fallback_allocation *allocation;
    if (cache == NULL || size == 0U) return NULL;
    cache->arena_failed_request = 0U;
    memory = arenalib_arena_malloc(&cache->arena, size);
    if (memory != NULL) return memory;
    memory = malloc(size);
    if (memory == NULL) {
        if (cache->arena_failed_request != 0U) {
            fprintf(stderr,
                    "CBox: out of memory: %zu-byte cache arena request and %zu-byte heap fallback failed\n",
                    (size_t)cache->arena_failed_request, size);
        } else {
            fprintf(stderr,
                    "CBox: out of memory allocating %zu bytes for build cache\n",
                    size);
        }
        return NULL;
    }
    allocation = malloc(sizeof(*allocation));
    if (allocation == NULL) {
        free(memory);
        fprintf(stderr,
                "CBox: out of memory tracking %zu-byte build-cache allocation\n",
                size);
        return NULL;
    }
    allocation->memory = memory;
    allocation->next = cache->fallback_allocations;
    cache->fallback_allocations = allocation;
    return memory;
}

static void *cbox_cache_allocate_zeroed(cbox_cache *cache,
                                        size_t count, size_t size) {
    void *memory;
    if (count == 0U || size == 0U || count > SIZE_MAX / size) return NULL;
    memory = cbox_cache_allocate(cache, count * size);
    if (memory != NULL) memset(memory, 0, count * size);
    return memory;
}

static char *cbox_cache_duplicate(cbox_cache *cache, const char *text) {
    size_t length;
    char *copy;
    if (cache == NULL || text == NULL) return NULL;
    length = strlen(text);
    if (length > CBOX_CACHE_MAX_STRING) return NULL;
    copy = cbox_cache_allocate(cache, length + 1U);
    if (copy != NULL) memcpy(copy, text, length + 1U);
    return copy;
}

static bool cbox_cache_read_u32(FILE *file, uint32_t *value) {
    unsigned char bytes[4];
    if (fread(bytes, 1U, sizeof(bytes), file) != sizeof(bytes)) return false;
    /* The cache format is little-endian even when the host isn't. */
    *value = mybit_load_le32(bytes);
    return true;
}

static bool cbox_cache_write_u32(FILE *file, uint32_t value) {
    unsigned char bytes[4];
    mybit_store_le32(bytes, value);
    return fwrite(bytes, 1U, sizeof(bytes), file) == sizeof(bytes);
}

static bool cbox_cache_read_string(cbox_cache *cache, FILE *file, char **value) {
    uint32_t length;
    char *text;
    if (!cbox_cache_read_u32(file, &length) || length > CBOX_CACHE_MAX_STRING) {
        return false;
    }
    text = cbox_cache_allocate(cache, (size_t)length + 1U);
    if (text == NULL) return false;
    if (length == 0U || fread(text, 1U, length, file) != length
        || memchr(text, '\0', length) != NULL) {
        return false;
    }
    text[length] = '\0';
    *value = text;
    return true;
}

static bool cbox_cache_write_string(FILE *file, const char *value) {
    size_t length = strlen(value);
    return length != 0U && length <= CBOX_CACHE_MAX_STRING && length <= UINT32_MAX
        && cbox_cache_write_u32(file, (uint32_t)length)
        && fwrite(value, 1U, length, file) == length;
}

static uint64_t cbox_cache_key_hash(const char *key) {
    uint64_t hash = UINT64_C(14695981039346656037);
    while (*key != '\0') {
        hash ^= (unsigned char)*key++;
        hash *= UINT64_C(1099511628211);
    }
    return hash;
}

static size_t cbox_cache_index_slot(const cbox_cache_entry entries[],
                                    const uint32_t index[], size_t count,
                                    size_t capacity,
                                    const char *key, bool *found) {
    size_t slot = (size_t)cbox_cache_key_hash(key) & (capacity - 1U);
    while (index[slot] != 0U) {
        uint32_t entry_id = index[slot] - 1U;
        size_t entry_index = (size_t)entry_id;
        if (entry_index >= count) {
            *found = false;
            return capacity;
        }
        if (strcmp(entries[entry_index].key, key) == 0) {
            *found = true;
            return slot;
        }
        slot = (slot + 1U) & (capacity - 1U);
    }
    *found = false;
    return slot;
}

static size_t cbox_cache_find_entry(const cbox_cache *cache, const char *key) {
    bool found;
    size_t slot;
    if (cache->key_index_capacity == 0U) return SIZE_MAX;
    slot = cbox_cache_index_slot(cache->entries, cache->key_index,
                                 cache->count, cache->key_index_capacity,
                                 key, &found);
    if (slot == cache->key_index_capacity) return SIZE_MAX;
    return found ? cache->key_index[slot] - 1U : SIZE_MAX;
}

static bool cbox_cache_index_build(const cbox_cache_entry entries[], size_t count,
                                   size_t capacity, uint32_t **result) {
    uint32_t *index;
    size_t entry_index;
    if (result == NULL || (count != 0U && entries == NULL)
        || (capacity != 0U && (capacity & (capacity - 1U)) != 0U)) {
        return false;
    }
    *result = NULL;
    if (capacity == 0U) return count == 0U;
    if (count > capacity / 2U) return false;
    index = calloc(capacity, sizeof(*index));
    if (index == NULL) return false;
    for (entry_index = 0U; entry_index < count; ++entry_index) {
        bool found;
        size_t slot = cbox_cache_index_slot(entries, index, count,
                                            capacity,
                                            entries[entry_index].key, &found);
        if (slot == capacity || found || entry_index >= UINT32_MAX) {
            free(index);
            return false;
        }
        index[slot] = (uint32_t)(entry_index + 1U);
    }
    *result = index;
    return true;
}

static bool cbox_cache_index_reserve(cbox_cache *cache, size_t count) {
    size_t capacity = cache->key_index_capacity;
    uint32_t *index;
    if (count > UINT32_MAX) return false;
    if (capacity != 0U && count <= capacity / 2U) return true;
    capacity = capacity == 0U ? 16U : capacity;
    while (count > capacity / 2U) {
        if (capacity > SIZE_MAX / 2U) return false;
        capacity *= 2U;
    }
    if (!cbox_cache_index_build(cache->entries, cache->count, capacity, &index)) {
        return false;
    }
    free(cache->key_index);
    cache->key_index = index;
    cache->key_index_capacity = capacity;
    return true;
}

void cbox_cache_free(cbox_cache *cache) {
    cbox_cache_fallback_allocation *allocation;
    if (cache == NULL) return;
    free(cache->entries);
    free(cache->key_index);
    allocation = cache->fallback_allocations;
    while (allocation != NULL) {
        cbox_cache_fallback_allocation *next = allocation->next;
        free(allocation->memory);
        free(allocation);
        allocation = next;
    }
    arenalib_arena_destroy(&cache->arena);
    free(cache);
}

cbox_cache *cbox_cache_open(const char *path) {
    static const unsigned char magic[4] = {'C', 'B', 'O', 'X'};
    cbox_cache *cache = cbox_cache_create();
    FILE *file;
    unsigned char header[4];
    uint32_t count;
    uint32_t version;
    size_t index;
    bool valid = true;

    if (cache == NULL || path == NULL) {
        cbox_cache_free(cache);
        return NULL;
    }
    file = fopen(path, "rb");
    if (file == NULL) {
        if (errno != ENOENT) {
            fprintf(stderr, "CBox: cannot read build cache '%s'\n", path);
            cbox_cache_free(cache);
            return NULL;
        }
        return cache;
    }
    if (fread(header, 1U, sizeof(header), file) != sizeof(header)
        || memcmp(header, magic, sizeof(magic)) != 0
        || !cbox_cache_read_u32(file, &version) || version != 1U
        || !cbox_cache_read_u32(file, &count) || count > CBOX_CACHE_MAX_RECORDS) {
        (void)fclose(file);
        fprintf(stderr, "CBox: build cache '%s' has an unsupported or invalid format; rebuilding\n",
                path);
        return cache;
    }
    if (count != 0U) {
        cache->entries = calloc(count, sizeof(*cache->entries));
        if (cache->entries == NULL) {
            (void)fclose(file);
            cbox_cache_free(cache);
            return NULL;
        }
        if (!cbox_cache_index_reserve(cache, count)) {
            (void)fclose(file);
            cbox_cache_free(cache);
            return NULL;
        }
    }
    for (index = 0U; index < count; ++index) {
        cbox_cache_entry *entry = &cache->entries[index];
        uint32_t dependency_count;
        size_t dependency_index;
        if (!cbox_cache_read_string(cache, file, &entry->key)
            || fread(entry->fingerprint, 1U, CBOX_SHA256_DIGEST_SIZE, file)
                != CBOX_SHA256_DIGEST_SIZE
            || fread(entry->output_hash, 1U, CBOX_SHA256_DIGEST_SIZE, file)
                != CBOX_SHA256_DIGEST_SIZE
            || !cbox_cache_read_u32(file, &dependency_count)
            || dependency_count == 0U
            || dependency_count > CBOX_CACHE_MAX_RECORDS
            || cbox_cache_find_entry(cache, entry->key) != SIZE_MAX) {
            valid = false;
            break;
        }
        entry->dependency_paths = cbox_cache_allocate_zeroed(
            cache, dependency_count, sizeof(*entry->dependency_paths));
        entry->dependency_hashes = cbox_cache_allocate(
            cache, (size_t)dependency_count * CBOX_SHA256_DIGEST_SIZE);
        if (entry->dependency_paths == NULL || entry->dependency_hashes == NULL) {
            valid = false;
            break;
        }
        for (dependency_index = 0U; dependency_index < dependency_count;
             ++dependency_index) {
            if (!cbox_cache_read_string(cache, file,
                                        &entry->dependency_paths[dependency_index])) {
                valid = false;
                break;
            }
            ++entry->dependency_count;
            if (fread(entry->dependency_hashes
                          + dependency_index * CBOX_SHA256_DIGEST_SIZE,
                      1U, CBOX_SHA256_DIGEST_SIZE, file)
                    != CBOX_SHA256_DIGEST_SIZE) {
                valid = false;
                break;
            }
        }
        if (dependency_index != dependency_count) {
            valid = false;
            break;
        }
        {
            bool found;
            size_t slot = cbox_cache_index_slot(
                cache->entries, cache->key_index, cache->count,
                cache->key_index_capacity, entry->key, &found);
            if (slot == cache->key_index_capacity || found || index >= UINT32_MAX) {
                valid = false;
                break;
            }
            cache->key_index[slot] = (uint32_t)(index + 1U);
        }
        ++cache->count;
    }
    if (index != count || fgetc(file) != EOF || ferror(file)) valid = false;
    if (fclose(file) != 0) valid = false;
    if (!valid) {
        cbox_cache_free(cache);
        fprintf(stderr, "CBox: build cache '%s' is truncated or corrupt; rebuilding\n",
                path);
        return cbox_cache_create();
    }
    return cache;
}

bool cbox_cache_check_batch(
    const cbox_cache *cache, const cbox_cache_check_request requests[],
    size_t request_count, bool results[],
    cbox_cache_batch_change_callback on_change, void *context) {
    const cbox_cache_entry **entries = NULL;
    const char **paths = NULL;
    uint32_t *path_slots = NULL;
    unsigned char *digests = NULL;
    unsigned char *hash_valid = NULL;
    size_t path_capacity = 0U;
    size_t slot_capacity = 16U;
    size_t path_count = 0U;
    size_t reference_capacity = 0U;
    size_t request_index;
    bool success = false;
    cbox_cache_hash_batch hash_batch;

    if (cache == NULL || (requests == NULL && request_count != 0U)
        || (results == NULL && request_count != 0U)) {
        return false;
    }
    if (request_count == 0U) return true;
    if (request_count > SIZE_MAX / sizeof(*entries)
        || request_count > SIZE_MAX / sizeof(*paths)) {
        return false;
    }
    entries = calloc(request_count, sizeof(*entries));
    if (entries == NULL) goto cleanup;
    for (request_index = 0U; request_index < request_count; ++request_index) {
        size_t entry_index;
        if (requests[request_index].output_path == NULL) {
            results[request_index] = false;
            continue;
        }
        if (requests[request_index].fingerprint == NULL) goto cleanup;
        entry_index = cbox_cache_find_entry(cache,
                                            requests[request_index].output_path);
        if (entry_index == SIZE_MAX) {
            results[request_index] = false;
            continue;
        }
        entries[request_index] = &cache->entries[entry_index];
        if (memcmp(entries[request_index]->fingerprint,
                   requests[request_index].fingerprint,
                   CBOX_SHA256_DIGEST_SIZE) != 0) {
            continue;
        }
        if (reference_capacity == SIZE_MAX) goto cleanup;
        ++reference_capacity;
        if (entries[request_index]->dependency_count
            > SIZE_MAX - reference_capacity) {
            goto cleanup;
        }
        reference_capacity += entries[request_index]->dependency_count;
    }
    if (reference_capacity == 0U) {
        success = true;
        goto report_changes;
    }
    if (reference_capacity > UINT32_MAX - 1U
        || reference_capacity > SIZE_MAX / sizeof(*paths)
        || reference_capacity > SIZE_MAX / CBOX_SHA256_DIGEST_SIZE
        || reference_capacity > SIZE_MAX / 2U) {
        goto cleanup;
    }
    path_capacity = reference_capacity;
    while (slot_capacity < reference_capacity * 2U) {
        if (slot_capacity > SIZE_MAX / 2U) goto cleanup;
        slot_capacity *= 2U;
    }
    if (slot_capacity > SIZE_MAX / sizeof(*path_slots)) goto cleanup;
    paths = malloc(path_capacity * sizeof(*paths));
    path_slots = calloc(slot_capacity, sizeof(*path_slots));
    digests = malloc(path_capacity * CBOX_SHA256_DIGEST_SIZE);
    hash_valid = calloc(path_capacity, sizeof(*hash_valid));
    if (paths == NULL || path_slots == NULL || digests == NULL
        || hash_valid == NULL) {
        goto cleanup;
    }
    for (request_index = 0U; request_index < request_count; ++request_index) {
        const cbox_cache_entry *entry = entries[request_index];
        size_t dependency_index;
        if (entry == NULL
            || memcmp(entry->fingerprint, requests[request_index].fingerprint,
                      CBOX_SHA256_DIGEST_SIZE) != 0) {
            continue;
        }
        if (cbox_cache_batch_intern_path(requests[request_index].output_path,
                paths, &path_count, path_slots, slot_capacity) == UINT32_MAX) {
            goto cleanup;
        }
        for (dependency_index = 0U;
             dependency_index < entry->dependency_count;
             ++dependency_index) {
            if (cbox_cache_batch_intern_path(
                    entry->dependency_paths[dependency_index], paths,
                    &path_count, path_slots, slot_capacity) == UINT32_MAX) {
                goto cleanup;
            }
        }
    }
    hash_batch.paths = paths;
    hash_batch.digests = digests;
    hash_batch.valid = hash_valid;
    if (path_count >= CBOX_CACHE_PARALLEL_HASH_THRESHOLD) {
        if (!cbox_parallel_for(path_count, cbox_processor_count(),
                               cbox_cache_hash_batch_job, &hash_batch)) {
            goto cleanup;
        }
    } else {
        size_t path_index;
        for (path_index = 0U; path_index < path_count; ++path_index) {
            if (!cbox_cache_hash_batch_job(path_index, &hash_batch)) {
                goto cleanup;
            }
        }
    }

report_changes:
    for (request_index = 0U; request_index < request_count; ++request_index) {
        const cbox_cache_entry *entry = entries[request_index];
        bool current = true;
        uint32_t path_id;
        size_t dependency_index;

        if (requests[request_index].output_path == NULL) {
            results[request_index] = false;
            continue;
        }
        if (entry == NULL) {
            results[request_index] = false;
            if (on_change != NULL) {
                on_change(request_index, requests[request_index].output_path,
                          CBOX_CACHE_CHANGE_UNCACHED, context);
            }
            continue;
        }
        if (memcmp(entry->fingerprint, requests[request_index].fingerprint,
                   CBOX_SHA256_DIGEST_SIZE) != 0) {
            if (on_change != NULL) {
                on_change(request_index, requests[request_index].output_path,
                          CBOX_CACHE_CHANGE_SETTINGS, context);
            }
            results[request_index] = false;
            continue;
        }
        path_id = cbox_cache_batch_find_path(
            requests[request_index].output_path, paths, path_slots,
            slot_capacity);
        if (path_id == UINT32_MAX || hash_valid[path_id] == 0U
            || memcmp(digests + (size_t)path_id * CBOX_SHA256_DIGEST_SIZE,
                      entry->output_hash, CBOX_SHA256_DIGEST_SIZE) != 0) {
            current = false;
            if (on_change != NULL) {
                on_change(request_index, requests[request_index].output_path,
                          CBOX_CACHE_CHANGE_OUTPUT, context);
            }
        }
        for (dependency_index = 0U;
             dependency_index < entry->dependency_count;
             ++dependency_index) {
            path_id = cbox_cache_batch_find_path(
                entry->dependency_paths[dependency_index], paths,
                path_slots, slot_capacity);
            if (path_id == UINT32_MAX || hash_valid[path_id] == 0U
                || memcmp(digests + (size_t)path_id * CBOX_SHA256_DIGEST_SIZE,
                          entry->dependency_hashes
                              + dependency_index * CBOX_SHA256_DIGEST_SIZE,
                          CBOX_SHA256_DIGEST_SIZE) != 0) {
                current = false;
                if (on_change != NULL) {
                    on_change(request_index,
                              entry->dependency_paths[dependency_index],
                              CBOX_CACHE_CHANGE_DEPENDENCY, context);
                }
            }
        }
        results[request_index] = current && entry->dependency_count > 0U;
    }
    success = true;

cleanup:
    free(entries);
    free(paths);
    free(path_slots);
    free(digests);
    free(hash_valid);
    return success;
}

bool cbox_cache_check(const cbox_cache *cache, const char *output_path,
                      const unsigned char fingerprint[32],
                      cbox_cache_change_callback on_change, void *context) {
    cbox_single_change_context change_context;
    cbox_cache_check_request request;
    bool result = false;

    if (output_path == NULL || fingerprint == NULL) return false;
    request.output_path = output_path;
    request.fingerprint = fingerprint;
    change_context.callback = on_change;
    change_context.context = context;
    if (!cbox_cache_check_batch(cache, &request, 1U, &result,
                                on_change != NULL ? report_single_cache_change : NULL,
                                &change_context)) {
        return false;
    }
    return result;
}

bool cbox_cache_is_current(const cbox_cache *cache, const char *output_path,
                           const unsigned char fingerprint[32]) {
    return cbox_cache_check(cache, output_path, fingerprint, NULL, NULL);
}

bool cbox_cache_store(cbox_cache *cache, const char *output_path,
                      const unsigned char fingerprint[32],
                      const char *const dependencies[], size_t dependency_count) {
    cbox_cache_entry replacement = {0};
    size_t index;

    if (cache == NULL || output_path == NULL || output_path[0] == '\0'
        || fingerprint == NULL
        || dependencies == NULL || dependency_count == 0U
        || dependency_count > CBOX_CACHE_MAX_RECORDS) {
        return false;
    }
    replacement.key = cbox_cache_duplicate(cache, output_path);
    replacement.dependency_paths = cbox_cache_allocate_zeroed(
        cache, dependency_count, sizeof(*replacement.dependency_paths));
    replacement.dependency_hashes = cbox_cache_allocate(
        cache, dependency_count * CBOX_SHA256_DIGEST_SIZE);
    if (replacement.key == NULL || replacement.dependency_paths == NULL
        || replacement.dependency_hashes == NULL
        || !cbox_sha256_file(output_path, replacement.output_hash)) goto failure;
    memcpy(replacement.fingerprint, fingerprint, CBOX_SHA256_DIGEST_SIZE);
    for (index = 0U; index < dependency_count; ++index) {
        replacement.dependency_paths[index] =
            cbox_cache_duplicate(cache, dependencies[index]);
        if (replacement.dependency_paths[index] == NULL
            || !cbox_sha256_file(
                dependencies[index],
                replacement.dependency_hashes
                    + index * CBOX_SHA256_DIGEST_SIZE)) {
            goto failure;
        }
        ++replacement.dependency_count;
    }
    index = cbox_cache_find_entry(cache, output_path);
    if (index != SIZE_MAX) {
        cache->entries[index] = replacement;
        return true;
    }
    {
        cbox_cache_entry *entries;
        if (cache->count >= CBOX_CACHE_MAX_RECORDS
            || cache->count >= SIZE_MAX / sizeof(*entries)
            || !cbox_cache_index_reserve(cache, cache->count + 1U)) goto failure;
        entries = realloc(cache->entries, (cache->count + 1U) * sizeof(*entries));
        if (entries == NULL) goto failure;
        cache->entries = entries;
        cache->entries[cache->count++] = replacement;
        {
            bool found;
            size_t slot = cbox_cache_index_slot(
                cache->entries, cache->key_index, cache->count,
                cache->key_index_capacity, output_path, &found);
            if (slot == cache->key_index_capacity || found
                || cache->count > UINT32_MAX) {
                --cache->count;
                return false;
            }
            cache->key_index[slot] = (uint32_t)cache->count;
        }
    }
    return true;

failure:
    return false;
}

bool cbox_cache_prune(cbox_cache *cache, const char *const keys[], size_t key_count) {
    unsigned char *keep;
    cbox_cache_entry *entries = NULL;
    uint32_t *index = NULL;
    size_t kept_count = 0U;
    size_t key_index;
    size_t entry_index;
    size_t index_capacity = 16U;

    if (cache == NULL || (keys == NULL && key_count != 0U)) return false;
    if (key_count == 0U) {
        free(cache->entries);
        free(cache->key_index);
        cache->entries = NULL;
        cache->key_index = NULL;
        cache->key_index_capacity = 0U;
        cache->count = 0U;
        return true;
    }
    keep = calloc(cache->count, sizeof(*keep));
    if (keep == NULL && cache->count != 0U) return false;
    for (key_index = 0U; key_index < key_count; ++key_index) {
        size_t found;
        if (keys[key_index] == NULL) {
            free(keep);
            return false;
        }
        found = cbox_cache_find_entry(cache, keys[key_index]);
        if (found != SIZE_MAX) keep[found] = 1U;
    }
    for (entry_index = 0U; entry_index < cache->count; ++entry_index) {
        if (keep[entry_index] != 0U) ++kept_count;
    }
    if (kept_count != 0U) {
        while (kept_count > index_capacity / 2U) {
            if (index_capacity > SIZE_MAX / 2U) {
                free(keep);
                return false;
            }
            index_capacity *= 2U;
        }
        entries = malloc(kept_count * sizeof(*entries));
        if (entries == NULL) {
            free(keep);
            return false;
        }
        entry_index = 0U;
        for (key_index = 0U; key_index < cache->count; ++key_index) {
            if (keep[key_index] != 0U) {
                entries[entry_index++] = cache->entries[key_index];
            }
        }
        if (!cbox_cache_index_build(entries, kept_count, index_capacity, &index)) {
            free(keep);
            free(entries);
            return false;
        }
    }
    free(keep);
    free(cache->entries);
    free(cache->key_index);
    cache->entries = entries;
    cache->count = kept_count;
    cache->key_index = index;
    cache->key_index_capacity = kept_count == 0U ? 0U : index_capacity;
    return true;
}

bool cbox_cache_save(const cbox_cache *cache, const char *path) {
    static const unsigned char magic[4] = {'C', 'B', 'O', 'X'};
    size_t path_length;
    char *temporary_path;
    FILE *file;
    size_t index;
    bool success = false;

    if (cache == NULL || path == NULL || cache->count > UINT32_MAX) return false;
    path_length = strlen(path);
    if (path_length > SIZE_MAX - sizeof(".tmp")) return false;
    temporary_path = malloc(path_length + sizeof(".tmp"));
    if (temporary_path == NULL) return false;
    memcpy(temporary_path, path, path_length);
    memcpy(temporary_path + path_length, ".tmp", sizeof(".tmp"));
    /* Write beside the destination, then replace it; a half-written cache is useless. */
    file = fopen(temporary_path, "wb");
    if (file == NULL) {
        free(temporary_path);
        return false;
    }
    success = fwrite(magic, 1U, sizeof(magic), file) == sizeof(magic)
        && cbox_cache_write_u32(file, 1U)
        && cbox_cache_write_u32(file, (uint32_t)cache->count);
    for (index = 0U; success && index < cache->count; ++index) {
        const cbox_cache_entry *entry = &cache->entries[index];
        size_t dependency_index;
        success = cbox_cache_write_string(file, entry->key)
            && fwrite(entry->fingerprint, 1U, CBOX_SHA256_DIGEST_SIZE, file)
                == CBOX_SHA256_DIGEST_SIZE
            && fwrite(entry->output_hash, 1U, CBOX_SHA256_DIGEST_SIZE, file)
                == CBOX_SHA256_DIGEST_SIZE
            && entry->dependency_count <= UINT32_MAX
            && cbox_cache_write_u32(file, (uint32_t)entry->dependency_count);
        for (dependency_index = 0U; success
             && dependency_index < entry->dependency_count; ++dependency_index) {
            success = cbox_cache_write_string(
                    file, entry->dependency_paths[dependency_index])
                && fwrite(entry->dependency_hashes
                              + dependency_index * CBOX_SHA256_DIGEST_SIZE,
                          1U, CBOX_SHA256_DIGEST_SIZE, file)
                    == CBOX_SHA256_DIGEST_SIZE;
        }
    }
    if (fclose(file) != 0) success = false;
    if (success) {
#ifdef _WIN32
        success = MoveFileExA(temporary_path, path,
                              MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
        success = rename(temporary_path, path) == 0;
#endif
    }
    if (!success) (void)remove(temporary_path);
    free(temporary_path);
    return success;
}
