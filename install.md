# Building and installing jinja2-tcl

## Requirements

- CMake 3.22.1 or newer and a C++17 compiler.
- Tcl 8.6 or Tcl 9 headers and its matching stub library.
- pkg-config (or pkgconf).
- An installed upstream-compatible cJSON development package (headers, library,
  and `libcjson.pc`).
  cJSON 1.7.19 is used for local validation. `cJSON_ParseWithLengthOpts` is required.
- A matching `tclsh` with `tcltest` for tests, which are enabled by default.

cJSON is external: the project never downloads or copies it. On Debian/Ubuntu,
the usual development packages are `tcl-dev` and `libcjson-dev`. An existing
installation under `/usr/local/include/cjson` and `/usr/local/lib` is supported.
On macOS or Windows, provide matching Tcl and cJSON installations through the
cache variables below. Use the same architecture/toolchain for all libraries.

## Build and test

From the `jinja2-tcl` directory:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

On systems with multiple Tcl installations, select one explicitly. Keep a
separate build directory for each Tcl major version; Tcl 8 and Tcl 9 builds are
not interchangeable. For this Linux development setup:

```sh
# Tcl 8.6
cmake -S . -B build-tcl86 -DCMAKE_BUILD_TYPE=Release \
  -DTCL_INCLUDE_PATH=/usr/include/tcl8.6 \
  -DTCL_STUB_LIBRARY=/usr/lib/x86_64-linux-gnu/libtclstub8.6.a \
  -DTCL_TCLSH=/usr/bin/tclsh8.6
cmake --build build-tcl86 --parallel
ctest --test-dir build-tcl86 --output-on-failure

# Tcl 9.1 installed in /usr/local
cmake -S . -B build-tcl91 -DCMAKE_BUILD_TYPE=Release \
  -DTCL_INCLUDE_PATH=/usr/local/include \
  -DTCL_STUB_LIBRARY=/usr/local/lib/libtclstub.a \
  -DTCL_TCLSH=/usr/local/bin/tclsh9.1
cmake --build build-tcl91 --parallel
ctest --test-dir build-tcl91 --output-on-failure
```

Tests cover the original C++ rendering API and the Tcl command layer. The two
optional tjson tests are skipped when a matching tjson package is unavailable.
To enable them, add the directory containing tjson's matching `pkgIndex.tcl` to
`TCLLIBPATH` before running CTest. No tjson source is needed to build jinja2-tcl.

Local validation covered Linux shared and static builds with Tcl 8.6.17 and
Tcl 9.1.0, using cJSON 1.7.19 from `/usr/local/lib`. The full Tcl suite passed
152 tests on both interpreters with tjson available. macOS and Windows builds
have not been exercised.

To try the build without installation:

```sh
TCLLIBPATH="$PWD/build-tcl91" /usr/local/bin/tclsh9.1 examples/basic.tcl
TCLLIBPATH="$PWD/build-tcl91" /usr/local/bin/tclsh9.1 examples/templates.tcl
```

For multi-configuration generators, pass `--config Release` when building and
`-C Release` to CTest; install the package before trying the examples.

## Dependency discovery

| Variable | Purpose |
| --- | --- |
| `CMAKE_PREFIX_PATH` | Additional installation prefixes for cJSON/Tcl discovery. |
| `PKG_CONFIG_PATH` (environment) | Additional directories containing `libcjson.pc`. |
| `TCL_INCLUDE_PATH` | Directory containing `tcl.h`. |
| `TCL_STUB_LIBRARY` | Full path to the matching Tcl stub library. |
| `TCL_TCLSH` | Matching Tcl interpreter used by shared-build tests. |
| `TCL_INCLUDE_DIR`, `TCL_LIBRARY_DIR` | Search hints following tjson's build convention. |
| `BUILD_TESTING` | Set to `OFF` to build without test executables/interpreter. |
| `BUILD_SHARED_LIBS` | `ON` for a loadable extension; `OFF` for a static archive. |

cJSON is discovered through pkg-config's `libcjson` module, which supplies its
include directories and linker flags. For a custom installation, use
`-DCMAKE_PREFIX_PATH=/path/to/prefix` or point `PKG_CONFIG_PATH` at its `.pc` directory:

```sh
PKG_CONFIG_PATH=/usr/local/lib/pkgconfig cmake -S . -B build
```

Headers and library must describe the same ABI. The cJSON embedded inside
tjson is modified and is not a replacement for the upstream library.

## Install

```sh
cmake --install build --prefix "$HOME/.local"
```

The shared build installs the library and `pkgIndex.tcl` under
`lib/jinja2<version>` (or the platform's `CMAKE_INSTALL_LIBDIR`), the embedding
header under `include/jinja2`, and documentation under `share/doc/jinja2`.
If the prefix is outside Tcl's package search path, add the package directory:

```tcl
lappend auto_path [file join $env(HOME) .local lib jinja20.1.0]
package require jinja2 0.1.0
```

Do not install Tcl 8 and Tcl 9 builds into the same package directory; use
separate prefixes. Ensure the system dynamic loader can locate cJSON at runtime.
For nonstandard prefixes, configure `CMAKE_INSTALL_RPATH` as appropriate on Unix,
or place the cJSON DLL on `PATH` on Windows. This project does not install cJSON.

## Static embedding

```sh
cmake -S . -B build-static -DBUILD_SHARED_LIBS=OFF \
  -DTCL_INCLUDE_PATH=/usr/local/include \
  -DTCL_STUB_LIBRARY=/usr/local/lib/libtclstub.a \
  -DTCL_LIBRARY=/usr/local/lib/libtcl9.1.so
cmake --build build-static --parallel
ctest --test-dir build-static --output-on-failure
```

This produces `libjinja2.a` (or `.lib`). No `pkgIndex.tcl` is installed for a
static archive. Link the application with jinja2, Tcl, cJSON, and the C++ runtime;
the static archive does not absorb cJSON. When using `add_subdirectory`, linking
the CMake target `jinja2` propagates its cJSON dependency. A fully static executable
also requires static versions of those dependencies.

Compile the host with `STATIC_BUILD` and include `library.h`. After `Tcl_Init`,
call `Jinja2_Init(interp)` and check for `TCL_OK`. For loading into additional
interpreters, register `Jinja2_Init` with `Tcl_StaticLibrary` on Tcl 9 or
`Tcl_StaticPackage` on Tcl 8.6. `tests/static_shell.cpp` is a complete example;
the static build uses that executable to run the same Tcl test suite.
On Tcl 9, the example calls `TclZipfs_AppHook` before `Tcl_Main` so installations
with an embedded ZIP script library can initialize correctly.

Do not combine an unmodified static tjson archive and upstream cJSON in the same
executable: tjson's bundled cJSON exports the same symbol names with a modified
structure layout. That embedding arrangement requires isolating tjson's cJSON
symbols first. Exchanging JSON text between separately loaded extensions does
not require sharing their private structures. Avoid globally loading a library
that exports an incompatible cJSON ABI.
