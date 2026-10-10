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

#ifndef CBOX_TOOLCHAIN_H
#define CBOX_TOOLCHAIN_H

#include "cbox.h"
#include "linker/linker.h"

#include <stdbool.h>

typedef bool (*cbox_driver_compile_fn)(const cbox_target *target,
									  const cbox_profile *profile, bool cpp, bool module,
									  const char *source, const char *object,
									  const char *module_output,
									  const char *const module_names[],
									  const char *const module_outputs[],
									  size_t module_count,
									  const char *module_mapper,
									  const char *dependency_file);
typedef bool (*cbox_driver_assemble_fn)(const cbox_target *target,
									   const cbox_profile *profile, bool cpp,
									   const char *source, const char *assembly,
									   const char *object);
typedef bool (*cbox_driver_check_fn)(const cbox_target *target,
									 const cbox_profile *profile, bool cpp,
									 const char *source);
typedef bool (*cbox_driver_archive_fn)(const cbox_target *target, const char *output,
									  char *const objects[], size_t object_count);
typedef bool (*cbox_driver_version_fn)(char *buffer, size_t capacity);
typedef bool (*cbox_driver_link_fn)(const cbox_target *target,
								   const cbox_profile *profile, const char *output,
								   char *const objects[], size_t object_count,
								   bool shared, const char *import_output);

typedef struct {
	const char *name;
	const char *object_extension;
	const char *module_extension;
	cbox_linker_kind linker_kind;
	cbox_driver_version_fn version;
	cbox_driver_compile_fn compile;
	cbox_driver_assemble_fn assemble;
	cbox_driver_check_fn check;
	cbox_driver_archive_fn archive;
	cbox_driver_link_fn link;
} cbox_toolchain_driver;

const cbox_toolchain_driver *cbox_gcc_driver(void);
const cbox_toolchain_driver *cbox_clang_driver(void);
const cbox_toolchain_driver *cbox_msvc_driver(void);
bool cbox_toolchain_build(const cbox_toolchain_driver *driver,
						  const cbox_target *target, const cbox_profile *profile);
bool cbox_toolchain_assemble(const cbox_toolchain_driver *driver,
							 const cbox_target *target,
							 const cbox_profile *profile, bool one_assembly);
bool cbox_toolchain_check(const cbox_toolchain_driver *driver,
						   const cbox_target *target,
						   const cbox_profile *profile);
bool cbox_msvc_available(void);
char *cbox_target_directory_name(const char *target_name);
char *cbox_build_directory_name(const char *compiler, const char *target_name);
char *cbox_target_artifact_path(cbox_linker_kind linker_kind,
                                const char *compiler,
                                const cbox_target *target,
                                const cbox_profile *profile);
bool cbox_prepare_object_paths(const char *target_directory,
							  const char *const sources[], size_t count,
							  const char *profile_name, const char *extension,
							  char ***paths);
void cbox_discard_object_paths(char **paths, size_t count);
char *cbox_read_cpp_module_name(const char *source);
bool cbox_read_cpp_module_imports(const char *source,
                                 char ***imports, size_t *import_count);
void cbox_free_cpp_module_imports(char **imports, size_t import_count);

#endif