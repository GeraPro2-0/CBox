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

#ifndef _WIN32
#define _XOPEN_SOURCE 700
#endif

#include "platform/filesystem.h"

#include <stdio.h>
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#endif

typedef struct {
    const char *name;
    bool is_directory;
} cbox_directory_entry;

typedef bool (*cbox_entry_callback)(const cbox_directory_entry *entry, void *context);

typedef struct {
    const char *const *segments;
    size_t segment_count;
    cbox_path_list *paths;
    bool include_directories;
} cbox_glob_context;

static char *duplicate_string(const char *text) {
    size_t length = strlen(text);
    char *copy = malloc(length + 1U);
    if (copy != NULL) {
        memcpy(copy, text, length + 1U);
    }
    return copy;
}

static char *join_path(const char *directory, const char *name) {
    size_t directory_length = strlen(directory);
    size_t name_length = strlen(name);
    size_t path_length;
    bool needs_separator = directory_length > 0U
        && directory[directory_length - 1U] != '/';
    char *path;

    if (directory_length > SIZE_MAX - name_length) {
        return NULL;
    }
    path_length = directory_length + name_length;
    if (needs_separator) {
        if (path_length == SIZE_MAX) {
            return NULL;
        }
        ++path_length;
    }
    if (path_length == SIZE_MAX) {
        return NULL;
    }
    path = malloc(path_length + 1U);
    if (path == NULL) {
        return NULL;
    }
    memcpy(path, directory, directory_length);
    if (needs_separator) {
        path[directory_length++] = '/';
    }
    memcpy(path + directory_length, name, name_length + 1U);
    return path;
}

static bool wildcard_match(const char *pattern, const char *text) {
    while (*pattern != '\0') {
        if (*pattern == '*') {
            while (*pattern == '*') {
                ++pattern;
            }
            if (*pattern == '\0') {
                return true;
            }
            do {
                if (wildcard_match(pattern, text)) {
                    return true;
                }
            } while (*text++ != '\0');
            return false;
        }
        if (*text == '\0' || (*pattern != '?' && *pattern != *text)) {
            return false;
        }
        ++pattern;
        ++text;
    }
    return *text == '\0';
}

static bool path_is_file(const char *path) {
#ifdef _WIN32
    DWORD attributes = GetFileAttributesA(path);
    return attributes != INVALID_FILE_ATTRIBUTES
        && (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
#else
    struct stat info;
    return stat(path, &info) == 0 && S_ISREG(info.st_mode);
#endif
}

static bool path_is_directory(const char *path) {
#ifdef _WIN32
    DWORD attributes = GetFileAttributesA(path);
    return attributes != INVALID_FILE_ATTRIBUTES
        && (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0
        && (attributes & FILE_ATTRIBUTE_REPARSE_POINT) == 0;
#else
    struct stat info;
    struct stat link_info;
    return lstat(path, &link_info) == 0 && S_ISDIR(link_info.st_mode)
        && stat(path, &info) == 0 && S_ISDIR(info.st_mode);
#endif
}

bool cbox_create_directory(const char *path) {
    if (path == NULL || path[0] == '\0') {
        return false;
    }
#ifdef _WIN32
    if (CreateDirectoryA(path, NULL)) {
        return true;
    }
    return GetLastError() == ERROR_ALREADY_EXISTS && path_is_directory(path);
#else
    if (mkdir(path, 0777) == 0) {
        return true;
    }
    return errno == EEXIST && path_is_directory(path);
#endif
}

bool cbox_create_directories(const char *path) {
    char *copy;
    size_t length;
    size_t index;

    if (path == NULL || path[0] == '\0') return false;
    length = strlen(path);
    copy = malloc(length + 1U);
    if (copy == NULL) return false;
    memcpy(copy, path, length + 1U);
    for (index = 1U; index <= length; ++index) {
        if (copy[index] == '/' || copy[index] == '\\' || copy[index] == '\0') {
            char separator = copy[index];
            copy[index] = '\0';
            if (copy[0] != '\0'
#ifdef _WIN32
                && !(index == 2U && copy[1] == ':')
#endif
                && !cbox_create_directory(copy)) {
                free(copy);
                return false;
            }
            copy[index] = separator;
        }
    }
    free(copy);
    return true;
}

static bool visit_directory(const char *directory, cbox_entry_callback callback,
                            void *context) {
#ifdef _WIN32
    char *search_path = join_path(directory, "*");
    WIN32_FIND_DATAA data;
    HANDLE find_handle;
    bool success = true;
    if (search_path == NULL) {
        return false;
    }
    find_handle = FindFirstFileA(search_path, &data);
    free(search_path);
    if (find_handle == INVALID_HANDLE_VALUE) {
        return true;
    }
    do {
        cbox_directory_entry entry;
        if (strcmp(data.cFileName, ".") == 0 || strcmp(data.cFileName, "..") == 0) {
            continue;
        }
        entry.name = data.cFileName;
        entry.is_directory = (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0
            && (data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) == 0;
        if (!callback(&entry, context)) {
            success = false;
            break;
        }
    } while (FindNextFileA(find_handle, &data));
    FindClose(find_handle);
    return success;
#else
    DIR *handle = opendir(directory[0] == '\0' ? "." : directory);
    struct dirent *item;
    bool success = true;
    if (handle == NULL) {
        return true;
    }
    while ((item = readdir(handle)) != NULL) {
        cbox_directory_entry entry;
        char *path;
        struct stat info;
        struct stat link_info;
        if (strcmp(item->d_name, ".") == 0 || strcmp(item->d_name, "..") == 0) {
            continue;
        }
        path = join_path(directory, item->d_name);
        if (path == NULL) {
            success = false;
            break;
        }
        entry.name = item->d_name;
        entry.is_directory = lstat(path, &link_info) == 0 && S_ISDIR(link_info.st_mode)
            && stat(path, &info) == 0 && S_ISDIR(info.st_mode);
        free(path);
        if (!callback(&entry, context)) {
            success = false;
            break;
        }
    }
    closedir(handle);
    return success;
#endif
}

static bool append_path(cbox_path_list *paths, const char *path) {
    char **items;

    if (paths->count >= SIZE_MAX / sizeof(*items)) {
        return false;
    }
    items = realloc(paths->items, (paths->count + 1U) * sizeof(*items));
    if (items == NULL) {
        return false;
    }
    paths->items = items;
    paths->items[paths->count] = duplicate_string(path);
    if (paths->items[paths->count] == NULL) {
        return false;
    }
    ++paths->count;
    return true;
}

static bool expand_segments(cbox_glob_context *context, size_t segment_index,
                            const char *directory);

typedef struct {
    cbox_glob_context *glob;
    size_t segment_index;
    const char *directory;
    const char *segment;
    bool recursive_segment;
} cbox_visit_context;

static bool visit_glob_entry(const cbox_directory_entry *entry, void *opaque) {
    cbox_visit_context *context = opaque;
    char *child_path;
    bool success = true;
    if (entry->name[0] == '.'
        && (context->recursive_segment || context->segment[0] != '.')) {
        return true;
    }
    if (!context->recursive_segment
        && !wildcard_match(context->segment, entry->name)) {
        return true;
    }
    child_path = join_path(context->directory, entry->name);
    if (child_path == NULL) {
        return false;
    }
    if (context->recursive_segment) {
        if (entry->is_directory) {
            success = expand_segments(context->glob, context->segment_index, child_path);
        }
    } else if (context->segment_index + 1U == context->glob->segment_count) {
        if ((!entry->is_directory && path_is_file(child_path))
            || (context->glob->include_directories && entry->is_directory
                && path_is_directory(child_path))) {
            success = append_path(context->glob->paths, child_path);
        }
    } else if (entry->is_directory) {
        success = expand_segments(context->glob, context->segment_index + 1U, child_path);
    }
    free(child_path);
    return success;
}

static bool expand_segments(cbox_glob_context *context, size_t segment_index,
                            const char *directory) {
    const char *segment;
    cbox_visit_context visit_context;
    if (segment_index == context->segment_count) {
        if (path_is_file(directory)
            || (context->include_directories && path_is_directory(directory))) {
            return append_path(context->paths, directory);
        }
        return true;
    }
    segment = context->segments[segment_index];
    if (strcmp(segment, "**") == 0) {
        if (!expand_segments(context, segment_index + 1U, directory)) {
            return false;
        }
        visit_context.glob = context;
        visit_context.segment_index = segment_index;
        visit_context.directory = directory;
        visit_context.segment = segment;
        visit_context.recursive_segment = true;
        return visit_directory(directory[0] == '\0' ? "." : directory,
                               visit_glob_entry, &visit_context);
    }
    if (strchr(segment, '*') == NULL && strchr(segment, '?') == NULL) {
        char *child_path = join_path(directory, segment);
        bool success;
        if (child_path == NULL) {
            return false;
        }
        if (segment_index + 1U == context->segment_count) {
            success = (path_is_file(child_path)
                || (context->include_directories && path_is_directory(child_path)))
                ? append_path(context->paths, child_path) : true;
        } else {
            success = path_is_directory(child_path)
                ? expand_segments(context, segment_index + 1U, child_path) : true;
        }
        free(child_path);
        return success;
    }
    visit_context.glob = context;
    visit_context.segment_index = segment_index;
    visit_context.directory = directory;
    visit_context.segment = segment;
    visit_context.recursive_segment = false;
    return visit_directory(directory[0] == '\0' ? "." : directory,
                           visit_glob_entry, &visit_context);
}

static int compare_paths(const void *left, const void *right) {
    const char *const *left_path = left;
    const char *const *right_path = right;
    return strcmp(*left_path, *right_path);
}

char *cbox_path_identity(const char *path) {
    if (path == NULL) return NULL;
#ifdef _WIN32
    DWORD required = GetFullPathNameA(path, 0U, NULL, NULL);
    char *absolute;
    DWORD length;
    size_t index;
    if (required == 0U || required == MAXDWORD) return NULL;
    absolute = malloc((size_t)required + 1U);
    if (absolute == NULL) return NULL;
    length = GetFullPathNameA(path, required + 1U, absolute, NULL);
    if (length == 0U || length > required) {
        free(absolute);
        return NULL;
    }
    for (index = 0U; index < length; ++index) {
        if (absolute[index] == '/') absolute[index] = '\\';
    }
    return absolute;
#else
    char *resolved = realpath(path, NULL);
    char *absolute;
    char **segments;
    char *normalized;
    char *cursor;
    size_t length;
    size_t segment_count = 0U;
    size_t output_length = 1U;

    if (resolved != NULL) return resolved;
    if (path[0] == '/') {
        absolute = duplicate_string(path);
    } else {
        char *working_directory = getcwd(NULL, 0U);
        size_t base_length;
        size_t path_length = strlen(path);
        if (working_directory == NULL) return NULL;
        base_length = strlen(working_directory);
        if (base_length > SIZE_MAX - path_length - 2U) {
            free(working_directory);
            return NULL;
        }
        absolute = malloc(base_length + path_length + 2U);
        if (absolute != NULL) {
            memcpy(absolute, working_directory, base_length);
            absolute[base_length] = '/';
            memcpy(absolute + base_length + 1U, path, path_length + 1U);
        }
        free(working_directory);
    }
    if (absolute == NULL) return NULL;
    length = strlen(absolute);
    if (length == SIZE_MAX || length + 1U > SIZE_MAX / sizeof(*segments)) {
        free(absolute);
        return NULL;
    }
    segments = malloc((length + 1U) * sizeof(*segments));
    normalized = malloc(length + 2U);
    if (segments == NULL || normalized == NULL) {
        free(segments);
        free(normalized);
        free(absolute);
        return NULL;
    }
    cursor = absolute;
    while (*cursor != '\0') {
        char *segment;
        while (*cursor == '/') ++cursor;
        if (*cursor == '\0') break;
        segment = cursor;
        while (*cursor != '\0' && *cursor != '/') ++cursor;
        if (*cursor == '/') *cursor++ = '\0';
        if (segment[0] == '\0' || strcmp(segment, ".") == 0) continue;
        if (strcmp(segment, "..") == 0) {
            if (segment_count > 0U) --segment_count;
        } else {
            segments[segment_count++] = segment;
        }
    }
    normalized[0] = '/';
    for (size_t index = 0U; index < segment_count; ++index) {
        size_t segment_length = strlen(segments[index]);
        if (index > 0U) normalized[output_length++] = '/';
        memcpy(normalized + output_length, segments[index], segment_length);
        output_length += segment_length;
    }
    normalized[output_length] = '\0';
    free(segments);
    free(absolute);
    return normalized;
#endif
}

bool cbox_path_identity_equal(const char *left, const char *right) {
    if (left == NULL || right == NULL) return false;
#ifdef _WIN32
    return _stricmp(left, right) == 0;
#else
    return strcmp(left, right) == 0;
#endif
}

typedef struct {
    char *path;
    char *identity;
} cbox_path_identity_entry;

static int compare_path_identities(const void *left, const void *right) {
    const cbox_path_identity_entry *left_entry = left;
    const cbox_path_identity_entry *right_entry = right;
    int identity_order;
#ifdef _WIN32
    identity_order = _stricmp(left_entry->identity, right_entry->identity);
#else
    identity_order = strcmp(left_entry->identity, right_entry->identity);
#endif
    if (identity_order != 0) return identity_order;
#ifdef _WIN32
    {
        bool left_absolute = left_entry->path[0] == '\\'
            || left_entry->path[0] == '/'
            || (left_entry->path[0] != '\0' && left_entry->path[1] == ':');
        bool right_absolute = right_entry->path[0] == '\\'
            || right_entry->path[0] == '/'
            || (right_entry->path[0] != '\0' && right_entry->path[1] == ':');
#else
    {
        bool left_absolute = left_entry->path[0] == '/';
        bool right_absolute = right_entry->path[0] == '/';
#endif
        if (left_absolute != right_absolute) return left_absolute ? 1 : -1;
    }
    return strcmp(left_entry->path, right_entry->path);
}

static bool deduplicate_path_list(cbox_path_list *paths) {
    cbox_path_identity_entry *entries;
    size_t index;
    size_t unique_count = 0U;
    if (paths->count < 2U) return true;
    if (paths->count > SIZE_MAX / sizeof(*entries)) return false;
    entries = malloc(paths->count * sizeof(*entries));
    if (entries == NULL) return false;
    for (index = 0U; index < paths->count; ++index) {
        entries[index].path = paths->items[index];
        entries[index].identity = cbox_path_identity(paths->items[index]);
        if (entries[index].identity == NULL) {
            while (index > 0U) free(entries[--index].identity);
            free(entries);
            return false;
        }
    }
    // Las rutas equivalentes con distinta escritura son bien castrosas: compare
    // identities, and keep them alive until the next-entry comparison is done.
    qsort(entries, paths->count, sizeof(*entries), compare_path_identities);
    for (index = 0U; index < paths->count; ++index) {
        if (unique_count > 0U
#ifdef _WIN32
            && _stricmp(entries[index].identity,
                        entries[index - 1U].identity) == 0
#else
            && strcmp(entries[index].identity,
                      entries[index - 1U].identity) == 0
#endif
            ) {
            free(entries[index].path);
        } else {
            paths->items[unique_count++] = entries[index].path;
        }
    }
    for (index = 0U; index < paths->count; ++index) {
        free(entries[index].identity);
    }
    paths->count = unique_count;
    free(entries);
    return true;
}

static bool expand_one_pattern(const char *pattern, cbox_path_list *paths,
                               bool include_directories, bool require_match) {
    char *normalized = duplicate_string(pattern);
    char **segments;
    char *cursor;
    char *root = NULL;
    const char *root_path = "";
    size_t segment_count = 0U;
    size_t capacity = 4U;
    size_t old_count = paths->count;
    size_t index;
    cbox_glob_context context;
    bool success;
    if (normalized == NULL) {
        return false;
    }
    for (cursor = normalized; *cursor != '\0'; ++cursor) {
        if (*cursor == '\\') {
            *cursor = '/';
        }
    }
    segments = malloc(capacity * sizeof(*segments));
    if (segments == NULL) {
        free(normalized);
        return false;
    }
    cursor = normalized;
    if (cursor[0] == '/' || (cursor[0] != '\0' && cursor[1] == ':' && cursor[2] == '/')) {
        size_t root_length = cursor[0] == '/' ? 1U : 3U;
        root = malloc(root_length + 1U);
        if (root == NULL) {
            free(segments);
            free(normalized);
            return false;
        }
        memcpy(root, cursor, root_length);
        root[root_length] = '\0';
        root_path = root;
        cursor += root_length;
    }
    while (*cursor != '\0') {
        char *start;
        while (*cursor == '/') ++cursor;
        if (*cursor == '\0') break;
        start = cursor;
        while (*cursor != '\0' && *cursor != '/') ++cursor;
        if (*cursor == '/') *cursor++ = '\0';
        if (strcmp(start, ".") == 0) {
            continue;
        }
        if (segment_count == capacity) {
            char **new_segments;
            if (capacity > SIZE_MAX / (2U * sizeof(*segments))) {
                success = false;
                goto cleanup;
            }
            capacity *= 2U;
            new_segments = realloc(segments, capacity * sizeof(*segments));
            if (new_segments == NULL) {
                success = false;
                goto cleanup;
            }
            segments = new_segments;
        }
        segments[segment_count++] = start;
    }
    context.segments = (const char *const *)segments;
    context.segment_count = segment_count;
    context.paths = paths;
    context.include_directories = include_directories;
    success = segment_count == 0U
        ? (path_is_file(root_path) ? append_path(paths, root_path) : true)
        : expand_segments(&context, 0U, root_path);
    if (success && require_match && paths->count == old_count) {
        fprintf(stderr, "CBox: source pattern matched no files: %s\n", pattern);
        success = false;
    }

cleanup:
    free(root);
    free(segments);
    free(normalized);
    for (index = old_count; !success && index < paths->count; ++index) {
        free(paths->items[index]);
    }
    if (!success) paths->count = old_count;
    return success;
}

bool cbox_expand_globs(const char *const patterns[], size_t pattern_count,
                       cbox_path_list *paths) {
    size_t index;
    if (paths == NULL || (patterns == NULL && pattern_count != 0U)) {
        return false;
    }
    paths->items = NULL;
    paths->count = 0U;
    for (index = 0; index < pattern_count; ++index) {
        if (patterns[index] == NULL
            || !expand_one_pattern(patterns[index], paths, false, true)) {
            cbox_path_list_free(paths);
            return false;
        }
    }
    if (paths->count > 1U) {
        qsort(paths->items, paths->count, sizeof(*paths->items), compare_paths);
    }
    if (paths->count > 1U && !deduplicate_path_list(paths)) {
        fprintf(stderr, "CBox: failed to normalize source paths\n");
        cbox_path_list_free(paths);
        return false;
    }
    return true;
}

static bool delete_pattern_is_safe(const char *pattern) {
    const char *cursor;
    size_t segment_count = 0U;
    bool first_segment = true;

    if (pattern == NULL || pattern[0] == '/' || pattern[0] == '\\'
        || (pattern[0] != '\0' && pattern[1] == ':')) {
        return false;
    }
    cursor = pattern;
    while (*cursor != '\0') {
        const char *segment;
        size_t length;
        while (*cursor == '/' || *cursor == '\\') ++cursor;
        segment = cursor;
        while (*cursor != '\0' && *cursor != '/' && *cursor != '\\') ++cursor;
        length = (size_t)(cursor - segment);
        if (length == 2U && segment[0] == '.' && segment[1] == '.') {
            return false;
        }
        if (length > 0U && !(length == 1U && segment[0] == '.')) {
            if (first_segment
                && (memchr(segment, '*', length) != NULL
                    || memchr(segment, '?', length) != NULL)) {
                return false;
            }
            ++segment_count;
            first_segment = false;
        }
    }
    return segment_count > 0U;
}

static bool delete_path_is_safe(const char *path) {
    const char *cursor = path;
    size_t segment_count = 0U;

    if (path == NULL || path[0] == '\0') return false;
    while (*cursor != '\0') {
        const char *segment;
        size_t length;
        while (*cursor == '/' || *cursor == '\\') ++cursor;
        if (*cursor == '\0') break;
        segment = cursor;
        while (*cursor != '\0' && *cursor != '/' && *cursor != '\\') ++cursor;
        length = (size_t)(cursor - segment);
        if (length == 4U && segment[0] == '.'
            && (segment[1] == 'g' || segment[1] == 'G')
            && (segment[2] == 'i' || segment[2] == 'I')
            && (segment[3] == 't' || segment[3] == 'T')) {
            return false;
        }
        ++segment_count;
    }
    return segment_count >= 2U;
}

static bool delete_path_is_within_project(const char *path) {
#ifdef _WIN32
    DWORD root_capacity = GetFullPathNameA(".", 0U, NULL, NULL);
    DWORD path_capacity = GetFullPathNameA(path, 0U, NULL, NULL);
    char *root = NULL;
    char *absolute_path = NULL;
    size_t root_length;
    size_t path_length;
    size_t index;
    bool within_project = false;
    DWORD attributes;

    if (root_capacity == 0U || path_capacity == 0U) return false;
    root = malloc((size_t)root_capacity + 1U);
    absolute_path = malloc((size_t)path_capacity + 1U);
    if (root == NULL || absolute_path == NULL) goto cleanup;
    if (GetFullPathNameA(".", root_capacity + 1U, root, NULL) == 0U
        || GetFullPathNameA(path, path_capacity + 1U, absolute_path, NULL) == 0U) {
        goto cleanup;
    }
    root_length = strlen(root);
    path_length = strlen(absolute_path);
    while (root_length > 3U
           && (root[root_length - 1U] == '\\' || root[root_length - 1U] == '/')) {
        root[--root_length] = '\0';
    }
    if (path_length <= root_length) goto cleanup;
    for (index = 0U; index < root_length; ++index) {
        unsigned char root_character = (unsigned char)root[index];
        unsigned char path_character = (unsigned char)absolute_path[index];
        if (root_character == '/') root_character = '\\';
        if (path_character == '/') path_character = '\\';
        if (root_character >= 'A' && root_character <= 'Z') {
            root_character = (unsigned char)(root_character - 'A' + 'a');
        }
        if (path_character >= 'A' && path_character <= 'Z') {
            path_character = (unsigned char)(path_character - 'A' + 'a');
        }
        if (root_character != path_character) goto cleanup;
    }
    if (root[root_length - 1U] != '\\'
        && absolute_path[root_length] != '\\'
        && absolute_path[root_length] != '/') {
        goto cleanup;
    }
    attributes = GetFileAttributesA(path);
    if (attributes == INVALID_FILE_ATTRIBUTES
        || (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0U) {
        goto cleanup;
    }
    within_project = true;

cleanup:
    free(root);
    free(absolute_path);
    return within_project;
#else
    char *project_root = realpath(".", NULL);
    char *resolved_path = realpath(path, NULL);
    size_t root_length;
    bool within_project = false;

    if (project_root == NULL || resolved_path == NULL) goto cleanup;
    root_length = strlen(project_root);
    if (strcmp(project_root, "/") == 0) {
        within_project = resolved_path[0] == '/'
            && resolved_path[1] != '\0';
    } else {
        within_project = strncmp(resolved_path, project_root, root_length) == 0
            && resolved_path[root_length] == '/';
    }

cleanup:
    free(project_root);
    free(resolved_path);
    return within_project;
#endif
}

static bool delete_path_recursive(const char *path) {
#ifdef _WIN32
    DWORD attributes = GetFileAttributesA(path);
    if (attributes == INVALID_FILE_ATTRIBUTES) {
        DWORD error = GetLastError();
        return error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND;
    }
    if ((attributes & FILE_ATTRIBUTE_DIRECTORY) == 0) {
        return DeleteFileA(path) != 0;
    }
    if ((attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0) {
        return RemoveDirectoryA(path) != 0;
    }
    {
        char *search_path = join_path(path, "*");
        WIN32_FIND_DATAA data;
        HANDLE find_handle;
        bool success = true;
        if (search_path == NULL) {
            return false;
        }
        find_handle = FindFirstFileA(search_path, &data);
        free(search_path);
        if (find_handle == INVALID_HANDLE_VALUE) {
            return false;
        }
        do {
            char *child_path;
            if (strcmp(data.cFileName, ".") == 0 || strcmp(data.cFileName, "..") == 0) {
                continue;
            }
            child_path = join_path(path, data.cFileName);
            if (child_path == NULL || !delete_path_recursive(child_path)) {
                free(child_path);
                success = false;
                break;
            }
            free(child_path);
        } while (FindNextFileA(find_handle, &data));
        FindClose(find_handle);
        return success && RemoveDirectoryA(path) != 0;
    }
#else
    struct stat info;
    if (lstat(path, &info) != 0) {
        return errno == ENOENT;
    }
    if (!S_ISDIR(info.st_mode) || S_ISLNK(info.st_mode)) {
        return unlink(path) == 0;
    }
    {
        DIR *directory = opendir(path);
        struct dirent *entry;
        bool success = true;
        if (directory == NULL) {
            return false;
        }
        while ((entry = readdir(directory)) != NULL) {
            char *child_path;
            if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
                continue;
            }
            child_path = join_path(path, entry->d_name);
            if (child_path == NULL || !delete_path_recursive(child_path)) {
                free(child_path);
                success = false;
                break;
            }
            free(child_path);
        }
        closedir(directory);
        return success && rmdir(path) == 0;
    }
#endif
}

bool cbox_delete_globs(const char *const patterns[], size_t pattern_count) {
    cbox_path_list paths = {0};
    size_t pattern_index;
    size_t path_index;

    if (patterns == NULL && pattern_count != 0U) {
        return false;
    }
    for (pattern_index = 0U; pattern_index < pattern_count; ++pattern_index) {
        if (!delete_pattern_is_safe(patterns[pattern_index])
            || !expand_one_pattern(patterns[pattern_index], &paths, true, false)) {
            fprintf(stderr, "CBox: invalid or unexpandable delete pattern '%s'\n",
                    patterns[pattern_index] != NULL ? patterns[pattern_index] : "(null)");
            cbox_path_list_free(&paths);
            return false;
        }
    }
    for (path_index = 0U; path_index < paths.count; ++path_index) {
        if (!delete_path_is_safe(paths.items[path_index])
            || !delete_path_is_within_project(paths.items[path_index])) {
            fprintf(stderr, "CBox: refusing to delete path outside the project or a protected project path '%s'\n",
                    paths.items[path_index]);
            cbox_path_list_free(&paths);
            return false;
        }
    }
    for (path_index = 0U; path_index < paths.count; ++path_index) {
        if (!delete_path_recursive(paths.items[path_index])) {
            fprintf(stderr, "CBox: failed to delete '%s'\n", paths.items[path_index]);
            cbox_path_list_free(&paths);
            return false;
        }
    }
    cbox_path_list_free(&paths);
    return true;
}

void cbox_path_list_free(cbox_path_list *paths) {
    size_t index;
    if (paths == NULL) {
        return;
    }
    for (index = 0; index < paths->count; ++index) {
        free(paths->items[index]);
    }
    free(paths->items);
    paths->items = NULL;
    paths->count = 0U;
}