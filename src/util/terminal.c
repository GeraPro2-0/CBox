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
#define _POSIX_C_SOURCE 200809L
#endif

#include "util/terminal.h"

#include <stdarg.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <io.h>
#include <windows.h>
static SRWLOCK terminal_output_lock = SRWLOCK_INIT;
#else
#include <pthread.h>
#include <unistd.h>
static pthread_mutex_t terminal_output_lock = PTHREAD_MUTEX_INITIALIZER;
#endif

static bool terminal_initialized;
static bool stdout_supports_style;
static bool stderr_supports_style;

void cbox_terminal_output_lock(void) {
#ifdef _WIN32
    AcquireSRWLockExclusive(&terminal_output_lock);
#else
    (void)pthread_mutex_lock(&terminal_output_lock);
#endif
}

void cbox_terminal_output_unlock(void) {
#ifdef _WIN32
    ReleaseSRWLockExclusive(&terminal_output_lock);
#else
    (void)pthread_mutex_unlock(&terminal_output_lock);
#endif
}

static bool stream_supports_style(FILE *stream) {
    const char *term = getenv("TERM");
    const char *no_color = getenv("NO_COLOR");
    const char *force_color = getenv("FORCE_COLOR");
    bool force = force_color != NULL && force_color[0] != '\0'
        && strcmp(force_color, "0") != 0;

    if (no_color != NULL && no_color[0] != '\0') {
        return false;
    }
    if (!force && term != NULL && strcmp(term, "dumb") == 0) return false;
#ifdef _WIN32
    {
        int descriptor = _fileno(stream);
        HANDLE handle;
        DWORD mode;
        if (descriptor < 0 || !_isatty(descriptor)) return force;
        handle = (HANDLE)_get_osfhandle(descriptor);
        if (handle == INVALID_HANDLE_VALUE
            || !GetConsoleMode(handle, &mode)) {
            return force;
        }
        if ((mode & ENABLE_VIRTUAL_TERMINAL_PROCESSING) == 0U
            && !SetConsoleMode(handle, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING)) {
            return force;
        }
        return true;
    }
#else
    return force || isatty(fileno(stream)) != 0;
#endif
}

void cbox_terminal_init(void) {
    stdout_supports_style = stream_supports_style(stdout);
    stderr_supports_style = stream_supports_style(stderr);
    terminal_initialized = true;
}

static bool stream_styled(FILE *stream) {
    if (!terminal_initialized || stream == NULL) return false;
    if (stream == stdout) return stdout_supports_style;
    if (stream == stderr) return stderr_supports_style;
    return false;
}

static int write_styles(FILE *stream, cbox_terminal_styles styles) {
    static const struct {
        cbox_terminal_styles flag;
        const char *code;
    } style_codes[] = {
        {CBOX_TERMINAL_BOLD, "\x1b[1m"},
        {CBOX_TERMINAL_ITALIC, "\x1b[3m"},
        {CBOX_TERMINAL_CYAN, "\x1b[36m"},
        {CBOX_TERMINAL_GREEN, "\x1b[32m"},
        {CBOX_TERMINAL_YELLOW, "\x1b[33m"},
        {CBOX_TERMINAL_RED, "\x1b[31m"}
    };
    size_t index;
    int result = 0;

    if (!stream_styled(stream)) return 0;
    for (index = 0U; index < sizeof(style_codes) / sizeof(style_codes[0]);
         ++index) {
        if ((styles & style_codes[index].flag) != 0U
            && fputs(style_codes[index].code, stream) == EOF) {
            result = -1;
        }
    }
    return result;
}

static int terminal_vprintf(FILE *stream, cbox_terminal_styles styles,
                            bool prefix, const char *format,
                            va_list arguments) {
    int result = -1;
    bool failed = false;
    bool styled;

    if (stream == NULL || format == NULL) return -1;
    /* Lock the whole message; locking individual writes still lets worker output
       braid together in the terminal. */
    cbox_terminal_output_lock();
    styled = stream_styled(stream);
    if (prefix) {
        if (styled) (void)write_styles(stream, CBOX_TERMINAL_BOLD);
        if (fputs("CBox: ", stream) == EOF) failed = true;
        if (styled && fputs("\x1b[0m", stream) == EOF) failed = true;
    }
    if (write_styles(stream, styles) != 0) failed = true;
    result = vfprintf(stream, format, arguments);
    if (result < 0) failed = true;
    if (styled && fputs("\x1b[0m", stream) == EOF) failed = true;
    if (fflush(stream) == EOF) failed = true;
    cbox_terminal_output_unlock();
    return failed ? -1 : result;
}

int cbox_printf(cbox_terminal_styles styles, const char *format, ...) {
    int result;
    va_list arguments;
    va_start(arguments, format);
    result = terminal_vprintf(stdout, styles, false, format, arguments);
    va_end(arguments);
    return result;
}

int cbox_fprintf(FILE *stream, cbox_terminal_styles styles,
                 const char *format, ...) {
    int result;
    va_list arguments;
    va_start(arguments, format);
    result = terminal_vprintf(stream, styles, false, format, arguments);
    va_end(arguments);
    return result;
}

int cbox_terminal_printf(FILE *stream, cbox_terminal_styles styles,
                         const char *format, ...) {
    int result;
    va_list arguments;
    va_start(arguments, format);
    result = terminal_vprintf(stream, styles, true, format, arguments);
    va_end(arguments);
    return result;
}
