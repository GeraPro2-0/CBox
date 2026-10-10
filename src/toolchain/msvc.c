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
#include "linker/linker.h"
#include "platform/process.h"
#include "util/terminal.h"

#ifdef _WIN32
#define ARENALIB_POOL_BLOCKS 1
#define ARENALIB_POOL_BLOCK_SIZE 65536
#include "arenalib.h"

#include <io.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

static int msvc_run_tool(const char *const arguments[], const char *vcvars_path);
static char *msvc_cached_vcvars_path;
static unsigned msvc_cached_version;
static char msvc_cached_compiler_identity[128];
static int msvc_toolchain_initialized;
static bool msvc_toolchain_available;
static bool msvc_availability_checked;
static bool msvc_cached_availability;
static SRWLOCK msvc_toolchain_lock = SRWLOCK_INIT;
static volatile LONG msvc_warned_c11_feature_gaps;
static volatile LONG msvc_warned_c17_feature_gaps;

static void msvc_arena_oom(void *user_data, arenalib_size_t size_requested) {
	const char *purpose = user_data != NULL ? (const char *)user_data : "temporary data";
	fprintf(stderr, "CBox: out of memory allocating %zu bytes for %s\n",
			(size_t)size_requested, purpose);
}

static char *msvc_vcvars_for_installation(const char *installation_path) {
	static const char suffix[] = "\\VC\\Auxiliary\\Build\\vcvars64.bat";
	size_t path_length = strlen(installation_path);
	char *vcvars_path;

	if (path_length > SIZE_MAX - sizeof(suffix)) return NULL;
	vcvars_path = malloc(path_length + sizeof(suffix));
	if (vcvars_path == NULL) return NULL;
	(void)snprintf(vcvars_path, path_length + sizeof(suffix), "%s%s",
				   installation_path, suffix);
	if (_access(vcvars_path, 0) != 0) {
		free(vcvars_path);
		return NULL;
	}
	return vcvars_path;
}

// No saben lo culero que fue hacer que se pueda llamar a cl.exe y manejar vsvarsall.bat.
// Chinga tu madre, Microsoft.
static char *detect_vcvars64_bat(void) {
	static const char *candidate_roots[] = {
		"C:\\Program Files (x86)\\Microsoft Visual Studio",
		"C:\\Program Files\\Microsoft Visual Studio",
		NULL
	};
	char command[2048];
	char result[1024];
	size_t root_index;

	for (root_index = 0; candidate_roots[root_index] != NULL; ++root_index) {
		const char *root = candidate_roots[root_index];
		FILE *pipe;

		(void)snprintf(result, sizeof(result), "%s\\Installer\\vswhere.exe", root);
		if (_access(result, 0) != 0) continue;
		(void)snprintf(command, sizeof(command),
			"\"\"%s\" -all -products * -requires "
			"Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath\"",
			result);
		pipe = _popen(command, "r");
		if (pipe == NULL) continue;
		while (fgets(result, sizeof(result), pipe) != NULL) {
			char *vcvars_path;
			result[strcspn(result, "\r\n")] = '\0';
			if (result[0] == '\0') continue;
			vcvars_path = msvc_vcvars_for_installation(result);
			if (vcvars_path != NULL) {
				(void)_pclose(pipe);
				return vcvars_path;
			}
		}
		(void)_pclose(pipe);
	}

	for (root_index = 0; candidate_roots[root_index] != NULL; ++root_index) {
		char *vcvars_path;
		FILE *pipe;

		(void)snprintf(command, sizeof(command), "where /R \"%s\" vcvars64.bat 2>nul",
					   candidate_roots[root_index]);
		pipe = _popen(command, "r");
		if (pipe == NULL) continue;
		if (fgets(result, sizeof(result), pipe) == NULL) {
			(void)_pclose(pipe);
			continue;
		}
		(void)_pclose(pipe);
		result[strcspn(result, "\r\n")] = '\0';
		if (result[0] == '\0') continue;
		vcvars_path = malloc(strlen(result) + 1U);
		if (vcvars_path == NULL) return NULL;
		memcpy(vcvars_path, result, strlen(result) + 1U);
		if (_access(vcvars_path, 0) == 0) return vcvars_path;
		free(vcvars_path);
	}
	return NULL;
}

static bool msvc_parse_compiler_version(const char *line, unsigned *version) {
	const char *value = strstr(line, "Compiler Version");
	char *end;
	unsigned long major;
	unsigned long minor;

	if (value == NULL) return false;
	value += strlen("Compiler Version");
	while (*value == ' ' || *value == '\t') ++value;
	major = strtoul(value, &end, 10);
	if (end == value || *end != '.') return false;
	value = end + 1;
	minor = strtoul(value, &end, 10);
	if (end == value || minor > UINT_MAX
		|| major > (UINT_MAX - minor) / 100U) {
		return false;
	}
	*version = (unsigned)(major * 100U + minor);
	return true;
}

static bool msvc_read_compiler_version(const char *vcvars_path,
									   unsigned *version, char *identity,
									   size_t identity_capacity) {
	static const char command_suffix[] = " >nul 2>nul && cl /Bv 2>&1";
	char *command;
	size_t command_length;
	FILE *pipe;
	char line[1024];
	bool found = false;

	if (version == NULL || identity == NULL || identity_capacity == 0U) return false;
	if (vcvars_path == NULL) {
		command = malloc(sizeof("cl /Bv 2>&1"));
		if (command == NULL) return false;
		memcpy(command, "cl /Bv 2>&1", sizeof("cl /Bv 2>&1"));
	} else {
		size_t path_length = strlen(vcvars_path);
		if (path_length > SIZE_MAX - sizeof("call \"\"") - sizeof(command_suffix)) {
			return false;
		}
		command_length = path_length + sizeof("call \"\"")
			+ sizeof(command_suffix);
		command = malloc(command_length);
		if (command == NULL) return false;
		(void)snprintf(command, command_length, "call \"%s\"%s",
					   vcvars_path, command_suffix);
	}
	pipe = _popen(command, "r");
	free(command);
	if (pipe == NULL) return false;
	while (fgets(line, sizeof(line), pipe) != NULL) {
		if (msvc_parse_compiler_version(line, version)) {
			size_t length = strcspn(line, "\r\n");
			if (length >= identity_capacity) break;
			memcpy(identity, line, length);
			identity[length] = '\0';
			found = true;
			break;
		}
	}
	(void)_pclose(pipe);
	return found;
}

static bool msvc_initialize_toolchain(void) {
	char *vcvars_path = NULL;
	bool available = false;

	AcquireSRWLockExclusive(&msvc_toolchain_lock);
	if (msvc_toolchain_initialized) {
		available = msvc_toolchain_available;
		ReleaseSRWLockExclusive(&msvc_toolchain_lock);
		return available;
	}
	if (!cbox_program_exists("cl")) {
		vcvars_path = detect_vcvars64_bat();
		if (vcvars_path == NULL) {
			fprintf(stderr, "CBox: MSVC build tools were not found\n");
			msvc_toolchain_initialized = 1;
			msvc_toolchain_available = false;
			ReleaseSRWLockExclusive(&msvc_toolchain_lock);
			return false;
		}
	}
	if (!msvc_read_compiler_version(vcvars_path, &msvc_cached_version,
									msvc_cached_compiler_identity,
									sizeof(msvc_cached_compiler_identity))) {
		fprintf(stderr, "CBox: could not determine the installed MSVC compiler version\n");
		free(vcvars_path);
		msvc_toolchain_initialized = 1;
		msvc_toolchain_available = false;
		ReleaseSRWLockExclusive(&msvc_toolchain_lock);
		return false;
	}
	msvc_cached_vcvars_path = vcvars_path;
	msvc_toolchain_available = true;
	msvc_toolchain_initialized = 1;
	ReleaseSRWLockExclusive(&msvc_toolchain_lock);
	return true;
}

static bool msvc_get_toolchain(unsigned *version, char **vcvars_path) {
	size_t path_length;

	if (version == NULL || vcvars_path == NULL
		|| !msvc_initialize_toolchain()) {
		return false;
	}
	*version = 0U;
	*vcvars_path = NULL;
	*version = msvc_cached_version;
	if (msvc_cached_vcvars_path == NULL) return true;
	path_length = strlen(msvc_cached_vcvars_path);
	*vcvars_path = malloc(path_length + 1U);
	if (*vcvars_path == NULL) return false;
	memcpy(*vcvars_path, msvc_cached_vcvars_path, path_length + 1U);
	return true;
}

bool cbox_msvc_available(void) {
	char *vcvars_path;
	bool available;

	AcquireSRWLockExclusive(&msvc_toolchain_lock);
	if (msvc_availability_checked) {
		available = msvc_cached_availability;
		ReleaseSRWLockExclusive(&msvc_toolchain_lock);
		return available;
	}
	available = cbox_program_exists("cl");
	if (!available) {
		vcvars_path = detect_vcvars64_bat();
		available = vcvars_path != NULL;
		free(vcvars_path);
	}
	msvc_cached_availability = available;
	msvc_availability_checked = true;
	ReleaseSRWLockExclusive(&msvc_toolchain_lock);
	return available;
}

static bool msvc_optimization_flags(const cbox_profile *profile,
									const char **primary,
									const char **secondary) {
	if (primary == NULL || secondary == NULL) return false;
	*primary = NULL;
	*secondary = NULL;
	if (profile == NULL || profile->optimization == NULL
		|| strcmp(profile->optimization, "debug") == 0
		|| strcmp(profile->optimization, "none") == 0) {
		*primary = "/Od";
		return true;
	}
	if (strcmp(profile->optimization, "release") == 0) {
		*primary = "/O2";
		return true;
	}
	if (strcmp(profile->optimization, "fast") == 0) {
		*primary = "/O2";
		*secondary = "/Ob2";
		return true;
	}
	if (strcmp(profile->optimization, "small") == 0) {
		*primary = "/Os";
		return true;
	}
	if (strcmp(profile->optimization, "basic") == 0
		|| strcmp(profile->optimization, "tiny") == 0) {
		*primary = "/O1";
		return true;
	}
	fprintf(stderr, "CBox: unsupported MSVC optimization profile '%s'\n",
			profile->optimization);
	return false;
}

static bool msvc_warnings_enabled(const cbox_profile *profile) {
	return profile != NULL && profile->warnings != NULL
		&& strcmp(profile->warnings, "none") != 0;
}

static const char *msvc_warning_flag(const cbox_profile *profile) {
	if (profile == NULL || profile->warnings == NULL
		|| strcmp(profile->warnings, "none") == 0) return NULL;
	return strcmp(profile->warnings, "basic") == 0 ? "/W3" : "/W4";
}

static bool msvc_c_standard_option(const char *standard, unsigned version,
								   const char **option) {
	*option = NULL;
	// MSVC cannot select older C dialects. Warn and use its default instead of
	// quietly pretending that a switch it doesn't have was honored.
	if (strcmp(standard, "c89") == 0 || strcmp(standard, "c90") == 0
		|| strcmp(standard, "ansi c") == 0 || strcmp(standard, "ansic") == 0
		|| strcmp(standard, "ansi") == 0 || strcmp(standard, "c95") == 0
		|| strcmp(standard, "c99") == 0) {
		(void)cbox_terminal_printf(stderr, CBOX_TERMINAL_YELLOW,
								   "warning: MSVC cannot enforce %s; using its default C mode\n",
								   standard);
		return true;
	}
	if (strcmp(standard, "c11") == 0 || strcmp(standard, "c17") == 0
		|| strcmp(standard, "c18") == 0) {
		if (version < 1928U) {
			(void)cbox_terminal_printf(stderr, CBOX_TERMINAL_YELLOW,
									   "warning: MSVC %u does not support a selectable %s mode; using its default C mode\n",
									   version, standard);
			return true;
		}
		if (strcmp(standard, "c18") == 0) {
			(void)cbox_terminal_printf(stderr, CBOX_TERMINAL_YELLOW,
									   "warning: MSVC maps C18 to its C17 mode\n");
			*option = "c17";
		} else {
			volatile LONG *warning_flag = strcmp(standard, "c11") == 0
				? &msvc_warned_c11_feature_gaps : &msvc_warned_c17_feature_gaps;
			if (InterlockedCompareExchange(warning_flag, 1, 0) == 0) {
				(void)cbox_terminal_printf(stderr, CBOX_TERMINAL_YELLOW,
										   "warning: MSVC %s omits optional features such as VLAs, atomics, and threads\n",
										   standard);
			}
			*option = standard;
		}
		return true;
	}
	if (strcmp(standard, "c23") == 0) {
		if (version < 1940U) {
			fprintf(stderr, "CBox: MSVC %u does not support C23 mode; upgrade to Visual Studio 2022 17.10 or later\n",
					version);
			return false;
		}
		(void)cbox_terminal_printf(stderr, CBOX_TERMINAL_YELLOW,
								   "warning: MSVC C23 support is experimental\n");
		*option = "clatest";
		return true;
	}
	fprintf(stderr, "CBox: unsupported MSVC C standard '%s'\n", standard);
	return false;
}

static bool msvc_cpp_standard_option(const char *standard, unsigned version,
									 const char **option) {
	*option = NULL;
	if (strcmp(standard, "c++98") == 0 || strcmp(standard, "c++03") == 0
		|| strcmp(standard, "c++11") == 0) {
		(void)cbox_terminal_printf(stderr, CBOX_TERMINAL_YELLOW,
								   "warning: MSVC does not provide a selectable %s mode; using its default C++ mode\n",
								   standard);
		return true;
	}
	if (strcmp(standard, "c++14") == 0 || strcmp(standard, "c++17") == 0) {
		if (version < 1911U) {
			(void)cbox_terminal_printf(stderr, CBOX_TERMINAL_YELLOW,
									   "warning: MSVC %u does not support a selectable %s mode; using its default C++ mode\n",
									   version, standard);
			return true;
		}
		*option = standard;
		return true;
	}
	if (strcmp(standard, "c++20") == 0) {
		if (version < 1929U) {
			fprintf(stderr, "CBox: MSVC %u does not support C++20 mode; upgrade to Visual Studio 2019 16.11 or later\n",
					version);
			return false;
		}
		*option = standard;
		return true;
	}
	if (strcmp(standard, "c++23") == 0 || strcmp(standard, "c++26") == 0) {
		if (version < 1911U) {
			fprintf(stderr, "CBox: MSVC %u does not support a preview mode for %s\n",
					version, standard);
			return false;
		}
		(void)cbox_terminal_printf(stderr, CBOX_TERMINAL_YELLOW,
								   "warning: MSVC %s support is preview-only and uses /std:c++latest\n",
								   standard);
		*option = "c++latest";
		return true;
	}
	fprintf(stderr, "CBox: unsupported MSVC C++ standard '%s'\n", standard);
	return false;
}

static bool append_command_text(char *command, size_t capacity, size_t *length,
								const char *text) {
	size_t text_length = strlen(text);
	if (text_length >= capacity - *length) return false;
	memcpy(command + *length, text, text_length);
	*length += text_length;
	command[*length] = '\0';
	return true;
}

static bool append_command_arg(char *command, size_t capacity, size_t *length,
							   const char *argument) {
	if (strpbrk(argument, "\"%\r\n") != NULL
		|| !append_command_text(command, capacity, length, " \"")
		|| !append_command_text(command, capacity, length, argument)
		|| !append_command_text(command, capacity, length, "\"")) {
		return false;
	}
	return true;
}

/* cl.exe needs the environment set up by vcvars64.bat, and that environment only
lives in the shell that called the batch file. So, yes, we have to do both in
one fucking command. */
static int msvc_run_tool(const char *const arguments[], const char *vcvars_path) {
	char command[32768] = {0};
	size_t command_length = 0U;
	size_t index;

	if (vcvars_path == NULL) {
		return cbox_run_process(arguments);
	}
	if (!append_command_text(command, sizeof(command), &command_length,
						  "call \"")
		|| !append_command_text(command, sizeof(command), &command_length, vcvars_path)
		|| !append_command_text(command, sizeof(command), &command_length,
							"\" >nul 2>nul &&")) {
		return -1;
	}
	for (index = 0; arguments[index] != NULL; ++index) {
		if (!append_command_arg(command, sizeof(command), &command_length, arguments[index])) {
			return -1;
		}
	}
	return cbox_run_shell_process(command);
}

static bool msvc_compile_object(const cbox_target *target, const cbox_profile *profile,
								const char *optimization, const char *secondary_optimization,
								const char *standard_option, const char *language_option,
								bool cpp_language,
								const char *source, const char *object,
								bool module, const char *module_output,
								const char *const module_names[],
								const char *const module_outputs[],
								size_t module_count, const char *dependency_file,
								const char *vcvars_path) {
	const char **arguments;
	char *object_option;
	char *program_database_option = NULL;
	arenalib_arena_t argument_arena = {0};
	unsigned char arena_storage[512];
	size_t argument_count;
	size_t index = 0U;
	size_t include_index;
	size_t module_index;
	int result;

	if (target->include_dir_count > SIZE_MAX - 13U
		|| module_count > (SIZE_MAX - 13U - target->include_dir_count)
			/ 2U
		|| !arenalib_arena_init(&argument_arena, arena_storage,
								sizeof(arena_storage), 0U)) {
		return false;
	}
	arenalib_arena_set_oom_callback(&argument_arena, msvc_arena_oom,
									"MSVC compiler arguments");
	argument_count = 6U + (standard_option != NULL ? 1U : 0U)
		+ (cpp_language ? 1U : 0U)
		+ (dependency_file != NULL ? 2U : 0U)
		+ (secondary_optimization != NULL ? 1U : 0U)
		+ (profile != NULL && profile->debug_symbols ? 3U : 0U)
		+ (msvc_warnings_enabled(profile) ? 1U : 0U)
		+ target->include_dir_count + target->compile_arg_count
		+ module_count * 2U + (module ? 3U : 0U);
	arguments = arenalib_arena_calloc(&argument_arena, argument_count + 1U,
									  sizeof(*arguments));
	object_option = arenalib_arena_malloc(&argument_arena, strlen(object) + 4U);
	if (profile != NULL && profile->debug_symbols) {
		size_t option_length = strlen(object) + sizeof("/Fd.pdb");
		program_database_option = arenalib_arena_malloc(&argument_arena,
													 option_length);
		if (program_database_option != NULL) {
			(void)snprintf(program_database_option, option_length,
						   "/Fd%s.pdb", object);
		}
	}
	if (arguments == NULL || object_option == NULL
		|| (profile != NULL && profile->debug_symbols
			&& program_database_option == NULL)) {
		arenalib_arena_destroy(&argument_arena);
		return false;
	}
	(void)snprintf(object_option, strlen(object) + 4U, "/Fo%s", object);
	arguments[index++] = "cl";
	arguments[index++] = language_option;
	if (cpp_language) arguments[index++] = "/EHsc";
	if (module) arguments[index++] = "/interface";
	if (standard_option != NULL) arguments[index++] = standard_option;
	arguments[index++] = optimization;
	if (secondary_optimization != NULL) arguments[index++] = secondary_optimization;
	if (profile != NULL && profile->debug_symbols) {
		arguments[index++] = "/Zi";
		arguments[index++] = "/RTC1";
		arguments[index++] = program_database_option;
	}
	if (msvc_warnings_enabled(profile)) {
		arguments[index++] = msvc_warning_flag(profile);
	}
	for (include_index = 0U; include_index < target->compile_arg_count; ++include_index) {
		arguments[index++] = target->compile_args[include_index];
	}
	for (include_index = 0U; include_index < target->include_dir_count; ++include_index) {
		size_t include_length = strlen(target->include_dirs[include_index]);
		char *include_option = arenalib_arena_malloc(&argument_arena, include_length + 3U);
		if (include_option == NULL) {
			arenalib_arena_destroy(&argument_arena);
			return false;
		}
		(void)snprintf(include_option, include_length + 3U, "/I%s",
					   target->include_dirs[include_index]);
		arguments[index++] = include_option;
	}
	for (module_index = 0U; module_index < module_count; ++module_index) {
		size_t name_length = strlen(module_names[module_index]);
		size_t output_length = strlen(module_outputs[module_index]);
		char *reference = arenalib_arena_malloc(&argument_arena,
												 name_length + output_length + 2U);
		if (reference == NULL) {
			arenalib_arena_destroy(&argument_arena);
			return false;
		}
		(void)snprintf(reference, name_length + output_length + 2U, "%s=%s",
					   module_names[module_index], module_outputs[module_index]);
		arguments[index++] = "/reference";
		arguments[index++] = reference;
	}
	if (module) {
		arguments[index++] = "/ifcOutput";
		arguments[index++] = module_output;
	}
	if (dependency_file != NULL) {
		arguments[index++] = "/sourceDependencies";
		arguments[index++] = dependency_file;
	}
	arguments[index++] = "/c";
	arguments[index++] = source;
	arguments[index++] = object_option;
	arguments[index] = NULL;
	result = msvc_run_tool(arguments, vcvars_path);
	arenalib_arena_destroy(&argument_arena);
	return result == 0;
}

static bool msvc_assemble_source(const cbox_target *target,
								const cbox_profile *profile, bool cpp,
								const char *source, const char *assembly,
								const char *object) {
	const char *optimization;
	const char *secondary_optimization;
	const char *standard_name;
	const char *standard_option_name;
	const char *language_option = cpp ? "/TP" : "/TC";
	char standard[64];
	const char *standard_option = NULL;
	const char **arguments;
	char *assembly_option;
	char *object_option;
	arenalib_arena_t arena = {0};
	unsigned char storage[512];
	size_t argument_count;
	size_t index = 0U;
	size_t include_index;
	unsigned version;
	char *vcvars_path = NULL;
	bool success = false;

	if (!msvc_optimization_flags(profile, &optimization,
								 &secondary_optimization)
		|| !msvc_get_toolchain(&version, &vcvars_path)) {
		free(vcvars_path);
		return false;
	}
	if (cpp) {
		standard_name = target->cpp != NULL ? target->cpp : "c++11";
		success = msvc_cpp_standard_option(standard_name, version,
										   &standard_option_name);
	} else {
		standard_name = target->c != NULL ? target->c : "c99";
		success = msvc_c_standard_option(standard_name, version,
										 &standard_option_name);
	}
	if (!success) goto cleanup_vcvars;
	if (standard_option_name != NULL) {
		if (snprintf(standard, sizeof(standard), "/std:%s",
					 standard_option_name) >= (int)sizeof(standard)) {
			goto cleanup_vcvars;
		}
		standard_option = standard;
	}
	if (target->include_dir_count > SIZE_MAX - target->compile_arg_count
		|| target->include_dir_count + target->compile_arg_count
			> SIZE_MAX - 12U
		|| !arenalib_arena_init(&arena, storage, sizeof(storage), 0U)) {
		goto cleanup_vcvars;
	}
	arenalib_arena_set_oom_callback(&arena, msvc_arena_oom,
									"MSVC assembly arguments");
	argument_count = 10U + (cpp ? 1U : 0U)
		+ (standard_option != NULL ? 1U : 0U)
		+ (secondary_optimization != NULL ? 1U : 0U)
		+ (msvc_warnings_enabled(profile) ? 1U : 0U)
		+ target->include_dir_count + target->compile_arg_count;
	arguments = arenalib_arena_calloc(&arena, argument_count + 1U,
									  sizeof(*arguments));
	assembly_option = arenalib_arena_malloc(&arena, strlen(assembly) + 5U);
	object_option = arenalib_arena_malloc(&arena, strlen(object) + 4U);
	if (arguments == NULL || assembly_option == NULL || object_option == NULL) {
		goto cleanup_arena;
	}
	(void)snprintf(assembly_option, strlen(assembly) + 5U, "/Fa%s", assembly);
	(void)snprintf(object_option, strlen(object) + 4U, "/Fo%s", object);
	arguments[index++] = "cl";
	arguments[index++] = language_option;
	if (cpp) arguments[index++] = "/EHsc";
	if (standard_option != NULL) arguments[index++] = standard_option;
	arguments[index++] = optimization;
	if (secondary_optimization != NULL) arguments[index++] = secondary_optimization;
	if (msvc_warnings_enabled(profile)) {
		arguments[index++] = msvc_warning_flag(profile);
	}
	for (include_index = 0U; include_index < target->compile_arg_count;
		 ++include_index) {
		arguments[index++] = target->compile_args[include_index];
	}
	for (include_index = 0U; include_index < target->include_dir_count;
		 ++include_index) {
		size_t include_length = strlen(target->include_dirs[include_index]);
		char *include_option = arenalib_arena_malloc(&arena, include_length + 3U);
		if (include_option == NULL) goto cleanup_arena;
		(void)snprintf(include_option, include_length + 3U, "/I%s",
					   target->include_dirs[include_index]);
		arguments[index++] = include_option;
	}
	arguments[index++] = "/FA";
	arguments[index++] = assembly_option;
	arguments[index++] = "/c";
	arguments[index++] = source;
	arguments[index++] = object_option;
	arguments[index] = NULL;
	success = msvc_run_tool(arguments, vcvars_path) == 0;

cleanup_arena:
	arenalib_arena_destroy(&arena);
cleanup_vcvars:
	free(vcvars_path);
	return success;
}

static bool msvc_check_source(const cbox_target *target,
							  const cbox_profile *profile, bool cpp,
							  const char *source) {
	const char *optimization;
	const char *secondary_optimization;
	const char *standard_name;
	const char *standard_option_name;
	const char *language_option = cpp ? "/TP" : "/TC";
	char standard[64];
	const char *standard_option = NULL;
	const char **arguments;
	arenalib_arena_t arena = {0};
	unsigned char storage[512];
	size_t argument_count;
	size_t index = 0U;
	size_t include_index;
	unsigned version;
	char *vcvars_path = NULL;
	bool success = false;

	if (!msvc_optimization_flags(profile, &optimization,
								 &secondary_optimization)
		|| !msvc_get_toolchain(&version, &vcvars_path)) {
		free(vcvars_path);
		return false;
	}
	if (cpp) {
		standard_name = target->cpp != NULL ? target->cpp : "c++11";
		success = msvc_cpp_standard_option(standard_name, version,
										   &standard_option_name);
	} else {
		standard_name = target->c != NULL ? target->c : "c99";
		success = msvc_c_standard_option(standard_name, version,
										 &standard_option_name);
	}
	if (!success) goto cleanup_vcvars;
	if (standard_option_name != NULL) {
		if (snprintf(standard, sizeof(standard), "/std:%s",
					 standard_option_name) >= (int)sizeof(standard)) {
			goto cleanup_vcvars;
		}
		standard_option = standard;
	}
	if (target->include_dir_count > SIZE_MAX - target->compile_arg_count
		|| target->include_dir_count + target->compile_arg_count
			> SIZE_MAX - 8U
		|| !arenalib_arena_init(&arena, storage, sizeof(storage), 0U)) {
		goto cleanup_vcvars;
	}
	arenalib_arena_set_oom_callback(&arena, msvc_arena_oom,
									"MSVC syntax-check arguments");
	argument_count = 5U + (cpp ? 1U : 0U)
		+ (standard_option != NULL ? 1U : 0U)
		+ (secondary_optimization != NULL ? 1U : 0U)
		+ (msvc_warnings_enabled(profile) ? 1U : 0U)
		+ target->include_dir_count + target->compile_arg_count;
	arguments = arenalib_arena_calloc(&arena, argument_count + 1U,
									  sizeof(*arguments));
	if (arguments == NULL) goto cleanup_arena;
	arguments[index++] = "cl";
	arguments[index++] = language_option;
	if (cpp) arguments[index++] = "/EHsc";
	if (standard_option != NULL) arguments[index++] = standard_option;
	arguments[index++] = optimization;
	if (secondary_optimization != NULL) arguments[index++] = secondary_optimization;
	if (msvc_warnings_enabled(profile)) {
		arguments[index++] = msvc_warning_flag(profile);
	}
	for (include_index = 0U; include_index < target->compile_arg_count;
		 ++include_index) {
		arguments[index++] = target->compile_args[include_index];
	}
	for (include_index = 0U; include_index < target->include_dir_count;
		 ++include_index) {
		size_t include_length = strlen(target->include_dirs[include_index]);
		char *include_option = arenalib_arena_malloc(&arena, include_length + 3U);
		if (include_option == NULL) goto cleanup_arena;
		(void)snprintf(include_option, include_length + 3U, "/I%s",
					   target->include_dirs[include_index]);
		arguments[index++] = include_option;
	}
	arguments[index++] = "/Zs";
	arguments[index++] = source;
	arguments[index] = NULL;
	success = msvc_run_tool(arguments, vcvars_path) == 0;

cleanup_arena:
	arenalib_arena_destroy(&arena);
cleanup_vcvars:
	free(vcvars_path);
	return success;
}

static bool msvc_link_objects(const cbox_target *target, const cbox_profile *profile,
							  const char *output, char *const object_paths[],
							  size_t object_count, bool shared,
							  const char *import_output, const char *vcvars_path) {
	const char **arguments;
	char **library_arguments = NULL;
	size_t library_argument_count = 0U;
	char *output_option;
	char *import_option = NULL;
	arenalib_arena_t argument_arena = {0};
	unsigned char arena_storage[512];
	size_t argument_count;
	size_t index = 0U;
	size_t object_index;
	int result;

	if (!cbox_linker_resolve_libraries(CBOX_LINKER_MSVC, target->link_libraries,
									   target->link_library_count, &library_arguments,
									   &library_argument_count)
		|| library_argument_count > SIZE_MAX - target->link_arg_count
		|| library_argument_count + target->link_arg_count > SIZE_MAX - 4U
		|| (shared ? 2U : 0U) > SIZE_MAX - 4U - library_argument_count
			- target->link_arg_count
		|| object_count > SIZE_MAX - 4U - (shared ? 2U : 0U)
			- library_argument_count - target->link_arg_count
		|| !arenalib_arena_init(&argument_arena, arena_storage,
								sizeof(arena_storage), 0U)) {
		cbox_linker_free_arguments(library_arguments, library_argument_count);
		return false;
	}
	arenalib_arena_set_oom_callback(&argument_arena, msvc_arena_oom,
									"MSVC linker arguments");
	argument_count = 2U + object_count + library_argument_count + target->link_arg_count
		+ (shared ? 2U : 0U)
		+ (profile != NULL && profile->debug_symbols ? 1U : 0U);
	arguments = arenalib_arena_calloc(&argument_arena, argument_count + 1U,
									  sizeof(*arguments));
	output_option = arenalib_arena_malloc(&argument_arena, strlen(output) + 6U);
	if (shared && import_output != NULL) {
		import_option = arenalib_arena_malloc(&argument_arena,
											  strlen(import_output) + 10U);
		if (import_option != NULL) {
			(void)snprintf(import_option, strlen(import_output) + 10U,
						   "/IMPLIB:%s", import_output);
		}
	}
	if (arguments == NULL || output_option == NULL
		|| (shared && (import_option == NULL || import_output == NULL))) {
		arenalib_arena_destroy(&argument_arena);
		cbox_linker_free_arguments(library_arguments, library_argument_count);
		return false;
	}
	(void)snprintf(output_option, strlen(output) + 6U, "/OUT:%s", output);
	arguments[index++] = "link.exe";
	if (shared) arguments[index++] = "/DLL";
	for (object_index = 0U; object_index < object_count; ++object_index) {
		arguments[index++] = object_paths[object_index];
	}
	for (object_index = 0U; object_index < library_argument_count; ++object_index) {
		arguments[index++] = library_arguments[object_index];
	}
	for (object_index = 0U; object_index < target->link_arg_count; ++object_index) {
		arguments[index++] = target->link_args[object_index];
	}
	arguments[index++] = output_option;
	if (shared) arguments[index++] = import_option;
	if (profile != NULL && profile->debug_symbols) arguments[index++] = "/DEBUG";
	arguments[index] = NULL;
	(void)cbox_terminal_printf(stdout, CBOX_TERMINAL_CYAN,
							   "linking \"%s\" with MSVC\n",
							   target->name != NULL ? target->name : output);
	result = msvc_run_tool(arguments, vcvars_path);
	arenalib_arena_destroy(&argument_arena);
	cbox_linker_free_arguments(library_arguments, library_argument_count);
	return result == 0;
}

static bool msvc_archive_objects(const cbox_target *target, const char *output,
								char *const object_paths[], size_t object_count,
								const char *vcvars_path) {
	const char **arguments;
	char *output_option;
	arenalib_arena_t argument_arena = {0};
	unsigned char arena_storage[512];
	size_t argument_count;
	size_t index = 0U;
	size_t object_index;
	int result;

	if (object_count > SIZE_MAX - 3U
		|| !arenalib_arena_init(&argument_arena, arena_storage,
								sizeof(arena_storage), 0U)) {
		return false;
	}
	arenalib_arena_set_oom_callback(&argument_arena, msvc_arena_oom,
									"MSVC archiver arguments");
	argument_count = 2U + object_count;
	arguments = arenalib_arena_calloc(&argument_arena, argument_count + 1U,
									  sizeof(*arguments));
	output_option = arenalib_arena_malloc(&argument_arena, strlen(output) + 6U);
	if (arguments == NULL || output_option == NULL) {
		arenalib_arena_destroy(&argument_arena);
		return false;
	}
	(void)snprintf(output_option, strlen(output) + 6U, "/OUT:%s", output);
	arguments[index++] = "lib.exe";
	arguments[index++] = output_option;
	for (object_index = 0U; object_index < object_count; ++object_index) {
		arguments[index++] = object_paths[object_index];
	}
	arguments[index] = NULL;
	(void)cbox_terminal_printf(stdout, CBOX_TERMINAL_CYAN,
							   "archiving %s with lib.exe\n",
							   target->name != NULL ? target->name : output);
	result = msvc_run_tool(arguments, vcvars_path);
	arenalib_arena_destroy(&argument_arena);
	return result == 0;
}

static bool msvc_driver_compile(const cbox_target *target, const cbox_profile *profile,
								bool cpp, bool module, const char *source,
								const char *object, const char *module_output,
								const char *const module_names[],
								const char *const module_outputs[],
								size_t module_count, const char *module_mapper,
								const char *dependency_file) {
	const char *optimization;
	const char *secondary_optimization;
	const char *standard_name;
	const char *standard_option_name;
	char standard[64];
	const char *standard_option = NULL;
	unsigned version;
	char *vcvars_path;
	bool success;

	(void)module_mapper;
	if (!msvc_optimization_flags(profile, &optimization, &secondary_optimization)) {
		return false;
	}
	if (!msvc_get_toolchain(&version, &vcvars_path)) return false;
	if (module && version < 1929U) {
		fprintf(stderr, "CBox: MSVC %u does not support C++20 modules; use Visual Studio 2019 16.11 or later\n",
				version);
		free(vcvars_path);
		return false;
	}
	if (cpp) {
		standard_name = target->cpp != NULL ? target->cpp : "c++11";
		success = msvc_cpp_standard_option(standard_name, version,
										   &standard_option_name);
	} else {
		success = msvc_c_standard_option(target->c != NULL ? target->c : "c99",
										 version, &standard_option_name);
	}
	if (!success) {
		free(vcvars_path);
		return false;
	}
	if (standard_option_name != NULL) {
		if (snprintf(standard, sizeof(standard), "/std:%s", standard_option_name)
			>= (int)sizeof(standard)) {
			fprintf(stderr, "CBox: invalid language standard for MSVC\n");
			free(vcvars_path);
			return false;
		}
		standard_option = standard;
	}
	success = msvc_compile_object(target, profile, optimization, secondary_optimization,
								  standard_option, cpp ? "/TP" : "/TC", cpp,
								  source, object,
								  module, module_output, module_names, module_outputs,
								  module_count,
								  version >= 1928U ? dependency_file : NULL,
								  vcvars_path);
	free(vcvars_path);
	return success;
}

static bool msvc_driver_version(char *buffer, size_t capacity) {
	unsigned version;
	char *vcvars_path;
	int written;
	if (!msvc_get_toolchain(&version, &vcvars_path)) return false;
	(void)version;
	written = snprintf(buffer, capacity, "%s%s%s%s",
					   msvc_cached_compiler_identity,
					   vcvars_path != NULL ? " (" : "",
					   vcvars_path != NULL ? vcvars_path : "",
					   vcvars_path != NULL ? ")" : "");
	free(vcvars_path);
	return written >= 0 && (size_t)written < capacity;
}

static bool msvc_driver_archive(const cbox_target *target, const char *output,
								char *const objects[], size_t count) {
	unsigned version;
	char *vcvars_path;
	bool success;
	if (!msvc_get_toolchain(&version, &vcvars_path)) return false;
	(void)version;
	success = msvc_archive_objects(target, output, objects, count, vcvars_path);
	free(vcvars_path);
	return success;
}

static bool msvc_driver_link(const cbox_target *target, const cbox_profile *profile,
							 const char *output, char *const objects[], size_t count,
							 bool shared, const char *import_output) {
	unsigned version;
	char *vcvars_path;
	bool success;
	if (!msvc_get_toolchain(&version, &vcvars_path)) return false;
	(void)version;
	success = msvc_link_objects(target, profile, output, objects, count,
								shared, import_output, vcvars_path);
	free(vcvars_path);
	return success;
}

static const cbox_toolchain_driver msvc_driver = {
	"MSVC", ".obj", ".ifc", CBOX_LINKER_MSVC, msvc_driver_version, msvc_driver_compile,
	msvc_assemble_source, msvc_check_source, msvc_driver_archive, msvc_driver_link
};

const cbox_toolchain_driver *cbox_msvc_driver(void) {
	return &msvc_driver;
}
#else
bool cbox_msvc_available(void) {
	return false;
}

const cbox_toolchain_driver *cbox_msvc_driver(void) {
	return NULL;
}
#endif
