# Phase 1 host example

This example is intentionally descriptive. It is the smallest scenario that
can be explained without prototype terminology or implementation fields.

## Scenario

Two Reactors are registered with the cooperative Kernel at explicit strict
priority identities:

- `button_reactor` has priority 1 and subscribes to topic `BUTTON_PRESSED`.
- `lamp_reactor` has priority 2 and subscribes to topic `LAMP_COMMAND`.

The host test creates a dynamic Message with topic `BUTTON_PRESSED`, allocates
space for a typed `ButtonPressed` payload, initializes that payload through a
`ZMsg` pointer, and publishes it. Publication transfers the message and snapshots
subscribers and enqueues the same immutable Message into each successful
Mailbox. Each Mailbox claim is released after dispatch. A handler can validate
the topic and payload size before reading the typed payload. The button Reactor
then creates a second building Message with topic `LAMP_COMMAND` and transfers
its reference to the lamp Reactor with a normal post, which nulls the sender
pointer automatically.

## Contract-shaped pseudocode

```c
ZKernel kernel;
ZMessagePool message_pool;
ZMessagePoolSet message_pools;
ZTopicTable topics;
ZReactor button_reactor;
ZReactor lamp_reactor;
ZReactorId button_id;
ZReactorId lamp_id;
    ZMsg *button_message = NULL;
uint16_t delivered;

typedef struct {
    uint8_t button;
} ButtonPressed;

z_kernel_init(&kernel, &host_port);
z_message_pool_init(&message_pool, message_storage, sizeof(message_storage),
                    MESSAGE_BLOCK_SIZE, MESSAGE_BLOCK_COUNT);
z_message_pool_set_init(&message_pools, &message_pool, 1);
z_topic_table_init(&topics, &kernel);
button_id = z_reactor_register(&kernel, &button_reactor, 1);
lamp_id = z_reactor_register(&kernel, &lamp_reactor, 2);
z_topic_subscribe(&topics, BUTTON_PRESSED, button_id);

ButtonPressed *button =
    Z_MSG_BEGIN(&message_pools, BUTTON_PRESSED, ButtonPressed,
                &button_message);
button->button = 3;
delivered = z_topic_publish(&kernel, &topics, &button_message);

while (z_kernel_run_one(&kernel) == Z_WORK_DONE) {
    /* A finite host step dispatches one Message. */
}
```

The example demonstrates that a caller can reason about construction, typed
payload access, automatic sender-pointer consumption at publication, Mailbox delivery, Reactor
dispatch, and cooperative scheduling using only the Phase 1 vocabulary. It
does not define storage declarations or final function names.
