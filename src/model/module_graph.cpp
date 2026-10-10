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

#include "model/module_graph.h"
#define ARENALIB_POOL_BLOCKS 1
#define ARENALIB_POOL_BLOCK_SIZE 65536
#include "arenalib.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <limits>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace {

class scratch_arena {
public:
    scratch_arena() : initialized_(arenalib::init(&arena_, storage_,
                                                   sizeof(storage_))) {
        if (initialized_) {
            arenalib::set_oom_callback(&arena_, record_oom, this);
        }
    }

    ~scratch_arena() {
        for (void *allocation : heap_allocations_) {
            std::free(allocation);
        }
        if (initialized_) {
            arenalib::destroy(&arena_);
        }
    }

    scratch_arena(const scratch_arena&) = delete;
    scratch_arena& operator=(const scratch_arena&) = delete;

    bool initialized() const {
        return initialized_;
    }

    template <typename T>
    T *allocate(size_t count) {
        if (count == 0U || count > std::numeric_limits<size_t>::max() / sizeof(T)) {
            return nullptr;
        }
        failed_request_ = 0U;
        T *memory = arenalib::alloc<T>(&arena_, count);
        if (memory == nullptr) {
            const size_t bytes = count * sizeof(T);
            memory = static_cast<T *>(std::malloc(bytes));
            if (memory == nullptr) {
                if (failed_request_ != 0U) {
                    std::fprintf(stderr,
                        "CBox: out of memory: %zu-byte module graph arena request and %zu-byte heap fallback failed\n",
                        static_cast<size_t>(failed_request_), bytes);
                } else {
                    std::fprintf(stderr,
                        "CBox: out of memory allocating %zu bytes for module graph scratch data\n",
                        bytes);
                }
            } else {
                try {
                    heap_allocations_.push_back(memory);
                } catch (...) {
                    std::free(memory);
                    throw;
                }
            }
        }
        return memory;
    }

private:
    static void record_oom(void *user_data, arenalib_size_t size_requested) {
        static_cast<scratch_arena *>(user_data)->failed_request_ = size_requested;
    }

    arenalib::arena_t arena_{};
    unsigned char storage_[65536]{};
    bool initialized_;
    arenalib_size_t failed_request_ = 0U;
    std::vector<void *> heap_allocations_;
};

bool set_error(char *buffer, size_t capacity, const char *message) {
    if (buffer != nullptr && capacity > 0U) {
        (void)std::snprintf(buffer, capacity, "%s", message);
    }
    return false;
}

}

extern "C" bool cbox_module_graph_create_plan(
    const cbox_module_graph_node nodes[], size_t node_count,
    cbox_module_graph_plan *plan, char *error_message,
    size_t error_capacity) {
    if (plan == nullptr) {
        return set_error(error_message, error_capacity,
                         "module graph plan output is null");
    }
    *plan = {};
    if (error_message != nullptr && error_capacity > 0U) {
        error_message[0] = '\0';
    }
    if (nodes == nullptr || node_count == 0U) {
        return set_error(error_message, error_capacity,
                         "module graph has no nodes");
    }
    if (node_count >= std::numeric_limits<size_t>::max()
            / sizeof(*plan->module_indices)
        || node_count + 1U > std::numeric_limits<size_t>::max()
            / sizeof(*plan->level_offsets)) {
        return set_error(error_message, error_capacity,
                         "module graph is too large");
    }

    try {
        scratch_arena scratch;
        if (!scratch.initialized()) {
            return set_error(error_message, error_capacity,
                             "failed to initialize module graph scratch arena");
        }
        std::unordered_map<std::string, size_t> indices;
        indices.reserve(node_count);
        for (size_t index = 0U; index < node_count; ++index) {
            if (nodes[index].name == nullptr || nodes[index].name[0] == '\0') {
                return set_error(error_message, error_capacity,
                                 "module graph contains an empty module name");
            }
            if (!indices.emplace(nodes[index].name, index).second) {
                if (error_message != nullptr && error_capacity > 0U) {
                    (void)std::snprintf(error_message, error_capacity,
                                        "duplicate C++ module '%s'",
                                        nodes[index].name);
                }
                return false;
            }
        }

        std::vector<std::vector<size_t>> dependents(node_count);
        size_t *remaining = scratch.allocate<size_t>(node_count);
        if (remaining == nullptr) {
            return set_error(error_message, error_capacity,
                             "out of memory allocating module graph scratch data");
        }
        std::fill_n(remaining, node_count, 0U);
        for (size_t importer = 0U; importer < node_count; ++importer) {
            const cbox_module_graph_node& node = nodes[importer];
            if (node.import_count > 0U && node.imports == nullptr) {
                return set_error(error_message, error_capacity,
                                 "module import list is null");
            }
            // A partition import like ":detail" is relative to its primary module.
            const std::string importer_name{node.name};
            const size_t partition = importer_name.find(':');
            const std::string partition_prefix = partition == std::string::npos
                ? std::string{} : importer_name.substr(0U, partition + 1U);
            std::unordered_set<size_t> unique_providers;
            unique_providers.reserve(node.import_count);
            for (size_t import_index = 0U;
                 import_index < node.import_count; ++import_index) {
                if (node.imports[import_index] == nullptr
                    || node.imports[import_index][0] == '\0') {
                    return set_error(error_message, error_capacity,
                                     "module graph contains an empty import");
                }
                std::string imported_name{node.imports[import_index]};
                if (!partition_prefix.empty() && imported_name[0] == ':') {
                    imported_name.insert(0U, partition_prefix.substr(
                        0U, partition_prefix.size() - 1U));
                }
                const auto provider = indices.find(imported_name);
                if (provider == indices.end()) {
                    continue;
                }
                const size_t provider_index = provider->second;
                if (unique_providers.insert(provider_index).second) {
                    dependents[provider_index].push_back(importer);
                    ++remaining[importer];
                }
            }
        }

        size_t *ordered = scratch.allocate<size_t>(node_count);
        size_t *ready = scratch.allocate<size_t>(node_count);
        size_t *next_ready = scratch.allocate<size_t>(node_count);
        size_t *level_offsets = scratch.allocate<size_t>(node_count + 1U);
        if (ordered == nullptr || ready == nullptr || next_ready == nullptr
            || level_offsets == nullptr) {
            return set_error(error_message, error_capacity,
                             "out of memory allocating module graph scratch data");
        }
        size_t ordered_count = 0U;
        size_t ready_count = 0U;
        size_t next_ready_count = 0U;
        size_t level_count = 0U;
        level_offsets[level_count++] = 0U;
        for (size_t index = 0U; index < node_count; ++index) {
            if (remaining[index] == 0U) {
                ready[ready_count++] = index;
            }
        }
        // Keep each ready set as a separate level so callers can compile independent
        // modules concurrently without outrunning their imports.
        while (ready_count > 0U) {
            for (size_t ready_index = 0U; ready_index < ready_count;
                 ++ready_index) {
                const size_t module = ready[ready_index];
                ordered[ordered_count++] = module;
                for (const size_t dependent : dependents[module]) {
                    if (--remaining[dependent] == 0U) {
                        next_ready[next_ready_count++] = dependent;
                    }
                }
            }
            level_offsets[level_count++] = ordered_count;
            std::sort(next_ready, next_ready + next_ready_count);
            std::swap(ready, next_ready);
            ready_count = next_ready_count;
            next_ready_count = 0U;
        }
        if (ordered_count != node_count) {
            return set_error(error_message, error_capacity,
                             "C++ module import cycle prevents compilation");
        }

        plan->module_indices = static_cast<size_t *>(
            std::malloc(ordered_count * sizeof(*plan->module_indices)));
        plan->level_offsets = static_cast<size_t *>(
            std::malloc(level_count * sizeof(*plan->level_offsets)));
        if (plan->module_indices == nullptr || plan->level_offsets == nullptr) {
            cbox_module_graph_free_plan(plan);
            return set_error(error_message, error_capacity,
                             "out of memory creating module graph plan");
        }
        std::copy_n(ordered, ordered_count, plan->module_indices);
        std::copy_n(level_offsets, level_count, plan->level_offsets);
        plan->module_count = ordered_count;
        plan->level_count = level_count - 1U;
        return true;
    } catch (const std::exception&) {
        return set_error(error_message, error_capacity,
                         "failed to allocate module graph planning data");
    } catch (...) {
        return set_error(error_message, error_capacity,
                         "unexpected failure while planning module graph");
    }
}

extern "C" void cbox_module_graph_free_plan(cbox_module_graph_plan *plan) {
    if (plan == nullptr) {
        return;
    }
    std::free(plan->module_indices);
    std::free(plan->level_offsets);
    *plan = {};
}
