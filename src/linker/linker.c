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

#include "linker/linker.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *duplicate_string(const char *text) {
    size_t length = strlen(text);
    char *copy = malloc(length + 1U);
    if (copy != NULL) {
        memcpy(copy, text, length + 1U);
    }
    return copy;
}

static bool extension_equals(const char *left, const char *right) {
    while (*left != '\0' && *right != '\0') {
        char left_character = *left >= 'A' && *left <= 'Z'
            ? (char)(*left - 'A' + 'a') : *left;
        char right_character = *right >= 'A' && *right <= 'Z'
            ? (char)(*right - 'A' + 'a') : *right;
        if (left_character != right_character) return false;
        ++left;
        ++right;
    }
    return *left == '\0' && *right == '\0';
}

static bool is_known_target_extension(const char *target_kind,
                                     const char *extension) {
    static const char *executable_extensions[] = {".exe"};
    static const char *static_extensions[] = {".a", ".lib"};
    static const char *shared_extensions[] = {".dll", ".so", ".dylib"};
    const char *const *extensions;
    size_t extension_count;
    size_t index;

    /* Accept common platform extensions instead of appending a second one to an
       intentional user-supplied output name. */
    if (strcmp(target_kind, "executable") == 0) {
        extensions = executable_extensions;
        extension_count = sizeof(executable_extensions) / sizeof(executable_extensions[0]);
    } else if (strcmp(target_kind, "static") == 0) {
        extensions = static_extensions;
        extension_count = sizeof(static_extensions) / sizeof(static_extensions[0]);
    } else {
        extensions = shared_extensions;
        extension_count = sizeof(shared_extensions) / sizeof(shared_extensions[0]);
    }
    for (index = 0U; index < extension_count; ++index) {
        if (extension_equals(extension, extensions[index])) return true;
    }
    return false;
}

static const char *target_default_extension(const char *target_kind) {
    if (strcmp(target_kind, "executable") == 0) {
#if defined(_WIN32) || defined(_WIN64)
        return ".exe";
#else
        return "";
#endif
    }
    if (strcmp(target_kind, "static") == 0) {
#if defined(_WIN32) || defined(_WIN64)
        return ".lib";
#else
        return ".a";
#endif
    }
#if defined(_WIN32) || defined(_WIN64)
    return ".dll";
#elif defined(__APPLE__) && defined(__MACH__)
    return ".dylib";
#else
    return ".so";
#endif
}

char *cbox_linker_target_output(cbox_linker_kind kind, const char *target_kind,
                                const char *configured_output,
                                const char *target_name) {
    const char *basename;
    const char *slash;
    const char *backslash;
    const char *dot;
    const char *default_extension;
    size_t stem_length;
    size_t extension_length;
    char *output;

    if ((kind != CBOX_LINKER_GNU && kind != CBOX_LINKER_MSVC)
        || target_kind == NULL
        || (strcmp(target_kind, "executable") != 0
            && strcmp(target_kind, "static") != 0
            && strcmp(target_kind, "shared") != 0)) {
        return NULL;
    }
    if (configured_output == NULL) {
        if (target_name == NULL || target_name[0] == '\0') return NULL;
        if (strcmp(target_kind, "static") == 0) {
            return cbox_linker_static_library_output(kind, target_name);
        }
        if (strcmp(target_kind, "shared") == 0) {
            return cbox_linker_shared_library_output(kind, target_name);
        }
        configured_output = target_name;
    }

    slash = strrchr(configured_output, '/');
    backslash = strrchr(configured_output, '\\');
    basename = slash == NULL ? configured_output
        : backslash == NULL || slash > backslash ? slash + 1 : backslash + 1;
    if (backslash != NULL && (slash == NULL || backslash > slash)) {
        basename = backslash + 1;
    }
    if (basename[0] == '\0') return NULL;
    dot = strrchr(basename, '.');
    if (dot == basename || (dot != NULL && dot[1] == '\0')) dot = NULL;
    if (dot != NULL && !is_known_target_extension(target_kind, dot)) {
        return duplicate_string(basename);
    }

    stem_length = dot == NULL ? strlen(basename) : (size_t)(dot - basename);
    default_extension = target_default_extension(target_kind);
    extension_length = strlen(default_extension);
    if (stem_length > SIZE_MAX - extension_length - 1U) return NULL;
    output = malloc(stem_length + extension_length + 1U);
    if (output == NULL) return NULL;
    memcpy(output, basename, stem_length);
    memcpy(output + stem_length, default_extension, extension_length + 1U);
    return output;
}

char *cbox_linker_static_library_output(cbox_linker_kind kind,
                                        const char *target_name) {
    const char *format;
    int required;
    char *output;

    if (target_name == NULL || target_name[0] == '\0'
        || (kind != CBOX_LINKER_GNU && kind != CBOX_LINKER_MSVC)) {
        return NULL;
    }
#if defined(_WIN32) || defined(_WIN64)
    format = "%s.lib";
#else
    format = kind == CBOX_LINKER_MSVC ? "%s.lib" : "lib%s.a";
#endif
    required = snprintf(NULL, 0, format, target_name);
    if (required < 0 || (size_t)required == SIZE_MAX) {
        return NULL;
    }
    output = malloc((size_t)required + 1U);
    if (output != NULL) {
        (void)snprintf(output, (size_t)required + 1U, format, target_name);
    }
    return output;
}

char *cbox_linker_shared_library_output(cbox_linker_kind kind,
                                        const char *target_name) {
    const char *format;
    int required;
    char *output;

    if (target_name == NULL || target_name[0] == '\0'
        || (kind != CBOX_LINKER_GNU && kind != CBOX_LINKER_MSVC)) {
        return NULL;
    }
#if defined(_WIN32) || defined(_WIN64)
    format = "%s.dll";
#elif defined(__APPLE__) && defined(__MACH__)
    format = "lib%s.dylib";
#else
    format = "lib%s.so";
#endif
    required = snprintf(NULL, 0, format, target_name);
    if (required < 0 || (size_t)required == SIZE_MAX) {
        return NULL;
    }
    output = malloc((size_t)required + 1U);
    if (output != NULL) {
        (void)snprintf(output, (size_t)required + 1U, format, target_name);
    }
    return output;
}

char *cbox_linker_shared_import_output(cbox_linker_kind kind,
                                       const char *shared_output) {
#if defined(_WIN32) || defined(_WIN64)
    const char *suffix;
    const char *basename;
    size_t stem_length;
    size_t directory_length;
    const char *prefix;
    const char *extension;
    char *output;

    if (shared_output == NULL
        || (kind != CBOX_LINKER_GNU && kind != CBOX_LINKER_MSVC)) {
        return NULL;
    }
    basename = shared_output;
    {
        const char *slash = strrchr(shared_output, '/');
        const char *backslash = strrchr(shared_output, '\\');
        if (slash != NULL || backslash != NULL) {
            basename = slash == NULL ? backslash
                : backslash == NULL || slash > backslash ? slash : backslash;
            ++basename;
        }
    }
    suffix = strrchr(basename, '.');
    directory_length = (size_t)(basename - shared_output);
    stem_length = suffix == NULL ? strlen(basename) : (size_t)(suffix - basename);
    prefix = kind == CBOX_LINKER_GNU ? "lib" : "";
    {
        size_t prefix_length = strlen(prefix);
        extension = kind == CBOX_LINKER_GNU ? ".dll.a" : ".lib";
        if (stem_length > SIZE_MAX - directory_length - prefix_length
            - strlen(extension) - 1U) {
            return NULL;
        }
        output = malloc(directory_length + prefix_length + stem_length
                        + strlen(extension) + 1U);
        if (output == NULL) {
            return NULL;
        }
        memcpy(output, shared_output, directory_length);
        memcpy(output + directory_length, prefix, prefix_length);
        memcpy(output + directory_length + prefix_length, basename, stem_length);
        memcpy(output + directory_length + prefix_length + stem_length,
               extension, strlen(extension) + 1U);
    }
    return output;
#else
    (void)kind;
    (void)shared_output;
    return NULL;
#endif
}

// ¿Por qué vergas GCC/Clang y MSVC tienen flags de compilador tan diferentes?
// Por esa razón todos sufren con los linkers y el método de enlazado >:[