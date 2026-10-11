# ZEF requirements and design

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

The `.sdoc` files are the authoritative work-item records. Generated StrictDoc
output belongs in the ignored output directory. Implementation and test source
files should use `@relation(ZEF-..., scope=...)` markers as code is added.
