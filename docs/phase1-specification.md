# ZEF Phase 1 specification

Status: baseline for review, version 0.2.0, 2026-10-10.

This document resolves the design choices needed before implementation. The
formal work-item IDs in the four layer documents are the traceability anchors;
this document explains the selected contract in readable form. The formal
behavior baseline is in [`what-requirements.sdoc`](what-requirements.sdoc), the
public surface is in [`api-requirements.sdoc`](api-requirements.sdoc), and
planned code links are in [`implementation-work.sdoc`](implementation-work.sdoc).

## Selected vocabulary

The public model is a `Reactor` containing a `Machine` and a bounded
`Mailbox`. A `Message` is immutable after sealing. A `Clock service` posts
timer Messages. An `Endpoint` is the scheduler-visible identity of a Reactor.
The `Kernel` owns runnable selection. A `Message pool` provides fixed-size
storage.

The names are part of the documentation and API review baseline, not a claim
of legal distinction by themselves. The implementation must remain structurally
independent from the prototype and from reference frameworks.

## Message contract

The public construction API is conceptually:

The declarations below abbreviate the type names used by the sketch. The
public headers provide complete definitions for these runtime structures; they
are not opaque handles. Framework-owned members are reserved by documentation,
and application code uses the public operations to access them.

```c
typedef struct ZMsgHandle ZMsgHandle;
typedef struct ZMsg ZMsg;
typedef struct ZMessagePoolSet ZMessagePoolSet;
typedef uint16_t ZTopic;

void *z_message_begin(ZMessagePoolSet *pool_set,
                      ZTopic topic,
                      size_t payload_size,
                      ZMsg *message);
void *z_message_payload_mut(ZMsg *message);
void *z_message_payload_mut_checked(ZMsg *message,
                                    size_t expected_size);
void z_message_retain(ZMsgHandle const *message);
void z_message_release(ZMsgHandle const *message);
void z_mailbox_post(ZMailbox *mailbox, ZMsg *message);   /* seal + transfer */
void z_mailbox_postpone(ZMailbox *mailbox,
                        ZMsgHandle const *message); /* retain + defer */
```

The typed construction macro is the normal entry point for application code.
It supplies `sizeof(Type)` to the allocator, so the complete fixed payload
size is known when the pool is selected:

```c
#define Z_MSG_BEGIN(pool_set_, topic_, Type_, msg_) \
    ((Type_ *)z_message_begin((pool_set_), (topic_), sizeof(Type_), (msg_)))

```

`z_message_begin` uses that size together with the envelope size to select the
smallest available pool block that can contain the complete Message. Equal-size
pools are considered in caller-supplied order. It returns the non-null mutable
payload pointer directly, so the caller does not need a separate untyped
payload lookup during construction.

The fixed-pool construction functions are bounded and ISR-safe in Phase 1;
they do not use a general-purpose allocator. This is a contract sketch, not an implementation prescription. The final
headers may choose names or types that better fit C portability, provided the
observable rules remain the same:

1. `begin` creates one creator claim and writable building storage.
2. `post`, `post_front`, and publication seal and consume the `ZMsg` before
   enqueueing or taking the subscriber snapshot.
3. A successful normal post transfers the creator or caller claim.
4. A successful `postpone` adds a mailbox claim and preserves the caller's
   claim.
5. A fatal enqueue does not return and must not assert a claim or alter the
   queued state before invoking the panic handler.
6. A dequeued dynamic mailbox claim belongs to the receiver for the duration of
   dispatch; retention is required beyond dispatch. A dequeued static Message
   is a reference with no claim.
7. Publication adds one claim per successful recipient and never copies the
   sealed storage.
8. A zero dynamic claim count returns the block to its pool.
9. Retain and release are no-ops for static Messages, so receivers may use the
   same cleanup path for static and dynamic Messages.

Operations whose failure indicates a violated framework assumption do not
return `ZResult`; they invoke the panic handler. `ZResult` is reserved for
the enumerated expected observations, including an empty mailbox or stopping an
already stopped timer.

Typed payload access for sealed Messages uses `Z_MSG_PAYLOAD(message, Type)`.
Initial construction uses `Z_MSG_BEGIN(pool_set, topic, Type, msg)`, which passes
`sizeof(Type)` to `z_message_begin` and returns a typed mutable payload pointer.
The allocator uses that size, including the message envelope, to select the
smallest available pool block. The macro and checked accessor compare the payload
length with the requested type size, invoke the panic handler on mismatch, and
never return a null pointer. Payloads that need more data require a separate
message type or an application-owned external buffer.

`Z_MSG_PAYLOAD_MUT(msg, Type)` remains available for checked mutable
re-access during construction, but it is not needed for the initial lookup:
`Z_MSG_BEGIN` already returns the typed payload pointer. The consuming post or
publication performs the seal automatically.

Application handlers receive a read-only `ZMsgHandle const *` or equivalent
read-only handle. Mutable payload access exists only through the `ZMsg` handle
before a consuming post or publication seals it. Debug builds diagnose dynamic
release-without-claim, dynamic claim overflow, and post-seal mutation. Retain
and release on static Messages do nothing.

### Application-defined payloads

`ZMsgHandle` is a complete public C structure. Its framework-owned members are
reserved by documentation and are accessed through the public Message
operations; it is not a base class for application payload structures.
Applications define custom payload types and store them in the Message payload.
The `ZTopic` value is the type discriminator for Phase 1, so each topic must
have one documented payload schema. Application dispatch selects the expected
type from the topic, and `Z_MSG_PAYLOAD(message, Type)` then checks that the
payload size equals `sizeof(Type)` before returning the typed pointer. Any
mismatch is fatal and never produces a null pointer.

For example, an IO request with an externally owned buffer can be represented
as:

```c
typedef struct {
    uintptr_t address;
    uint8_t  *buffer;
    size_t     length;
} IoRequest;

ZMsg msg;
IoRequest *request =
    Z_MSG_BEGIN(&message_pools, IO_REQUEST, IoRequest, &msg);
request->address = address;
request->buffer = buffer;
request->length = length;
z_mailbox_post(&mailbox, &msg);

void handle_io_request(ZMsgHandle const *message)
{
    IoRequest const *received = Z_MSG_PAYLOAD(message, IoRequest);
}
```

This replaces QP-style derived `QEvt` structures while preserving typed,
application-defined fields and keeping allocation, pool, and ownership state
private to ZEF.

## Machine contract

Each State node has a parent, a handler, an enter action, and a leave action.
A handler returns `UNHANDLED`, `HANDLED`, or `TRANSITION(target)`. Dispatch
walks toward the root until it gets a handled or transition result; if every
visited handler returns `UNHANDLED`, the current state is unchanged and no
entry or leave hook runs. A transition computes the common ancestor, leaves
the source path while excluding that ancestor, sets the target, enters the
target path, and follows initial children until stable. A self-transition
leaves and re-enters the current state before following its initial children.

The callback types are:

The declarations below abbreviate the type names used by the sketch. The
public headers provide the complete runtime structure definitions; the
framework-owned members remain reserved by documentation.

```c
typedef struct ZMachine ZMachine;
typedef struct ZStateNode ZStateNode;

typedef enum {
    Z_ACTION_UNHANDLED,
    Z_ACTION_HANDLED,
    Z_ACTION_TRANSITION
} ZActionKind;

typedef struct {
    ZActionKind kind;
    ZStateNode const *target;
} ZAction;

typedef ZAction (*ZStateHandler)(void *context,
                                 ZMsgHandle const *message);
typedef void (*ZStateHook)(void *context);

void z_machine_init(ZMachine *machine,
                    ZStateNode const *root,
                    void *context);
void z_machine_start(ZMachine *machine);
void z_machine_dispatch(ZMachine *machine,
                        ZMsgHandle const *message);
ZStateNode const *z_machine_state(ZMachine const *machine);
```

`ZStateHandler` is called while dispatching a Message and is responsible for
interpreting it before requesting a transition. `ZStateHook` is called when
its State node is entered or left; hooks receive only application context and
maintain State invariants like constructors and destructors. They do not
receive or depend on the Message that caused the transition.

The current node is the only machine-owned traversal state. Handlers do not
modify traversal state. A self-transition leaves and re-enters the current
node. Parent validation and transition depth are bounded by configuration.

Machine initialization and startup are separate operations. Initialization
binds the root descriptor and context and, when assertion checking is enabled,
validates that the root is non-null with no parent, every reachable non-root
node has a parent chain reaching the root, no parent or initial link cycles,
every initial child points back to its containing node, and the configured
depth is not exceeded. `z_machine_start` enters the root and follows its
initial-child chain. Start is explicit and may be called only once. In
assertion-disabled builds the application is responsible for providing this
valid graph. All Machine operations are public and synchronous. The application context
associated with a Reactor may contain several independent Machines, including
submachines. Application code creates and manages those Machines and may
dispatch the same read-only Message to each one in sequence; the framework
manages only the primary Machine named by the Reactor configuration:

```c
z_machine_start(&application->power_machine);
z_machine_start(&application->protocol_machine);

void reactor_handle(void *context, ZMsgHandle const *message)
{
    Application *application = context;
    z_machine_dispatch(&application->power_machine, message);
    z_machine_dispatch(&application->protocol_machine, message);
}
```

Dispatch before startup or repeated startup is a fatal lifecycle violation.

### Local deferred Mailboxes

Every `ZMailbox` is caller-owned and may either be configured as a Reactor's
scheduler-visible inbox or left unassigned as an application-owned local queue.
A local queue uses the same bounded FIFO, front-post, ownership, and removal
rules, but it does not set a Kernel runnable bit and is never drained by the
framework. Application code chooses when the current Machine state is ready to
process deferred work.

A handler that receives a sealed Message can retain it for later processing by
sharing it into the local queue:

```c
ZMailbox deferred;
ZMsgHandle const *deferred_slots[4];
z_mailbox_init(&deferred, deferred_slots, 4);

/* In a state that cannot process this Message yet. */
z_mailbox_postpone(&deferred, message);

/* In the application-selected state that can process deferred work. */
ZMsgHandle const *deferred_message;
if (z_mailbox_take(&deferred, &deferred_message) == Z_OK) {
    z_machine_dispatch(machine, deferred_message);
    z_message_release(deferred_message);
}
```

`z_mailbox_postpone` adds the local queue's claim for a dynamic Message while
the current dispatch keeps its own claim. For a static Message it enqueues the
same reference without changing claim state. In either case it retains the
existing sealed Message for later handling; the current owner and local queue
share the Message storage, and the payload is not copied. The same
`z_message_release` cleanup call is valid for either kind.
`z_mailbox_take` transfers a dynamic queue claim to the application, which
releases it after processing; taking a static entry transfers no claim. A local
queue is therefore a bounded deferral mechanism, not another scheduler
endpoint.

## Cooperative Kernel contract

The first Kernel is cooperative and run-to-completion. `run_one()` performs
zero work when no Reactor is runnable, otherwise it selects the highest-priority
runnable Reactor, breaking equal-priority ties by ascending zero-based Reactor
identity, dequeues one Message, dispatches it, and updates runnable state.
`run_forever()` repeatedly performs this finite step and calls the idle operation
when no work exists.

The runnable bitmap has one documented bit per scheduler identity. Internal
identities are zero-based. A zero bitmap has no selected Reactor; a nonzero
bitmap selects its highest-priority set bit. A Reactor is runnable exactly when
it is registered, running, and its Mailbox is nonempty. Phase 1 has no stop or
remove operation; a registered Reactor remains in the Kernel registry for the
Kernel lifetime.

## Port contract

The port supplies:

```c
typedef uint32_t ZCriticalState;

ZCriticalState z_port_critical_enter(void);
void z_port_critical_leave(ZCriticalState state);
void z_port_request_reschedule(void);
void z_port_idle(void);
```

`critical_enter` returns the prior interrupt state. `critical_leave` restores
that state, which makes nesting composable. Fatal conditions such as mailbox
overflow, pool exhaustion, invalid arguments, ownership violations, and
invalid state transitions invoke the application-supplied `z_panic` function
and do not return. The application provides this non-returning function; it is
not a Port callback. The handler must make the hardware safe and normally
stops the target in debug builds or resets it in release builds. The port
specification is an internal framework adapter. In Phase 1, fixed-pool Message
construction, Mailbox post, front-post, `postpone`, and topic publication are
supported from ISR context; all other application-callable APIs are thread-only.

## Phase boundary

Phase 1 is complete when this requirements graph, this contract, the host
example, and the traceability workflow are reviewed. Phase 2 may then
implement the host-only core. Cortex-M0 critical sections and PendSV remain
later-port requirements and are not silently implied by the host contract.

## Resolved contract decisions

1. **Panic routing:** all fatal framework conditions call the one
   application-provided, non-returning `z_panic` function. It is not stored in
   `ZPort` or duplicated in individual runtime objects.
2. **Interrupt classification:** Phase 1 supports ISR calls for fixed-pool
   Message construction, Mailbox post, front-post, `postpone`, and topic
   publication. All other application-callable APIs are thread-only.

The runtime structures are intentionally complete public C structures. Their
framework-owned members are private by documentation, not hidden behind opaque
handles or an allocation-indirection layer. An application may embed
`ZReactor` as the first member of an extension structure; the framework manages
the embedded base Reactor, while the application manages extension state and
all auxiliary Machines or submachines.

## Decision log

| Decision | Choice | Reason |
| --- | --- | --- |
| Ownership model | Explicit claims with transfer and retain operations | Makes queue, broadcast, removal, and reclamation behavior observable |
| Message mutability | `ZMsg`-only before consuming post; read-only after seal | Allows sharing without copying and prevents consumer races |
| Scheduler identity | Zero-based internal endpoint index | Avoids mixed 0-based/1-based arithmetic |
| Cooperative scheduling | Highest-priority runnable endpoint, one message per finite step | Gives deterministic host tests and a small kernel |
| Timer structure | Delta-list contract for the initial design | Bounded, simple, and appropriate for a small timer set |
| Local deferred work | Reuse caller-owned `ZMailbox` instances without attaching them to a Reactor | Provides bounded deferral without another queue abstraction or scheduler identity |
| Error policy | Panic on resource exhaustion and invariant violations; status only for expected state observations | Matches fail-stop bare-metal systems and keeps error paths explicit |
| Panic boundary | One application-provided non-returning `z_panic` function, separate from `ZPort` | Keeps application policy out of the framework port and avoids per-object callback storage |
| ISR API | Fixed-pool Message construction plus Mailbox post/postpone/front-post and topic publication are ISR-safe in Phase 1 | Allows an ISR to construct and deliver a bounded fixed-size Message |
| License | Apache License 2.0 for new artifacts, pending provenance review | Establishes distribution intent before implementation |
