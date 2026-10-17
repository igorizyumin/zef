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
`Mailbox`. A `Message` is mutable only during construction; framework-delivered
messages are exposed as const borrowed references. A `Clock service` posts
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
typedef struct ZMsg ZMsg;
typedef struct ZMsg ZMsg;
typedef struct ZMessagePoolSet ZMessagePoolSet;
typedef uint16_t ZTopic;

void *z_message_begin(ZMessagePoolSet *pool_set,
                      ZTopic topic,
                      size_t payload_size,
                       ZMsg **message);
void z_message_factory_init(ZMessagePoolSet *pool_set);
void *z_message_begin_default(ZTopic topic,
                              size_t payload_size,
                              ZMsg **message);
void *z_message_payload_mut(ZMsg *message);
void *z_message_payload_mut_checked(ZMsg *message,
                                    size_t expected_size);
void z_message_retain(ZMsg const *message);
void z_message_release(ZMsg const *message);
void z_mailbox_post(ZMailbox *mailbox, ZMsg **message);  /* transfer + null */
void z_mailbox_postpone(ZMailbox *mailbox,
                        ZMsg const *message);             /* retain + defer */
ZResult z_mailbox_recall(ZMailbox *destination,
                         ZMailbox *source);              /* newest + front */
```

The typed construction macro is the normal entry point for application code.
It supplies `sizeof(Type)` to the allocator, so the complete fixed payload
size is known when the pool is selected:

```c
#define Z_MSG_BEGIN(pool_set_, topic_, Type_, msg_) \
    ((Type_ *)z_message_begin((pool_set_), (topic_), sizeof(Type_), (msg_)))
#define Z_MSG_NEW(topic_, Type_, msg_) \
    ((Type_ *)z_message_begin_default((topic_), sizeof(Type_), (msg_)))

```

`z_message_begin` uses that size together with the envelope size to select the
smallest available pool block that can contain the complete Message. Equal-size
pools are considered in caller-supplied order. It returns the non-null mutable
payload pointer directly, so the caller does not need a separate untyped
payload lookup during construction.

For applications with one process-wide pool set, `z_message_factory_init` can
configure the default message factory once. `Z_MSG_NEW(topic, Type, msg)` then
provides the same typed construction behavior without carrying a pool-set
pointer through every producer. The explicit `Z_MSG_BEGIN` form remains
available when a caller needs to select a particular pool set, including tests
and applications with multiple independent sets. Using `Z_MSG_NEW` before the
factory is initialized is a lifecycle panic.

The fixed-pool construction functions are bounded and ISR-safe in Phase 1;
they do not use a general-purpose allocator. This is a contract sketch, not an implementation prescription. The final
headers may choose names or types that better fit C portability, provided the
observable rules remain the same:

1. `begin` creates one creator claim and writable building storage.
2. `post`, `post_front`, and publication consume the caller's `ZMsg *` and
   leave it null before enqueueing or taking the subscriber snapshot.
3. A successful normal post transfers a dynamic creator reference to the
   framework; static Messages have no claim to transfer but are accepted and
   the caller's construction pointer is still consumed.
4. A successful `postpone` adds a mailbox claim and preserves the caller's
   claim.
5. A fatal enqueue does not return and must not assert a claim or alter the
   queued state before invoking the panic handler.
6. A dequeued dynamic Message is borrowed by the handler for the duration of
   dispatch; retention is required beyond dispatch. A dequeued static Message
   is a reference with no claim.
7. Publication adds one reference per successful recipient and never copies the
   message storage.
8. A zero dynamic claim count returns the block to its pool.
9. Retain and release are no-ops for static Messages, so receivers may use the
   same cleanup path for static and dynamic Messages.

Operations whose failure indicates a violated framework assumption do not
return `ZResult`; they invoke the panic handler. `ZResult` is reserved for
the enumerated expected observations, including an empty mailbox or stopping an
already stopped timer.

Typed payload access for delivered Messages uses `Z_MSG_PAYLOAD(message, Type)`.
Initial construction uses `Z_MSG_BEGIN(pool_set, topic, Type, msg)` or, after
default factory initialization, `Z_MSG_NEW(topic, Type, msg)`. Both pass
`sizeof(Type)` to the message allocator and return a typed mutable payload pointer.
The allocator uses that size, including the message envelope, to select the
smallest available pool block. The macro and checked accessor compare the payload
length with the requested type size, invoke the panic handler on mismatch, and
never return a null pointer. Payloads that need more data require a separate
message type or an application-owned external buffer.

For immutable notifications with no payload, `Z_MSG_STATIC(name, topic)`
declares a function-local or file-scope static constant `ZMsg` ready for
posting or postponement. It requires no pool and has no ownership claim.

`Z_MSG_PAYLOAD_MUT(msg, Type)` remains available for checked mutable
re-access during construction, but it is not needed for the initial lookup:
`Z_MSG_BEGIN` already returns the typed payload pointer. The consuming post or
 publication consumes and nulls the construction pointer automatically.

Application handlers receive a read-only `ZMsg const *` borrowed reference.
Mutable payload access exists through the `ZMsg *` construction pointer before
a consuming post or publication. The sender pointer is null after transfer.
Debug builds diagnose dynamic release-without-reference and reference-count
overflow. Retain and release on static Messages do nothing.

### Application-defined payloads

`ZMsg` is a complete public C structure. Its framework-owned members are
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

void handle_io_request(ZMsg const *message)
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
                                 ZMsg const *message);
typedef void (*ZStateHook)(void *context);

void z_machine_init(ZMachine *machine,
                    ZStateNode const *root,
                    void *context);
void z_machine_start(ZMachine *machine);
void z_machine_dispatch(ZMachine *machine,
                        ZMsg const *message);
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

void reactor_handle(void *context, ZMsg const *message)
{
    Application *application = context;
    z_machine_dispatch(&application->power_machine, message);
    z_machine_dispatch(&application->protocol_machine, message);
}
```

Dispatch before startup or repeated startup is a fatal lifecycle violation.

### Actor convenience composition

`ZActor` is the convenience form for the common one-machine, one-mailbox,
one-reactor arrangement. It owns the three embedded framework objects but uses
caller-owned state descriptors, application context, and mailbox slot storage:

```c
void z_actor_init(ZActor *actor,
                  ZStateNode const *root,
                  void *context,
                  ZMsg const **mailbox_slots,
                  uint16_t mailbox_capacity);
ZReactorId z_actor_register(ZKernel *kernel, ZActor *actor, ZReactorId id);
void z_actor_start(ZKernel *kernel, ZActor *actor);
#define Z_MSG_POST(actor_, msg_) \
    do { \
        ZMsg *zef_post_message_ = (ZMsg *)(msg_); \
        z_mailbox_post(&(actor_)->mailbox, &zef_post_message_); \
        (msg_) = zef_post_message_; \
    } while (0)
```

Actor registration and startup are convenience forwards to the embedded
Reactor. The embedded Mailbox remains the normal message-posting interface,
and an Actor has the same identity, priority, ownership, and lifecycle rules
as a manually composed Reactor. `Z_MSG_POST(actor, msg)` is the convenience
form for posting to an Actor and transfers-and-nulls a variable pointing to
either a dynamic or static Message; static Messages carry no claim. Direct
Mailbox posting remains available for standalone or non-Actor destinations.
`Z_MSG_POST` is ISR-safe because it delegates to the bounded,
critical-section-protected `z_mailbox_post` operation; there is no separate
Actor-specific ISR posting API.

### Local deferred Mailboxes

Every `ZMailbox` is caller-owned and may either be configured as a Reactor's
scheduler-visible inbox or left unassigned as an application-owned local queue.
A local queue uses the same bounded FIFO, front-post, ownership, and removal
rules, but it does not set a Kernel runnable bit and is never drained by the
framework. Application code chooses when the current Machine state is ready to
process deferred work.

A handler that receives a borrowed Message can retain it for later processing by
sharing it into the local queue:

```c
ZMailbox deferred;
ZMsg const *deferred_slots[4];
z_mailbox_init(&deferred, deferred_slots, 4);

/* In a state that cannot process this Message yet. */
z_mailbox_postpone(&deferred, message);

/* In the application-selected state that can process deferred work. */
ZMsg const *deferred_message;
if (z_mailbox_take(&deferred, &deferred_message) == Z_OK) {
    z_machine_dispatch(machine, deferred_message);
    z_message_release(deferred_message);
}
```

`z_mailbox_postpone` adds the local queue's reference for a dynamic Message while
the current dispatch keeps its own claim. For a static Message it enqueues the
same reference without changing claim state. In either case it retains the
existing Message for later handling; the current framework reference and local
queue share the Message storage, and the payload is not copied. The same
`z_message_release` cleanup call is valid for either kind.
`z_mailbox_take` transfers a dynamic queue claim to the application, which
releases it after processing; taking a static entry transfers no claim. A local
queue is therefore a bounded deferral mechanism, not another scheduler
endpoint. `z_mailbox_recall` is the complementary one-entry operation for
state entry actions: it transfers the newest entry from an unassigned deferred
mailbox to the front of the actor mailbox, preserving the deferred mailbox's
LIFO order.

## Cooperative Kernel contract

The first Kernel is cooperative and run-to-completion. `run_one()` performs
zero work when no Reactor is runnable, otherwise it selects the highest-index
runnable Reactor, dequeues one Message, dispatches it, and updates runnable
state. Reactor identity is also strict priority, so larger identities run
first.
`run_forever()` repeatedly performs this finite step and calls the idle operation
when no work exists.

The runnable bitmap has one documented bit per scheduler identity. Internal
identities are zero-based and may have gaps. A zero bitmap has no selected
Reactor; a nonzero bitmap selects its highest set bit. A Reactor is runnable
exactly when it is registered, running, and its Mailbox is nonempty. Phase 1
has no stop or remove operation; a registered Reactor remains in the Kernel
registry for the Kernel lifetime.

## Port contract

The port supplies:

```c
typedef uint32_t ZCriticalState;

ZCriticalState z_port_critical_enter(void);
void z_port_critical_leave(ZCriticalState state);

struct ZPort {
    void (*request_reschedule)(void);
    void (*idle)(void);
};
```

`critical_enter` returns the prior interrupt state. `critical_leave` restores
that state, which makes nesting composable. Critical entry and leave are global
processor-port operations; they are not looked up through a ``ZKernel`` or a
``ZMailbox``. The `ZPort` callback table supplied to `z_kernel_init` contains
only Kernel-owned scheduling hooks for reschedule and idle behavior.
The framework uses the global critical operations around scheduler, mailbox,
message-claim, and fixed-pool free-list mutations, including for unregistered
Mailboxes. Fatal conditions such as mailbox
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
example, and the traceability workflow are reviewed. Phase 2 implements and
accepts the generic cooperative core and host Port. Cortex-M0/M0+ critical
sections, PendSV, and target startup remain explicitly deferred to Phase 3;
the selected Phase 3 model is shared-stack activation on MSP, not one process
stack per Reactor, and the host contract does not claim target support.

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
| Message mutability | `ZMsg *` during construction; `ZMsg const *` after transfer | Allows sharing without copying while making handler access borrowed |
| Scheduler identity | Explicit zero-based Reactor index that is also strict priority | Makes scheduler and publication order a single descending bitmap scan; permits temporary identity gaps |
| Cooperative scheduling | Highest-priority runnable endpoint, one message per finite step | Gives deterministic host tests and a small kernel |
| Timer structure | Delta-list contract for the initial design | Bounded, simple, and appropriate for a small timer set |
| Local deferred work | Reuse caller-owned `ZMailbox` instances without attaching them to a Reactor | Provides bounded deferral without another queue abstraction or scheduler identity |
| Error policy | Panic on resource exhaustion and invariant violations; status only for expected state observations | Matches fail-stop bare-metal systems and keeps error paths explicit |
| Panic boundary | One application-provided non-returning `z_panic` function, separate from `ZPort` | Keeps application policy out of the framework port and avoids per-object callback storage |
| ISR API | Fixed-pool Message construction plus Mailbox post/postpone/front-post and topic publication are ISR-safe in Phase 1 | Allows an ISR to construct and deliver a bounded fixed-size Message |
| License | Apache License 2.0 for new artifacts, pending provenance review | Establishes distribution intent before implementation |
