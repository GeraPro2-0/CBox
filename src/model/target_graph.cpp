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

#include "model/target_graph.h"

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

bool set_error(char *buffer, size_t capacity, const char *message) {
    if (buffer != nullptr && capacity > 0U) {
        (void)std::snprintf(buffer, capacity, "%s", message);
    }
    return false;
}

bool resolve_target(const std::unordered_map<std::string, size_t>& indices,
                    const std::unordered_set<std::string>& ambiguous,
                    const char *name, size_t *index,
                    char *error_message, size_t error_capacity) {
    if (name == nullptr) {
        return set_error(error_message, error_capacity,
                         "target dependency name is null");
    }
    const std::string key{name};
    if (ambiguous.find(key) != ambiguous.end()) {
        if (error_message != nullptr && error_capacity > 0U) {
            (void)std::snprintf(error_message, error_capacity,
                                "target name '%s' is ambiguous", name);
        }
        return false;
    }
    const auto found = indices.find(key);
    if (found == indices.end()) {
        if (error_message != nullptr && error_capacity > 0U) {
            (void)std::snprintf(error_message, error_capacity,
                                "unknown target '%s'", name);
        }
        return false;
    }
    *index = found->second;
    return true;
}

}

extern "C" bool cbox_target_graph_create_plan(
    const cbox_project *project, const cbox_target *root,
    cbox_target_graph_plan *plan, char *error_message,
    size_t error_capacity) {
    if (plan == nullptr) {
        return set_error(error_message, error_capacity,
                         "target graph plan output is null");
    }
    *plan = {};
    if (error_message != nullptr && error_capacity > 0U) {
        error_message[0] = '\0';
    }
    if (project == nullptr || root == nullptr || project->targets == nullptr
        || project->target_count == 0U) {
        return set_error(error_message, error_capacity,
                         "project has no targets or root is null");
    }
    if (project->target_count > std::numeric_limits<size_t>::max()
            / sizeof(*plan->target_indices)
        || project->target_count == std::numeric_limits<size_t>::max()) {
        return set_error(error_message, error_capacity,
                         "target dependency graph is too large");
    }

    try {
        const size_t target_count = project->target_count;
        size_t root_index = target_count;
        std::unordered_map<std::string, size_t> indices;
        std::unordered_set<std::string> ambiguous;
        indices.reserve(target_count);
        ambiguous.reserve(target_count);

        for (size_t index = 0U; index < target_count; ++index) {
            const cbox_target& target = project->targets[index];
            if (&target == root) {
                root_index = index;
            }
            if (target.name == nullptr) {
                continue;
            }
            const std::string name{target.name};
            const auto [iterator, inserted] = indices.emplace(name, index);
            if (!inserted) {
                ambiguous.insert(name);
                (void)iterator;
            }
        }
        if (root_index == target_count) {
            return set_error(error_message, error_capacity,
                             "selected target is not part of the project");
        }

        // Walk only dependencies reachable from the requested target; unrelated targets
        // should neither slow this build nor fail it because of their own cycle.
        std::vector<std::vector<size_t>> prerequisites(target_count);
        std::vector<std::vector<size_t>> dependents(target_count);
        std::vector<unsigned char> included(target_count, 0U);
        std::vector<size_t> pending;
        pending.reserve(target_count);
        included[root_index] = 1U;
        pending.push_back(root_index);

        while (!pending.empty()) {
            const size_t current_index = pending.back();
            pending.pop_back();
            const cbox_target& current = project->targets[current_index];
            auto append = [&](char *const names[], size_t count, bool linking) {
                if (count > 0U && names == nullptr) {
                    return set_error(error_message, error_capacity,
                                     "target dependency list is null");
                }
                for (size_t item = 0U; item < count; ++item) {
                    size_t dependency_index;
                    if (!resolve_target(indices, ambiguous, names[item],
                                        &dependency_index, error_message,
                                        error_capacity)) {
                        return false;
                    }
                    if (linking
                        && (project->targets[dependency_index].kind == nullptr
                            || std::string{project->targets[dependency_index].kind}
                                != "static")) {
                        if (error_message != nullptr && error_capacity > 0U) {
                            (void)std::snprintf(error_message, error_capacity,
                                                "link_targets entry '%s' must name a static target",
                                                names[item]);
                        }
                        return false;
                    }
                    if (std::find(prerequisites[current_index].begin(),
                                  prerequisites[current_index].end(),
                                  dependency_index)
                        == prerequisites[current_index].end()) {
                        prerequisites[current_index].push_back(dependency_index);
                        dependents[dependency_index].push_back(current_index);
                    }
                    if (included[dependency_index] == 0U) {
                        included[dependency_index] = 1U;
                        pending.push_back(dependency_index);
                    }
                }
                return true;
            };

            if (!append(current.depends_on, current.dependency_count, false)
                || !append(current.link_targets, current.link_target_count, true)) {
                return false;
            }
        }

        std::vector<size_t> remaining(target_count, 0U);
        size_t included_count = 0U;
        for (size_t index = 0U; index < target_count; ++index) {
            if (included[index] != 0U) {
                remaining[index] = prerequisites[index].size();
                ++included_count;
            }
        }

        std::vector<size_t> ordered;
        std::vector<size_t> level_offsets;
        ordered.reserve(included_count);
        level_offsets.reserve(included_count + 1U);
        level_offsets.push_back(0U);
        size_t completed = 0U;
        // Each pass is one build level. Targets in the same level can run together.
        while (completed < included_count) {
            const size_t level_start = ordered.size();
            for (size_t index = 0U; index < target_count; ++index) {
                if (included[index] != 0U && remaining[index] == 0U) {
                    ordered.push_back(index);
                    remaining[index] = std::numeric_limits<size_t>::max();
                }
            }
            if (ordered.size() == level_start) {
                return set_error(error_message, error_capacity,
                                 "target dependency cycle prevents building the selected target");
            }
            const size_t level_end = ordered.size();
            for (size_t position = level_start; position < level_end; ++position) {
                for (const size_t dependent : dependents[ordered[position]]) {
                    if (included[dependent] != 0U
                        && remaining[dependent] != std::numeric_limits<size_t>::max()) {
                        --remaining[dependent];
                    }
                }
            }
            completed += level_end - level_start;
            level_offsets.push_back(level_end);
        }

        plan->target_indices = static_cast<size_t *>(
            std::malloc(ordered.size() * sizeof(*plan->target_indices)));
        plan->level_offsets = static_cast<size_t *>(
            std::malloc(level_offsets.size() * sizeof(*plan->level_offsets)));
        if (plan->target_indices == nullptr || plan->level_offsets == nullptr) {
            cbox_target_graph_free_plan(plan);
            return set_error(error_message, error_capacity,
                             "out of memory creating target graph plan");
        }
        std::copy(ordered.begin(), ordered.end(), plan->target_indices);
        std::copy(level_offsets.begin(), level_offsets.end(),
                  plan->level_offsets);
        plan->target_count = ordered.size();
        plan->level_count = level_offsets.size() - 1U;
        return true;
    } catch (const std::exception&) {
        return set_error(error_message, error_capacity,
                         "failed to allocate target graph planning data");
    } catch (...) {
        return set_error(error_message, error_capacity,
                         "unexpected failure while planning target graph");
    }
}

extern "C" void cbox_target_graph_free_plan(cbox_target_graph_plan *plan) {
    if (plan == nullptr) {
        return;
    }
    std::free(plan->target_indices);
    std::free(plan->level_offsets);
    *plan = {};
}
