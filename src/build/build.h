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

#ifndef CBOX_BUILD_H
#define CBOX_BUILD_H

#include "cbox.h"

#include <stdbool.h>

bool cbox_build_target(const cbox_target *target, const cbox_profile *profile,
					   const char *compiler);
bool cbox_build_target_freestanding(const cbox_target *target,
									const cbox_profile *profile,
									const char *compiler);
bool cbox_build_target_assembly(const cbox_target *target,
								const cbox_profile *profile,
								const char *compiler, bool one_assembly);
bool cbox_check_target(const cbox_target *target, const cbox_profile *profile,
					   const char *compiler);
const char *cbox_resolve_compiler(const char *compiler, bool needs_cpp);

#endif