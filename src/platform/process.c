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

#include "platform/process.h"
#include "util/terminal.h"

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <io.h>
#include <windows.h>
#define cbox_access _access
#define cbox_execute_access 0

typedef struct {
    char *data;
    size_t length;
    size_t capacity;
} cbox_process_output;

static bool append_process_output(cbox_process_output *output,
                                 const char *data, size_t length) {
    size_t required;

    if (length > SIZE_MAX - output->length) return false;
    required = output->length + length;
    if (required > output->capacity) {
        size_t capacity = output->capacity == 0U ? 4096U : output->capacity;
        char *resized;
        while (capacity < required) {
            if (capacity > SIZE_MAX / 2U) {
                capacity = required;
                break;
            }
            capacity *= 2U;
        }
        resized = realloc(output->data, capacity);
        if (resized == NULL) return false;
        output->data = resized;
        output->capacity = capacity;
    }
    memcpy(output->data + output->length, data, length);
    output->length = required;
    return true;
}

static bool drain_process_pipe(HANDLE pipe,
                               cbox_process_output *output,
                               bool *pipe_closed) {
    char buffer[4096];

    while (!*pipe_closed) {
        DWORD available = 0U;
        DWORD amount;
        DWORD received = 0U;

        if (!PeekNamedPipe(pipe, NULL, 0U, NULL, &available, NULL)) {
            if (GetLastError() == ERROR_BROKEN_PIPE) {
                *pipe_closed = true;
                return true;
            }
            return false;
        }
        if (available == 0U) return true;
        amount = available < sizeof(buffer) ? available : (DWORD)sizeof(buffer);
        if (!ReadFile(pipe, buffer, amount, &received, NULL)) {
            if (GetLastError() == ERROR_BROKEN_PIPE) {
                *pipe_closed = true;
                return true;
            }
            return false;
        }
        if (received == 0U
            || !append_process_output(output, buffer, (size_t)received)) {
            return false;
        }
    }
    return true;
}

static bool replay_process_output(const cbox_process_output *stdout_output,
                                  const cbox_process_output *stderr_output) {
    bool success = true;

    cbox_terminal_output_lock();
    if (stdout_output->length > 0U
        && fwrite(stdout_output->data, 1U, stdout_output->length, stdout)
            != stdout_output->length) {
        success = false;
    }
    if (stderr_output->length > 0U
        && fwrite(stderr_output->data, 1U, stderr_output->length, stderr)
            != stderr_output->length) {
        success = false;
    }
    if (fflush(stdout) == EOF || fflush(stderr) == EOF) success = false;
    cbox_terminal_output_unlock();
    return success;
}

static bool append_windows_argument(char *command_line, size_t capacity,
                                    size_t *length, const char *argument) {
    const char *cursor = argument;

    if (*length >= capacity || capacity - *length < 2U) return false;
    // CreateProcess takes one command-line string, not argv. Match the CRT quoting
    // rules or paths with spaces and trailing backslashes get mangled.
    command_line[(*length)++] = '"';
    while (*cursor != '\0') {
        size_t backslashes = 0U;
        while (cursor[backslashes] == '\\') ++backslashes;
        cursor += backslashes;
        if (*cursor == '"') {
            size_t escaped_count;
            if (backslashes > (SIZE_MAX - 1U) / 2U) return false;
            escaped_count = backslashes * 2U + 1U;
            if (escaped_count > capacity - *length - 1U) return false;
            while (escaped_count-- > 0U) {
                command_line[(*length)++] = '\\';
            }
            command_line[(*length)++] = *cursor++;
        } else if (*cursor == '\0') {
            if (backslashes > capacity - *length - 1U) return false;
            while (backslashes-- > 0U) {
                command_line[(*length)++] = '\\';
            }
            break;
        } else {
            if (backslashes > capacity - *length - 2U) return false;
            while (backslashes-- > 0U) {
                command_line[(*length)++] = '\\';
            }
            command_line[(*length)++] = *cursor++;
        }
    }
    if (*length >= capacity - 1U) return false;
    command_line[(*length)++] = '"';
    command_line[*length] = '\0';
    return true;
}

static int run_windows_command_line(char *command_line,
                                    const char *program_name) {
    SECURITY_ATTRIBUTES security_attributes = {0};
    STARTUPINFOA startup_info = {0};
    PROCESS_INFORMATION process_info = {0};
    HANDLE stdout_read = NULL;
    HANDLE stdout_write = NULL;
    HANDLE stderr_read = NULL;
    HANDLE stderr_write = NULL;
    cbox_process_output stdout_output = {0};
    cbox_process_output stderr_output = {0};
    DWORD exit_code;
    DWORD wait_result;
    bool stdout_closed = false;
    bool stderr_closed = false;
    bool process_started = false;
    bool process_reaped = false;
    int result = -1;

    security_attributes.nLength = sizeof(security_attributes);
    security_attributes.bInheritHandle = TRUE;
    if (!CreatePipe(&stdout_read, &stdout_write, &security_attributes, 0U)
        || !SetHandleInformation(stdout_read, HANDLE_FLAG_INHERIT, 0U)
        || !CreatePipe(&stderr_read, &stderr_write, &security_attributes, 0U)
        || !SetHandleInformation(stderr_read, HANDLE_FLAG_INHERIT, 0U)) {
        fprintf(stderr, "CBox: failed to capture '%s' output (Windows error %lu)\n",
                program_name, (unsigned long)GetLastError());
        goto cleanup;
    }
    startup_info.cb = sizeof(startup_info);
    startup_info.dwFlags = STARTF_USESTDHANDLES;
    startup_info.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    startup_info.hStdOutput = stdout_write;
    startup_info.hStdError = stderr_write;
    if (!CreateProcessA(NULL, command_line, NULL, NULL, TRUE, 0U, NULL, NULL,
                        &startup_info, &process_info)) {
        fprintf(stderr, "CBox: failed to start '%s' (Windows error %lu)\n",
                program_name, (unsigned long)GetLastError());
        goto cleanup;
    }
    process_started = true;
    (void)CloseHandle(stdout_write);
    stdout_write = NULL;
    (void)CloseHandle(stderr_write);
    stderr_write = NULL;
    (void)CloseHandle(process_info.hThread);
    process_info.hThread = NULL;

    // Drain both pipes while the child runs; waiting first can deadlock when either
    // compiler stream fills its pipe buffer.
    for (;;) {
        if (!drain_process_pipe(stdout_read, &stdout_output, &stdout_closed)
            || !drain_process_pipe(stderr_read, &stderr_output, &stderr_closed)) {
            fprintf(stderr, "CBox: failed to capture '%s' output (Windows error %lu)\n",
                    program_name, (unsigned long)GetLastError());
            (void)TerminateProcess(process_info.hProcess, ERROR_WRITE_FAULT);
            process_reaped = WaitForSingleObject(process_info.hProcess, INFINITE)
                == WAIT_OBJECT_0;
            break;
        }
        wait_result = WaitForSingleObject(process_info.hProcess, 10U);
        if (wait_result == WAIT_OBJECT_0) {
            process_reaped = true;
            if (!drain_process_pipe(stdout_read, &stdout_output, &stdout_closed)
                || !drain_process_pipe(stderr_read, &stderr_output,
                                       &stderr_closed)) {
                fprintf(stderr,
                        "CBox: failed to capture '%s' output (Windows error %lu)\n",
                        program_name, (unsigned long)GetLastError());
                result = -1;
                break;
            }
            if (GetExitCodeProcess(process_info.hProcess, &exit_code)) {
                result = (int)exit_code;
            } else {
                fprintf(stderr,
                        "CBox: failed to read '%s' exit status (Windows error %lu)\n",
                        program_name, (unsigned long)GetLastError());
            }
            break;
        }
        if (wait_result == WAIT_FAILED) {
            fprintf(stderr, "CBox: failed waiting for '%s' (Windows error %lu)\n",
                    program_name, (unsigned long)GetLastError());
            (void)TerminateProcess(process_info.hProcess, ERROR_GEN_FAILURE);
            process_reaped = WaitForSingleObject(process_info.hProcess, INFINITE)
                == WAIT_OBJECT_0;
            break;
        }
    }

cleanup:
    if (process_started && !process_reaped) {
        (void)TerminateProcess(process_info.hProcess, ERROR_GEN_FAILURE);
        process_reaped = WaitForSingleObject(process_info.hProcess, INFINITE)
            == WAIT_OBJECT_0;
        if (!process_reaped) {
            fprintf(stderr, "CBox: could not stop '%s' after process failure\n",
                    program_name);
        }
    }
    if (process_info.hProcess != NULL) (void)CloseHandle(process_info.hProcess);
    if (process_info.hThread != NULL) (void)CloseHandle(process_info.hThread);
    if (stdout_read != NULL) (void)CloseHandle(stdout_read);
    if (stdout_write != NULL) (void)CloseHandle(stdout_write);
    if (stderr_read != NULL) (void)CloseHandle(stderr_read);
    if (stderr_write != NULL) (void)CloseHandle(stderr_write);
    if (process_started
        && !replay_process_output(&stdout_output, &stderr_output)) {
        fprintf(stderr, "CBox: failed to write captured '%s' output\n", program_name);
        result = -1;
    }
    free(stdout_output.data);
    free(stderr_output.data);
    if (result == -1) {
        return -1;
    }
    return result;
}

int cbox_run_process(const char *const arguments[]) {
    char *command_line;
    size_t capacity = 1U;
    size_t length = 0U;
    size_t index;
    int result;

    if (arguments == NULL || arguments[0] == NULL || arguments[0][0] == '\0') {
        fprintf(stderr, "CBox: cannot start a process without a program name\n");
        return -1;
    }
    for (index = 0U; arguments[index] != NULL; ++index) {
        size_t argument_length = strlen(arguments[index]);
        if (capacity > SIZE_MAX - 3U
            || argument_length > (SIZE_MAX - capacity - 3U) / 2U) {
            fprintf(stderr, "CBox: process command line is too large\n");
            return -1;
        }
        capacity += argument_length * 2U + 3U;
    }
    command_line = malloc(capacity);
    if (command_line == NULL) {
        fprintf(stderr, "CBox: out of memory building process command line\n");
        return -1;
    }
    for (index = 0U; arguments[index] != NULL; ++index) {
        if (index != 0U) {
            if (length >= capacity - 1U) {
                free(command_line);
                return -1;
            }
            command_line[length++] = ' ';
            command_line[length] = '\0';
        }
        if (!append_windows_argument(command_line, capacity, &length,
                                     arguments[index])) {
            fprintf(stderr, "CBox: process command line is too large\n");
            free(command_line);
            return -1;
        }
    }
    result = run_windows_command_line(command_line, arguments[0]);
    free(command_line);
    return result;
}

int cbox_run_shell_process(const char *command) {
    static const char prefix[] = "cmd.exe /d /s /c ";
    size_t command_length;
    char *command_line;
    int result;

    if (command == NULL || command[0] == '\0') {
        fprintf(stderr, "CBox: cannot run an empty shell command\n");
        return -1;
    }
    command_length = strlen(command);
    if (command_length > SIZE_MAX - sizeof(prefix)) {
        fprintf(stderr, "CBox: shell command line is too large\n");
        return -1;
    }
    command_line = malloc(sizeof(prefix) - 1U + command_length + 1U);
    if (command_line == NULL) {
        fprintf(stderr, "CBox: out of memory building shell command line\n");
        return -1;
    }
    memcpy(command_line, prefix, sizeof(prefix) - 1U);
    memcpy(command_line + sizeof(prefix) - 1U, command, command_length + 1U);
    result = run_windows_command_line(command_line, "cmd.exe");
    free(command_line);
    return result;
}
#else
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#define cbox_access access
#define cbox_execute_access X_OK

int cbox_run_process(const char *const arguments[]) {
    pid_t child = fork();
    int status;

    if (child < 0) {
        perror("CBox: failed to start compiler");
        return -1;
    }
    if (child == 0) {
        execvp(arguments[0], (char *const *)arguments);
        perror("CBox: failed to execute compiler");
        _exit(127);
    }
    if (waitpid(child, &status, 0) < 0) {
        perror("CBox: failed waiting for compiler");
        return -1;
    }
    if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
    }
    return -1;
}

int cbox_run_shell_process(const char *command) {
    const char *arguments[] = {"sh", "-c", command, NULL};
    if (command == NULL || command[0] == '\0') {
        fprintf(stderr, "CBox: cannot run an empty shell command\n");
        return -1;
    }
    return cbox_run_process(arguments);
}
#endif

bool cbox_run_shell_command(const char *shell, const char *command) {
    const char *program;
    const char *arguments[6];
    size_t index = 0U;
    bool windows_command = false;
    bool powershell_command = false;

    if (shell == NULL || command == NULL) {
        return false;
    }
    if (strcmp(shell, "native") == 0) {
#ifdef _WIN32
        program = "cmd.exe";
        windows_command = true;
#else
        program = "sh";
#endif
    } else if (strcmp(shell, "cmd") == 0) {
#ifdef _WIN32
        program = "cmd.exe";
        windows_command = true;
#else
        fprintf(stderr, "CBox: the cmd shell is only available on Windows\n");
        return false;
#endif
    } else if (strcmp(shell, "powershell") == 0) {
        powershell_command = true;
        if (cbox_program_exists("pwsh")) {
            program = "pwsh";
#ifdef _WIN32
        } else if (cbox_program_exists("powershell.exe")) {
            program = "powershell.exe";
#endif
        } else {
            fprintf(stderr, "CBox: PowerShell was requested, but pwsh was not found\n");
            return false;
        }
    } else if (strcmp(shell, "sh") == 0 || strcmp(shell, "bash") == 0
               || strcmp(shell, "zsh") == 0) {
        program = shell;
    } else {
        fprintf(stderr, "CBox: unsupported shell '%s'\n", shell);
        return false;
    }

    if (!cbox_program_exists(program)) {
        fprintf(stderr, "CBox: requested shell '%s' is not available\n", program);
        return false;
    }
    arguments[index++] = program;
    if (windows_command) {
        arguments[index++] = "/d";
        arguments[index++] = "/s";
        arguments[index++] = "/c";
    } else if (powershell_command) {
        arguments[index++] = "-NoProfile";
        arguments[index++] = "-NonInteractive";
        arguments[index++] = "-Command";
    } else {
        arguments[index++] = "-c";
    }
    arguments[index++] = command;
    arguments[index] = NULL;
    return cbox_run_process(arguments) == 0;
}

static bool executable_file_exists(const char *path) {
    if (cbox_access(path, cbox_execute_access) == 0) {
        return true;
    }
#if defined(_WIN32) || defined(_WIN64)
    {
        const char *base_name = strrchr(path, '\\');
        const char *forward_slash = strrchr(path, '/');
        const char *extension = strrchr(path, '.');
        size_t length;
        char *with_extension;
        bool exists;

        if (forward_slash != NULL && (base_name == NULL || forward_slash > base_name)) {
            base_name = forward_slash;
        }
        if (extension == NULL || (base_name != NULL && extension < base_name)) {
            length = strlen(path);
            with_extension = malloc(length + 5U);
            if (with_extension == NULL) {
                return false;
            }
            memcpy(with_extension, path, length);
            memcpy(with_extension + length, ".exe", 5U);
            exists = cbox_access(with_extension, 0) == 0;
            free(with_extension);
            return exists;
        }
    }
#endif
    return false;
}

bool cbox_program_exists(const char *program) {
    const char *path_value;
    const char *path_entry;
    const char *path_end;
    size_t program_length;
    char *candidate;

    if (program == NULL || program[0] == '\0') {
        return false;
    }
    if (strchr(program, '/') != NULL || strchr(program, '\\') != NULL) {
        return executable_file_exists(program);
    }

    path_value = getenv("PATH");
    if (path_value == NULL) {
        return false;
    }
    program_length = strlen(program);
    path_entry = path_value;
    while (true) {
#ifdef _WIN32
        path_end = strchr(path_entry, ';');
#else
        path_end = strchr(path_entry, ':');
#endif
        {
            size_t directory_length = path_end != NULL
                ? (size_t)(path_end - path_entry)
                : strlen(path_entry);
            size_t candidate_directory_length = directory_length == 0U
                ? 1U : directory_length;
            size_t candidate_length;

            if (candidate_directory_length > SIZE_MAX - program_length ||
                candidate_directory_length + program_length > SIZE_MAX - 2U) {
                return false;
            }
            candidate_length = candidate_directory_length + 1U + program_length;
            candidate = malloc(candidate_length + 1U);
            if (candidate == NULL) {
                return false;
            }
            if (directory_length == 0U) {
                candidate[0] = '.';
            } else {
                memcpy(candidate, path_entry, directory_length);
            }
#ifdef _WIN32
            candidate[candidate_directory_length] = '\\';
#else
            candidate[candidate_directory_length] = '/';
#endif
            memcpy(candidate + candidate_directory_length + 1U, program, program_length + 1U);
            if (executable_file_exists(candidate)) {
                free(candidate);
                return true;
            }
            free(candidate);
        }
        if (path_end == NULL) {
            break;
        }
        path_entry = path_end + 1;
    }
    return false;
}