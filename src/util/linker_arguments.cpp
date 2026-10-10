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

#include <cstdlib>
#include <cstring>
#include <limits>
#include <new>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

bool is_library_path(const char *library) {
    return std::strchr(library, '/') != nullptr
        || std::strchr(library, '\\') != nullptr;
}

bool has_library_extension(const char *library) {
    static constexpr const char *extensions[] = {
        ".a", ".lib", ".so", ".dylib", ".dll.a"
    };
    const std::size_t length = std::strlen(library);
    for (const char *extension : extensions) {
        const std::size_t extension_length = std::strlen(extension);
        if (length >= extension_length
            && std::strcmp(library + length - extension_length, extension) == 0) {
            return true;
        }
    }
    return false;
}

std::string resolve_library(cbox_linker_kind kind, const char *library) {
    if (kind == CBOX_LINKER_MSVC && std::strcmp(library, "m") == 0) {
        return {};
    }
    if (is_library_path(library) || has_library_extension(library)
        || (kind == CBOX_LINKER_GNU
            && std::strncmp(library, "-l", 2U) == 0)) {
        return library;
    }
    if (kind == CBOX_LINKER_MSVC) {
        return std::string{library} + ".lib";
    }
    return std::string{"-l"} + library;
}

}

extern "C" bool cbox_linker_resolve_libraries(
    cbox_linker_kind kind, char *const libraries[], size_t library_count,
    char ***arguments, size_t *argument_count) {
    if (arguments == nullptr || argument_count == nullptr
        || (libraries == nullptr && library_count != 0U)
        || (kind != CBOX_LINKER_GNU && kind != CBOX_LINKER_MSVC)
        || library_count > std::numeric_limits<size_t>::max()
            / sizeof(char *)) {
        return false;
    }
    *arguments = nullptr;
    *argument_count = 0U;
    if (library_count == 0U) {
        return true;
    }

    char **resolved = nullptr;
    size_t resolved_count = 0U;
    try {
        std::vector<std::string> formatted;
        formatted.reserve(library_count);
        for (size_t index = 0U; index < library_count; ++index) {
            if (libraries[index] == nullptr || libraries[index][0] == '\0') {
                return false;
            }
            std::string argument = resolve_library(kind, libraries[index]);
            if (!argument.empty()) {
                formatted.push_back(std::move(argument));
            }
        }
        if (formatted.empty()) {
            return true;
        }

        resolved = static_cast<char **>(
            std::calloc(formatted.size(), sizeof(*resolved)));
        if (resolved == nullptr) {
            return false;
        }
        for (const std::string& argument : formatted) {
            if (argument.size() == std::numeric_limits<size_t>::max()) {
                cbox_linker_free_arguments(resolved, resolved_count);
                return false;
            }
            resolved[resolved_count] = static_cast<char *>(
                std::malloc(argument.size() + 1U));
            if (resolved[resolved_count] == nullptr) {
                cbox_linker_free_arguments(resolved, resolved_count);
                return false;
            }
            std::memcpy(resolved[resolved_count], argument.c_str(),
                        argument.size() + 1U);
            ++resolved_count;
        }
        *arguments = resolved;
        *argument_count = resolved_count;
        return true;
    } catch (const std::bad_alloc&) {
        cbox_linker_free_arguments(resolved, resolved_count);
        return false;
    } catch (const std::length_error&) {
        cbox_linker_free_arguments(resolved, resolved_count);
        return false;
    }
}

extern "C" void cbox_linker_free_arguments(char **arguments,
                                             size_t argument_count) {
    if (arguments == nullptr) {
        return;
    }
    for (size_t index = 0U; index < argument_count; ++index) {
        std::free(arguments[index]);
    }
    std::free(arguments);
}
