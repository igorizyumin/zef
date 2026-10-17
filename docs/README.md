# ZEF requirements and design

Start with the [ZEF introduction and user guide](introduction.md) for an
overview of the actor model, hierarchical state machines, and practical
state-machine examples.

The requirements are organized as a recursive four-layer work-item graph:

1. [`requirements.sdoc`](requirements.sdoc) — WHY: problems and user needs.
2. [`what-requirements.sdoc`](what-requirements.sdoc) — WHAT: formal system-boundary behavior using `shall` statements.
3. [`api-requirements.sdoc`](api-requirements.sdoc) — INTERFACE: public types, operations, inputs, limits, and assumptions.
4. [`implementation-work.sdoc`](implementation-work.sdoc) — IMPLEMENTATION: selected solutions and planned code links.

The parent chain is `WHY → WHAT → INTERFACE → IMPLEMENTATION`. A work item may
have multiple children, and a child may refine more than one parent when a
behavior crosses subsystem boundaries.

## Companion documents

- [`phase1-specification.md`](phase1-specification.md) records selected contract decisions.
- The detailed C-facing interface, signatures, and examples are embedded in [`api-requirements.sdoc`](api-requirements.sdoc) as RST rich text.
- [`phase1-host-example.md`](phase1-host-example.md) demonstrates the Phase 1 vocabulary.
- [`traceability.md`](traceability.md) records the migration map and link audit.
- [`test-work.sdoc`](test-work.sdoc) records executable verification work items,
  their upstream traceability, and the source test file that implements them.

The `.sdoc` files are the authoritative work-item records. Generated StrictDoc
output belongs in the ignored output directory. Implementation and test source
files use `@relation(ZEF-..., scope=...)` markers to connect code to the work
items it implements or verifies.

## Doxygen API reference

The public C API documentation is generated from `include/` and the
Cortex-M0+ port header with:

```text
doxygen Doxyfile
```

The generated HTML entry point is `docs/doxygen/html/index.html`. Generated
files are ignored by Git.

## LLVM coverage

LLVM source-based coverage is available through the opt-in `coverage` target.
Configure it with Clang from a Visual Studio Developer Command Prompt:

```text
cmake -S . -B build-coverage -G "NMake Makefiles" ^
  -DCMAKE_C_COMPILER=C:/LLVM/clang+llvm-18.1.8-x86_64-pc-windows-msvc/bin/clang.exe ^
  -DZEF_ENABLE_COVERAGE=ON -DBUILD_TESTING=ON
cmake --build build-coverage --target coverage
```

The target runs all CTest tests, merges the LLVM profiles, prints line,
function, region, and branch summaries, and writes the HTML report under
`build-coverage/coverage/html`.
