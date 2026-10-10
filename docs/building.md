# Building C and C++ targets

CBox compiles each source according to its language: C sources use the C
compiler driver, while C++ sources and modules use the corresponding C++
compiler driver. Executable and shared targets containing C++ sources or modules are linked
with the C++ driver so the C++ runtime is included. C-only targets use the C
driver.

Supported compiler selections are `auto`, `gcc`, `clang`, and `msvc`. MSVC is
available only on Windows. The requested compiler must be installed and
discoverable. A C++ target also needs its matching C++ compiler driver (for
example, `g++` or `clang++`).

## Source and object handling

Targets may combine `c_sources`, `cpp_sources`, and `cpp_modules`. CBox expands
source globs and places generated files in
`CBoxBuild/<compiler>/<target>/<profile>/`.
The compiler directory is `gcc`, `clang`, or `msvc`, keeping toolchain outputs
and caches separate so switching compilers cannot reuse another compiler's
artifacts.
Target directory names are sanitized and include a stable hash to keep
different target names isolated. Executables, static archives, and shared
libraries go under `bin/`; objects and module files go under `obj/`, and the
cache is in the compiler/target/profile directory. A target with no resolved source
files is rejected. If no profile is selected, CBox uses the `default` profile
directory.

For C++ module targets, CBox reads named imports and builds a dependency DAG.
Module interfaces are compiled in dependency order regardless of their order in
`cpp_modules`; import cycles are reported before compilation starts. Imports
provided outside the target are left to the selected compiler.

The configured `output` value is treated as a filename; any directory portion
is ignored. If its extension is absent, CBox adds the platform default for the
target kind (executables use `.exe` on Windows and no suffix elsewhere; static
libraries use `.lib` on Windows and `.a` elsewhere; shared libraries use
`.dll`, `.dylib`, or `.so`). A recognized extension for the target kind is
replaced with that platform default. An unrelated extension is preserved as
requested. With no configured output, CBox derives the name from the target
name and uses the normal platform-specific library naming convention.

## Incremental builds

Each target/profile has a binary `CBoxCache.bin`. Format version 2 starts with
the four-byte `CBOX` magic, a little-endian 32-bit version and record count,
then records containing a length-prefixed output path, a 32-byte compile/link
fingerprint, a 32-byte output hash, and a count followed by length-prefixed
dependency paths and their 32-byte SHA-256 hashes. Integer fields are little
endian; digests are stored as raw bytes. This keeps the record structure
versioned and independent of host byte order. Older or malformed cache files
are discarded and rebuilt.

Records form a dependency graph: object records point to compiler-reported
source/header/module inputs, and final target records point to their object
files, linked target libraries, and any shared-library import library. Output hashes are stored on the
record rather than as self-dependencies. Compiler-reported Make dependency
output (GCC/Clang) and source-dependency JSON (MSVC) provide the input edges;
content hashes detect changed or missing inputs and outputs. A hash index
locates records without scanning the full cache, and obsolete records are
pruned after a successful build. Changed inputs, modified outputs, or missing
outputs cause the relevant object to be recompiled or the target to be
relinked. With MSVC debug symbols enabled, each object writes its compiler PDB
beside that object in the target's `obj/` directory rather than creating
`vc140.pdb` in the project root.

Before compiling ordinary translation units, CBox batches their cache checks.
The batch interns repeated file paths into compact 32-bit IDs, hashes each
unique output or dependency only once for that batch, and stores digests in a
contiguous scratch array. Batches with at least 16 unique paths hash files
through the bounded worker pool; smaller batches stay sequential. These IDs
and arrays are in-memory only and do not change the little-endian cache format.
Hashing remains portable scalar SHA-256: AVX2/NEON are not assumed to accelerate
file I/O, and architecture-specific SHA instructions are deferred until
profiling demonstrates that compression—not reading files—is the bottleneck.
Run `make benchmark-cache` to compare warmed per-entry validation against
batched validation at two fixture sizes. Worker startup and initial file-cache
warm-up are excluded from timed rounds. The benchmark reports elapsed time,
speedup, and the number of file hashes, and removes its temporary fixtures.

For a target whose source files total at least 50 KiB, CBox dispatches
independent C and C++ translation-unit compilations that need rebuilding to a
producer-consumer worker queue. Cache hits are filtered before workers are
started, so an unchanged build does not pay for build-worker startup. A
persistent pool reuses its workers across compile batches and caps total
parallel participants (including the calling thread) at eight, the available
processor count, or the number of stale translation units, whichever is
smallest. A single-processor system always compiles sequentially. If worker
creation fails, CBox continues with the workers that started, or falls back to
sequential compilation if none could be started. Smaller targets compile
sequentially to avoid scheduling overhead. C++ module interface units remain
ordered, and linking and task execution remain synchronous.

Targets can declare `depends_on` build-order edges and `link_targets` static
library references in the TOML configuration. A linked target is also built
before its consumer, and its artifact is passed to the linker automatically.
For a task that selects a target, CBox builds its transitive prerequisites
first and runs independent ready targets concurrently under the same bounded
worker pool. Compilation inside concurrently built targets runs sequentially,
preventing nested worker pools from oversubscribing the machine. Changes to a
linked library artifact invalidate the consumer's link cache.

Target dependency planning and linker library argument construction are
implemented in C++20 under `src/model/` and `src/util/`. Their C-compatible
APIs keep target scheduling, compilation, and toolchain handling in C. Building
CBox therefore requires a C++20-capable compiler as well as a C compiler.

Object filenames are derived from a stable hash of the normalized source path,
not the source's current position in the target's source list. Adding or
reordering other sources therefore keeps existing source-to-object mappings
stable.

The cache is disposable: deleting a target's `CBoxCache.bin` forces a fresh
compile for that target, and deleting `CBoxBuild/` removes all generated
outputs. C++ module interface units are rebuilt on each invocation because
module compiler artifacts are toolchain-specific; importing translation units
still use cached dependency hashes.

For `kind = "static"`, CBox archives the compiled objects. By default the
archive name is `libNAME.a` for GNU-style toolchains and `NAME.lib` for MSVC.
For `kind = "shared"`, CBox links a shared object: `.so` on Linux, `.dylib` on
macOS, and `.dll` on Windows. Windows builds also generate an import library
(`.lib` for MSVC or `.dll.a` for GNU toolchains). The `output` key overrides
the primary output name. Static targets cannot specify link dependencies;
executable and shared targets can use `link_libraries` and `link_args`.

## C++ modules

List module interface units in `cpp_modules`. CBox discovers named imports
between those units, compiles the modules in dependency order, and reports
cycles before compiling; their order in the list does not need to match their
dependency order. CBox then compiles regular `cpp_sources` and links or
archives all resulting objects.

```toml
[[target]]
name = "app"
kind = "executable"
cpp = "c++20"
cpp_modules = ["src/modules/base.cppm", "src/modules/math.cppm"]
cpp_sources = ["src/main.cpp"]
```

The source units should declare distinct exported named modules. CBox reads
those declarations and named imports to prepare compiler module references,
rejects duplicate module names, and ignores imports not provided by the
declared module units. Module compilation requires C++20 or later and depends
on the installed compiler's level of module support.

## Toolchain notes

- **GCC:** uses `gcc` and `g++`; module builds enable GCC's module support with
  `-fmodules-ts` and generate a module mapper for the declared modules. GCC's
  modules implementation is experimental/incomplete in some releases.
- **Clang:** uses `clang` and `clang++`; module interfaces are precompiled and
  then compiled to object files, with module references provided to importers.
- **MSVC:** uses `cl` to compile C and C++ files, using its module-interface
  and IFC-reference options for modules. `link.exe` links executables and
  `lib.exe` creates static libraries.

Compiler-option support and module compatibility depend on the installed
compiler version. CBox forwards `compile_args` to compilation and `link_args` to executable and
shared-target linking. When `cl` is already on `PATH`, CBox uses that configured
toolchain; otherwise it searches Visual Studio installations registered with
`vswhere` and falls back to locating `vcvars64.bat`.

MSVC options are selected using the detected compiler version. C11/C17 modes
require MSVC 19.28 or later; C23 mode requires 19.40 or later. C++14/C++17
selectable modes require 19.11 or later, and C++20 plus modules require 19.29
or later. C++23/C++26 use `/std:c++latest` where available and remain
preview-level support. Older compilers use their default mode for standards
they cannot select, with a warning; C++20+, C23, and module requests that the
compiler cannot honor fail with an upgrade message.

## Building CBox

From the repository root:

```sh
make
```

The Makefile also defines `make clean`, `make lint`, and `make test-cpp`.
