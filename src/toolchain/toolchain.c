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
#include "platform/filesystem.h"
#include "linker/linker.h"

#include <ctype.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

char *cbox_target_directory_name(const char *target_name) {
    const unsigned char *byte;
    uint64_t hash = UINT64_C(14695981039346656037);
    size_t length;
    size_t visible_length;
    char *directory_name;
    size_t index;

    if (target_name == NULL || target_name[0] == '\0') return NULL;
    length = strlen(target_name);
    // Keep paths readable, but hash the full name so truncation or sanitizing
    // punctuation cannot make two different targets share build artifacts.
    visible_length = length < 48U ? length : 48U;
    if (visible_length > SIZE_MAX - 18U) return NULL;
    directory_name = malloc(visible_length + 18U);
    if (directory_name == NULL) return NULL;
    for (index = 0U; index < visible_length; ++index) {
        unsigned char character = (unsigned char)target_name[index];
        directory_name[index] = ((character >= 'a' && character <= 'z')
            || (character >= 'A' && character <= 'Z')
            || (character >= '0' && character <= '9') || character == '-'
            || character == '_') ? (char)character : '_';
    }
    for (byte = (const unsigned char *)target_name; *byte != '\0'; ++byte) {
        hash ^= *byte;
        hash *= UINT64_C(1099511628211);
    }
    (void)snprintf(directory_name + visible_length, 18U, "-%016" PRIx64, hash);
    return directory_name;
}

char *cbox_build_directory_name(const char *compiler, const char *target_name) {
    const char *compiler_directory;
    char *target_directory;
    char *build_directory;
    int required;

    if (compiler == NULL || target_name == NULL) return NULL;
    if (strcmp(compiler, "gcc") == 0 || strcmp(compiler, "GCC") == 0) {
        compiler_directory = "gcc";
    } else if (strcmp(compiler, "clang") == 0 || strcmp(compiler, "Clang") == 0) {
        compiler_directory = "clang";
    } else if (strcmp(compiler, "msvc") == 0 || strcmp(compiler, "MSVC") == 0
               || strcmp(compiler, "cl") == 0) {
        compiler_directory = "msvc";
    } else {
        return NULL;
    }
    target_directory = cbox_target_directory_name(target_name);
    if (target_directory == NULL) return NULL;
    required = snprintf(NULL, 0, "%s/%s", compiler_directory, target_directory);
    if (required < 0 || (size_t)required == SIZE_MAX) {
        free(target_directory);
        return NULL;
    }
    build_directory = malloc((size_t)required + 1U);
    if (build_directory != NULL) {
        (void)snprintf(build_directory, (size_t)required + 1U,
                       "%s/%s", compiler_directory, target_directory);
    }
    free(target_directory);
    return build_directory;
}

char *cbox_target_artifact_path(cbox_linker_kind linker_kind,
                                const char *compiler,
                                const cbox_target *target,
                                const cbox_profile *profile) {
    const char *profile_name = profile != NULL && profile->name != NULL
        ? profile->name : "default";
    char *safe_profile = NULL;
    char *target_directory = NULL;
    char *output = NULL;
    char *path = NULL;
    const char *basename;
    const char *slash;
    const char *backslash;
    size_t profile_length;
    size_t index;
    int required;

    if (target == NULL || target->kind == NULL) return NULL;
    output = cbox_linker_target_output(linker_kind, target->kind,
                                       target->output, target->name);
    if (output == NULL) goto cleanup;
    basename = output;
    slash = strrchr(output, '/');
    backslash = strrchr(output, '\\');
    if (slash != NULL || backslash != NULL) {
        const char *separator = slash == NULL ? backslash
            : backslash == NULL || slash > backslash ? slash : backslash;
        basename = separator + 1;
    }
    if (basename[0] == '\0') goto cleanup;
    target_directory = cbox_build_directory_name(compiler,
        target->name != NULL ? target->name : output);
    if (target_directory == NULL) goto cleanup;
    profile_length = strlen(profile_name);
    if (profile_length == 0U || profile_length > SIZE_MAX - 1U) goto cleanup;
    safe_profile = malloc(profile_length + 1U);
    if (safe_profile == NULL) goto cleanup;
    for (index = 0U; index < profile_length; ++index) {
        unsigned char character = (unsigned char)profile_name[index];
        safe_profile[index] = ((character >= 'a' && character <= 'z')
            || (character >= 'A' && character <= 'Z')
            || (character >= '0' && character <= '9') || character == '-'
            || character == '_') ? (char)character : '_';
    }
    safe_profile[profile_length] = '\0';
    required = snprintf(NULL, 0, "CBoxBuild/%s/%s/bin/%s",
                        target_directory, safe_profile, basename);
    if (required < 0 || (size_t)required == SIZE_MAX) goto cleanup;
    path = malloc((size_t)required + 1U);
    if (path == NULL) goto cleanup;
    (void)snprintf(path, (size_t)required + 1U, "CBoxBuild/%s/%s/bin/%s",
                   target_directory, safe_profile, basename);

cleanup:
    free(output);
    free(target_directory);
    free(safe_profile);
    return path;
}

static int next_cpp_token(FILE *file, char *token, size_t capacity) {
    int character;

    for (;;) {
        character = fgetc(file);
        if (character == EOF) {
            return 0;
        }
        if (isspace((unsigned char)character)) {
            continue;
        }
        if (character == '/') {
            int next = fgetc(file);
            if (next == '/') {
                while ((character = fgetc(file)) != EOF && character != '\n') {
                }
                continue;
            }
            if (next == '*') {
                int previous = 0;
                while ((character = fgetc(file)) != EOF) {
                    if (previous == '*' && character == '/') {
                        break;
                    }
                    previous = character;
                }
                continue;
            }
            if (next != EOF) {
                (void)ungetc(next, file);
            }
        }
        if (character == '"' || character == '\'') {
            int quote = character;
            int escaped = 0;
            while ((character = fgetc(file)) != EOF) {
                if (!escaped && character == quote) {
                    break;
                }
                if (!escaped && character == '\\') {
                    escaped = 1;
                } else {
                    escaped = 0;
                }
            }
            continue;
        }
        break;
    }

    if (isalpha((unsigned char)character) || character == '_') {
        size_t length = 0U;
        do {
            if (length + 1U < capacity) {
                token[length++] = (char)character;
            }
            character = fgetc(file);
        } while (character != EOF
                 && (isalnum((unsigned char)character) || character == '_'));
        if (character != EOF) {
            (void)ungetc(character, file);
        }
        token[length] = '\0';
        return 1;
    }

    token[0] = (char)character;
    token[1] = '\0';
    return 1;
}

char *cbox_read_cpp_module_name(const char *source) {
    FILE *file = fopen(source, "rb");
    char token[256];
    char *name = NULL;

    if (file == NULL) {
        fprintf(stderr, "CBox: cannot read C++ module source '%s'\n", source);
        return NULL;
    }
    while (next_cpp_token(file, token, sizeof(token))) {
        if (strcmp(token, "export") != 0) {
            continue;
        }
        if (!next_cpp_token(file, token, sizeof(token))
            || strcmp(token, "module") != 0) {
            continue;
        }
        {
            size_t length = 0U;
            size_t capacity = 64U;
            char *result = malloc(capacity);
            bool valid = result != NULL;
            bool terminated = false;

            while (valid && next_cpp_token(file, token, sizeof(token))) {
                if (strcmp(token, ";") == 0) {
                    if (length == 0U) {
                        valid = false;
                    } else {
                        terminated = true;
                    }
                    break;
                }
                if (!(isalpha((unsigned char)token[0]) || token[0] == '_'
                      || strcmp(token, ".") == 0 || strcmp(token, ":") == 0)) {
                    valid = false;
                    break;
                }
                if (length + strlen(token) + 1U > capacity) {
                    size_t new_capacity = capacity * 2U;
                    char *resized;
                    if (new_capacity < capacity
                        || new_capacity < length + strlen(token) + 1U) {
                        valid = false;
                        break;
                    }
                    resized = realloc(result, new_capacity);
                    if (resized == NULL) {
                        valid = false;
                        break;
                    }
                    result = resized;
                    capacity = new_capacity;
                }
                memcpy(result + length, token, strlen(token));
                length += strlen(token);
                result[length] = '\0';
            }
            if (valid && terminated && length > 0U) {
                name = result;
            } else {
                free(result);
                fprintf(stderr, "CBox: invalid named module declaration in '%s'\n",
                        source);
            }
        }
        break;
    }
    if (fclose(file) != 0) {
        free(name);
        return NULL;
    }
    if (name == NULL) {
        fprintf(stderr, "CBox: no exported named module declaration found in '%s'\n",
                source);
    }
    return name;
}

static bool append_cpp_module_import(char ***imports, size_t *import_count,
                                    const char *name, size_t name_length) {
    char **resized;
    char *copy;
    size_t index;

    for (index = 0U; index < *import_count; ++index) {
        if (strlen((*imports)[index]) == name_length
            && memcmp((*imports)[index], name, name_length) == 0) {
            return true;
        }
    }
    if (*import_count >= SIZE_MAX / sizeof(**imports)
        || name_length == SIZE_MAX) {
        return false;
    }
    copy = malloc(name_length + 1U);
    if (copy == NULL) return false;
    memcpy(copy, name, name_length);
    copy[name_length] = '\0';
    resized = realloc(*imports, (*import_count + 1U) * sizeof(**imports));
    if (resized == NULL) {
        free(copy);
        return false;
    }
    *imports = resized;
    (*imports)[(*import_count)++] = copy;
    return true;
}

void cbox_free_cpp_module_imports(char **imports, size_t import_count) {
    size_t index;
    for (index = 0U; index < import_count; ++index) {
        free(imports[index]);
    }
    free(imports);
}

bool cbox_read_cpp_module_imports(const char *source,
                                 char ***imports, size_t *import_count) {
    FILE *file;
    char token[256];
    bool success = true;

    if (imports == NULL || import_count == NULL || source == NULL) return false;
    *imports = NULL;
    *import_count = 0U;
    file = fopen(source, "rb");
    if (file == NULL) {
        fprintf(stderr, "CBox: cannot read C++ module source '%s'\n", source);
        return false;
    }
    while (success && next_cpp_token(file, token, sizeof(token))) {
        char *name = NULL;
        size_t name_length = 0U;
        size_t name_capacity = 0U;
        bool valid_name = true;
        bool terminated = false;

        if (strcmp(token, "import") != 0) continue;
        while (next_cpp_token(file, token, sizeof(token))) {
            size_t token_length;
            char *resized;
            if (strcmp(token, ";") == 0) {
                terminated = true;
                break;
            }
            if (!(isalpha((unsigned char)token[0]) || token[0] == '_'
                  || strcmp(token, ".") == 0 || strcmp(token, ":") == 0)) {
                valid_name = false;
            }
            if (!valid_name) continue;
            token_length = strlen(token);
            if (token_length > SIZE_MAX - name_length - 1U) {
                success = false;
                break;
            }
            if (name_length + token_length + 1U > name_capacity) {
                size_t new_capacity = name_capacity == 0U ? 64U : name_capacity;
                while (new_capacity < name_length + token_length + 1U) {
                    if (new_capacity > SIZE_MAX / 2U) {
                        success = false;
                        break;
                    }
                    new_capacity *= 2U;
                }
                if (!success) break;
                resized = realloc(name, new_capacity);
                if (resized == NULL) {
                    success = false;
                    break;
                }
                name = resized;
                name_capacity = new_capacity;
            }
            memcpy(name + name_length, token, token_length);
            name_length += token_length;
        }
        if (success && valid_name && terminated && name_length > 0U) {
            success = append_cpp_module_import(imports, import_count,
                                               name, name_length);
        }
        free(name);
    }
    if (ferror(file)) success = false;
    if (fclose(file) != 0) success = false;
    if (!success) {
        cbox_free_cpp_module_imports(*imports, *import_count);
        *imports = NULL;
        *import_count = 0U;
    }
    return success;
}

bool cbox_prepare_object_paths(const char *target_directory,
                               const char *const sources[], size_t count,
                               const char *profile_name, const char *extension,
                               char ***paths) {
    char directory[512];
    char profile_directory[256];
    char **object_paths;
    size_t index;
    size_t profile_length;

    if (paths == NULL || target_directory == NULL || sources == NULL
        || profile_name == NULL
        || extension == NULL || count == 0U
        || count > SIZE_MAX / sizeof(*object_paths)) {
        return false;
    }
    *paths = NULL;
    profile_length = strlen(profile_name);
    if (profile_length == 0U || profile_length >= sizeof(profile_directory)) {
        return false;
    }
    for (index = 0U; index < profile_length; ++index) {
        unsigned char character = (unsigned char)profile_name[index];
        profile_directory[index] = ((character >= 'a' && character <= 'z')
            || (character >= 'A' && character <= 'Z')
            || (character >= '0' && character <= '9') || character == '-'
            || character == '_') ? (char)character : '_';
    }
    profile_directory[profile_length] = '\0';
    {
        int required = snprintf(directory, sizeof(directory),
                                "CBoxBuild/%s/%s/obj",
                                target_directory, profile_directory);
        if (required < 0 || required >= (int)sizeof(directory)
            || !cbox_create_directories(directory)) {
            fprintf(stderr, "CBox: failed to create object directory\n");
            return false;
        }
    }

    object_paths = calloc(count, sizeof(*object_paths));
    if (object_paths == NULL) {
        return false;
    }
    for (index = 0; index < count; ++index) {
        unsigned char digest[32];
        char identity[33];
        char *normalized;
        size_t source_length;
        size_t byte_index;
        int required;

        if (sources[index] == NULL || sources[index][0] == '\0') {
            cbox_discard_object_paths(object_paths, count);
            return false;
        }
        source_length = strlen(sources[index]);
        normalized = malloc(source_length + 1U);
        if (normalized == NULL) {
            cbox_discard_object_paths(object_paths, count);
            return false;
        }
        for (byte_index = 0U; byte_index < source_length; ++byte_index) {
            normalized[byte_index] = sources[index][byte_index] == '\\'
                ? '/' : sources[index][byte_index];
        }
        normalized[source_length] = '\0';
        cbox_sha256_buffer(normalized, source_length, digest);
        free(normalized);
        for (byte_index = 0U; byte_index < 16U; ++byte_index) {
            (void)snprintf(identity + byte_index * 2U,
                           sizeof(identity) - byte_index * 2U,
                           "%02x", digest[byte_index]);
        }
        required = snprintf(NULL, 0, "%s/object-%s%s", directory, identity,
                            extension);
        if (required < 0) {
            cbox_discard_object_paths(object_paths, count);
            return false;
        }
        object_paths[index] = malloc((size_t)required + 1U);
        if (object_paths[index] == NULL) {
            cbox_discard_object_paths(object_paths, count);
            return false;
        }
        (void)snprintf(object_paths[index], (size_t)required + 1U,
                       "%s/object-%s%s", directory, identity, extension);
        {
            size_t previous;
            for (previous = 0U; previous < index; ++previous) {
                if (strcmp(object_paths[previous], object_paths[index]) == 0) {
                    fprintf(stderr,
                            "CBox: source paths '%s' and '%s' map to the same object\n",
                            sources[previous], sources[index]);
                    cbox_discard_object_paths(object_paths, count);
                    return false;
                }
            }
        }
    }
    *paths = object_paths;
    return true;
}

void cbox_discard_object_paths(char **paths, size_t count) {
    size_t index;
    if (paths == NULL) {
        return;
    }
    for (index = 0; index < count; ++index) {
        if (paths[index] != NULL) {
            free(paths[index]);
        }
    }
    free(paths);
}