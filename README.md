[![CBox](https://img.shields.io/badge/CBox-blue.svg)](https://github.com/GeraPro2-0/CBox)
[![C](https://img.shields.io/badge/c-2017-green.svg)](https://en.cppreference.com/w/c)
[![C++](https://img.shields.io/badge/c++-2020-red.svg)](https://en.cppreference.com/w/cpp)
[![License](https://img.shields.io/badge/license-Apache%202.0-yellow.svg)](LICENSE)

# CBox

CBox is a TOML-configured build tool and task runner for C and C++ projects.
Define targets, compiler settings, build profiles, and project tasks in
`CBox.toml`, then invoke tasks from the project directory.

CBox is written in C (C17) and a bit of C++ (C++20).

## Features

- Build C and C++ executables, static libraries, or shared libraries with GCC,
  Clang, or MSVC.
- Select C and C++ language standards and compiler/linker arguments per target.
- Configure reusable optimization and debug-information profiles.
- Store target- and profile-specific binaries, objects, dependency data, and a
  SHA-256 incremental-build cache under `CBoxBuild/`.
- Compile independent sources in parallel for targets with at least 50 KiB of
  source text, while keeping small builds procedural.
- Run shell commands, platform-specific commands, or target builds as tasks.
- Compile C++ module interface units in discovered import dependency order.
- Print project metadata and list available tasks from the command line.

## Getting started

Create a `CBox.toml` in your project root:

```toml
[cbox]
version = "1.0"

[project]
name = "hello"
version = "1.0.0"

[toolchain]
compiler = "auto" # It can be "gcc", "clang", "msvc", or "auto" for automatic selection (this is the default option).

[[target]]
name = "hello"
kind = "executable"
c_sources = ["src/**/*.c"]
cpp_sources = ["src/**/*.cpp"]

[profile.release]
optimization = "release"
warnings = "strict"
debug_symbols = false

[task.release]
target = "hello"
profile = "release"
```

Run CBox from that directory:

```text
cbox --version
cbox --list
cbox debug
```

If `c` or `cpp` is omitted from a target, CBox defaults to C99 and C++11,
respectively. To build C++ sources, list them in `cpp_sources`. To build C++
modules, list named module interface paths in `cpp_modules`; CBox discovers
imports between those units and orders compilation automatically. Module
support depends on the selected compiler and version.

## Build from source

The project uses CMake to configure and build the CBox executable:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

To install the executable into a staging directory:

```sh
cmake --install build --config Release --prefix package
```

Optional developer targets can be enabled at configure time with
`-DCBOX_BUILD_TESTS=ON`, `-DCBOX_BUILD_BENCHMARKS=ON`, or
`-DCBOX_ENABLE_CLANG_TIDY=ON`.

GitHub Actions builds Linux, Windows, and macOS packages on pushes, pull
requests, and manual runs, and runs the integration suite on Ubuntu. Each
workflow run publishes short-lived package artifacts. To publish a release,
push a tag matching `[cbox].version` in `CBox.toml` (currently `1.0`, with no
`v` prefix). The workflow names the release from `[project].version`
(`1.0.0`) and attaches archives named `CBox-1.0-linux-latest.tar.gz`,
`CBox-1.0-windows-latest.zip`, and `CBox-1.0-macos-latest.tar.gz`. Each archive
contains a matching top-level folder with the installed executable under
`bin/`.

See [CONTRIBUTING.md](CONTRIBUTING.md) for contribution guidance and
[CODE_OF_CONDUCT.md](CODE_OF_CONDUCT.md) for community expectations. Security
issues should be reported privately as described in [SECURITY.md](SECURITY.md).

## Documentation

See the [CBox documentation](docs/index.md):

- [Configuration reference](docs/configuration.md)
- [CLI and tasks](docs/tasks.md)
- [Building C and C++ targets](docs/building.md)

## License

CBox is distributed under the Apache License 2.0. See the license terms at
<https://www.apache.org/licenses/LICENSE-2.0>.
