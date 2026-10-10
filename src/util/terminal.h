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

#ifndef CBOX_UTIL_TERMINAL_H
#define CBOX_UTIL_TERMINAL_H

#include <stdio.h>

typedef unsigned int cbox_terminal_styles;

enum {
    CBOX_TERMINAL_BOLD = 1U << 0,
    CBOX_TERMINAL_ITALIC = 1U << 1,
    CBOX_TERMINAL_CYAN = 1U << 2,
    CBOX_TERMINAL_GREEN = 1U << 3,
    CBOX_TERMINAL_YELLOW = 1U << 4,
    CBOX_TERMINAL_RED = 1U << 5
};

#define CBOX_BOLD CBOX_TERMINAL_BOLD
#define CBOX_ITALIC CBOX_TERMINAL_ITALIC
#define CBOX_CYAN CBOX_TERMINAL_CYAN
#define CBOX_GREEN CBOX_TERMINAL_GREEN
#define CBOX_YELLOW CBOX_TERMINAL_YELLOW
#define CBOX_RED CBOX_TERMINAL_RED

void cbox_terminal_init(void);
void cbox_terminal_output_lock(void);
void cbox_terminal_output_unlock(void);
/* Call once to enable styles; redirected streams stay unstyled. */
int cbox_printf(cbox_terminal_styles styles, const char *format, ...);
int cbox_fprintf(FILE *stream, cbox_terminal_styles styles,
                 const char *format, ...);
int cbox_terminal_printf(FILE *stream, cbox_terminal_styles styles,
                         const char *format, ...);

#endif
