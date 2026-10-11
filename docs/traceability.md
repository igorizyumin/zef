# ZEF traceability and audit

## Recursive structure

The requirements graph now follows one explicit chain:

```text
WHY: problems and user needs
  ↓
WHAT: formal system behavior and boundary
  ↓
INTERFACE: public surface, inputs, limits, and assumptions
  ↓
IMPLEMENTATION: selected solution and code link
```

`requirements.sdoc` is the single StrictDoc root. `what-requirements.sdoc`,
`api-requirements.sdoc`, and `implementation-work.sdoc` are non-root documents
linked with `Parent` relations. `File` relations point to readable contract
documents. IMPLEMENTATION comments carry planned code links until those source
files exist; they become `File` relations when the code is added.

## Migration of the previous baseline

The former `requirements.sdoc` and `api-requirements.sdoc` mixed levels. The
existing records were retained by intent and redistributed as follows:

| Previous records | New location | Treatment |
| --- | --- | --- |
| `ZEF-REQ-001..004` | `ZEF-WHY-001..003`, `ZEF-WHAT-001`, `ZEF-WHAT-004..005` | Product need, boundary, independence, and license split apart |
| `ZEF-REQ-010..012` | `ZEF-WHY-010`, `ZEF-WHAT-003`, `ZEF-IF-004` | Vocabulary moved to intent; limits moved to contract and surface |
| `ZEF-REQ-020..033` | `ZEF-WHY-011`, `ZEF-WHAT-006`, `ZEF-WHAT-010..013`, `ZEF-IF-002`, `ZEF-IF-010..012` | Envelope, lifecycle, ownership, typed payload, panic, and pointer behavior split |
| `ZEF-REQ-040..046` | `ZEF-WHY-012`, `ZEF-WHAT-020..023`, `ZEF-IF-020..021` | State semantics separated from descriptor and callback surface |
| `ZEF-REQ-050..054` | `ZEF-WHY-013`, `ZEF-WHAT-030..032`, `ZEF-IF-013`, `ZEF-IF-022` | Mailbox behavior and permanent Reactor registration split |
| `ZEF-REQ-060..064` | `ZEF-WHY-020`, `ZEF-WHAT-040..041`, `ZEF-IF-030` | Scheduling contract separated from bitmap and API surface |
| `ZEF-REQ-070..073` | `ZEF-WHY-021..022`, `ZEF-WHAT-050`, `ZEF-WHAT-060..061`, `ZEF-IF-031..032` | Publication and clock behavior split from their interfaces |
| `ZEF-REQ-080..082` | `ZEF-WHY-023..024`, `ZEF-WHAT-070..071`, `ZEF-IF-040..041` | Port contract and optional preemption separated |
| `ZEF-REQ-090..093` | `ZEF-WHY-030..031`, `ZEF-WHAT-080..082`, `ZEF-IF-050` | Verification, matrix, traceability, and release needs split |
| `ZEF-API-001-*` | `ZEF-IF-001..004`, `ZEF-WHAT-002`, `ZEF-WHAT-006` | Common API conventions moved to INTERFACE; observable failure rules remain WHAT |
| `ZEF-API-020-*` | `ZEF-IF-010..013`, `ZEF-WHAT-010..013` | Message and pool signatures moved to INTERFACE; behavior remains WHAT |
| `ZEF-API-040-*` | `ZEF-IF-020..021`, `ZEF-WHAT-020..023` | Machine surface separated from state semantics |
| `ZEF-API-050-*` | `ZEF-IF-022`, `ZEF-WHAT-030..032` | Reactor API separated from permanent-registration behavior |
| `ZEF-API-051..052-*` | `ZEF-IF-013`, `ZEF-WHAT-031..032` | Mailbox signatures separated from queue and cleanup behavior |
| `ZEF-API-060..062-*` | `ZEF-IF-030`, `ZEF-WHAT-040..041` | Kernel API separated from scheduling behavior |
| `ZEF-API-070..071-*` | `ZEF-IF-031`, `ZEF-WHAT-050` | Topic surface separated from publication behavior |
| `ZEF-API-072..073-*` | `ZEF-IF-032`, `ZEF-WHAT-060..061` | Clock surface separated from timer behavior |
| `ZEF-API-080..081-*` | `ZEF-IF-040..041`, `ZEF-WHAT-070` | Port surface separated from concurrency contract |
| `ZEF-API-090..093-*` | `ZEF-IF-050`, `ZEF-WHAT-080..082`, `ZEF-IMP-080..081` | Consumer verification and completeness moved to downstream work |

This table is the compatibility map for the previous IDs. New work uses the
layer prefix and does not reuse the old mixed-level identifiers.

## Audit result

The graph was reviewed after the split:

- 14 WHY items have downstream WHAT coverage.
- 28 WHAT items have at least one INTERFACE child.
- 18 INTERFACE items have at least one IMPLEMENTATION child.
- Every non-root item has a `Parent` relation.
- Every IMPLEMENTATION item has an explicit planned code link in its comment;
  those links are promoted to `File` relations when implementation files exist.
- INTERFACE statements may repeat caller-visible preconditions and exposure
  rules; WHAT remains the formal behavioral contract.
- No WHY statement defines an API, algorithm, data structure, or system
  boundary.
- Optional Cortex-M0 preemption remains explicitly downstream and does not
  silently expand the host-only Phase 1 boundary.

## Ongoing rules

1. Add or revise WHY before introducing a new intent.
2. Add or revise WHAT before changing observable behavior or the system boundary.
3. Add INTERFACE when a caller-visible type, operation, input, limit, or
   assumption changes.
4. Add IMPLEMENTATION when a concrete design decision or code path is selected.
5. Put `@relation(ZEF-..., scope=...)` markers in implementation and test files.
6. Run StrictDoc export and review orphan, missing-parent, and missing-file
   reports before each phase exit.
