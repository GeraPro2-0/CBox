# CBox documentation

CBox is a TOML-configured build tool and task runner for C and C++ projects. It
reads `CBox.toml` from the current working directory, builds configured targets
with GCC, Clang, or MSVC, and runs named project tasks.

## Getting started

1. Create a `CBox.toml` in the root of your project.
2. Declare targets, profiles, and tasks in that file.
3. Run CBox from that directory:

   ```text
   cbox --list
   cbox debug
   cbox release
   ```

With no arguments, CBox prints a summary of the configuration and project
metadata. Use `cbox --help` for command-line help.

For an end-to-end configuration example, see
[Configuration reference](configuration.md). The rest of the guide is split
into [CLI and tasks](tasks.md) and [building C/C++ targets](building.md).

## Build CBox from source

The repository's Makefile builds the CBox executable:

```sh
make
```

Run the integration tests with:

```sh
make test-cpp
```

The test target exercises the configured C++ compiler drivers available on the
machine. Building CBox or its targets requires an installed C compiler; building
C++ targets also requires the corresponding C++ compiler.
