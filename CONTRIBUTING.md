# Contributing to CBox

Thanks for considering a contribution. Bug reports, focused fixes, tests, and
small improvements to the documentation are all useful.

Please report security vulnerabilities privately using the process in
[SECURITY.md](SECURITY.md), rather than opening a public issue.

## Before opening a pull request

- For a significant change, open an issue or discussion first so the approach
  can be agreed on before implementation work piles up.
- Keep pull requests focused. Explain the problem, the behavior being changed,
  and any trade-offs or platform-specific details.
- Include or update tests when behavior changes. Update documentation when a
  user-facing command, setting, or workflow changes.
- Do not bundle unrelated formatting or cleanup with a functional change.

## Build and test

CMake is the bootstrap build system:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

To build and run the integration test runner:

```sh
cmake -S . -B build-tests -DCMAKE_BUILD_TYPE=Release -DCBOX_BUILD_TESTS=ON
cmake --build build-tests --config Release
ctest --test-dir build-tests -C Release --output-on-failure
```

The test runner invokes installed compiler toolchains for some integration
tests. When a test depends on an unavailable compiler or platform feature,
include the relevant environment details and the exact failure in the pull
request.

## Code and review expectations

- Follow the surrounding C17/C++20 style and existing ownership/error-handling
  patterns.
- Prefer changes that are portable across the supported GCC, Clang, and MSVC
  toolchains.
- Handle allocation, I/O, and process failures explicitly. Avoid silently
  ignoring errors or returning success-shaped fallback values.
- Add comments for non-obvious constraints or decisions; comments should help
  a future maintainer understand why the code is written that way.
- Do not commit generated binaries, build directories, credentials, or other
  machine-specific files.

## Third-party code and license policy

Third-party libraries and copied code must have a license that is compatible
with CBox's Apache-2.0 distribution and approved by the maintainers before
being added. Permissive licenses that may be considered include:

- Apache License 2.0
- MIT
- BSD 2-Clause or BSD 3-Clause
- zlib

This list is not automatic approval. Review the exact license text and any
additional notices, patent terms, attribution requirements, or bundled
components. If the license or provenance is unclear, do not add the code until
the maintainers have reviewed it.

GPL- and AGPL-licensed code, as well as licenses with comparable strong
copyleft or source-disclosure requirements, are not accepted for inclusion.
LGPL and other weak-copyleft or reciprocal licenses are restricted: they may
only be proposed for explicit maintainer review and approval before any code
is copied, vendored, linked, or made a required dependency. Without that
approval, treat them as not permitted. Do not assume that dynamic linking,
optional use, or a dual-license offer resolves compatibility concerns.

For every proposed dependency or copied code, include in the pull request:

- The component name, version or commit, and canonical source URL.
- The exact license identifier and a link or copy of the license text.
- Any required copyright and attribution notices.
- Whether code is copied into CBox, linked, downloaded at build time, or used
  only for development/tests.

Preserve the component's license and copyright notices. Update
[NOTICE.md](NOTICE.md) and include the license text or required notices in the
appropriate location when required by the license. CBox currently includes
third-party notices there; check it before adding another component.

## Pull requests

Before requesting review, make sure the relevant build and tests pass and that
the pull request description covers:

1. What problem the change solves.
2. How it was tested, including compiler and operating-system details when
   relevant.
3. Any user-visible impact, compatibility change, or follow-up work.
4. Third-party provenance and licensing details, if applicable.

Maintainers may request revisions, additional tests, or licensing clarification
before merging.
