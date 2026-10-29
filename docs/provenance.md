# Source provenance

The project keeps its own copies of the engine and tests so builds do not need
neighboring repositories or their build helpers.

- `src/engine/jinja2.cpp` and `jinja2.h`: copied from
  `jerily-webserver/core/jinja2/src` at commit
  `63d7fdf972e4c8ce3f1d0ed86ef701db345bae58`.
- `tests/test_engine.cpp`: copied from that module’s `tests/test_jinja2.cpp`.
- `tests/language.test`: its literal render fixtures ported to the Tcl command API.
  `tests/validation.test` ports diagnostic, macro-order, and inheritance cases.
- `docs/template-language.md`: adapted from that module’s `readme.md`.
- `src/engine/string_utils.*`: only the seven required helpers copied from
  `jerily-webserver/core/string-utils/src/string_utils.cpp` at the same commit.
  The unrelated C API and other helpers are omitted.
- `cmake/FindTclStub.cmake`: adapted from `tjson` at commit
  `7074dfb9eb3f8ea06ac0d63e43166796e8fb0496`, with Tcl 9.1 lookup and preference
  for the selected Tcl ABI’s stub library.

Engine changes are mechanical namespace/header-guard isolation:
`jerily::jinja2` becomes `jinja2_tcl::engine`, and `jerily::string_utils` becomes
`jinja2_tcl::string_utils`. The original renderer behavior and regression tests
are retained. Tcl ownership, encodings, errors, and the dictionary loader live
in the separate `src/library.cpp` adapter.

cJSON is an external link dependency. No cJSON implementation or headers are
copied into this repository. The existing MIT license is preserved.
