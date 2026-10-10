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

#include "toolchain/toolchain.h"
#include "cache/cache.h"
#include "model/module_graph.h"
#include "platform/filesystem.h"
#include "platform/threads.h"
#include "util/terminal.h"
#include "arenalib.h"

#include <ctype.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    char **items;
    char **identities;
    size_t count;
    arenalib_arena_t arena;
    unsigned char arena_storage[4096];
    bool arena_initialized;
} cbox_dependency_list;

typedef struct {
    bool cpp;
    bool needs_compile;
    const char *source;
    const char *object;
    unsigned char fingerprint[32];
} cbox_compile_job;

typedef struct {
    const cbox_toolchain_driver *driver;
    const char *toolchain_version;
    const cbox_target *target;
    const cbox_profile *profile;
    const char *const *module_names;
    const char *const *module_outputs;
    size_t module_count;
    const char *module_mapper;
    cbox_cache *cache;
    cbox_mutex *cache_mutex;
    cbox_compile_job *jobs;
} cbox_compile_batch;

typedef struct {
    cbox_compile_job *const *jobs;
    char **dependency_path_identities;
    size_t dependency_path_count;
    size_t dependency_path_capacity;
} cbox_cache_change_report;

#define CBOX_PARALLEL_THRESHOLD_BYTES (50U * 1024U)

static char *append_suffix(const char *path, const char *suffix) {
    size_t path_length = strlen(path);
    size_t suffix_length = strlen(suffix);
    char *result;
    if (path_length > SIZE_MAX - suffix_length - 1U) return NULL;
    result = malloc(path_length + suffix_length + 1U);
    if (result == NULL) return NULL;
    memcpy(result, path, path_length);
    memcpy(result + path_length, suffix, suffix_length + 1U);
    return result;
}

static bool dependency_list_add(cbox_dependency_list *dependencies,
                                const char *path) {
    char **items;
    char **identities;
    size_t index;
    size_t length = strlen(path);
    char *copy;
    char *identity;

    if (length == 0U) return true;
    identity = cbox_path_identity(path);
    if (identity == NULL) {
        fprintf(stderr, "CBox: failed to normalize dependency path '%s'\n", path);
        return false;
    }
    for (index = 0U; index < dependencies->count; ++index) {
        if (cbox_path_identity_equal(dependencies->identities[index], identity)) {
            free(identity);
            return true;
        }
    }
    if (!dependencies->arena_initialized
        || length == SIZE_MAX
        || dependencies->count >= SIZE_MAX / sizeof(*items)) {
        free(identity);
        return false;
    }
    copy = arenalib_arena_malloc(&dependencies->arena, length + 1U);
    if (copy == NULL) {
        free(identity);
        return false;
    }
    memcpy(copy, path, length + 1U);
    items = realloc(dependencies->items,
                    (dependencies->count + 1U) * sizeof(*items));
    if (items == NULL) {
        free(identity);
        return false;
    }
    dependencies->items = items;
    identities = realloc(dependencies->identities,
                         (dependencies->count + 1U) * sizeof(*identities));
    if (identities == NULL) {
        free(identity);
        return false;
    }
    dependencies->identities = identities;
    dependencies->items[dependencies->count++] = copy;
    dependencies->identities[dependencies->count - 1U] = identity;
    return true;
}

static void dependency_list_free(cbox_dependency_list *dependencies) {
    size_t index;
    free(dependencies->items);
    for (index = 0U; index < dependencies->count; ++index) {
        free(dependencies->identities[index]);
    }
    free(dependencies->identities);
    if (dependencies->arena_initialized) {
        arenalib_arena_destroy(&dependencies->arena);
    }
    memset(dependencies, 0, sizeof(*dependencies));
}

static void dependency_list_arena_oom(void *user_data,
                                      arenalib_size_t size_requested) {
    (void)user_data;
    fprintf(stderr,
            "CBox: out of memory allocating %zu bytes for compiler dependencies\n",
            (size_t)size_requested);
}

static bool parse_make_dependencies(const char *path,
                                    cbox_dependency_list *dependencies) {
    FILE *file = fopen(path, "rb");
    char *token = NULL;
    size_t length = 0U;
    size_t capacity = 0U;
    bool after_colon = false;
    bool escaped = false;
    bool continued_line = false;
    bool success = file != NULL;
    int character;

    if (file == NULL) return false;
    while ((character = fgetc(file)) != EOF) {
        if (!after_colon) {
            if (character == ':') after_colon = true;
            continue;
        }
        if (continued_line) {
            continued_line = false;
            if (character == '\n') continue;
        }
        if (escaped) {
            escaped = false;
            if (character == '\n') continue;
            if (character == '\r') {
                continued_line = true;
                continue;
            }
            if (character != ' ' && character != '\\' && character != '#'
                && character != '$' && character != ':') {
                if (length + 1U >= capacity) {
                    size_t new_capacity = capacity == 0U ? 128U : capacity * 2U;
                    char *resized;
                    if (new_capacity < capacity) {
                        success = false;
                        break;
                    }
                    resized = realloc(token, new_capacity);
                    if (resized == NULL) {
                        success = false;
                        break;
                    }
                    token = resized;
                    capacity = new_capacity;
                }
                token[length++] = '\\';
            }
        } else if (character == '\\') {
            escaped = true;
            continue;
        } else if (character == '\n') {
            if (length > 0U) {
                token[length] = '\0';
                if (!dependency_list_add(dependencies, token)) {
                    success = false;
                }
            }
            break;
        } else if (isspace((unsigned char)character)) {
            if (length > 0U) {
                token[length] = '\0';
                if (!dependency_list_add(dependencies, token)) {
                    success = false;
                    break;
                }
                length = 0U;
            }
            continue;
        }
        if (length + 1U >= capacity) {
            size_t new_capacity = capacity == 0U ? 128U : capacity * 2U;
            char *resized;
            if (new_capacity < capacity) {
                success = false;
                break;
            }
            resized = realloc(token, new_capacity);
            if (resized == NULL) {
                success = false;
                break;
            }
            token = resized;
            capacity = new_capacity;
        }
        token[length++] = (char)character;
    }
    if (length > 0U && success) {
        token[length] = '\0';
        success = dependency_list_add(dependencies, token);
    }
    if (ferror(file)) success = false;
    if (fclose(file) != 0) success = false;
    free(token);
    return success && after_colon;
}

static bool json_string_value(const char **cursor, char **value) {
    const char *position = *cursor;
    char *result;
    size_t capacity = strlen(position) + 1U;
    size_t length = 0U;

    if (*position != '"') return false;
    result = malloc(capacity);
    if (result == NULL) return false;
    ++position;
    while (*position != '\0' && *position != '"') {
        unsigned char character = (unsigned char)*position++;
        if (character == '\\') {
            character = (unsigned char)*position++;
            if (character == '\0') {
                free(result);
                return false;
            }
            if (character == 'u') {
                size_t digit;
                unsigned codepoint = 0U;
                for (digit = 0U; digit < 4U; ++digit) {
                    unsigned char hex = (unsigned char)*position++;
                    if (hex >= '0' && hex <= '9') codepoint = codepoint * 16U + hex - '0';
                    else if (hex >= 'a' && hex <= 'f') codepoint = codepoint * 16U + hex - 'a' + 10U;
                    else if (hex >= 'A' && hex <= 'F') codepoint = codepoint * 16U + hex - 'A' + 10U;
                    else {
                        free(result);
                        return false;
                    }
                }
                if (codepoint >= 0xd800U && codepoint <= 0xdbffU) {
                    unsigned low = 0U;
                    if (position[0] != '\\' || position[1] != 'u') {
                        free(result);
                        return false;
                    }
                    position += 2;
                    for (digit = 0U; digit < 4U; ++digit) {
                        unsigned char hex = (unsigned char)*position++;
                        if (hex >= '0' && hex <= '9') low = low * 16U + hex - '0';
                        else if (hex >= 'a' && hex <= 'f') low = low * 16U + hex - 'a' + 10U;
                        else if (hex >= 'A' && hex <= 'F') low = low * 16U + hex - 'A' + 10U;
                        else {
                            free(result);
                            return false;
                        }
                    }
                    if (low < 0xdc00U || low > 0xdfffU) {
                        free(result);
                        return false;
                    }
                    codepoint = 0x10000U + ((codepoint - 0xd800U) << 10U)
                        + (low - 0xdc00U);
                }
                if (codepoint <= 0x7fU) {
                    result[length++] = (char)codepoint;
                } else if (codepoint <= 0x7ffU) {
                    result[length++] = (char)(0xc0U | (codepoint >> 6U));
                    result[length++] = (char)(0x80U | (codepoint & 0x3fU));
                } else if (codepoint <= 0xffffU) {
                    result[length++] = (char)(0xe0U | (codepoint >> 12U));
                    result[length++] = (char)(0x80U | ((codepoint >> 6U) & 0x3fU));
                    result[length++] = (char)(0x80U | (codepoint & 0x3fU));
                } else {
                    result[length++] = (char)(0xf0U | (codepoint >> 18U));
                    result[length++] = (char)(0x80U | ((codepoint >> 12U) & 0x3fU));
                    result[length++] = (char)(0x80U | ((codepoint >> 6U) & 0x3fU));
                    result[length++] = (char)(0x80U | (codepoint & 0x3fU));
                }
                continue;
            } else if (character == '"' || character == '\\' || character == '/') {
            } else if (character == 'b') {
                character = '\b';
            } else if (character == 'f') {
                character = '\f';
            } else if (character == 'n') {
                character = '\n';
            } else if (character == 'r') {
                character = '\r';
            } else if (character == 't') {
                character = '\t';
            } else {
                free(result);
                return false;
            }
        }
        result[length++] = (char)character;
    }
    if (*position != '"') {
        free(result);
        return false;
    }
    result[length] = '\0';
    *cursor = position + 1;
    *value = result;
    return true;
}

static bool parse_msvc_dependencies(const char *path,
                                   cbox_dependency_list *dependencies) {
    static const char source_key[] = "\"Source\"";
    static const char includes_key[] = "\"Includes\"";
    FILE *file = fopen(path, "rb");
    char *contents;
    long file_length;
    bool success = file != NULL;

    if (file == NULL) return false;
    if (fseek(file, 0L, SEEK_END) != 0 || (file_length = ftell(file)) < 0
        || fseek(file, 0L, SEEK_SET) != 0) {
        fclose(file);
        return false;
    }
    contents = malloc((size_t)file_length + 1U);
    if (contents == NULL) {
        fclose(file);
        return false;
    }
    success = fread(contents, 1U, (size_t)file_length, file) == (size_t)file_length;
    if (fclose(file) != 0) success = false;
    if (!success) {
        free(contents);
        return false;
    }
    contents[file_length] = '\0';
    {
        const char *cursor = strstr(contents, source_key);
        char *value = NULL;
        if (cursor == NULL) {
            free(contents);
            return false;
        }
        cursor += sizeof(source_key) - 1U;
        while (isspace((unsigned char)*cursor)) ++cursor;
        if (*cursor++ != ':') {
            free(contents);
            return false;
        }
        while (isspace((unsigned char)*cursor)) ++cursor;
        if (!json_string_value(&cursor, &value)
            || !dependency_list_add(dependencies, value)) {
            free(value);
            free(contents);
            return false;
        }
        free(value);
    }
    {
        const char *cursor = strstr(contents, includes_key);
        if (cursor == NULL) {
            free(contents);
            return false;
        }
        cursor += sizeof(includes_key) - 1U;
        while (isspace((unsigned char)*cursor)) ++cursor;
        if (*cursor++ != ':') {
            free(contents);
            return false;
        }
        while (isspace((unsigned char)*cursor)) ++cursor;
        if (*cursor++ != '[') {
            free(contents);
            return false;
        }
        for (;;) {
            char *value = NULL;
            while (isspace((unsigned char)*cursor) || *cursor == ',') ++cursor;
            if (*cursor == ']') break;
            if (!json_string_value(&cursor, &value)
                || !dependency_list_add(dependencies, value)) {
                free(value);
                free(contents);
                return false;
            }
            free(value);
        }
    }
    free(contents);
    return dependencies->count > 0U;
}

static bool path_is_file(const char *path) {
    FILE *file = fopen(path, "rb");
    if (file == NULL) return false;
    return fclose(file) == 0;
}

static bool source_footprint_add(size_t *total, const char *path) {
    FILE *file;
    long end;
    if (path == NULL || total == NULL) return false;
    file = fopen(path, "rb");
    if (file == NULL) return false;
    if (fseek(file, 0L, SEEK_END) != 0 || (end = ftell(file)) < 0L) {
        (void)fclose(file);
        return false;
    }
    if (fclose(file) != 0 || (unsigned long)end > SIZE_MAX - *total) return false;
    *total += (size_t)end;
    return true;
}

static bool make_compile_fingerprint(const cbox_toolchain_driver *driver,
                                     const char *toolchain_version,
                                     const cbox_target *target,
                                     const cbox_profile *profile, bool cpp,
                                     bool module, const char *source,
                                     const char *const module_names[],
                                     size_t module_count,
                                     unsigned char fingerprint[32]) {
    size_t capacity = 512U;
    size_t length = 0U;
    char *data = malloc(capacity);
    size_t index;
    bool success = data != NULL;
    const char *fields[] = {
        /* Bump this tag when fingerprint meaning changes, not just its contents. */
        "compile-fingerprint-v1", driver->name, toolchain_version, target->kind,
        cpp ? target->cpp : target->c,
        profile != NULL ? profile->optimization : NULL,
        profile != NULL ? profile->warnings : NULL,
        profile != NULL && profile->debug_symbols ? "debug-symbols" : "no-debug-symbols",
        module ? "module" : "source",
        cpp && strcmp(driver->name, "MSVC") == 0
            ? "msvc-cpp-exceptions-enabled" : "",
        source
    };

    if (!success) return false;
    for (index = 0U; index < sizeof(fields) / sizeof(fields[0]); ++index) {
        const char *field = fields[index] != NULL ? fields[index] : "";
        size_t field_length = strlen(field) + 1U;
        if (length > SIZE_MAX - field_length) {
            success = false;
            break;
        }
        if (length + field_length > capacity) {
            size_t new_capacity = capacity;
            char *resized;
            while (new_capacity < length + field_length) {
                if (new_capacity > SIZE_MAX / 2U) {
                    success = false;
                    break;
                }
                new_capacity *= 2U;
            }
            if (!success) break;
            resized = realloc(data, new_capacity);
            if (resized == NULL) {
                success = false;
                break;
            }
            data = resized;
            capacity = new_capacity;
        }
        memcpy(data + length, field, field_length);
        length += field_length;
    }
    if (success) {
        /* NUL-terminate each field so ["ab", "c"] can't hash like ["a", "bc"]. */
        const char *const *arrays[] = {
            (const char *const *)target->include_dirs,
            (const char *const *)target->compile_args,
            module_names
        };
        static const char *array_markers[] = {
            "include-dirs", "compile-args", "imported-modules"
        };
        size_t counts[] = {target->include_dir_count, target->compile_arg_count,
                           module_count};
        for (index = 0U; index < 3U && success; ++index) {
            size_t item;
            size_t marker_length = strlen(array_markers[index]) + 1U;
            if (length > SIZE_MAX - marker_length) {
                success = false;
                break;
            }
            if (length + marker_length > capacity) {
                size_t new_capacity = capacity;
                char *resized;
                while (new_capacity < length + marker_length) {
                    if (new_capacity > SIZE_MAX / 2U) {
                        success = false;
                        break;
                    }
                    new_capacity *= 2U;
                }
                if (!success) break;
                resized = realloc(data, new_capacity);
                if (resized == NULL) {
                    success = false;
                    break;
                }
                data = resized;
                capacity = new_capacity;
            }
            memcpy(data + length, array_markers[index], marker_length);
            length += marker_length;
            for (item = 0U; item < counts[index]; ++item) {
                const char *field = arrays[index][item] != NULL
                    ? arrays[index][item] : "";
                size_t field_length = strlen(field) + 1U;
                if (length > SIZE_MAX - field_length) {
                    success = false;
                    break;
                }
                if (length + field_length > capacity) {
                    size_t new_capacity = capacity;
                    char *resized;
                    while (new_capacity < length + field_length) {
                        if (new_capacity > SIZE_MAX / 2U) {
                            success = false;
                            break;
                        }
                        new_capacity *= 2U;
                    }
                    if (!success) break;
                    resized = realloc(data, new_capacity);
                    if (resized == NULL) {
                        success = false;
                        break;
                    }
                    data = resized;
                    capacity = new_capacity;
                }
                memcpy(data + length, field, field_length);
                length += field_length;
            }
        }
    }
    if (success) {
        cbox_sha256_buffer(data, length, fingerprint);
    }
    free(data);
    return success;
}

static bool make_link_fingerprint(const cbox_toolchain_driver *driver,
                                  const char *toolchain_version,
                                  const cbox_target *target,
                                  const cbox_profile *profile,
                                  const char *const object_paths[],
                                  size_t object_count,
                                  unsigned char fingerprint[32]) {
    const char *fields[] = {
        "link", driver->name, toolchain_version, target->kind,
        profile != NULL ? profile->optimization : NULL,
        profile != NULL && profile->debug_symbols ? "debug-symbols" : "no-debug-symbols",
        driver->linker_kind == CBOX_LINKER_MSVC ? "msvc-linker" : "gnu-linker"
    };
    const char *const *arrays[] = {
        (const char *const *)target->link_libraries,
        (const char *const *)target->link_args,
        object_paths,
        (const char *const *)target->link_target_paths
    };
    const size_t counts[] = {
        target->link_library_count, target->link_arg_count, object_count,
        target->link_target_path_count
    };
    static const char *markers[] = {
        "link-libraries", "link-args", "object-files", "linked-targets"
    };
    char *data = NULL;
    size_t capacity = 0U;
    size_t length = 0U;
    size_t index;
    bool success = true;

    for (index = 0U; index < sizeof(fields) / sizeof(fields[0]) && success; ++index) {
        const char *field = fields[index] != NULL ? fields[index] : "";
        size_t field_length = strlen(field) + 1U;
        char *resized;
        size_t new_capacity = capacity;
        if (field_length > SIZE_MAX - length) {
            success = false;
            break;
        }
        while (new_capacity < length + field_length) {
            new_capacity = new_capacity == 0U ? 256U : new_capacity * 2U;
            if (new_capacity < capacity) {
                success = false;
                break;
            }
        }
        if (!success) break;
        if (new_capacity != capacity) {
            resized = realloc(data, new_capacity);
            if (resized == NULL) {
                success = false;
                break;
            }
            data = resized;
            capacity = new_capacity;
        }
        memcpy(data + length, field, field_length);
        length += field_length;
    }
    for (index = 0U; index < 4U && success; ++index) {
        size_t marker_length = strlen(markers[index]) + 1U;
        size_t item;
        char *resized;
        size_t new_capacity = capacity;
        if (marker_length > SIZE_MAX - length) {
            success = false;
            break;
        }
        while (new_capacity < length + marker_length) {
            new_capacity = new_capacity == 0U ? 256U : new_capacity * 2U;
            if (new_capacity < capacity) {
                success = false;
                break;
            }
        }
        if (!success) break;
        if (new_capacity != capacity) {
            resized = realloc(data, new_capacity);
            if (resized == NULL) {
                success = false;
                break;
            }
            data = resized;
            capacity = new_capacity;
        }
        memcpy(data + length, markers[index], marker_length);
        length += marker_length;
        for (item = 0U; item < counts[index]; ++item) {
            const char *field = arrays[index][item] != NULL ? arrays[index][item] : "";
            size_t field_length = strlen(field) + 1U;
            new_capacity = capacity;
            if (field_length > SIZE_MAX - length) {
                success = false;
                break;
            }
            while (new_capacity < length + field_length) {
                new_capacity *= 2U;
                if (new_capacity < capacity) {
                    success = false;
                    break;
                }
            }
            if (!success) break;
            if (new_capacity != capacity) {
                resized = realloc(data, new_capacity);
                if (resized == NULL) {
                    success = false;
                    break;
                }
                data = resized;
                capacity = new_capacity;
            }
            memcpy(data + length, field, field_length);
            length += field_length;
        }
    }
    if (success) cbox_sha256_buffer(data, length, fingerprint);
    free(data);
    return success;
}

static void report_cache_change(const char *path,
                                cbox_cache_change_reason reason,
                                void *context) {
    const char *source = context != NULL
        ? *(const char *const *)context : NULL;
    switch (reason) {
        case CBOX_CACHE_CHANGE_UNCACHED:
            (void)cbox_terminal_printf(stdout, CBOX_TERMINAL_YELLOW,
                                       "no cache entry for %s\n",
                                       source != NULL ? source : path);
            break;
        case CBOX_CACHE_CHANGE_SETTINGS:
            (void)cbox_terminal_printf(stdout, CBOX_TERMINAL_YELLOW,
                                       "build settings changed for %s\n",
                                       source != NULL ? source : path);
            break;
        case CBOX_CACHE_CHANGE_OUTPUT:
            (void)cbox_terminal_printf(stdout, CBOX_TERMINAL_YELLOW,
                                       "output changed: %s\n", path);
            break;
        case CBOX_CACHE_CHANGE_DEPENDENCY:
            (void)cbox_terminal_printf(stdout, CBOX_TERMINAL_YELLOW,
                                       "dependency changed: %s\n", path);
            break;
    }
}

static bool compile_cached(const cbox_toolchain_driver *driver,
                           const char *toolchain_version,
                           const cbox_target *target,
                           const cbox_profile *profile, bool cpp, bool module,
                           const char *source, const char *object,
                           const char *module_output,
                           const char *const module_names[],
                           const char *const module_outputs[],
                           size_t module_count, const char *module_mapper,
                           cbox_cache *cache, cbox_mutex *cache_mutex,
                           const unsigned char prepared_fingerprint[32]) {
    char *dependency_file = append_suffix(object, ".deps");
    char *debug_database_file = NULL;
    unsigned char fingerprint_storage[32];
    const unsigned char *fingerprint = prepared_fingerprint;
    cbox_dependency_list dependencies = {0};
    bool success = false;
    bool is_msvc = driver->linker_kind == CBOX_LINKER_MSVC
        && strcmp(driver->name, "MSVC") == 0;

    if (!arenalib_arena_init(&dependencies.arena, dependencies.arena_storage,
                             sizeof(dependencies.arena_storage), 0U)) {
        fprintf(stderr, "CBox: failed to initialize compiler dependency arena\n");
        goto cleanup;
    }
    dependencies.arena_initialized = true;
    arenalib_arena_set_oom_callback(&dependencies.arena,
                                    dependency_list_arena_oom, NULL);
    if (dependency_file == NULL
        || (fingerprint == NULL
            && !make_compile_fingerprint(driver, toolchain_version, target,
                                    profile, cpp, module,
                                    source, module_names, module_count,
                                    fingerprint_storage))) {
        goto cleanup;
    }
    if (fingerprint == NULL) fingerprint = fingerprint_storage;
    if (is_msvc && profile != NULL && profile->debug_symbols) {
        debug_database_file = append_suffix(object, ".pdb");
        if (debug_database_file == NULL) goto cleanup;
    }
    if (module) {
        (void)cbox_terminal_printf(stdout, CBOX_TERMINAL_CYAN,
                                   "compiling module %s\n", source);
    }
    (void)remove(dependency_file);
    if (!driver->compile(target, profile, cpp, module, source, object,
                         module_output, module_names, module_outputs,
                         module_count, module_mapper, dependency_file)) {
        goto cleanup;
    }
    if (!path_is_file(dependency_file)) {
        if (is_msvc) {
            fprintf(stderr, "CBox: MSVC did not emit dependency data; this object will be rebuilt next time\n");
            success = true;
            goto cleanup;
        }
        fprintf(stderr, "CBox: compiler did not emit dependency data for '%s'\n", source);
        goto cleanup;
    }
    if (!dependency_list_add(&dependencies, source)) goto cleanup;
    if (debug_database_file != NULL
        && (!path_is_file(debug_database_file)
            || !dependency_list_add(&dependencies, debug_database_file))) {
        fprintf(stderr, "CBox: MSVC did not emit debug information file '%s'\n",
                debug_database_file);
        goto cleanup;
    }
    if (path_is_file("CBox.toml")
        && !dependency_list_add(&dependencies, "CBox.toml")) goto cleanup;
    if (module && module_output != NULL
        && strcmp(driver->name, "Clang") == 0
        && !dependency_list_add(&dependencies, module_output)) goto cleanup;
    {
        size_t module_index = 0U;
        for (module_index = 0U; module_index < module_count; ++module_index) {
            if (module_outputs != NULL
                && path_is_file(module_outputs[module_index])
                && !dependency_list_add(&dependencies, module_outputs[module_index])) {
                goto cleanup;
            }
        }
    }
    if (is_msvc) {
        if (!parse_msvc_dependencies(dependency_file, &dependencies)) goto cleanup;
    } else if (!parse_make_dependencies(dependency_file, &dependencies)) {
        goto cleanup;
    }
    /* Check the whole batch under one lock; workers start only after every job has
       a stable answer from the shared cache. */
    cbox_mutex_lock(cache_mutex);
    success = cbox_cache_store(cache, object, fingerprint,
                               (const char *const *)dependencies.items,
                               dependencies.count);
    cbox_mutex_unlock(cache_mutex);
    if (!success) {
        fprintf(stderr, "CBox: failed to update dependency cache for '%s'\n", source);
        goto cleanup;
    }

cleanup:
    if (!success) {
        fprintf(stderr, "CBox: failed to compile or record dependencies for '%s'\n", source);
    }
    dependency_list_free(&dependencies);
    free(dependency_file);
    free(debug_database_file);
    return success;
}

static void report_batch_cache_change(size_t request_index, const char *path,
                                      cbox_cache_change_reason reason,
                                      void *context) {
    cbox_cache_change_report *report = context;
    size_t index;
    if (reason == CBOX_CACHE_CHANGE_DEPENDENCY) {
        char *identity = cbox_path_identity(path);
        if (identity == NULL) {
            fprintf(stderr,
                "CBox: failed to normalize changed dependency path '%s'\n", path);
            report_cache_change(path, reason,
                                &report->jobs[request_index]->source);
            return;
        }
        for (index = 0U; index < report->dependency_path_count; ++index) {
            if (cbox_path_identity_equal(
                    report->dependency_path_identities[index], identity)) {
                free(identity);
                return;
            }
        }
        if (report->dependency_path_count == report->dependency_path_capacity) {
            size_t capacity = report->dependency_path_capacity == 0U
                ? 8U : report->dependency_path_capacity * 2U;
            char **paths;
            if (capacity < report->dependency_path_capacity
                || capacity > SIZE_MAX / sizeof(*paths)) {
                fprintf(stderr,
                    "CBox: unable to deduplicate dependency-change diagnostics\n");
                free(identity);
            } else {
                paths = realloc(report->dependency_path_identities,
                                capacity * sizeof(*paths));
                if (paths == NULL) {
                    fprintf(stderr,
                        "CBox: unable to deduplicate dependency-change diagnostics\n");
                    free(identity);
                } else {
                    report->dependency_path_identities = paths;
                    report->dependency_path_capacity = capacity;
                }
            }
        }
        if (report->dependency_path_count
            < report->dependency_path_capacity) {
            report->dependency_path_identities[
                report->dependency_path_count++] = identity;
        } else {
            report_cache_change(path, reason,
                                &report->jobs[request_index]->source);
            return;
        }
    }
    report_cache_change(path, reason, &report->jobs[request_index]->source);
}

static bool prepare_compile_jobs(const cbox_toolchain_driver *driver,
                                 const char *toolchain_version,
                                 const cbox_target *target,
                                 const cbox_profile *profile,
                                 const char *const module_names[],
                                 size_t module_count,
                                 cbox_compile_job *c_jobs, size_t c_count,
                                 cbox_compile_job *cpp_jobs, size_t cpp_count,
                                 cbox_cache *cache, cbox_mutex *cache_mutex) {
    cbox_cache_check_request *requests = NULL;
    cbox_compile_job **jobs = NULL;
    bool *current = NULL;
    cbox_cache_change_report change_report = {0};
    size_t job_count;
    size_t index;
    bool success = false;

    if (c_count > SIZE_MAX - cpp_count) return false;
    job_count = c_count + cpp_count;
    if (job_count == 0U) return true;
    if (job_count > SIZE_MAX / sizeof(*requests)
        || job_count > SIZE_MAX / sizeof(*jobs)
        || job_count > SIZE_MAX / sizeof(*current)) {
        return false;
    }
    requests = calloc(job_count, sizeof(*requests));
    jobs = malloc(job_count * sizeof(*jobs));
    current = calloc(job_count, sizeof(*current));
    if (requests == NULL || jobs == NULL || current == NULL) goto cleanup;
    change_report.jobs = jobs;

    for (index = 0U; index < c_count; ++index) {
        c_jobs[index].cpp = false;
        jobs[index] = &c_jobs[index];
    }
    for (index = 0U; index < cpp_count; ++index) {
        jobs[c_count + index] = &cpp_jobs[index];
    }
    for (index = 0U; index < job_count; ++index) {
        cbox_compile_job *job = jobs[index];
        if (!make_compile_fingerprint(driver, toolchain_version, target, profile,
                                      job->cpp, false, job->source,
                                      module_names, module_count,
                                      job->fingerprint)) {
            goto cleanup;
        }
        if (path_is_file(job->object)) {
            requests[index].output_path = job->object;
            requests[index].fingerprint = job->fingerprint;
        } else {
            requests[index].output_path = NULL;
            requests[index].fingerprint = job->fingerprint;
            (void)cbox_terminal_printf(stdout, CBOX_TERMINAL_CYAN,
                                       "compiling \"%s\" (output missing)\n",
                                       job->source);
        }
    }

    cbox_mutex_lock(cache_mutex);
    success = cbox_cache_check_batch(cache, requests, job_count, current,
                                     report_batch_cache_change, &change_report);
    cbox_mutex_unlock(cache_mutex);
    if (!success) {
        fprintf(stderr, "CBox: failed to validate incremental cache for compile batch\n");
        goto cleanup;
    }

    for (index = 0U; index < job_count; ++index) {
        jobs[index]->needs_compile = !current[index];
        if (current[index]) {
            (void)cbox_terminal_printf(stdout, CBOX_TERMINAL_GREEN,
                                       "up to date %s\n", jobs[index]->source);
        } else if (requests[index].output_path != NULL) {
            (void)cbox_terminal_printf(stdout, CBOX_TERMINAL_YELLOW,
                                       "recompiling %s\n", jobs[index]->source);
        }
    }

cleanup:
    for (index = 0U; index < change_report.dependency_path_count; ++index) {
        free(change_report.dependency_path_identities[index]);
    }
    free(change_report.dependency_path_identities);
    free(requests);
    free(jobs);
    free(current);
    return success;
}

static bool compile_batch_job(size_t index, void *context) {
    cbox_compile_batch *batch = context;
    const cbox_compile_job *job = &batch->jobs[index];
    return compile_cached(batch->driver, batch->toolchain_version,
                          batch->target, batch->profile, job->cpp, false,
                          job->source, job->object, NULL, batch->module_names,
                          batch->module_outputs, batch->module_count,
                          batch->module_mapper, batch->cache,
                          batch->cache_mutex, job->fingerprint);
}

static bool compile_batch(const cbox_toolchain_driver *driver,
                          const char *toolchain_version,
                          const cbox_target *target,
                          const cbox_profile *profile,
                          const char *const module_names[],
                          const char *const module_outputs[],
                          size_t module_count, const char *module_mapper,
                          cbox_cache *cache, cbox_mutex *cache_mutex,
                          cbox_compile_job jobs[], size_t count,
                          size_t worker_count) {
    cbox_compile_batch batch;
    if (count == 0U) return true;
    batch.driver = driver;
    batch.toolchain_version = toolchain_version;
    batch.target = target;
    batch.profile = profile;
    batch.module_names = module_names;
    batch.module_outputs = module_outputs;
    batch.module_count = module_count;
    batch.module_mapper = module_mapper;
    batch.cache = cache;
    batch.cache_mutex = cache_mutex;
    batch.jobs = jobs;
    if (worker_count > count) worker_count = count;
    return cbox_parallel_for(count, worker_count, compile_batch_job, &batch);
}

static bool module_name_matches_import(const char *module_name,
                                       const char *import_name,
                                       const char *candidate) {
    const char *partition;
    size_t prefix_length;
    size_t import_length;
    size_t candidate_length;

    if (module_name == NULL || import_name == NULL || import_name[0] != ':') {
        return false;
    }
    partition = strchr(module_name, ':');
    if (partition == NULL) return false;
    prefix_length = (size_t)(partition - module_name);
    import_length = strlen(import_name);
    candidate_length = strlen(candidate);
    return prefix_length <= SIZE_MAX - import_length
        && candidate_length == prefix_length + import_length
        && memcmp(candidate, module_name, prefix_length) == 0
        && memcmp(candidate + prefix_length, import_name, import_length) == 0;
}

static bool resolve_module_import(const char *module_name,
                                  const char *import_name,
                                  const char *const module_names[],
                                  size_t module_count,
                                  size_t *provider_index) {
    size_t index;

    for (index = 0U; index < module_count; ++index) {
        bool matches = import_name[0] == ':'
            ? module_name_matches_import(module_name, import_name,
                                         module_names[index])
            : strcmp(module_names[index], import_name) == 0;
        if (matches) {
            *provider_index = index;
            return true;
        }
    }
    return false;
}

static char *create_gcc_module_mapper(const char *module_output,
                                      const char *const module_names[],
                                      const char *const module_outputs[],
                                      size_t module_count) {
    size_t path_length = strlen(module_output);
    char *path;
    FILE *file;
    size_t index;

    if (path_length > SIZE_MAX - sizeof(".mapper")) {
        return NULL;
    }
    path = malloc(path_length + sizeof(".mapper"));
    if (path == NULL) {
        return NULL;
    }
    memcpy(path, module_output, path_length);
    memcpy(path + path_length, ".mapper", sizeof(".mapper"));
    file = fopen(path, "wb");
    if (file == NULL) {
        fprintf(stderr, "CBox: cannot create GCC module mapper '%s'\n", path);
        free(path);
        return NULL;
    }
    for (index = 0U; index < module_count; ++index) {
        if (fprintf(file, "%s %s\n", module_names[index], module_outputs[index]) < 0) {
            (void)fclose(file);
            (void)remove(path);
            free(path);
            return NULL;
        }
    }
    if (fclose(file) != 0) {
        (void)remove(path);
        free(path);
        return NULL;
    }
    return path;
}

static char *profile_directory_name(const cbox_profile *profile) {
    const char *name = profile != NULL && profile->name != NULL
        ? profile->name : "default";
    size_t length = strlen(name);
    char *safe_name;
    size_t index;

    if (length == 0U || length > SIZE_MAX - 1U) return NULL;
    safe_name = malloc(length + 1U);
    if (safe_name == NULL) return NULL;
    for (index = 0U; index < length; ++index) {
        unsigned char character = (unsigned char)name[index];
        safe_name[index] = ((character >= 'a' && character <= 'z')
            || (character >= 'A' && character <= 'Z')
            || (character >= '0' && character <= '9') || character == '-'
            || character == '_') ? (char)character : '_';
    }
    safe_name[length] = '\0';
    return safe_name;
}

static char *profile_output_path(const char *target_directory,
                                 const char *profile_name, const char *output) {
    const char *basename = output;
    const char *slash = strrchr(output, '/');
    const char *backslash = strrchr(output, '\\');
    int required;
    char *path;

    if (slash != NULL || backslash != NULL) {
        const char *separator = slash == NULL ? backslash
            : backslash == NULL || slash > backslash ? slash : backslash;
        basename = separator + 1;
    }
    if (basename[0] == '\0') return NULL;
    required = snprintf(NULL, 0, "CBoxBuild/%s/%s/bin/%s",
                        target_directory, profile_name, basename);
    if (required < 0) return NULL;
    path = malloc((size_t)required + 1U);
    if (path == NULL) return NULL;
    (void)snprintf(path, (size_t)required + 1U,
                   "CBoxBuild/%s/%s/bin/%s",
                   target_directory, profile_name, basename);
    required = snprintf(NULL, 0, "CBoxBuild/%s/%s/bin",
                        target_directory, profile_name);
    if (required < 0) {
        free(path);
        return NULL;
    }
    {
        char *directory = malloc((size_t)required + 1U);
        if (directory == NULL) {
            free(path);
            return NULL;
        }
        (void)snprintf(directory, (size_t)required + 1U,
                       "CBoxBuild/%s/%s/bin", target_directory, profile_name);
        if (!cbox_create_directories(directory)) {
            free(directory);
            free(path);
            return NULL;
        }
        free(directory);
    }
    return path;
}

static char *assembly_output_path(const char *directory, const char *source,
                                  const char *extension) {
    const char *basename = source;
    const char *slash = strrchr(source, '/');
    const char *backslash = strrchr(source, '\\');
    const char *dot;
    unsigned char digest[32];
    char identity[33];
    size_t index;
    size_t stem_length;
    int required;
    char *path;

    if (slash != NULL || backslash != NULL) {
        const char *separator = slash == NULL ? backslash
            : backslash == NULL || slash > backslash ? slash : backslash;
        basename = separator + 1;
    }
    if (basename[0] == '\0') return NULL;
    dot = strrchr(basename, '.');
    stem_length = dot == NULL || dot == basename
        ? strlen(basename) : (size_t)(dot - basename);
    if (stem_length > INT_MAX) return NULL;
    cbox_sha256_buffer(source, strlen(source), digest);
    for (index = 0U; index < 16U; ++index) {
        (void)snprintf(identity + index * 2U, sizeof(identity) - index * 2U,
                       "%02x", digest[index]);
    }
    identity[32] = '\0';
    required = snprintf(NULL, 0, "%s/%.*s-%s%s", directory,
                        (int)stem_length, basename, identity, extension);
    if (required < 0) return NULL;
    path = malloc((size_t)required + 1U);
    if (path == NULL) return NULL;
    (void)snprintf(path, (size_t)required + 1U, "%s/%.*s-%s%s", directory,
                   (int)stem_length, basename, identity, extension);
    return path;
}

static char *single_assembly_output_path(const cbox_toolchain_driver *driver,
                                        const cbox_target *target,
                                        const char *directory,
                                        const char *extension) {
    char *resolved = cbox_linker_target_output(
        driver->linker_kind, target->kind, target->output, target->name);
    const char *basename;
    const char *slash;
    const char *backslash;
    const char *dot;
    size_t stem_length;
    int required;
    char *path;

    if (resolved == NULL) return NULL;
    slash = strrchr(resolved, '/');
    backslash = strrchr(resolved, '\\');
    basename = slash == NULL ? resolved
        : backslash == NULL || slash > backslash ? slash + 1 : backslash + 1;
    if (backslash != NULL && (slash == NULL || backslash > slash)) {
        basename = backslash + 1;
    }
    dot = strrchr(basename, '.');
    stem_length = dot == NULL || dot == basename
        ? strlen(basename) : (size_t)(dot - basename);
    if (stem_length > INT_MAX) {
        free(resolved);
        return NULL;
    }
    required = snprintf(NULL, 0, "%s/%.*s%s", directory, (int)stem_length,
                        basename, extension);
    if (required < 0) {
        free(resolved);
        return NULL;
    }
    path = malloc((size_t)required + 1U);
    if (path != NULL) {
        (void)snprintf(path, (size_t)required + 1U, "%s/%.*s%s", directory,
                       (int)stem_length, basename, extension);
    }
    free(resolved);
    return path;
}

static bool concatenate_assembly_files(const char *const inputs[],
                                       size_t input_count,
                                       const char *output) {
    unsigned char buffer[16384];
    FILE *destination = fopen(output, "wb");
    size_t index;
    bool success = destination != NULL;

    for (index = 0U; success && index < input_count; ++index) {
        FILE *input = fopen(inputs[index], "rb");
        size_t amount;
        if (input == NULL) {
            success = false;
            break;
        }
        while ((amount = fread(buffer, 1U, sizeof(buffer), input)) > 0U) {
            if (fwrite(buffer, 1U, amount, destination) != amount) {
                success = false;
                break;
            }
        }
        if (ferror(input)) success = false;
        if (fclose(input) != 0) success = false;
    }
    if (destination != NULL && fclose(destination) != 0) success = false;
    if (!success) {
        (void)remove(output);
        fprintf(stderr, "CBox: failed to combine assembly outputs into '%s'\n",
                output);
    }
    return success;
}

bool cbox_toolchain_assemble(const cbox_toolchain_driver *driver,
                             const cbox_target *target,
                             const cbox_profile *profile, bool one_assembly) {
    const char **sources = NULL;
    char **assembly_paths = NULL;
    char **object_paths = NULL;
    char **temporary_paths = NULL;
    char *profile_name = NULL;
    char *target_directory = NULL;
    char *directory = NULL;
    char *single_output = NULL;
    const char *extension;
    size_t source_count;
    size_t index;
    int required;
    bool success = false;

    if (driver == NULL || driver->assemble == NULL || target == NULL
        || target->kind == NULL
        || (strcmp(target->kind, "executable") != 0
            && strcmp(target->kind, "static") != 0
            && strcmp(target->kind, "shared") != 0)) {
        fprintf(stderr, "CBox: invalid assembly request\n");
        return false;
    }
    if (target->cpp_module_count > 0U) {
        fprintf(stderr, "CBox: assembly output for C++ module targets "
                "is not yet supported\n");
        return false;
    }
    if (target->c_source_count > SIZE_MAX - target->cpp_source_count) {
        return false;
    }
    source_count = target->c_source_count + target->cpp_source_count;
    if (source_count == 0U || source_count > SIZE_MAX / sizeof(*sources)
        || source_count > SIZE_MAX / sizeof(*assembly_paths)) {
        fprintf(stderr, "CBox: target '%s' has no source files for assembly output\n",
                target->name != NULL ? target->name : "(unnamed)");
        return false;
    }
    sources = malloc(source_count * sizeof(*sources));
    assembly_paths = calloc(source_count, sizeof(*assembly_paths));
    if (sources == NULL || assembly_paths == NULL) {
        fprintf(stderr, "CBox: out of memory preparing assembly source paths\n");
        goto cleanup;
    }
    for (index = 0U; index < target->c_source_count; ++index) {
        sources[index] = target->c_sources[index];
    }
    for (index = 0U; index < target->cpp_source_count; ++index) {
        sources[target->c_source_count + index] = target->cpp_sources[index];
    }
    profile_name = profile_directory_name(profile);
    target_directory = cbox_build_directory_name(
        driver->name, target->name != NULL ? target->name : target->output);
    if (profile_name == NULL || target_directory == NULL) {
        fprintf(stderr, "CBox: failed to resolve assembly output directory\n");
        goto cleanup;
    }
    required = snprintf(NULL, 0, "CBoxBuild/%s/%s/asm",
                        target_directory, profile_name);
    if (required < 0) goto cleanup;
    directory = malloc((size_t)required + 1U);
    if (directory == NULL) {
        fprintf(stderr, "CBox: out of memory preparing assembly output directory\n");
        goto cleanup;
    }
    (void)snprintf(directory, (size_t)required + 1U,
                   "CBoxBuild/%s/%s/asm", target_directory, profile_name);
    if (!cbox_create_directories(directory)) {
        fprintf(stderr, "CBox: failed to create assembly output directory '%s'\n",
                directory);
        goto cleanup;
    }
    extension = strcmp(driver->name, "MSVC") == 0 ? ".asm" : ".s";
    if (!cbox_prepare_object_paths(target_directory, sources, source_count,
                                   profile_name, driver->object_extension,
                                   &object_paths)) {
        fprintf(stderr, "CBox: failed to prepare assembly intermediate object paths\n");
        goto cleanup;
    }
    if (one_assembly) {
        single_output = single_assembly_output_path(
            driver, target, directory, extension);
        temporary_paths = calloc(source_count, sizeof(*temporary_paths));
        if (single_output == NULL || temporary_paths == NULL) {
            fprintf(stderr, "CBox: failed to prepare combined assembly output path\n");
            goto cleanup;
        }
    } else {
        for (index = 0U; index < source_count; ++index) {
            assembly_paths[index] = assembly_output_path(
                directory, sources[index], extension);
            if (assembly_paths[index] == NULL) {
                fprintf(stderr, "CBox: failed to prepare assembly path for '%s'\n",
                        sources[index]);
                goto cleanup;
            }
        }
    }
    for (index = 0U; index < source_count; ++index) {
        const char *destination;
        if (one_assembly) {
            required = snprintf(NULL, 0, "%s/.cbox-asm-%zu%s",
                                directory, index, extension);
            if (required < 0) {
                fprintf(stderr, "CBox: failed to size temporary assembly path\n");
                goto cleanup;
            }
            temporary_paths[index] = malloc((size_t)required + 1U);
            if (temporary_paths[index] == NULL) {
                fprintf(stderr, "CBox: out of memory preparing temporary assembly path\n");
                goto cleanup;
            }
            (void)snprintf(temporary_paths[index], (size_t)required + 1U,
                           "%s/.cbox-asm-%zu%s", directory, index, extension);
            destination = temporary_paths[index];
        } else {
            destination = assembly_paths[index];
        }
        (void)cbox_terminal_printf(stdout, CBOX_TERMINAL_CYAN,
                                   "generating assembly for %s\n", sources[index]);
        if (!driver->assemble(target, profile,
                              index >= target->c_source_count,
                              sources[index], destination,
                              object_paths[index])) {
            fprintf(stderr, "CBox: failed to generate assembly for '%s'\n",
                    sources[index]);
            goto cleanup;
        }
    }
    if (one_assembly) {
        success = concatenate_assembly_files(
            (const char *const *)temporary_paths, source_count, single_output);
        if (success) {
            (void)cbox_terminal_printf(stdout, CBOX_TERMINAL_GREEN,
                                       "assembly output: %s\n", single_output);
        }
    } else {
        success = true;
        for (index = 0U; index < source_count; ++index) {
            (void)cbox_terminal_printf(stdout, CBOX_TERMINAL_GREEN,
                                       "assembly output: %s\n",
                                       assembly_paths[index]);
        }
    }

cleanup:
    if (temporary_paths != NULL) {
        for (index = 0U; index < source_count; ++index) {
            if (temporary_paths[index] != NULL) {
                (void)remove(temporary_paths[index]);
                free(temporary_paths[index]);
            }
        }
    }
    if (assembly_paths != NULL) {
        for (index = 0U; index < source_count; ++index) {
            free(assembly_paths[index]);
        }
    }
    free(temporary_paths);
    free(assembly_paths);
    cbox_discard_object_paths(object_paths, source_count);
    free(sources);
    free(single_output);
    free(directory);
    free(target_directory);
    free(profile_name);
    return success;
}

bool cbox_toolchain_check(const cbox_toolchain_driver *driver,
                          const cbox_target *target,
                          const cbox_profile *profile) {
    size_t source_count;
    size_t index;
    bool success = true;

    if (driver == NULL || driver->check == NULL || target == NULL
        || target->kind == NULL) {
        fprintf(stderr, "CBox: invalid syntax-check request\n");
        return false;
    }
    if (target->cpp_module_count > 0U) {
        fprintf(stderr, "CBox: syntax checking C++ module targets is not yet supported\n");
        return false;
    }
    if (target->c_source_count > SIZE_MAX - target->cpp_source_count) {
        fprintf(stderr, "CBox: too many source files to syntax-check\n");
        return false;
    }
    source_count = target->c_source_count + target->cpp_source_count;
    if (source_count == 0U) {
        fprintf(stderr, "CBox: target '%s' has no source files to syntax-check\n",
                target->name != NULL ? target->name : "(unnamed)");
        return false;
    }
    for (index = 0U; index < source_count; ++index) {
        bool cpp = index >= target->c_source_count;
        size_t source_index = cpp ? index - target->c_source_count : index;
        const char *source = cpp ? target->cpp_sources[source_index]
            : target->c_sources[source_index];

        if (source == NULL || source[0] == '\0') {
            fprintf(stderr, "CBox: invalid source path in syntax-check request\n");
            success = false;
            continue;
        }
        (void)cbox_terminal_printf(stdout, CBOX_TERMINAL_CYAN,
                                   "checking syntax of %s\n", source);
        if (!driver->check(target, profile, cpp, source)) {
            fprintf(stderr, "CBox: syntax check failed for '%s'\n", source);
            success = false;
        }
    }
    if (success) {
        (void)cbox_terminal_printf(stdout, CBOX_TERMINAL_GREEN,
                                   "syntax check passed for target '%s'\n",
                                   target->name != NULL ? target->name : "(unnamed)");
    }
    return success;
}

bool cbox_toolchain_build(const cbox_toolchain_driver *driver,
                          const cbox_target *target, const cbox_profile *profile) {
    char **object_paths = NULL;
    char **module_outputs = NULL;
    const char **module_names = NULL;
    char ***module_imports = NULL;
    size_t *module_import_counts = NULL;
    cbox_module_graph_node *module_graph_nodes = NULL;
    cbox_module_graph_plan module_plan = {0};
    bool *module_compiled = NULL;
    char *module_mapper = NULL;
    char *resolved_output = NULL;
    char *profile_name = NULL;
    char *target_directory = NULL;
    char *profile_output = NULL;
    char *cache_path = NULL;
    char *profile_root = NULL;
    char *import_output = NULL;
    const char **link_dependencies = NULL;
    const char **cache_keys = NULL;
    const char **all_sources = NULL;
    char toolchain_version[512];
    cbox_cache *cache = NULL;
    cbox_mutex *cache_mutex = NULL;
    unsigned char link_fingerprint[32];
    const char *output;
    size_t object_count;
    size_t index;
    size_t module_index = 0U;
    size_t source_footprint = 0U;
    size_t worker_count;
    size_t pending_count;
    cbox_compile_job *c_jobs = NULL;
    cbox_compile_job *cpp_jobs = NULL;
    bool is_static;
    bool is_shared;
    bool success = false;

    if (driver == NULL || target == NULL || driver->version == NULL
        || driver->compile == NULL || driver->archive == NULL || driver->link == NULL) {
        fprintf(stderr, "CBox: invalid compiler driver or target\n");
        return false;
    }
    if (!driver->version(toolchain_version, sizeof(toolchain_version))) {
        fprintf(stderr, "CBox: failed to identify the %s toolchain\n", driver->name);
        return false;
    }
    if (target->kind == NULL ||
        (strcmp(target->kind, "executable") != 0
         && strcmp(target->kind, "static") != 0
         && strcmp(target->kind, "shared") != 0)) {
        fprintf(stderr, "CBox: unsupported target kind '%s'\n",
                target->kind != NULL ? target->kind : "(null)");
        return false;
    }
    is_static = strcmp(target->kind, "static") == 0;
    is_shared = strcmp(target->kind, "shared") == 0;
    if (target->cpp_module_count > 0U && target->cpp != NULL
        && strcmp(target->cpp, "c++20") != 0
        && strcmp(target->cpp, "c++23") != 0
        && strcmp(target->cpp, "c++26") != 0) {
        fprintf(stderr, "CBox: C++ modules require the C++20 standard or later\n");
        return false;
    }
    if (target->c_source_count > SIZE_MAX - target->cpp_source_count
        || target->c_source_count + target->cpp_source_count
            > SIZE_MAX - target->cpp_module_count) {
        return false;
    }
    object_count = target->c_source_count + target->cpp_module_count
        + target->cpp_source_count;
    if (object_count == 0U) {
        fprintf(stderr, "CBox: target '%s' has no source files\n",
                target->name != NULL ? target->name : "(unnamed)");
        return false;
    }
    for (index = 0U; index < target->c_source_count; ++index) {
        if (!source_footprint_add(&source_footprint, target->c_sources[index])) {
            fprintf(stderr, "CBox: cannot determine source size for '%s'\n",
                    target->c_sources[index]);
            return false;
        }
    }
    for (index = 0U; index < target->cpp_source_count; ++index) {
        if (!source_footprint_add(&source_footprint, target->cpp_sources[index])) {
            fprintf(stderr, "CBox: cannot determine source size for '%s'\n",
                    target->cpp_sources[index]);
            return false;
        }
    }
    for (index = 0U; index < target->cpp_module_count; ++index) {
        if (!source_footprint_add(&source_footprint, target->cpp_modules[index])) {
            fprintf(stderr, "CBox: cannot determine source size for '%s'\n",
                    target->cpp_modules[index]);
            return false;
        }
    }
    worker_count = source_footprint >= CBOX_PARALLEL_THRESHOLD_BYTES
        ? cbox_processor_count() : 1U;

    resolved_output = cbox_linker_target_output(
        driver->linker_kind, target->kind, target->output, target->name);
    if (resolved_output == NULL) {
        fprintf(stderr, "CBox: target '%s' has no output name\n",
                target->name != NULL ? target->name : "(unnamed)");
        return false;
    }
    output = resolved_output;
    profile_name = profile_directory_name(profile);
    target_directory = cbox_build_directory_name(driver->name,
        target->name != NULL ? target->name : output);
    if (profile_name == NULL
        || target_directory == NULL
        || (profile_output = profile_output_path(target_directory,
                                                 profile_name, output)) == NULL) {
        fprintf(stderr, "CBox: failed to create profile output directory\n");
        free(profile_name);
        free(target_directory);
        free(resolved_output);
        return false;
    }
    output = profile_output;
    {
        int required = snprintf(NULL, 0, "CBoxBuild/%s/%s/CBoxCache.bin",
                                target_directory, profile_name);
        if (required < 0) goto cleanup;
        cache_path = malloc((size_t)required + 1U);
        if (cache_path == NULL) goto cleanup;
        (void)snprintf(cache_path, (size_t)required + 1U,
                       "CBoxBuild/%s/%s/CBoxCache.bin",
                       target_directory, profile_name);
        profile_root = malloc((size_t)required + 1U);
        if (profile_root == NULL) goto cleanup;
        (void)snprintf(profile_root, (size_t)required + 1U,
                       "CBoxBuild/%s/%s", target_directory, profile_name);
        if (!cbox_create_directories(profile_root)) goto cleanup;
        cache = cbox_cache_open(cache_path);
        if (cache == NULL) goto cleanup;
    }
    cache_mutex = cbox_mutex_create();
    if (cache_mutex == NULL) goto cleanup;
    if (object_count > SIZE_MAX / sizeof(*all_sources)) goto cleanup;
    all_sources = malloc(object_count * sizeof(*all_sources));
    if (all_sources == NULL) goto cleanup;
    for (index = 0U; index < target->c_source_count; ++index) {
        all_sources[index] = target->c_sources[index];
    }
    for (index = 0U; index < target->cpp_module_count; ++index) {
        all_sources[target->c_source_count + index] = target->cpp_modules[index];
    }
    for (index = 0U; index < target->cpp_source_count; ++index) {
        all_sources[target->c_source_count + target->cpp_module_count + index] =
            target->cpp_sources[index];
    }
    if (!cbox_prepare_object_paths(target_directory,
                                   all_sources, object_count, profile_name,
                                   driver->object_extension, &object_paths)) {
        goto cleanup;
    }
    free(all_sources);
    all_sources = NULL;

    if (target->cpp_module_count > 0U) {
        char module_graph_error[256];
        if (driver->module_extension == NULL) {
            fprintf(stderr, "CBox: %s does not support C++ modules\n", driver->name);
            goto cleanup;
        }
        module_outputs = calloc(target->cpp_module_count, sizeof(*module_outputs));
        module_names = calloc(target->cpp_module_count, sizeof(*module_names));
        module_imports = calloc(target->cpp_module_count, sizeof(*module_imports));
        module_import_counts = calloc(target->cpp_module_count,
                                      sizeof(*module_import_counts));
        module_graph_nodes = calloc(target->cpp_module_count,
                                    sizeof(*module_graph_nodes));
        module_compiled = calloc(target->cpp_module_count,
                                 sizeof(*module_compiled));
        if (module_outputs == NULL || module_names == NULL
            || module_imports == NULL || module_import_counts == NULL
            || module_graph_nodes == NULL || module_compiled == NULL) {
            goto cleanup;
        }
        for (module_index = 0U; module_index < target->cpp_module_count; ++module_index) {
            const char *object_path = object_paths[target->c_source_count + module_index];
            size_t object_length = strlen(object_path);
            size_t extension_length = strlen(driver->module_extension);
            if (object_length > SIZE_MAX - extension_length - 1U) {
                goto cleanup;
            }
            module_outputs[module_index] = malloc(object_length + extension_length + 1U);
            if (module_outputs[module_index] == NULL) {
                goto cleanup;
            }
            memcpy(module_outputs[module_index], object_path, object_length);
            memcpy(module_outputs[module_index] + object_length,
                   driver->module_extension, extension_length + 1U);
            module_names[module_index] =
                cbox_read_cpp_module_name(target->cpp_modules[module_index]);
            if (module_names[module_index] == NULL) {
                goto cleanup;
            }
            if (!cbox_read_cpp_module_imports(target->cpp_modules[module_index],
                                              &module_imports[module_index],
                                              &module_import_counts[module_index])) {
                goto cleanup;
            }
            for (index = 0U; index < module_index; ++index) {
                if (strcmp(module_names[index], module_names[module_index]) == 0) {
                    fprintf(stderr, "CBox: duplicate C++ module '%s'\n",
                            module_names[module_index]);
                    goto cleanup;
                }
            }
            module_graph_nodes[module_index].name = module_names[module_index];
            module_graph_nodes[module_index].imports =
                (const char *const *)module_imports[module_index];
            module_graph_nodes[module_index].import_count =
                module_import_counts[module_index];
        }
        if (!cbox_module_graph_create_plan(module_graph_nodes,
                                           target->cpp_module_count,
                                           &module_plan, module_graph_error,
                                           sizeof(module_graph_error))) {
            fprintf(stderr, "CBox: %s\n", module_graph_error);
            goto cleanup;
        }
        if (strcmp(driver->name, "GCC") == 0) {
            module_mapper = create_gcc_module_mapper(
                module_outputs[0], module_names,
                (const char *const *)module_outputs, target->cpp_module_count);
            if (module_mapper == NULL) goto cleanup;
        }
    }

    if (target->c_source_count != 0U) {
        c_jobs = calloc(target->c_source_count, sizeof(*c_jobs));
        if (c_jobs == NULL) goto cleanup;
        for (index = 0U; index < target->c_source_count; ++index) {
            c_jobs[index].source = target->c_sources[index];
            c_jobs[index].object = object_paths[index];
        }
    }
    if (target->cpp_source_count != 0U) {
        cpp_jobs = calloc(target->cpp_source_count, sizeof(*cpp_jobs));
        if (cpp_jobs == NULL) goto cleanup;
        for (index = 0U; index < target->cpp_source_count; ++index) {
            cpp_jobs[index].cpp = true;
            cpp_jobs[index].source = target->cpp_sources[index];
            cpp_jobs[index].object = object_paths[target->c_source_count
                + target->cpp_module_count + index];
        }
    }
    if (!prepare_compile_jobs(driver, toolchain_version, target, profile,
                              module_names, target->cpp_module_count,
                              c_jobs, target->c_source_count,
                              cpp_jobs, target->cpp_source_count,
                              cache, cache_mutex)) {
        goto cleanup;
    }
    if (c_jobs != NULL) {
        pending_count = 0U;
        for (index = 0U; index < target->c_source_count; ++index) {
            if (c_jobs[index].needs_compile) {
                if (pending_count != index) c_jobs[pending_count] = c_jobs[index];
                ++pending_count;
            }
        }
        if (!compile_batch(driver, toolchain_version, target, profile, NULL, NULL,
                           0U, NULL, cache, cache_mutex, c_jobs,
                           pending_count, worker_count)) goto cleanup;
    }
    for (index = 0U; index < module_plan.module_count; ++index) {
        size_t import_index;
        size_t selected_count = 0U;
        size_t current_module = module_plan.module_indices[index];
        size_t import_count = module_import_counts[current_module];
        size_t *provider_indices;
        const char **selected_names;
        const char **selected_outputs;

        if (import_count > SIZE_MAX / sizeof(*provider_indices)
            || import_count > SIZE_MAX / sizeof(*selected_names)) {
            goto cleanup;
        }
        provider_indices = import_count > 0U
            ? malloc(import_count * sizeof(*provider_indices)) : NULL;
        selected_names = import_count > 0U
            ? malloc(import_count * sizeof(*selected_names)) : NULL;
        selected_outputs = import_count > 0U
            ? malloc(import_count * sizeof(*selected_outputs)) : NULL;
        if (import_count > 0U
            && (provider_indices == NULL || selected_names == NULL
                || selected_outputs == NULL)) {
            free(provider_indices);
            free(selected_names);
            free(selected_outputs);
            goto cleanup;
        }
        for (import_index = 0U; import_index < import_count; ++import_index) {
            size_t provider_index;
            if (resolve_module_import(module_names[current_module],
                                      module_imports[current_module][import_index],
                                      module_names, target->cpp_module_count,
                                      &provider_index)
                && module_compiled[provider_index]) {
                provider_indices[selected_count++] = provider_index;
            }
        }
        for (import_index = 0U; import_index < selected_count; ++import_index) {
            selected_names[import_index] = module_names[provider_indices[import_index]];
            selected_outputs[import_index] =
                module_outputs[provider_indices[import_index]];
        }
        if (!compile_cached(driver, toolchain_version, target, profile, true, true,
                            target->cpp_modules[current_module],
                            object_paths[target->c_source_count + current_module],
                            module_outputs[current_module], selected_names,
                            selected_outputs, selected_count,
                            module_mapper, cache, cache_mutex, NULL)) {
            free(provider_indices);
            free(selected_names);
            free(selected_outputs);
            goto cleanup;
        }
        module_compiled[current_module] = true;
        free(provider_indices);
        free(selected_names);
        free(selected_outputs);
    }
    if (cpp_jobs != NULL) {
        pending_count = 0U;
        for (index = 0U; index < target->cpp_source_count; ++index) {
            if (cpp_jobs[index].needs_compile) {
                if (pending_count != index) cpp_jobs[pending_count] = cpp_jobs[index];
                ++pending_count;
            }
        }
        if (!compile_batch(driver, toolchain_version, target, profile,
                           module_names, (const char *const *)module_outputs,
                           target->cpp_module_count, module_mapper, cache,
                           cache_mutex, cpp_jobs, pending_count,
                           worker_count)) goto cleanup;
    }
    if (is_shared) {
        import_output = cbox_linker_shared_import_output(driver->linker_kind, output);
#if defined(_WIN32) || defined(_WIN64)
        if (import_output == NULL) goto cleanup;
#endif
    }
    if (target->link_target_path_count > SIZE_MAX - object_count
        || object_count + target->link_target_path_count
            > (SIZE_MAX / sizeof(*link_dependencies)) - 1U) goto cleanup;
    link_dependencies = malloc((object_count + target->link_target_path_count + 1U)
                               * sizeof(*link_dependencies));
    cache_keys = malloc((object_count + 2U) * sizeof(*cache_keys));
    if (link_dependencies == NULL || cache_keys == NULL) goto cleanup;
    for (index = 0U; index < object_count; ++index) {
        link_dependencies[index] = object_paths[index];
        cache_keys[index] = object_paths[index];
    }
    for (index = 0U; index < target->link_target_path_count; ++index) {
        link_dependencies[object_count + index] = target->link_target_paths[index];
    }
    cache_keys[object_count] = output;
    if (import_output != NULL) {
        link_dependencies[object_count + target->link_target_path_count] = import_output;
        cache_keys[object_count + 1U] = import_output;
    }
    if (!make_link_fingerprint(driver, toolchain_version, target, profile,
                               (const char *const *)object_paths, object_count,
                               link_fingerprint)) {
        goto cleanup;
    }
    if (path_is_file(output)
        && cbox_cache_check(cache, output, link_fingerprint,
                            report_cache_change, NULL)) {
        (void)cbox_terminal_printf(stdout, CBOX_TERMINAL_GREEN,
                                   "up to date \"%s\"\n",
                                   target->name != NULL ? target->name : output);
        success = true;
    } else if (is_static) {
        success = driver->archive(target, output, object_paths, object_count);
    } else {
#if defined(_WIN32) || defined(_WIN64)
        if (is_shared && import_output == NULL) {
            goto cleanup;
        }
#endif
        success = driver->link(target, profile, output, object_paths, object_count,
                               is_shared, import_output);
    }
    if (!success) goto cleanup;
    if (!cbox_cache_store(cache, output, link_fingerprint,
                          link_dependencies, object_count
                              + target->link_target_path_count
                              + (import_output != NULL ? 1U : 0U))
        || !cbox_cache_prune(cache, cache_keys,
                             object_count + 1U + (import_output != NULL ? 1U : 0U))
        || !cbox_cache_save(cache, cache_path)) {
        fprintf(stderr, "CBox: failed to save build cache '%s'\n", cache_path);
        success = false;
    }

cleanup:
    if (module_mapper != NULL) {
        (void)remove(module_mapper);
        free(module_mapper);
    }
    if (module_outputs != NULL) {
        for (module_index = 0U; module_index < target->cpp_module_count; ++module_index) {
            if (module_outputs[module_index] != NULL) {
                free(module_outputs[module_index]);
            }
        }
    }
    if (module_names != NULL) {
        for (module_index = 0U; module_index < target->cpp_module_count; ++module_index) {
            free((void *)module_names[module_index]);
        }
    }
    if (module_imports != NULL) {
        for (module_index = 0U; module_index < target->cpp_module_count; ++module_index) {
            cbox_free_cpp_module_imports(
                module_imports[module_index],
                module_import_counts != NULL ? module_import_counts[module_index] : 0U);
        }
    }
    free(module_outputs);
    free(module_names);
    free(module_imports);
    free(module_import_counts);
    free(module_graph_nodes);
    free(module_compiled);
    cbox_module_graph_free_plan(&module_plan);
    cbox_discard_object_paths(object_paths, object_count);
    cbox_cache_free(cache);
    cbox_mutex_destroy(cache_mutex);
    free(c_jobs);
    free(cpp_jobs);
    free(link_dependencies);
    free(cache_keys);
    free(all_sources);
    free(import_output);
    free(cache_path);
    free(profile_root);
    free(profile_name);
    free(target_directory);
    free(profile_output);
    free(resolved_output);
    return success;
}