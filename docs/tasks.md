# CLI and tasks

CBox must be run from the project directory containing `CBox.toml`.

## Commands

```text
cbox
cbox --help
cbox --version
cbox --list
cbox <task-name>
cbox <task-name> --asm
cbox <task-name> --one-asm
cbox <task-name> --check
cbox <task-name> --freestanding
```

| Command | Effect |
| --- | --- |
| `cbox` | Print project metadata, tasks, and target names/kinds. |
| `cbox --help`, `cbox -h` | Print usage and built-in command help. |
| `cbox --version`, `cbox -V` | Print the CBox version (`1.0`) without loading `CBox.toml`. |
| `cbox --list`, `cbox -l` | List configured tasks and profiles. |
| `cbox <task-name>` | Run the named task. |
| `cbox <task-name> --asm` | Generate an assembly file for each target source under its profile's `asm/` directory. |
| `cbox <task-name> --one-asm` | Generate each source's assembly and concatenate it into one output-named file (or target-named file if no output is configured) under `asm/`. |
| `cbox <task-name> --check` | Check the target's C and C++ sources for syntax errors without producing build outputs or linking. |
| `cbox <task-name> --freestanding` | Build only if no target in the selected target graph declares `freestanding = false`; missing declarations proceed without verification. |

Task names are positional commands, not options: invoke `cbox release`, not
`cbox --release`. Most commands load the configuration before processing
options, so `CBox.toml` must be present even when requesting help. The version
command is the exception; it reports the executable's build version directly.

Compiler modes require a task with a configured target and are mutually
exclusive. GCC and Clang use `-S` and write `.s` files; MSVC uses `/FA` and
writes `.asm` files. `--asm` uses source basenames plus a path-derived suffix
to avoid collisions and does not use the target output name. `--one-asm`
respects the configured target output name (or the target name if no output is
configured), replaces its extension with the assembly extension, and combines
the generated source assembly text. C++ module-interface targets are not
supported by assembly mode.

`--check` also requires a task with a configured target and cannot be combined
with either assembly option. It uses GCC/Clang `-fsyntax-only` or MSVC `/Zs`;
it checks each ordinary C/C++ source without creating object files or linking.
C++ module-interface targets are not supported in check mode.

`--freestanding` rejects a selected target or prerequisite explicitly marked
`freestanding = false`. A `true` declaration permits the build; a missing
declaration proceeds without verification. The mode passes `-ffreestanding`
to GCC and Clang, and `/X` to MSVC. For executable and shared targets, MSVC
also receives `/NODEFAULTLIB`. A configured `entry_point` is passed as
`-Wl,-e,<function>` with GNU-style linkers, or `/ENTRY:<function>` with
MSVC-style linkers (including Clang on Windows). Static libraries are
compiled in freestanding mode but have no entry-point or linker options.
These options do not configure a target platform, startup objects, or the rest
of a bare-metal runtime; users remain responsible for those settings and for
any required libraries.

CBox styles its own interactive terminal output with color and emphasis.
Styling is disabled for redirected output, when `TERM=dumb`, or when
`NO_COLOR` is non-empty. Set `FORCE_COLOR=1` to enable ANSI styling when output
is redirected; `NO_COLOR` still takes precedence. Output from compiler and
linker subprocesses is not modified.

## Defining tasks

Define tasks in `[task.NAME]` tables:

```toml
[task.debug]
target = "hello"
profile = "debug"
print = "Building {target.name} ({profile.optimization})"
```

| Key | Type | Meaning |
| --- | --- | --- |
| `target` | string | Build the target with this name. |
| `profile` | string | Use the named profile. |
| `print` | string | Message printed after a successful target build, or when the task has no command. |
| `command` | string | Shell command to execute when no target is specified. |
| `shell` | string | Shell for `command`: `"native"`, `"cmd"`, `"sh"`, `"bash"`, `"zsh"`, or `"powershell"`. Defaults to `"native"`. |
| `delete` | array of non-empty strings | Project-relative file/directory glob patterns to delete before task execution. |
| `platform_commands` | table | Per-platform command and optional shell overrides. |

The task's `target`, if set, takes precedence over shell-command execution.
`delete` patterns are applied before the task action. Patterns must start with
a literal project-relative directory (for example, `"CBoxBuild/*"`); absolute
paths, parent-directory traversal, and wildcards in the first path component
are rejected. CBox refuses project-root entries and `.git` paths, so patterns
such as `"*"` and `"*/*"` cannot wipe project contents. All matches are
validated before deletion begins and resolved to ensure they remain beneath
the current project root. Matches that are symlinks resolving outside the
project are refused; recursive deletion does not follow symlinks. These
safeguards apply to `delete`; shell commands and
`platform_commands` remain arbitrary user commands and can remove other files.

`native` uses `cmd.exe` on Windows and `sh` on other supported platforms.
`powershell` selects `pwsh`, or `powershell.exe` on Windows when available.
`cmd` is Windows-only. Explicitly selected shells must be installed and on
`PATH`.

### Platform-specific commands

Platform overrides use the operating-system names `windows`, `linux`, and
`macos`. Each override requires a non-empty `command`; its `shell` is optional.

```toml
[task.clean.platform_commands.windows]
shell = "cmd"
command = "if exist build rmdir /s /q build"

[task.clean.platform_commands.linux]
shell = "sh"
command = "rm -rf build"
```

When the current platform has an override, CBox uses its command and uses its
shell if one is specified; otherwise it falls back to the task's `shell`.

## Template placeholders

The `print` text and task `command` support these placeholders:

| Placeholder | Value |
| --- | --- |
| `{project.name}` | Project name. |
| `{task.name}` | Task name. |
| `{target.name}` | Target name, when a target context exists. |
| `{target.kind}` | Target kind, when a target context exists. |
| `{profile.name}` | Profile name, when selected. |
| `{profile.optimization}` | Profile optimization preset. |
| `{profile.warnings}` | Profile warning preset. |
| `{profile.debug_symbols}` | `true` or `false`. |

If a task has no explicit target but its message or command contains a target
placeholder, CBox expands and prints it once per target. This placeholder-based
mode prints the expanded text; it does not build those targets.

## Example: shell task

```toml
[task.check]
shell = "native"
command = "echo checks passed"
```

Run it with:

```text
cbox check
```
