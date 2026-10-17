# Actors and hierarchical state machines

ZEF, short for **Z Event Framework**, is a small C runtime for building
event-driven applications on embedded systems and other resource-constrained
targets. It combines:

- actors with bounded mailboxes;
- hierarchical state machines for describing behavior;
- fixed-size message pools instead of an unbounded runtime heap;
- deterministic scheduling with explicit reactor priorities; and
- target-port hooks for critical sections, rescheduling, and idle processing.

The framework is designed for applications that need predictable memory use,
clear ownership rules, and behavior that can be understood as a sequence of
events and state transitions.

## The mental model

An application is a collection of actors. Each actor has:

1. a private context object containing application state;
2. a state machine that interprets messages;
3. a bounded mailbox containing pending messages; and
4. a reactor that lets the kernel schedule the actor.

In ZEF, `ZActor` is the convenient composition of those three runtime objects:
`ZMachine`, `ZMailbox`, and `ZReactor`. You can also assemble those objects
separately when an application needs a custom arrangement, such as an
additional deferred mailbox.

An actor does not expose methods that other actors call directly. Instead, an
actor receives a message, examines its topic and payload, updates its own
context, and optionally sends another message or changes state. This makes the
event boundary explicit and gives the scheduler a well-defined unit of work:
one message dispatched to one actor.

## Actor-model principles in ZEF

The actor model is a way to structure concurrent systems around isolated
objects that communicate by messages. ZEF applies a deliberately small,
embedded-oriented version of that model.

### Private state and message-based communication

An actor's mutable application state belongs in its context. Other actors
should communicate with it by posting messages or publishing messages to a
topic. A handler receives a borrowed `ZMsg const *`; it should inspect the
message and update its context, but it must not release the handler's borrowed
reference.

This is the important boundary:

```text
message -> mailbox -> reactor -> state-machine handler -> new state/messages
```

The boundary does not make every C data race impossible. An interrupt, DMA
callback, or other hardware integration code can still access shared memory.
Those interfaces must be protected by the target's synchronization rules. The
actor model provides a disciplined default for application-level interaction:
keep state local and communicate through messages.

### One message at a time

The kernel dispatches one message at a time to a reactor. A handler should do a
bounded amount of work and return an action. Long-running work should be split
into steps driven by later messages, or handed to hardware such as a DMA
controller. When the hardware completes, it can post a completion message back
to the actor.

### Bounded resources

Mailboxes have a configured maximum capacity. Dynamic messages come from
caller-provided fixed-size message pools. These choices make memory use
visible:

- choose a mailbox capacity for the burst you are willing to retain;
- provide a pool block large enough for each dynamic message type;
- treat a full mailbox or exhausted pool as a designed failure condition; and
- use `z_mailbox_min_free()` and the pool statistics to measure headroom.

ZEF does not require a general-purpose heap for ordinary message allocation.
The caller owns the storage used by pools and mailbox slot arrays.

### Ownership is explicit

Static messages are immutable and owned by the application. Dynamic messages
use ownership claims. The common rules are:

- `z_message_begin()` or `Z_MSG_BEGIN()` gives the caller a construction claim.
- `z_mailbox_post()` transfers a caller-owned dynamic message handle and clears
  the caller's pointer.
- `z_topic_publish()` transfers the message to the publication operation and
  distributes claims to successful subscribers.
- A handler borrows its dispatch message and must not release that borrow.
- `z_message_retain()` is needed when work will continue after the handler
  returns, for example while a DMA operation uses a payload.
- Every additional claim must eventually be released with
  `z_message_release()`.

The ownership rules are part of the design, not bookkeeping added afterward.
They let an application keep asynchronous payloads alive without copying them
unnecessarily.

### Mutable payload access

Use `Z_MSG_BEGIN()` or `Z_MSG_NEW()` when first allocating a typed dynamic
message. They return the mutable payload directly:

```c
ZMsg *message = NULL;
Packet *packet = Z_MSG_BEGIN(&pools, TOPIC_PACKET, Packet, &message);
packet->length = 0u;
```

`Z_MSG_PAYLOAD_MUT(message, Packet)` is for a narrower case: a producer-side
helper has a generic `ZMsg *` construction handle and needs to recover a typed
mutable payload while the caller still owns the message's construction claim.

```c
static void initialize_packet(ZMsg *message)
{
    Packet *packet = Z_MSG_PAYLOAD_MUT(message, Packet);
    packet->length = 0u;
}
```

This macro is not a state-handler facility. State handlers receive borrowed
`ZMsg const *` references and must use `Z_MSG_PAYLOAD()` for read-only access.
Do not use mutable access after posting or publishing a message, and do not use
it with static messages. Posting or publishing transfers the construction
claim and makes the payload part of the immutable message delivered to actors.

### Scheduling and priority

Each registered reactor has an explicit ID. That ID is also its strict
priority: a larger ID runs before a lower ID when both have queued work. This
gives the application a simple way to express urgency while keeping dispatch
deterministic.

The cooperative host loop usually looks like this:

```c
while (z_kernel_run_one(&kernel) == Z_WORK_DONE) {
    /* Dispatch one ready message per iteration. */
}
```

On a target, `z_kernel_run_forever()` can run the scheduler continuously. The
port supplies the critical-section and idle/rescheduling hooks needed by the
target integration.

## Hierarchical state machines

A state machine maps incoming messages to actions. A ZEF state machine is a
tree of `ZStateNode` objects:

- `parent` points to the containing state;
- `initial` names the child entered automatically when this state becomes
  active, or `NULL` when the state has no automatic child;
- `handle` processes messages;
- `enter` runs after entering the state; and
- `leave` runs before leaving the state.

The root state has `parent = NULL`. The machine's current state is always the
active leaf after the initial-state chain has been entered.

### How dispatch works

When a message arrives, ZEF starts at the current leaf and calls its handler.
The handler returns one of three actions:

- `z_action_handled()` consumes the message and keeps the current state;
- `z_action_unhandled()` passes the message to the parent state; and
- `z_action_transition(target)` changes to another state in the same
  hierarchy.

This gives child states first opportunity to handle specialized events while a
parent state can provide shared behavior. A child that does not understand an
event should return `z_action_unhandled()`, not duplicate the parent's logic.

For a transition, ZEF leaves states up to the least common ancestor of the old
and new states, then enters states down to the target. Entry and exit hooks run
in that order. After a state is entered, its `initial` child is followed
automatically, and this continues until a node with `initial = NULL` is
reached. `initial` therefore applies both when the machine starts and whenever
the corresponding superstate is entered later.

### State-machine design rules

Use these rules when designing a hierarchy:

1. Put behavior common to several child states on their nearest shared parent.
2. Return `z_action_unhandled()` for events that a child deliberately leaves to
   its parent.
3. Use `initial` to define the default substate of every composite state.
4. Set `initial = NULL` for a state that should remain active without entering a
   child.
5. Keep state definitions static or otherwise valid for the entire machine
   lifetime; transitions store pointers to them.
6. Use entry and exit hooks for resource ownership boundaries, such as starting
   and stopping a peripheral operation.
7. Keep handlers short. A handler should decide what happens next, not wait for
   an external operation to finish.

## Example 1: a flat counter state machine

The smallest useful machine has one root state and one handler. It handles two
topics and keeps its mutable data in a context structure.

```c
#include "zef.h"

enum {
    TOPIC_INCREMENT = 1u,
    TOPIC_RESET = 2u
};

typedef struct {
    unsigned count;
} CounterContext;

Z_STATE_HANDLER(counter_handle, CounterContext)
    switch (z_message_topic(message)) {
    case TOPIC_INCREMENT:
        ++ctx->count;
        return z_action_handled();

    case TOPIC_RESET:
        ctx->count = 0u;
        return z_action_handled();

    default:
        /* This is the root state, so there is no parent to try. */
        return z_action_handled();
    }
}

static ZStateNode const counter_root = {
    .parent = NULL,
    .initial = NULL,
    .handle = counter_handle,
    .enter = NULL,
    .leave = NULL
};
```

`Z_STATE_HANDLER(counter_handle, CounterContext)` defines the callback
directly with the framework's `void *context` signature and creates a typed
local `ctx` pointer inside its body:

```c
ZAction counter_handle(void *context, ZMsg const *message);
```

The macro opens the function body, so the statements follow the invocation and
the closing brace is written by the application. The cast is kept at the edge
of the framework so the handler body can work with `CounterContext *` directly.

## Example 2: parent and child states

Suppose a device can be disconnected, or connected and either idle or sending
data. The hierarchy is:

```text
root
├── disconnected
└── connected (initial: idle)
    ├── idle
    └── sending
```

The parent `connected` state handles events common to both `idle` and
`sending`. The child handlers return `z_action_unhandled()` for those events.

```c
#include "zef.h"

enum {
    TOPIC_CONNECT = 1u,
    TOPIC_DISCONNECT = 2u,
    TOPIC_SEND = 3u,
    TOPIC_SEND_DONE = 4u
};

typedef struct {
    unsigned sends_started;
} ConnectionContext;

static ZStateNode const root;
static ZStateNode const disconnected;
static ZStateNode const connected;
static ZStateNode const idle;
static ZStateNode const sending;

Z_STATE_HANDLER(disconnected_handle, ConnectionContext)
    switch (z_message_topic(message)) {
    case TOPIC_CONNECT:
        return z_action_transition(&connected);
    default:
        return z_action_handled();
    }
}

Z_STATE_HANDLER(connected_handle, ConnectionContext)
    switch (z_message_topic(message)) {
    case TOPIC_DISCONNECT:
        return z_action_transition(&disconnected);
    default:
        /* Let idle or sending handle their specialized events first. */
        return z_action_unhandled();
    }
}

Z_STATE_HANDLER(idle_handle, ConnectionContext)
    switch (z_message_topic(message)) {
    case TOPIC_SEND:
        ++ctx->sends_started;
        return z_action_transition(&sending);
    default:
        return z_action_unhandled();
    }
}

Z_STATE_HANDLER(sending_handle, ConnectionContext)
    switch (z_message_topic(message)) {
    case TOPIC_SEND_DONE:
        return z_action_transition(&idle);
    default:
        return z_action_unhandled();
    }
}

static ZStateNode const root = {
    .parent = NULL,
    .initial = &disconnected,
    .handle = NULL,
    .enter = NULL,
    .leave = NULL
};

static ZStateNode const disconnected = {
    .parent = &root,
    .initial = NULL,
    .handle = disconnected_handle,
    .enter = NULL,
    .leave = NULL
};

static ZStateNode const connected = {
    .parent = &root,
    .initial = &idle,
    .handle = connected_handle,
    .enter = NULL,
    .leave = NULL
};

static ZStateNode const idle = {
    .parent = &connected,
    .initial = NULL,
    .handle = idle_handle,
    .enter = NULL,
    .leave = NULL
};

static ZStateNode const sending = {
    .parent = &connected,
    .initial = NULL,
    .handle = sending_handle,
    .enter = NULL,
    .leave = NULL
};
```

At startup, `root.initial` enters `disconnected`. When `TOPIC_CONNECT` is
handled, the transition enters `connected`, then follows
`connected.initial` and enters `idle`. A later `TOPIC_DISCONNECT` can be
handled by `connected` even when the current leaf is `idle` or `sending`.

The `connected` handler is a policy boundary: it handles the common
disconnect event and delegates everything else to the specialized child. This
is usually clearer than adding the same disconnect case to every child.

## Example 3: entry and exit hooks

Hooks are useful when entering a state owns a resource or starts an operation.
For example, a `sending` state can start a DMA transfer on entry and stop or
clean it up on exit.

```c
typedef struct {
    unsigned dma_started;
    unsigned dma_stopped;
} TransferContext;

Z_STATE_HOOK(sending_enter, TransferContext)
    ++ctx->dma_started;
    hardware_start_dma();
}

Z_STATE_HOOK(sending_leave, TransferContext)
    ++ctx->dma_stopped;
    hardware_stop_dma();
}

static ZStateNode const sending = {
    .parent = &connected,
    .initial = NULL,
    .handle = sending_handle,
    .enter = sending_enter,
    .leave = sending_leave
};
```

The functions called by the example are application or board-support code;
ZEF does not define `hardware_start_dma()` or `hardware_stop_dma()`. The key
property is that a transition into `sending` starts the operation and every
transition out of `sending` runs the matching cleanup. If the operation is
asynchronous, the completion interrupt should post a `TOPIC_SEND_DONE` message
to the actor instead of calling the state handler directly.

## Example 4: wiring a state machine into an actor

The state definition becomes useful when it is connected to a mailbox and a
kernel. `ZActor` simplifies this setup.

```c
#include "zef.h"

typedef struct {
    ZActor actor;
    CounterContext context;
    ZMsg const *mailbox_slots[8];
} CounterActor;

void counter_actor_init(CounterActor *counter)
{
    z_actor_init(&counter->actor,
                 &counter_root,
                 &counter->context,
                 counter->mailbox_slots,
                 8u);
    counter->context.count = 0u;
}

void counter_actor_start(CounterActor *counter, ZKernel *kernel,
                         ZReactorId priority)
{
    (void)z_actor_register(kernel, &counter->actor, priority);
    z_actor_start(&counter->actor);
}
```

The surrounding application initializes the kernel first, then initializes and
registers the actor:

```c
ZPort const port = {
    .request_reschedule = NULL,
    .idle = NULL
};
ZKernel kernel;
CounterActor counter = { 0 };

z_kernel_init(&kernel, &port);
counter_actor_init(&counter);
counter_actor_start(&counter, &kernel, 2u);
```

The actor is now started and its root state has been entered. A static message
can be posted without allocating a pool block:

```c
Z_MSG_STATIC(increment_message, TOPIC_INCREMENT);

ZMsg const *message = &increment_message;
Z_MSG_POST(&counter.actor, message);
```

For a bounded cooperative loop, dispatch pending work with:

```c
while (z_kernel_run_one(&kernel) == Z_WORK_DONE) {
    /* Continue until all currently ready work has been dispatched. */
}
```

For dynamic payloads, initialize a message pool and use `Z_MSG_BEGIN` or
`Z_MSG_NEW`. The sender owns the construction handle until it posts or
publishes the message:

```c
typedef struct {
    uint8_t value;
} CounterCommand;

ZMessagePool pool;
ZMessagePoolSet pools;
_Alignas(ZEF_POOL_ALIGNMENT) unsigned char storage[4u * 32u];
ZMsg *command = NULL;

z_message_pool_init(&pool, storage, sizeof(storage), 32u, 4u);
z_message_pool_set_init(&pools, &pool, 1u);

CounterCommand *payload =
    Z_MSG_BEGIN(&pools, TOPIC_INCREMENT, CounterCommand, &command);
payload->value = 1u;
Z_MSG_POST(&counter.actor, command);
```

The block size must be large enough for the message header and payload, with
the alignment required by the configured pool. In production code, choose
pool and mailbox capacities from measured workload bounds rather than treating
them as arbitrary constants.

## Common state-machine patterns

### A parent handles a shared event

Put a shared event on the parent and return `z_action_unhandled()` from the
child for events it does not own. This is useful for `CANCEL`, `TIMEOUT`,
`DISCONNECT`, and fault events.

### A composite state chooses a default child

Set `parent.initial` to the default child. For example, a `connected` state can
enter `idle` automatically. Set it to `NULL` when the parent itself is the
intended active state and no child should be entered.

### An asynchronous operation is represented by a state

Enter a state to start an operation, handle intermediate messages while it is
active, and leave it to release resources. The interrupt or completion path
posts an event; it does not call `z_machine_dispatch()` from arbitrary context.

### A transition targets a sibling or an ancestor's child

Transitions can move between any nodes in the same hierarchy. The runtime
calculates which states to leave and enter. Keep the target pointer directed at
a static, valid `ZStateNode` definition.

### A handler consumes an event without changing state

Use `z_action_handled()` for acknowledgements, counters, diagnostics, and
other events that do not change the state configuration.

## Startup and lifecycle checklist

For a `ZActor`:

1. Define a root with `parent = NULL` and valid child parent pointers.
2. Define `initial` links for composite states that should automatically enter
   a child.
3. Initialize the actor with `z_actor_init()` and caller-owned mailbox slots.
4. Initialize the kernel with `z_kernel_init()`.
5. Register the actor with `z_actor_register()` using a unique priority ID.
6. Start it with `z_actor_start()`. This enters the initial-state chain.
7. Post or publish messages and run the scheduler.
8. Keep dynamic-message claims balanced across asynchronous operations.

The framework reports invalid lifecycle operations, ownership violations,
mailbox overflow, and state-machine errors through `z_panic()`. The application
must provide that callback. At panic time, application state is undefined; the
callback should preserve useful diagnostics, put the hardware into a safe
state, and recover in a controlled way, commonly by entering a diagnostic
path or resetting the target.

## Further reference

- [Public functions](group__zef__functions.html)
- [Configuration parameters](group__zef__macros.html)
- [State-machine types and fields](struct_z_state_node.html)
- [Actor API](struct_z_actor.html)
