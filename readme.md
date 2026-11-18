# jinja2-tcl

A Tcl extension for rendering a deterministic Jinja2-style template subset against
JSON data. This is not the Python Jinja2 package and does not require Python.

This module was generated with help from AI.

```tcl
package require jinja2 0.1.0

set template {<h1>Hello {{ name }}</h1><p>{{ items|join(', ') }}</p>}
puts [::jinja2::render $template {{"name":"Neo & friends","items":["Tcl","Jinja2"]}}]
# <h1>Hello Neo &amp; friends</h1><p>Tcl, Jinja2</p>
```

See [install.md](install.md) for building, testing, installing, and static embedding.
The source supports Tcl 8.6 and Tcl 9, with a separate build for each major ABI.

## Commands

| Command | Result |
| --- | --- |
| `::jinja2::render template json ?-templates dictionary? ?-path name?` | Rendered text; raises a Tcl error on failure. |
| `::jinja2::validate template ?-templates dictionary? ?-path name?` | `1` on success; raises a Tcl error on failure. |
| `::jinja2::environment ?-loader commandPrefix? ?-templates dictionary?` | A command owning a loader and a parsed-template cache. |
| `::jinja2::supported_concepts` | Description of the supported template language. |

The JSON context must be an object. Use `{{}}` in Tcl source to pass the JSON text
`{}` for an empty context. Context keys become template variables; there is no
implicit `this` or `product` alias. Ordinary strings are HTML-escaped by default.
Each render owns its context and renderer state. The string-based commands remain
stateless; an optional environment caches parsed templates across renders.

`validate` checks syntax, supported operations, macro references, and supplied
include/inheritance dependencies. It does not take a context or guarantee that
every data-dependent rendering operation will succeed. Invalid templates raise
errors rather than returning `0`:

```tcl
if {[catch {::jinja2::validate $template} message options]} {
    puts stderr "$message ([dict get $options -errorcode])"
}
```

Errors use `JINJA2 JSON`, `JINJA2 OPTION`, `JINJA2 RENDER`, `JINJA2 VALIDATE`,
`JINJA2 MEMORY`, or `JINJA2 INTERNAL`. Wrong argument counts use `TCL WRONGARGS`.
No partial output is returned on a rendering failure.

## Loading templates by name

An environment calls a trusted Tcl command prefix only when it needs a template.
The loader receives one additional argument, the relative template name, and
returns its text or raises an error. Creating the environment reads nothing.

```tcl
proc load_template {directory name} {
    # Template names are trusted application identifiers, not request paths.
    set channel [open [file join $directory $name] r]
    try {
        chan configure $channel -encoding utf-8
        read $channel
    } finally {close $channel}
}
set pages [::jinja2::environment -loader [list load_template /app/templates]]
puts [$pages render article.html.jinja {{"title":"First request"}}]
puts [$pages render article.html.jinja {{"title":"Next request"}}]
$pages destroy
```

The root template, inherited layouts, and executed includes are loaded and parsed
once per environment. An include in an unexecuted conditional branch is not read.
Request data and rendered output are never cached. Optional `-templates` entries
override the loader, allowing separate environments to compose different layouts.

| Environment method | Behavior |
| --- | --- |
| `render name json` | Render a named template with fresh request data. |
| `load name` | Preload and parse one template without rendering it or traversing dependencies. |
| `validate name` | Check the template and its dependency graph, including unexecuted branches. |
| `loaded` | List cached template names. |
| `clear` | Discard parsed templates; retain the loader and overrides. |
| `destroy` | Delete the command and release its cache and loader. |

Use `validate` during packaging/CI for every template in the artifact. A successful
render alone does not validate unexecuted branches. Eager hosts can call `load` for
each artifact template during initialization; lazy hosts just call `render`.

Caches are local to an environment in one interpreter and persist until `clear`,
`destroy`, or interpreter deletion. Files are not checked for changes automatically.
Use separate environments for different mounts or tenant overrides. Loader and
parse failures are retryable; successfully cached dependencies remain available.
Recursive use of an active environment raises `JINJA2 BUSY`; preload failures use
`JINJA2 LOAD`. The loader must enforce filesystem boundaries, including symlinks,
when reading files. The engine rejects absolute paths, dot segments, backslashes,
empty segments, NULs, and control characters before invoking the loader.

## Using tjson

JSON text is the interoperability boundary. `tjson` is optional:

```tcl
package require tjson
package require jinja2

set context [::tjson::parse {{"name":"Neo","items":[1,2,3]}}]
try {
    puts [::jinja2::render {Hello {{ name }}: {{ items|sum }}} \
        [::tjson::to_json $context]]
} finally {
    ::tjson::destroy $context
}

# Typed Tcl values work through tjson's public serializer too.
set typed [list M [list name {S Neo} enabled {BOOL 1}]]
puts [::jinja2::render {Hello {{ name }}} [::tjson::typed_to_json $typed]]
```

This avoids depending on tjson's private handle registry, modified cJSON layout,
allocator, or object lifetime. It also preserves JSON distinctions such as a
boolean versus a string and an empty array versus an empty object. Plain Tcl
lists/dictionaries do not preserve all these distinctions without type tags.
There is a serialization/parsing cost when starting from a tjson handle; the
renderer already copies the JSON tree into its own value model on every call.

The module finds and links an **external cJSON library** through pkg-config's
`libcjson` module. No cJSON source or header files are bundled, and nothing is
downloaded during configuration. Do not link
against tjson's private cJSON implementation. See the static-linking caveat in
[install.md](install.md#static-embedding).

## Includes and inheritance

Supply a dictionary mapping relative template names to their contents:

```tcl
set templates [dict create \
    layout.html {<main>{% block body %}Default{% endblock %}</main>} \
    parts/name.html {<b>{{ name }}</b>}]
set template {{% extends 'layout.html' %}{% block body %}{% include 'parts/name.html' %}{% endblock %}}

::jinja2::validate $template -templates $templates -path page.html
puts [::jinja2::render $template {{"name":"Neo"}} \
    -templates $templates -path page.html]
# <main><b>Neo</b></main>
```

Names are relative to the supplied dictionary, not the current directory or the
directory of the including template. `-path` names the root template for graph
validation and cycle detection; it does not read that name from the dictionary.
It requires `-templates`. An empty `-path` means an unnamed root.

The extension never opens files, runs Tcl scripts, or invokes loader callbacks.
Applications can read approved files themselves and populate the dictionary.
Referenced paths must be relative POSIX paths without backslashes, NUL/control
characters, empty segments, `.` or `..`. Dynamic paths, missing dependencies, and
dependency cycles fail. An extending template may contain only whitespace and
blocks at its top level; put assignments, macros, includes, and other content
inside a block.

## Template language and limits

The [template language reference](docs/template-language.md) documents expressions,
filters, tests, loops, assignments, macros, includes, and inheritance. Unsupported
constructs fail explicitly. The copied C++ tests protect the renderer's existing
behavior; Tcl tests cover the extension boundary and optional tjson integration.
The Tcl suite also ports 88 rendering fixtures and 14 validation/inheritance
cases from `core/jinja2/tests`. With tjson available, it runs 152 tests.

Use templates from trusted authors. HTML escaping is not a general HTML/JavaScript
sandbox; use `safe` only with trusted markup. There is no execution time or total
output limit. Tcl interpreter limits do not interrupt an active native render.
No `Jinja2_SafeInit` entry point is provided. Range creation is capped at 10,000
items and the engine bounds macro/block recursion, but nested expressions, input
collections, and template expansion can still consume substantial resources.

JSON parsing uses cJSON's syntax and double-precision numbers. Integers beyond
exact double precision can lose precision. Non-finite context numbers, trailing
non-whitespace input, and non-object roots are rejected. JSON strings and keys
containing NUL (`\u0000`) are rejected rather than silently truncated by cJSON's
C strings. JSON null values are supported. Tcl text is converted to/from standard
UTF-8; inherited string indexing, length, and slicing operate on UTF-8 bytes, and
case conversion is primarily ASCII rather than Unicode-aware.

## Architecture

- `src/library.cpp`: Tcl command registration, UTF-8 conversion, argument/error
  handling, JSON ownership, and the dictionary-backed template loader.
- `src/engine/`: copied parser/renderer and the small set of string helpers it uses.
- `cmake/`: Tcl stub discovery.
- `tests/`: original C++ renderer regressions plus Tcl command/integration tests.
- `examples/`: basic rendering, template dependencies, and optional tjson use.

Like tjson, this is a CMake-built native Tcl package with a namespace,
`Jinja2_Init`, a generated `pkgIndex.tcl`, a versioned install directory, stub
linking for shared builds, and a static build option. The C++ engine is an internal
implementation detail. The project builds independently of either source
repository.

MIT licensed; see [LICENSE](LICENSE).
