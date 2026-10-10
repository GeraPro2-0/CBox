# Configuration reference

CBox loads `CBox.toml` from the process's current working directory. The file is
TOML. The `[cbox]` table and its supported configuration-format version are
required.

## Minimal project

```toml
[cbox]
version = "1.0"

[project]
name = "hello"
version = "1.0.0"

[toolchain]
compiler = "auto"

[[target]]
name = "hello"
kind = "executable"
c_sources = ["src/**/*.c"]

[profile.debug]
optimization = "debug"
debug_symbols = true

[task.build]
target = "hello"
profile = "debug"
```

Run `cbox build` from the directory containing this file to build the `hello`
target.

## Top-level tables

### `[cbox]`

|    Key    |  Type  | Required |              Meaning               |
| --------- | ------ | -------- | ---------------------------------- |
| `version` | string |   Yes    | CBox configuration format version. |

### `[project]`

|      Key      |       Type       | Default  |          Meaning           |
| ------------- | ---------------- | -------- | -------------------------- |
|    `name`     | string           | `"CBox"` | Project name.              |
|   `version`   | string           |    —     | Project's own version.     |
| `description` | string           |    —     | Short project description. |
|   `authors`   | array of strings |    —     | Project authors.           |
|   `license`   | string           |    —     | License identifier.        |

These metadata fields are displayed by CBox's no-argument summary when
configured.

### `[toolchain]`

|    Key     |  Type  | Default  |       Meaning       |
| ---------- | ------ | -------- | ------------------- |
| `compiler` | string | `"auto"` | Compiler selection. |

Automatic selection uses the platform's preferred installed toolchain. C++
targets also require the matching C++ compiler driver.

The options for the compiler are: `auto` (automatic), `gcc`, `clang` and `msvc`

## Targets

Declare each target with an array-of-tables entry, `[[target]]`.

|  Key   |  Type  |      Default       | Meaning |
| ------ | ------ | ------------------ | ------- |
| `name` | string | `"unnamed-target"` | Target identifier used by tasks and as the default executable output name. |
| `kind` | string | `"executable"` | `"executable"`, `"static"`, or `"shared"`. |
| `output` | string | Toolchain-derived | Output filename under `CBoxBuild/<compiler>/<target>/<profile>/bin/`; directories are ignored. Missing or recognized target-kind extensions use the platform default; unrelated extensions are preserved. |
| `freestanding` | boolean | Unspecified | Suitability declaration consulted by `--freestanding`: explicit `false` rejects the mode; `true` accepts the declaration; omitted means CBox proceeds without verification. |
| `entry_point` | string | Unspecified | Entry-point symbol passed to the linker for executable/shared targets when `--freestanding` is used. |
| `c` | string | `"c99"` | C language standard. |
| `cpp` | string | `"c++11"` | C++ language standard. |
| `c_sources` | array of strings | Empty | C source paths or glob patterns. |
| `cpp_sources` | array of strings | Empty | C++ source paths or glob patterns. |
| `cpp_modules` | array of strings | Empty | C++ named module-interface source paths or glob patterns. |
| `include_dirs` | array of strings | Empty | Include search directories passed to compilation. |
| `depends_on` | array of target names | Empty | Targets that must build successfully before this target; does not pass outputs to the linker. |
| `link_targets` | array of target names | Empty | Static library targets to pass to the linker. Each target is also built first. |
| `compile_args` | array of strings | Empty | Additional compiler arguments. |
| `link_libraries` | array of strings | Empty | Libraries for executable or shared targets. |
| `link_args` | array of strings | Empty | Additional linker arguments for executable or shared targets. |

### Local library dependencies

Import a target from another local CBox project with `[[dependency]]`:

```toml
[[dependency]]
name = "math"
path = "../math/CBox.toml"
target = "math"

[[target]]
name = "application"
kind = "executable"
c_sources = ["src/main.c"]
link_targets = ["math"]
```

`path` names a CBox manifest and is resolved relative to the manifest that
declares the dependency. `target` must select a static library target in that
manifest. The selected target is exposed under `name`; other targets from the
package and its nested dependencies are namespaced under that alias. Nested
local dependencies are loaded recursively. Source files, module interfaces,
and include directories from imported manifests are resolved relative to the
manifest that defines each target. The current project supplies the compiler
and build profile for all imported targets. Imported include directories and
transitive `link_targets` are propagated to consumers.

Source patterns support `*`, `?`, and recursive `**` matching. CBox expands
patterns to files and sorts the resulting paths. A target must resolve to at
least one source file. Static targets cannot set `link_libraries` or `link_args`.

`depends_on` defines build-order-only edges by target name. `link_targets`
links the named static library artifacts automatically and implies build-order
edges. Static targets may use `link_targets` to declare static-library
dependencies; their archives remain independent, and executable/shared
consumers link the transitive libraries after the libraries that depend on
them. CBox builds only the selected task target and its transitive
prerequisites, dispatching independent ready targets together with a shared
worker limit. Changes to a linked library artifact invalidate the dependent
target's link cache. Cycles and missing or ambiguous target names are errors.
Use `link_libraries` for external libraries; static targets cannot use
`link_targets`.

`freestanding` is a suitability declaration checked by `--freestanding`:
selected targets and prerequisites that explicitly set it to `false` are
rejected, while omitted declarations proceed without verification. In this
mode GCC and Clang receive `-ffreestanding`; MSVC receives `/X` when compiling
and `/NODEFAULTLIB` when linking executable/shared targets. If `entry_point`
is set, CBox passes `-Wl,-e,<function>` to GNU-style linkers and
`/ENTRY:<function>` to MSVC-style linkers, including Clang on Windows. (CBox
invokes MSVC's linker directly, so linker-only options are passed without the
`cl.exe` `/link` separator.) Static targets are archived without entry-point
or linker options. These options do not select a target platform, startup
objects, or a complete bare-metal runtime; configure those separately.

For GNU-style toolchains, a library name such as `"m"` becomes `-lm`; names
already containing a path, library extension, or `-l` prefix pass through.
For MSVC, a bare library name becomes a `.lib` filename. The name `"m"` is
omitted for MSVC.

Supported names for C: `c89`, `c90`, `ansi c`, `ansic`, `ansi`, `c95`, `c99`, `c11`, `c17`, `c18`, `c23`.
Supported names for C++: `c++98`, `c++03`, `c++11`, `c++14`, `c++17`, `c++20`, `c++23`, `c++26`.

### C++ modules

`cpp_modules` is intended for named module interface units that declare an
exported module, for example:

```cpp
export module geometry;

export int area(int width, int height) {
    return width * height;
}
```

Module interface units are compiled before the target's `cpp_sources`, so those
sources can import the modules. CBox scans named imports between declared
module units and constructs a dependency graph, so source-list order need not
match import order. Import cycles and duplicate module names are errors.
Imports not provided by a declared module unit are passed through for the
selected compiler to resolve.

Modules require `cpp` to be `c++20`, `c++23`, or `c++26`. Module support and
compiler flags vary by compiler version; CBox reports a compiler failure if the
installed toolchain cannot process the module units.

## Profiles

Declare profiles as `[profile.NAME]`. Profiles can be selected by a task.

| Key | Type | Default | Meaning |
| --- | --- | --- | --- |
| `optimization` | string | `"debug"` | Optimization preset: `"debug"`, `"release"`, `"fast"`, `"small"`, `"none"`, `"basic"`, or `"tiny"`. |
| `warnings` | string | `"none"` | Warning preset: `"none"`, `"basic"`, `"extra"`, `"strict"`, or legacy alias `"all"`. |
| `debug_symbols` | boolean | `false` | Request debug information from the compiler/linker. |
| `print` | string | `"Profile: {profile.name}"` | Accepted and stored, but currently not used during task execution. |

Presets map to compiler-specific options; some compilers may not support every
preset.

Warning presets map approximately to compiler options:

| Preset | GCC/Clang | MSVC |
| --- | --- | --- |
| `"none"` | No additional warning flags | No additional warning flags |
| `"basic"` | `-Wall` | `/W3` |
| `"extra"` | `-Wall -Wextra` | `/W4` |
| `"strict"` | `-Wall -Wextra -Wpedantic` | `/W4` |
| `"all"` | Alias for `"strict"` | Alias for `"strict"` |

These presets do not enable every warning supported by each compiler and do
not treat warnings as errors. MSVC's warning levels are not exact equivalents
of the GCC/Clang presets.

## See also

- [CLI and task definitions](tasks.md)
- [Building targets and compiler behavior](building.md)
