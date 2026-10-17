#include "zef.h"

#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(expression_) \
    do { \
        if (!(expression_)) { \
            fprintf(stderr, "check failed: %s (%s:%d)\n", #expression_, __FILE__, __LINE__); \
            abort(); \
        } \
    } while (0)

static jmp_buf g_panic_env;
static ZPanicInfo g_last_panic;
static jmp_buf g_idle_env;
static ZBool g_jump_from_idle;

_Noreturn void z_panic(ZPanicInfo const *info)
{
    if (info != NULL) {
        g_last_panic = *info;
    }
    longjmp(g_panic_env, 1);
    abort();
}

#define EXPECT_PANIC(expression_, reason_) \
    do { \
        if (setjmp(g_panic_env) == 0) { \
            (expression_); \
            fprintf(stderr, "expected panic: %s (%s:%d)\n", #expression_, __FILE__, __LINE__); \
            abort(); \
        } else { \
            CHECK(g_last_panic.reason == (reason_)); \
        } \
    } while (0)

typedef struct {
    unsigned masked;
    unsigned enters;
    unsigned leaves;
    unsigned reschedules;
} HostPortState;

static HostPortState *g_port_state;

ZCriticalState z_port_critical_enter(void)
{
    ZCriticalState previous = g_port_state != NULL ? g_port_state->masked : 0u;
    if (g_port_state != NULL) {
        g_port_state->masked = 1u;
        ++g_port_state->enters;
    }
    return previous;
}

void z_port_critical_leave(ZCriticalState state)
{
    if (g_port_state != NULL) {
        g_port_state->masked = state;
        ++g_port_state->leaves;
    }
}

static void test_request_reschedule(void)
{
    if (g_port_state != NULL) {
        ++g_port_state->reschedules;
    }
}

static void test_idle(void)
{
    if (g_jump_from_idle) {
        longjmp(g_idle_env, 1);
    }
}

static ZPort test_port(void)
{
    ZPort port = {
        test_request_reschedule,
        test_idle
    };
    return port;
}

typedef struct {
    unsigned handled;
    unsigned topic_counts[8];
    unsigned priorities[8];
    unsigned priority_count;
    ZKernel *kernel;
} HandlerContext;

typedef struct {
    ZMailbox *destination;
    ZMsg *message;
    ZBool posted;
} PostProbe;

static void post_static_message(ZMailbox *mailbox, ZMsg *message);

static ZAction count_handler(void *context, ZMsg const *message)
{
    HandlerContext *handler = context;
    ZTopic topic = z_message_topic(message);
    if (g_port_state != NULL) {
        /* User code must run after the scheduler's critical section ends. */
        CHECK(g_port_state->masked == 0u);
    }
    ++handler->handled;
    if (topic < 8u) {
        ++handler->topic_counts[topic];
    }
    if (handler->kernel != NULL && handler->priority_count < 8u) {
        ZPriority priority = 0u;
        CHECK(z_kernel_current_priority(handler->kernel, &priority) == Z_OK);
        handler->priorities[handler->priority_count++] = priority;
    }
    return (ZAction){ Z_ACTION_HANDLED, NULL };
}

static ZAction post_probe_handler(void *context, ZMsg const *message)
{
    PostProbe *probe = context;
    (void)message;
    if (!probe->posted) {
        post_static_message(probe->destination, probe->message);
        probe->posted = 1u;
    }
    return (ZAction){ Z_ACTION_HANDLED, NULL };
}

static void post_static_message(ZMailbox *mailbox, ZMsg *message)
{
    ZMsg *pending = message;
    z_mailbox_post(mailbox, &pending);
    CHECK(pending == NULL);
}

static ZStateNode make_root(HandlerContext *context)
{
    ZStateNode root = { NULL, NULL, count_handler, NULL, NULL };
    (void)context;
    return root;
}

/* @relation(ZEF-TEST-001, scope=public-consumer-configuration) */
static void test_public_surface(void)
{
    _Static_assert(Z_MAX_REACTORS >= 8u, "reactor limit is public");
    _Static_assert(Z_MAX_TOPICS > 0u, "topic limit is public");
    _Static_assert(sizeof(ZEF_TICK_TYPE) == 4u, "default tick type is public");

    ZefMaxAlign alignment = { 0 };
    ZMessagePool pool = { 0 };
    ZMessagePoolSet pool_set = { 0 };
    ZMsg handle = { 0 };
    ZMsg message = { 0 };
    ZMailbox mailbox = { 0 };
    ZStateNode state = { 0 };
    ZMachine machine = { 0 };
    ZReactor reactor = { 0 };
    ZActor actor = { 0 };
    ZKernel kernel = { 0 };
    ZTopicTable topics = { 0 };
    ZClock clock = { 0 };
    ZTimer timer = { 0 };
    ZPort port = test_port();
    ZReactorConfig reactor_config = { 0 };
    ZAction action = { Z_ACTION_UNHANDLED, NULL };

    (void)alignment;
    (void)pool;
    (void)pool_set;
    (void)handle;
    (void)message;
    (void)mailbox;
    (void)state;
    (void)machine;
    (void)reactor;
    (void)actor;
    (void)kernel;
    (void)topics;
    (void)clock;
    (void)timer;
    (void)port;
    (void)reactor_config;
    (void)action;
    CHECK(Z_OK == 0);
    CHECK(Z_EMPTY != Z_OK);
    CHECK(Z_ACTION_HANDLED != Z_ACTION_UNHANDLED);
}

/* @relation(ZEF-TEST-035, scope=actor-composition) */
static void test_actor_composition(void)
{
    HostPortState port_state = { 0u, 0u, 0u, 0u };
    ZPort port;
    ZKernel kernel;
    ZActor actor;
    ZStateNode root;
    ZMsg const *slots[2];
    HandlerContext context = { 0 };
    ZMsg message_storage;
    ZMsg const *message = NULL;

    g_port_state = &port_state;
    port = test_port();
    z_kernel_init(&kernel, &port);
    root = make_root(&context);
    context.kernel = &kernel;
    z_actor_init(&actor, &root, &context, slots, 2u);
    CHECK(actor.reactor.machine == &actor.machine);
    CHECK(actor.reactor.mailbox == &actor.mailbox);
    CHECK(actor.mailbox.owner == NULL);
    CHECK(actor.reactor.context == &context);
    CHECK(z_actor_register(&kernel, &actor, 2u) == 2u);
    CHECK(actor.mailbox.owner == &actor.reactor);
    z_actor_start(&kernel, &actor);

    z_message_static_init(&message_storage, 4u, NULL, 0u);
    EXPECT_PANIC(z_mailbox_postpone(&actor.mailbox, &message_storage),
                 Z_PANIC_INVALID_ARGUMENT);
    message = &message_storage;
    Z_MSG_POST(&actor, message);
    CHECK(message == NULL);
    CHECK(z_kernel_run_one(&kernel) == Z_WORK_DONE);
    CHECK(context.handled == 1u);
    CHECK(z_machine_state(&actor.machine) == &root);
    g_port_state = NULL;
}

/* @relation(ZEF-TEST-002, scope=fixed-pool-message-access) */
static void test_pool_and_payload(void)
{
    _Alignas(ZEF_POOL_ALIGNMENT) unsigned char small_storage[64u * 2u];
    _Alignas(ZEF_POOL_ALIGNMENT) unsigned char large_storage[128u];
    ZMessagePool pools[2];
    ZMessagePoolSet set;
    ZMsg const *slots[2];
    ZMailbox mailbox;
    ZMsg *message = NULL;
    ZMsg *large_message = NULL;
    uint32_t *payload;
    unsigned char *large_payload;
    ZMsg const *taken = NULL;

    z_message_pool_init(&pools[0], small_storage, sizeof(small_storage), 64u, 2u);
    z_message_pool_init(&pools[1], large_storage, sizeof(large_storage), 128u, 1u);
    z_message_pool_set_init(&set, pools, 2u);
    z_message_factory_init(&set);
    z_mailbox_init(&mailbox, slots, 2u);

    payload = Z_MSG_BEGIN(&set, 5u, uint32_t, &message);
    *payload = 0x12345678u;
    CHECK(z_message_payload_mut(message) == payload);
    CHECK(Z_MSG_PAYLOAD_MUT(message, uint32_t) == payload);
    CHECK(pools[0].free_count == 1u);
    z_mailbox_post(&mailbox, &message);
    CHECK(message == NULL);
    CHECK(z_message_topic(mailbox.slots[0]) == 5u);
    CHECK(z_message_payload_size(mailbox.slots[0]) == sizeof(uint32_t));
    CHECK(z_message_payload(mailbox.slots[0]) == payload);
    CHECK(*Z_MSG_PAYLOAD(mailbox.slots[0], uint32_t) == 0x12345678u);
    CHECK(z_mailbox_take(&mailbox, &taken) == Z_OK);
    CHECK(*Z_MSG_PAYLOAD(taken, uint32_t) == 0x12345678u);
    z_message_release(taken);
    CHECK(pools[0].free_count == pools[0].block_count);

    large_payload = z_message_begin(&set, 6u, 70u, &large_message);
    CHECK(large_message->pool == &pools[1]);
    memset(large_payload, 0xA5, 70u);
    z_mailbox_post(&mailbox, &large_message);
    CHECK(z_mailbox_take(&mailbox, &taken) == Z_OK);
    CHECK(z_message_payload_size(taken) == 70u);
    z_message_release(taken);
    CHECK(pools[1].free_count == pools[1].block_count);

    message = NULL;
    payload = Z_MSG_NEW(7u, uint32_t, &message);
    *payload = 0xCAFEBABEu;
    CHECK(z_message_topic(message) == 7u);
    CHECK(*Z_MSG_PAYLOAD(message, uint32_t) == 0xCAFEBABEu);
    z_message_release(message);
    CHECK(pools[0].free_count == pools[0].block_count);
}

/* @relation(ZEF-TEST-004, scope=panic-boundary-and-preserved-state) */
static void test_failure_paths(void)
{
    _Alignas(ZEF_POOL_ALIGNMENT) unsigned char storage[64u * 3u];
    ZMessagePool pool;
    ZMessagePoolSet set;
    ZMsg const *slots[1];
    ZMailbox mailbox;
    ZMsg *first = NULL;
    ZMsg *second = NULL;
    ZMsg *abandoned = NULL;
    ZMsg const *taken = NULL;
    uint16_t free_before;
    ZMsg static_message;

    z_message_pool_init(&pool, storage, sizeof(storage), 64u, 3u);
    z_message_pool_set_init(&set, &pool, 1u);
    z_mailbox_init(&mailbox, slots, 1u);

    (void)Z_MSG_BEGIN(&set, 0u, uint32_t, &abandoned);
    z_message_release(abandoned);
    abandoned = NULL;
    CHECK(pool.free_count == pool.block_count);

    EXPECT_PANIC(z_message_pool_init(&pool, storage + 1u, sizeof(storage) - 1u,
                                     64u, 1u), Z_PANIC_CONFIGURATION);
    CHECK(g_last_panic.file != NULL && g_last_panic.line != 0u);
    EXPECT_PANIC(z_mailbox_init(&mailbox, slots, 0u), Z_PANIC_CONFIGURATION);
    EXPECT_PANIC(z_message_static_init(&static_message, 1u, NULL, 1u),
                 Z_PANIC_INVALID_ARGUMENT);

    free_before = pool.free_count;
    EXPECT_PANIC(z_message_begin(&set, 1u, 1024u, &first), Z_PANIC_NO_MEMORY);
    CHECK(pool.free_count == free_before);

    (void)Z_MSG_BEGIN(&set, 1u, uint32_t, &first);
    z_mailbox_post(&mailbox, &first);
    CHECK(first == NULL);
    (void)Z_MSG_BEGIN(&set, 2u, uint32_t, &second);
    free_before = pool.free_count;
    EXPECT_PANIC(z_mailbox_post(&mailbox, &second), Z_PANIC_MAILBOX_FULL);
    CHECK(second != NULL && second->kind == Z_MSG_KIND_DYNAMIC);
    CHECK(pool.free_count == free_before);

    CHECK(z_mailbox_take(&mailbox, &taken) == Z_OK);
    EXPECT_PANIC(z_message_payload_mut(first), Z_PANIC_LIFECYCLE);
    EXPECT_PANIC(z_message_payload_checked(taken, sizeof(uint16_t)), Z_PANIC_PAYLOAD_SIZE);
    z_message_release(taken);
    EXPECT_PANIC(z_message_release(taken), Z_PANIC_OWNERSHIP);

    z_mailbox_post(&mailbox, &second);
    CHECK(second == NULL);
    CHECK(z_mailbox_take(&mailbox, &taken) == Z_OK);
    z_message_release(taken);
    CHECK(pool.free_count == pool.block_count);

    {
        unsigned machine_context = 0u;
        ZStateNode root = { NULL, NULL, NULL, NULL, NULL };
        ZMachine machine;
        ZMsg static_message;
        z_machine_init(&machine, &root, &machine_context);
        z_message_static_init(&static_message, 3u, NULL, 0u);
        EXPECT_PANIC(z_machine_dispatch(&machine, &static_message), Z_PANIC_LIFECYCLE);
        z_machine_start(&machine);
        EXPECT_PANIC(z_machine_start(&machine), Z_PANIC_LIFECYCLE);
    }
}

/* @relation(ZEF-TEST-003, scope=message-claims-and-deferred-ownership) */
static void test_claims_and_static_messages(void)
{
    _Alignas(ZEF_POOL_ALIGNMENT) unsigned char storage[64u * 2u];
    ZMessagePool pool;
    ZMessagePoolSet set;
    ZMsg const *slots[2];
    ZMailbox mailbox;
    ZMsg *message = NULL;
    ZMsg const *taken = NULL;
    ZMsg static_message;

    z_message_pool_init(&pool, storage, sizeof(storage), 64u, 2u);
    z_message_pool_set_init(&set, &pool, 1u);
    z_mailbox_init(&mailbox, slots, 2u);

    (void)Z_MSG_BEGIN(&set, 1u, uint32_t, &message);
    z_mailbox_post(&mailbox, &message);
    CHECK(z_mailbox_take(&mailbox, &taken) == Z_OK);
    z_message_retain(taken);
    z_message_release(taken);
    z_mailbox_postpone(&mailbox, taken);
    z_message_release(taken);
    CHECK(z_mailbox_take(&mailbox, &taken) == Z_OK);
    CHECK(message == NULL);
    z_message_release(taken);
    CHECK(pool.free_count == pool.block_count);

    z_message_static_init(&static_message, 2u, NULL, 0u);
    z_mailbox_postpone(&mailbox, &static_message);
    CHECK(z_mailbox_take(&mailbox, &taken) == Z_OK);
    CHECK(taken == &static_message);
    z_message_retain(taken);
    z_message_release(taken);
}

static ZBool topic_predicate(ZMsg const *message, void *context)
{
    ZTopic expected = *(ZTopic *)context;
    return z_message_topic(message) == expected;
}

/* @relation(ZEF-TEST-010, scope=mailbox-ordering-and-removal) */
static void test_mailbox_behavior(void)
{
    _Alignas(ZEF_POOL_ALIGNMENT) unsigned char storage[64u * 4u];
    ZMessagePool pool;
    ZMessagePoolSet set;
    ZMsg const *slots[4];
    ZMailbox mailbox;
    ZMsg *first = NULL;
    ZMsg *second = NULL;
    ZMsg *front = NULL;
    ZMsg const *taken = NULL;
    ZTopic remove_topic = 2u;
    ZMsg one;
    ZMsg two;
    ZMsg three;
    ZMsg *removable_first = NULL;
    ZMsg *removable_second = NULL;

    z_message_pool_init(&pool, storage, sizeof(storage), 64u, 4u);
    z_message_pool_set_init(&set, &pool, 1u);
    z_mailbox_init(&mailbox, slots, 4u);
    CHECK(z_mailbox_capacity(&mailbox) == 4u);
    (void)Z_MSG_BEGIN(&set, 1u, uint32_t, &first);
    (void)Z_MSG_BEGIN(&set, 2u, uint32_t, &second);
    (void)Z_MSG_BEGIN(&set, 3u, uint32_t, &front);
    z_mailbox_post(&mailbox, &first);
    z_mailbox_post(&mailbox, &second);
    z_mailbox_post_front(&mailbox, &front);
    CHECK(z_mailbox_count(&mailbox) == 3u);
    CHECK(z_mailbox_free(&mailbox) == 1u);
    CHECK(z_mailbox_min_free(&mailbox) == 1u);

    CHECK(z_mailbox_take(&mailbox, &taken) == Z_OK);
    CHECK(z_message_topic(taken) == 3u);
    z_message_release(taken);
    CHECK(z_mailbox_take(&mailbox, &taken) == Z_OK);
    CHECK(z_message_topic(taken) == 1u);
    z_message_release(taken);
    CHECK(z_mailbox_take(&mailbox, &taken) == Z_OK);
    CHECK(z_message_topic(taken) == 2u);
    z_message_release(taken);
    CHECK(z_mailbox_take(&mailbox, &taken) == Z_EMPTY);

    (void)Z_MSG_BEGIN(&set, 1u, uint32_t, &removable_first);
    (void)Z_MSG_BEGIN(&set, 2u, uint32_t, &removable_second);
    z_mailbox_post(&mailbox, &removable_first);
    z_mailbox_post(&mailbox, &removable_second);
    CHECK(z_mailbox_remove_if(&mailbox, topic_predicate, &remove_topic) == 1u);
    CHECK(z_mailbox_take(&mailbox, &taken) == Z_OK);
    CHECK(z_message_topic(taken) == 1u);
    z_message_release(taken);
    CHECK(pool.free_count == pool.block_count);

    z_message_static_init(&one, 1u, NULL, 0u);
    z_message_static_init(&two, 2u, NULL, 0u);
    z_message_static_init(&three, 1u, NULL, 0u);
    z_mailbox_postpone(&mailbox, &one);
    z_mailbox_postpone(&mailbox, &two);
    z_mailbox_postpone(&mailbox, &three);
    CHECK(z_mailbox_remove_if(&mailbox, topic_predicate, &remove_topic) == 1u);
    CHECK(z_mailbox_count(&mailbox) == 2u);
    CHECK(z_mailbox_take(&mailbox, &taken) == Z_OK && taken == &one);
    z_message_release(taken);
    CHECK(z_mailbox_take(&mailbox, &taken) == Z_OK && taken == &three);
    z_message_release(taken);

    {
        ZMsg const *wrap_slots[2];
        ZMailbox wrap_mailbox;
        ZMsg *wrap_first = NULL;
        ZMsg *wrap_second = NULL;
        ZMsg *wrap_third = NULL;
        z_mailbox_init(&wrap_mailbox, wrap_slots, 2u);
        (void)Z_MSG_BEGIN(&set, 10u, uint32_t, &wrap_first);
        (void)Z_MSG_BEGIN(&set, 11u, uint32_t, &wrap_second);
        (void)Z_MSG_BEGIN(&set, 12u, uint32_t, &wrap_third);
        z_mailbox_post_front(&wrap_mailbox, &wrap_first);
        CHECK(z_mailbox_take(&wrap_mailbox, &taken) == Z_OK && taken != NULL);
        z_message_release(taken);
        z_mailbox_post(&wrap_mailbox, &wrap_second);
        z_mailbox_post(&wrap_mailbox, &wrap_third);
        CHECK(z_mailbox_take(&wrap_mailbox, &taken) == Z_OK && taken != NULL);
        z_message_release(taken);
        CHECK(z_mailbox_take(&wrap_mailbox, &taken) == Z_OK && taken != NULL);
        z_message_release(taken);
    }

    {
        ZMsg const *source_slots[4];
        ZMsg const *destination_slots[4];
        ZMailbox source;
        ZMailbox destination;
        ZMsg source_one;
        ZMsg source_two;
        ZMsg source_three;
        ZMsg destination_message;

        z_mailbox_init(&source, source_slots, 4u);
        z_mailbox_init(&destination, destination_slots, 4u);
        z_message_static_init(&source_one, 21u, NULL, 0u);
        z_message_static_init(&source_two, 22u, NULL, 0u);
        z_message_static_init(&source_three, 23u, NULL, 0u);
        z_message_static_init(&destination_message, 99u, NULL, 0u);
        z_mailbox_postpone(&source, &source_one);
        z_mailbox_postpone(&source, &source_two);
        z_mailbox_postpone(&source, &source_three);
        z_mailbox_postpone(&destination, &destination_message);

        CHECK(z_mailbox_recall(&destination, &source) == Z_OK);
        CHECK(z_mailbox_take(&destination, &taken) == Z_OK &&
              z_message_topic(taken) == 23u);
        z_message_release(taken);
        CHECK(z_mailbox_recall(&destination, &source) == Z_OK);
        CHECK(z_mailbox_take(&destination, &taken) == Z_OK &&
              z_message_topic(taken) == 22u);
        z_message_release(taken);
        CHECK(z_mailbox_recall(&destination, &source) == Z_OK);
        CHECK(z_mailbox_take(&destination, &taken) == Z_OK &&
              z_message_topic(taken) == 21u);
        z_message_release(taken);
        CHECK(z_mailbox_recall(&destination, &source) == Z_EMPTY);
        CHECK(z_mailbox_take(&destination, &taken) == Z_OK &&
              z_message_topic(taken) == 99u);
        z_message_release(taken);
    }

}

typedef struct {
    char events[16];
    unsigned length;
} MachineContext;

static void record_event(MachineContext *context, char event)
{
    CHECK(context->length < sizeof(context->events));
    context->events[context->length++] = event;
}

static void root_enter(void *context) { record_event(context, 'R'); }
static void first_enter(void *context) { record_event(context, 'A'); }
static void first_leave(void *context) { record_event(context, 'a'); }
static void second_enter(void *context) { record_event(context, 'B'); }
static void second_leave(void *context) { record_event(context, 'b'); }

static ZStateNode const *g_second_state;
static ZStateNode const *g_invalid_state;

static ZAction first_state_handler(void *context, ZMsg const *message)
{
    (void)context;
    if (z_message_topic(message) == 7u) {
        return (ZAction){ Z_ACTION_TRANSITION, g_second_state };
    }
    return (ZAction){ Z_ACTION_UNHANDLED, NULL };
}

static ZAction second_state_handler(void *context, ZMsg const *message)
{
    if (z_message_topic(message) == 8u) {
        return (ZAction){ Z_ACTION_TRANSITION, g_second_state };
    }
    if (z_message_topic(message) == 6u) {
        return (ZAction){ Z_ACTION_TRANSITION, g_invalid_state };
    }
    (void)context;
    return (ZAction){ Z_ACTION_UNHANDLED, NULL };
}

static ZAction machine_root_handler(void *context, ZMsg const *message)
{
    if (z_message_topic(message) == 9u) {
        record_event(context, 'H');
        return (ZAction){ Z_ACTION_HANDLED, NULL };
    }
    return (ZAction){ Z_ACTION_UNHANDLED, NULL };
}

/* @relation(ZEF-TEST-011, scope=hierarchical-machine-lifecycle) */
static void test_machine_behavior(void)
{
    MachineContext context = { { 0 }, 0u };
    ZStateNode second = { NULL, NULL, second_state_handler, second_enter, second_leave };
    ZStateNode first = { NULL, NULL, first_state_handler, first_enter, first_leave };
    ZStateNode root = { NULL, &first, machine_root_handler, root_enter, NULL };
    ZStateNode outside = { NULL, NULL, NULL, NULL, NULL };
    ZMachine machine;
    ZMsg message;

    first.parent = &root;
    second.parent = &root;
    g_second_state = &second;
    g_invalid_state = &outside;
    z_machine_init(&machine, &root, &context);
    z_machine_start(&machine);
    CHECK(z_machine_state(&machine) == &first);
    CHECK(context.length == 2u);
    CHECK(context.events[0] == 'R' && context.events[1] == 'A');

    z_message_static_init(&message, 7u, NULL, 0u);
    z_machine_dispatch(&machine, &message);
    CHECK(z_machine_state(&machine) == &second);
    CHECK(context.length == 4u);
    CHECK(context.events[2] == 'a' && context.events[3] == 'B');

    z_message_static_init(&message, 8u, NULL, 0u);
    z_machine_dispatch(&machine, &message);
    CHECK(z_machine_state(&machine) == &second);
    CHECK(context.length == 6u);
    CHECK(context.events[4] == 'b' && context.events[5] == 'B');

    z_message_static_init(&message, 9u, NULL, 0u);
    z_machine_dispatch(&machine, &message);
    CHECK(context.length == 7u && context.events[6] == 'H');

    z_message_static_init(&message, 6u, NULL, 0u);
    EXPECT_PANIC(z_machine_dispatch(&machine, &message), Z_PANIC_STATE);
    CHECK(z_machine_state(&machine) == &second);
}

typedef struct {
    char events[16];
    unsigned length;
} FastMachineContext;

static void fast_machine_event(FastMachineContext *context, char event)
{
    CHECK(context->length < sizeof(context->events));
    context->events[context->length++] = event;
}

static void fast_root_enter(void *context)
{
    fast_machine_event(context, 'R');
}

static void fast_a_enter(void *context)
{
    fast_machine_event(context, 'A');
}

static void fast_a_leave(void *context)
{
    fast_machine_event(context, 'a');
}

static void fast_child_enter(void *context)
{
    fast_machine_event(context, 'C');
}

static void fast_child_leave(void *context)
{
    fast_machine_event(context, 'c');
}

static void fast_sibling_enter(void *context)
{
    fast_machine_event(context, 'S');
}

static ZStateNode const *g_fast_child;
static ZStateNode const *g_fast_parent;
static ZStateNode const *g_fast_sibling;

static ZAction fast_a_handler(void *context, ZMsg const *message)
{
    (void)context;
    if (z_message_topic(message) == 1u) {
        return (ZAction){ Z_ACTION_TRANSITION, g_fast_child };
    }
    if (z_message_topic(message) == 2u) {
        return (ZAction){ Z_ACTION_TRANSITION, g_fast_sibling };
    }
    return (ZAction){ Z_ACTION_UNHANDLED, NULL };
}

static ZAction fast_child_handler(void *context, ZMsg const *message)
{
    (void)context;
    if (z_message_topic(message) == 3u) {
        return (ZAction){ Z_ACTION_TRANSITION, g_fast_parent };
    }
    return (ZAction){ Z_ACTION_UNHANDLED, NULL };
}

/* @relation(ZEF-TEST-014, scope=optimized-state-transitions) */
static void test_machine_fast_transitions(void)
{
    FastMachineContext context = { { 0 }, 0u };
    ZStateNode child = { NULL, NULL, fast_child_handler,
                         fast_child_enter, fast_child_leave };
    ZStateNode sibling = { NULL, NULL, NULL, fast_sibling_enter, NULL };
    ZStateNode parent = { NULL, NULL, fast_a_handler,
                          fast_a_enter, fast_a_leave };
    ZStateNode root = { NULL, &parent, NULL, fast_root_enter, NULL };
    ZMachine machine;
    ZMsg message;

    child.parent = &parent;
    parent.parent = &root;
    sibling.parent = &root;
    g_fast_child = &child;
    g_fast_parent = &parent;
    g_fast_sibling = &sibling;

    z_machine_init(&machine, &root, &context);
    z_machine_start(&machine);
    CHECK(z_machine_state(&machine) == &parent);
    CHECK(context.length == 2u && memcmp(context.events, "RA", 2u) == 0);

    z_message_static_init(&message, 1u, NULL, 0u);
    z_machine_dispatch(&machine, &message);
    CHECK(z_machine_state(&machine) == &child);
    CHECK(context.length == 3u && memcmp(context.events, "RAC", 3u) == 0);

    z_message_static_init(&message, 3u, NULL, 0u);
    z_machine_dispatch(&machine, &message);
    CHECK(z_machine_state(&machine) == &parent);
    CHECK(context.length == 4u && memcmp(context.events, "RACc", 4u) == 0);

    z_message_static_init(&message, 2u, NULL, 0u);
    z_machine_dispatch(&machine, &message);
    CHECK(z_machine_state(&machine) == &sibling);
    CHECK(context.length == 6u && memcmp(context.events, "RACcaS", 6u) == 0);
}

typedef struct {
    char events[64];
    unsigned length;
    ZStateNode const *left_sibling;
    ZStateNode const *right_leaf;
    ZStateNode const *app;
    ZStateNode const *emergency_child;
} ComplexMachineContext;

static void complex_event(ComplexMachineContext *context, char event)
{
    CHECK(context->length < sizeof(context->events));
    context->events[context->length++] = event;
}

static void complex_root_enter(void *context) { complex_event(context, 'R'); }
static void complex_app_enter(void *context) { complex_event(context, 'A'); }
static void complex_app_leave(void *context) { complex_event(context, 'a'); }
static void complex_left_enter(void *context) { complex_event(context, 'L'); }
static void complex_left_leave(void *context) { complex_event(context, 'l'); }
static void complex_leaf_enter(void *context) { complex_event(context, 'F'); }
static void complex_leaf_leave(void *context) { complex_event(context, 'f'); }
static void complex_sibling_enter(void *context) { complex_event(context, 'S'); }
static void complex_sibling_leave(void *context) { complex_event(context, 's'); }
static void complex_right_enter(void *context) { complex_event(context, 'r'); }
static void complex_right_leave(void *context) { complex_event(context, 'r'); }
static void complex_right_leaf_enter(void *context) { complex_event(context, 'T'); }
static void complex_right_leaf_leave(void *context) { complex_event(context, 't'); }
static void complex_emergency_enter(void *context) { complex_event(context, 'E'); }
static void complex_emergency_leave(void *context) { complex_event(context, 'e'); }
static void complex_emergency_child_enter(void *context) { complex_event(context, 'C'); }
static void complex_emergency_child_leave(void *context) { complex_event(context, 'c'); }

static ZAction complex_left_leaf_handler(void *context, ZMsg const *message)
{
    ComplexMachineContext *machine = context;
    if (z_message_topic(message) == 1u) {
        return (ZAction){ Z_ACTION_TRANSITION, machine->left_sibling };
    }
    if (z_message_topic(message) == 4u) {
        return (ZAction){ Z_ACTION_TRANSITION, machine->emergency_child };
    }
    return (ZAction){ Z_ACTION_UNHANDLED, NULL };
}

static ZAction complex_sibling_handler(void *context, ZMsg const *message)
{
    ComplexMachineContext *machine = context;
    if (z_message_topic(message) == 2u) {
        return (ZAction){ Z_ACTION_TRANSITION, machine->right_leaf };
    }
    return (ZAction){ Z_ACTION_UNHANDLED, NULL };
}

static ZAction complex_right_leaf_handler(void *context, ZMsg const *message)
{
    ComplexMachineContext *machine = context;
    if (z_message_topic(message) == 3u) {
        return (ZAction){ Z_ACTION_TRANSITION, machine->app };
    }
    if (z_message_topic(message) == 6u) {
        return (ZAction){ Z_ACTION_TRANSITION, machine->right_leaf };
    }
    return (ZAction){ Z_ACTION_UNHANDLED, NULL };
}

static ZAction complex_emergency_child_handler(void *context, ZMsg const *message)
{
    ComplexMachineContext *machine = context;
    if (z_message_topic(message) == 5u) {
        return (ZAction){ Z_ACTION_TRANSITION, machine->right_leaf };
    }
    return (ZAction){ Z_ACTION_UNHANDLED, NULL };
}

static ZAction complex_app_handler(void *context, ZMsg const *message)
{
    if (z_message_topic(message) == 7u) {
        complex_event(context, 'H');
        return (ZAction){ Z_ACTION_HANDLED, NULL };
    }
    return (ZAction){ Z_ACTION_UNHANDLED, NULL };
}

static void check_complex_events(ComplexMachineContext *context,
                                 char const *expected, unsigned length)
{
    CHECK(context->length == length);
    CHECK(memcmp(context->events, expected, length) == 0);
}

/* @relation(ZEF-TEST-012, scope=nested-machine-common-ancestor) */
static void test_complex_machine_hierarchy(void)
{
    ComplexMachineContext context = { { 0 }, 0u, NULL, NULL, NULL, NULL };
    ZStateNode root;
    ZStateNode app;
    ZStateNode left;
    ZStateNode left_leaf;
    ZStateNode left_sibling;
    ZStateNode right;
    ZStateNode right_leaf;
    ZStateNode emergency;
    ZStateNode emergency_child;
    ZMachine machine;
    ZMsg message;

    root = (ZStateNode){ NULL, &app, NULL, complex_root_enter, NULL };
    app = (ZStateNode){ &root, &left, complex_app_handler,
                        complex_app_enter, complex_app_leave };
    left = (ZStateNode){ &app, &left_leaf, NULL,
                         complex_left_enter, complex_left_leave };
    left_leaf = (ZStateNode){ &left, NULL, complex_left_leaf_handler,
                              complex_leaf_enter, complex_leaf_leave };
    left_sibling = (ZStateNode){ &left, NULL, complex_sibling_handler,
                                 complex_sibling_enter, complex_sibling_leave };
    right = (ZStateNode){ &app, &right_leaf, NULL,
                          complex_right_enter, complex_right_leave };
    right_leaf = (ZStateNode){ &right, NULL, complex_right_leaf_handler,
                               complex_right_leaf_enter, complex_right_leaf_leave };
    emergency = (ZStateNode){ &root, &emergency_child, NULL,
                              complex_emergency_enter, complex_emergency_leave };
    emergency_child = (ZStateNode){ &emergency, NULL,
                                    complex_emergency_child_handler,
                                    complex_emergency_child_enter,
                                    complex_emergency_child_leave };
    context.left_sibling = &left_sibling;
    context.right_leaf = &right_leaf;
    context.app = &app;
    context.emergency_child = &emergency_child;

    z_machine_init(&machine, &root, &context);
    z_machine_start(&machine);
    CHECK(z_machine_state(&machine) == &left_leaf);
    check_complex_events(&context, "RALF", 4u);

    z_message_static_init(&message, 1u, NULL, 0u);
    z_machine_dispatch(&machine, &message);
    CHECK(z_machine_state(&machine) == &left_sibling);
    check_complex_events(&context, "RALFfS", 6u);

    z_message_static_init(&message, 2u, NULL, 0u);
    z_machine_dispatch(&machine, &message);
    CHECK(z_machine_state(&machine) == &right_leaf);
    check_complex_events(&context, "RALFfSslrT", 10u);

    z_message_static_init(&message, 3u, NULL, 0u);
    z_machine_dispatch(&machine, &message);
    CHECK(z_machine_state(&machine) == &left_leaf);
    check_complex_events(&context, "RALFfSslrTtrLF", 14u);

    z_message_static_init(&message, 4u, NULL, 0u);
    z_machine_dispatch(&machine, &message);
    CHECK(z_machine_state(&machine) == &emergency_child);
    check_complex_events(&context, "RALFfSslrTtrLFflaEC", 19u);

    z_message_static_init(&message, 5u, NULL, 0u);
    z_machine_dispatch(&machine, &message);
    CHECK(z_machine_state(&machine) == &right_leaf);
    check_complex_events(&context, "RALFfSslrTtrLFflaECceArT", 24u);

    z_message_static_init(&message, 6u, NULL, 0u);
    z_machine_dispatch(&machine, &message);
    CHECK(z_machine_state(&machine) == &right_leaf);
    check_complex_events(&context, "RALFfSslrTtrLFflaECceArTtT", 26u);

    z_message_static_init(&message, 7u, NULL, 0u);
    z_machine_dispatch(&machine, &message);
    CHECK(z_machine_state(&machine) == &right_leaf);
    check_complex_events(&context, "RALFfSslrTtrLFflaECceArTtTH", 27u);
}

/* @relation(ZEF-TEST-020, scope=kernel-priority-scheduling) */
static void test_kernel_behavior(void)
{
    HostPortState port_state = { 0u, 0u, 0u, 0u };
    ZPort port;
    ZKernel kernel;
    ZStateNode root;
    HandlerContext contexts[3] = { { 0 }, { 0 }, { 0 } };
    ZMachine machines[3];
    ZMailbox mailboxes[3];
    ZMsg const *slots[3][2];
    ZReactor reactors[3];
    ZMsg message;
    ZReactorId ids[3];
    ZMsg const *taken = NULL;
    ZPriority priority = 0u;

    g_port_state = &port_state;
    port = test_port();
    root = make_root(NULL);
    z_kernel_init(&kernel, &port);
    for (unsigned index = 0u; index < 3u; ++index) {
        contexts[index].kernel = &kernel;
        z_machine_init(&machines[index], &root, &contexts[index]);
        z_mailbox_init(&mailboxes[index], slots[index], 2u);
        z_reactor_init(&reactors[index], &(ZReactorConfig){
            &machines[index], &mailboxes[index], &contexts[index]
        });
    }
    ids[0] = z_reactor_register(&kernel, &reactors[0], 1u);
    ids[1] = z_reactor_register(&kernel, &reactors[1], 3u);
    ids[2] = z_reactor_register(&kernel, &reactors[2], 2u);
    CHECK(ids[0] == 1u && ids[1] == 3u && ids[2] == 2u);
    CHECK(kernel.reactor_count == 3u && kernel.reactor_limit == 4u);
    z_reactor_start(&kernel, ids[0]);
    z_reactor_start(&kernel, ids[1]);

    z_message_static_init(&message, 1u, NULL, 0u);
    post_static_message(&mailboxes[0], &message);
    post_static_message(&mailboxes[1], &message);
    CHECK(z_kernel_run_one(&kernel) == Z_WORK_DONE);
    CHECK(contexts[1].handled == 1u && contexts[2].handled == 0u);
    CHECK(z_kernel_run_one(&kernel) == Z_WORK_DONE);
    CHECK(contexts[0].handled == 1u);
    post_static_message(&mailboxes[2], &message);
    CHECK(z_kernel_run_one(&kernel) == Z_NO_WORK);
    z_reactor_start(&kernel, ids[2]);
    post_static_message(&mailboxes[1], &message);
    CHECK(z_kernel_run_one(&kernel) == Z_WORK_DONE);
    CHECK(contexts[1].handled == 2u);
    CHECK(z_kernel_run_one(&kernel) == Z_WORK_DONE);
    CHECK(contexts[2].handled == 1u);
    CHECK(contexts[1].priorities[0] == 3u && contexts[2].priorities[0] == 2u);
    CHECK(contexts[0].priorities[0] == 1u);
    CHECK(z_kernel_current_priority(&kernel, &priority) == Z_NOT_RUNNING);
    CHECK(z_kernel_run_one(&kernel) == Z_NO_WORK);
    g_jump_from_idle = 1u;
    if (setjmp(g_idle_env) == 0) {
        z_kernel_run_forever(&kernel);
        CHECK(0);
    } else {
        g_jump_from_idle = 0u;
    }
    CHECK(z_mailbox_take(&mailboxes[0], &taken) == Z_EMPTY);
    g_port_state = NULL;
}

/* @relation(ZEF-TEST-034, scope=shared-stack-priority-gate) */
static void test_shared_stack_priority_gate(void)
{
    HostPortState port_state = { 0u, 0u, 0u, 0u };
    ZPort port;
    ZKernel kernel;
    ZStateNode low_root;
    ZStateNode high_root;
    ZMachine low_machine;
    ZMachine high_machine;
    ZMsg const *low_slots[2];
    ZMsg const *high_slots[2];
    ZMailbox low_mailbox;
    ZMailbox high_mailbox;
    ZReactor low_reactor;
    ZReactor high_reactor;
    ZMsg message;
    PostProbe low_probe;
    PostProbe high_probe;
    unsigned reschedules_before_high;

    g_port_state = &port_state;
    port = test_port();
    z_kernel_init(&kernel, &port);
    low_root = (ZStateNode){ NULL, NULL, post_probe_handler, NULL, NULL };
    high_root = (ZStateNode){ NULL, NULL, post_probe_handler, NULL, NULL };
    z_mailbox_init(&low_mailbox, low_slots, 2u);
    z_mailbox_init(&high_mailbox, high_slots, 2u);
    z_message_static_init(&message, 1u, NULL, 0u);
    low_probe = (PostProbe){ &high_mailbox, &message, 0u };
    high_probe = (PostProbe){ &low_mailbox, &message, 0u };
    z_machine_init(&low_machine, &low_root, &low_probe);
    z_machine_init(&high_machine, &high_root, &high_probe);
    z_reactor_init(&low_reactor, &(ZReactorConfig){
        &low_machine, &low_mailbox, NULL
    });
    z_reactor_init(&high_reactor, &(ZReactorConfig){
        &high_machine, &high_mailbox, NULL
    });
    (void)z_reactor_register(&kernel, &low_reactor, 1u);
    (void)z_reactor_register(&kernel, &high_reactor, 3u);
    z_reactor_start(&kernel, 1u);
    z_reactor_start(&kernel, 3u);

    post_static_message(&low_mailbox, &message);
    reschedules_before_high = port_state.reschedules;
    CHECK(z_kernel_run_one(&kernel) == Z_WORK_DONE);
    CHECK(low_probe.posted && port_state.reschedules > reschedules_before_high);

    reschedules_before_high = port_state.reschedules;
    CHECK(z_kernel_run_one(&kernel) == Z_WORK_DONE);
    CHECK(high_probe.posted && port_state.reschedules == reschedules_before_high);
    CHECK(z_kernel_run_one(&kernel) == Z_WORK_DONE);
    g_port_state = NULL;
}

/* @relation(ZEF-TEST-021, scope=topic-publication-order) */
static void test_topic_behavior(void)
{
    HostPortState port_state = { 0u, 0u, 0u, 0u };
    ZPort port;
    ZKernel kernel;
    ZStateNode root;
    HandlerContext first_context = { 0 };
    HandlerContext second_context = { 0 };
    ZMachine first_machine;
    ZMachine second_machine;
    ZMsg const *first_slots[2];
    ZMsg const *second_slots[2];
    ZMailbox first_mailbox;
    ZMailbox second_mailbox;
    ZReactor first;
    ZReactor second;
    ZReactorId first_id;
    ZReactorId second_id;
    ZTopicTable topics;
    _Alignas(ZEF_POOL_ALIGNMENT) unsigned char storage[64u];
    ZMessagePool pool;
    ZMessagePoolSet set;
    ZMsg *message = NULL;

    g_port_state = &port_state;
    port = test_port();
    z_kernel_init(&kernel, &port);
    root = make_root(NULL);
    first_context.kernel = &kernel;
    second_context.kernel = &kernel;
    z_machine_init(&first_machine, &root, &first_context);
    z_machine_init(&second_machine, &root, &second_context);
    z_mailbox_init(&first_mailbox, first_slots, 2u);
    z_mailbox_init(&second_mailbox, second_slots, 2u);
    z_reactor_init(&first, &(ZReactorConfig){ &first_machine, &first_mailbox, &first_context });
    z_reactor_init(&second, &(ZReactorConfig){ &second_machine, &second_mailbox, &second_context });
    first_id = z_reactor_register(&kernel, &first, 1u);
    second_id = z_reactor_register(&kernel, &second, 3u);
    z_reactor_start(&kernel, first_id);
    z_reactor_start(&kernel, second_id);
    z_topic_table_init(&topics, &kernel);
    z_topic_subscribe(&topics, 3u, first_id);
    z_topic_subscribe(&topics, 3u, first_id);
    z_topic_subscribe(&topics, 3u, second_id);

    z_message_pool_init(&pool, storage, sizeof(storage), 64u, 1u);
    z_message_pool_set_init(&set, &pool, 1u);
    (void)z_message_begin(&set, 3u, 0u, &message);
    CHECK(z_topic_publish(&kernel, &topics, &message) == 2u);
    CHECK(message == NULL);
    CHECK(z_kernel_run_one(&kernel) == Z_WORK_DONE);
    CHECK(second_context.handled == 1u && first_context.handled == 0u);
    CHECK(z_kernel_run_one(&kernel) == Z_WORK_DONE);
    CHECK(first_context.handled == 1u);
    CHECK(pool.free_count == pool.block_count);

    (void)z_message_begin(&set, 4u, 0u, &message);
    CHECK(z_topic_publish(&kernel, &topics, &message) == 0u);
    CHECK(message == NULL);
    CHECK(pool.free_count == pool.block_count);
    g_port_state = NULL;
}

/* @relation(ZEF-TEST-030, scope=timer-delivery-and-periodicity) */
static void test_timer_behavior(void)
{
    HostPortState port_state = { 0u, 0u, 0u, 0u };
    ZPort port;
    ZKernel kernel;
    ZStateNode root;
    HandlerContext context = { 0 };
    ZMachine machine;
    ZMsg const *slots[4];
    ZMailbox mailbox;
    ZReactor reactor;
    ZReactorId id;
    ZTopicTable topics;
    ZClock clock;
    ZTimer one_shot;
    ZTimer periodic;

    g_port_state = &port_state;
    port = test_port();
    z_kernel_init(&kernel, &port);
    root = make_root(NULL);
    context.kernel = &kernel;
    z_machine_init(&machine, &root, &context);
    z_mailbox_init(&mailbox, slots, 4u);
    z_reactor_init(&reactor, &(ZReactorConfig){ &machine, &mailbox, &context });
    id = z_reactor_register(&kernel, &reactor, 1u);
    z_reactor_start(&kernel, id);
    z_topic_table_init(&topics, &kernel);
    z_clock_init(&clock, &kernel);

    z_timer_init_topic(&one_shot, &clock, id, 1u);
    z_timer_arm(&one_shot, 0u, 0u);
    z_clock_tick(&clock);
    CHECK(z_mailbox_count(&mailbox) == 1u);
    CHECK(z_kernel_run_one(&kernel) == Z_WORK_DONE);
    CHECK(context.handled == 1u);
    CHECK(z_timer_stop(&one_shot) == Z_NOT_RUNNING);

    z_timer_init_topic(&periodic, &clock, id, 1u);
    z_timer_arm(&periodic, 1u, 2u);
    z_clock_tick(&clock);
    CHECK(z_kernel_run_one(&kernel) == Z_WORK_DONE);
    CHECK(context.handled == 2u);
    z_clock_tick(&clock);
    CHECK(z_kernel_run_one(&kernel) == Z_NO_WORK);
    z_clock_tick(&clock);
    CHECK(z_kernel_run_one(&kernel) == Z_WORK_DONE);
    CHECK(context.handled == 3u);
    CHECK(z_timer_stop(&periodic) == Z_OK);
    CHECK(z_timer_stop(&periodic) == Z_NOT_RUNNING);
    g_port_state = NULL;
}

/* @relation(ZEF-TEST-031, scope=timer-specific-cancellation) */
static void test_timer_cancellation(void)
{
    HostPortState port_state = { 0u, 0u, 0u, 0u };
    ZPort port;
    ZKernel kernel;
    ZStateNode root;
    HandlerContext context = { 0 };
    ZMachine machine;
    ZMsg const *slots[4];
    ZMailbox mailbox;
    ZReactor reactor;
    ZReactorId id;
    ZClock clock;
    ZTimer timer;
    ZMsg const *taken = NULL;

    g_port_state = &port_state;
    port = test_port();
    z_kernel_init(&kernel, &port);
    root = make_root(NULL);
    context.kernel = &kernel;
    z_machine_init(&machine, &root, &context);
    z_mailbox_init(&mailbox, slots, 4u);
    z_reactor_init(&reactor, &(ZReactorConfig){ &machine, &mailbox, &context });
    id = z_reactor_register(&kernel, &reactor, 1u);
    z_reactor_start(&kernel, id);
    z_clock_init(&clock, &kernel);
    ZMsg ordinary;
    z_message_static_init(&ordinary, 4u, NULL, 0u);
    post_static_message(&mailbox, &ordinary);
    z_timer_init_topic(&timer, &clock, id, 5u);
    z_timer_arm(&timer, 1u, 0u);
    z_clock_tick(&clock);
    CHECK(z_mailbox_count(&mailbox) == 2u);
    CHECK(z_timer_cancel(&timer) == Z_OK);
    CHECK(z_mailbox_count(&mailbox) == 1u);
    CHECK(z_mailbox_take(&mailbox, &taken) == Z_OK);
    CHECK(taken == &ordinary);
    z_message_release(taken);
    CHECK(z_timer_cancel(&timer) == Z_NOT_RUNNING);
    g_port_state = NULL;
}

/* @relation(ZEF-TEST-033, scope=timer-ownership-and-ordering-corners) */
static void test_timer_corner_cases(void)
{
    HostPortState port_state = { 0u, 0u, 0u, 0u };
    ZPort port;
    ZKernel kernel;
    ZStateNode root;
    HandlerContext context = { 0 };
    ZMachine machine;
    ZMsg const *slots[4];
    ZMailbox mailbox;
    ZReactor reactor;
    ZReactorId id;
    ZClock clock;
    ZTimer first;
    ZTimer second;
    ZTimer invalid;
    ZTimer dynamic_timer;
    ZTimer cancelled_timer;
    ZMsg const *taken = NULL;
    _Alignas(ZEF_POOL_ALIGNMENT) unsigned char storage[64u];
    ZMessagePool pool;
    ZMessagePoolSet set;
    uint32_t dynamic_payload = 123u;
    uint32_t cancelled_payload = 456u;

    g_port_state = &port_state;
    port = test_port();
    z_kernel_init(&kernel, &port);
    root = make_root(NULL);
    context.kernel = &kernel;
    z_machine_init(&machine, &root, &context);
    z_mailbox_init(&mailbox, slots, 4u);
    z_reactor_init(&reactor, &(ZReactorConfig){ &machine, &mailbox, &context });
    id = z_reactor_register(&kernel, &reactor, 1u);
    z_reactor_start(&kernel, id);
    z_clock_init(&clock, &kernel);

    z_timer_init_topic(&invalid, &clock, id, 9u);
    EXPECT_PANIC(z_timer_arm(&invalid, (ZTick)(ZEF_MAX_TIMER_SPAN + 1u), 0u),
                 Z_PANIC_OVERFLOW);
    z_timer_arm(&invalid, 1u, 0u);
    EXPECT_PANIC(z_timer_arm(&invalid, 1u, 0u), Z_PANIC_STATE);
    CHECK(z_timer_stop(&invalid) == Z_OK);

    z_timer_init_topic(&first, &clock, id, 10u);
    z_timer_init_topic(&second, &clock, id, 11u);
    z_timer_arm(&first, 1u, 0u);
    z_timer_arm(&second, 1u, 0u);
    z_clock_tick(&clock);
    CHECK(z_mailbox_count(&mailbox) == 2u);
    CHECK(z_mailbox_take(&mailbox, &taken) == Z_OK && z_message_topic(taken) == 10u);
    z_message_release(taken);
    CHECK(z_mailbox_take(&mailbox, &taken) == Z_OK && z_message_topic(taken) == 11u);
    z_message_release(taken);

    z_message_pool_init(&pool, storage, sizeof(storage), 64u, 1u);
    z_message_pool_set_init(&set, &pool, 1u);
    z_timer_init_payload(&dynamic_timer, &clock, id, &set, 12u,
                         &dynamic_payload, sizeof(dynamic_payload));
    z_timer_arm(&dynamic_timer, 1u, 1u);
    z_clock_tick(&clock);
    CHECK(pool.free_count == 0u);
    CHECK(z_timer_stop(&dynamic_timer) == Z_OK);
    CHECK(z_mailbox_take(&mailbox, &taken) == Z_OK);
    CHECK(taken != NULL);
    z_message_release(taken);
    CHECK(pool.free_count == 0u);
    z_timer_arm(&dynamic_timer, 1u, 0u);
    z_clock_tick(&clock);
    CHECK(z_mailbox_take(&mailbox, &taken) == Z_OK);
    CHECK(taken != NULL);
    z_message_release(taken);
    CHECK(pool.free_count == 0u);
    CHECK(z_timer_cancel(&dynamic_timer) == Z_OK);
    CHECK(pool.free_count == pool.block_count);

    z_timer_init_payload(&cancelled_timer, &clock, id, &set, 13u,
                         &cancelled_payload, sizeof(cancelled_payload));
    z_timer_arm(&cancelled_timer, 1u, 0u);
    z_clock_tick(&clock);
    CHECK(z_timer_cancel(&cancelled_timer) == Z_OK);
    CHECK(z_mailbox_count(&mailbox) == 0u);
    CHECK(pool.free_count == pool.block_count);
    CHECK(z_timer_cancel(&cancelled_timer) == Z_NOT_RUNNING);
    g_port_state = NULL;
}

/* @relation(ZEF-TEST-032, scope=port-critical-and-reschedule) */
static void test_port_behavior(void)
{
    HostPortState port_state = { 0u, 0u, 0u, 0u };
    ZPort port;
    ZCriticalState outer;
    ZCriticalState inner;
    ZKernel kernel;
    ZStateNode root;
    HandlerContext context = { 0 };
    ZMachine machine;
    ZMsg const *slots[2];
    ZMailbox mailbox;
    ZReactor reactor;
    ZReactorId id;
    ZTopicTable topics;
    ZMsg message;

    g_port_state = &port_state;
    port = test_port();
    outer = z_port_critical_enter();
    inner = z_port_critical_enter();
    CHECK(port_state.masked == 1u && port_state.enters == 2u);
    z_port_critical_leave(inner);
    CHECK(port_state.masked == 1u);
    z_port_critical_leave(outer);
    CHECK(port_state.masked == 0u && port_state.leaves == 2u);

    z_kernel_init(&kernel, &port);
    root = make_root(NULL);
    context.kernel = &kernel;
    z_machine_init(&machine, &root, &context);
    z_mailbox_init(&mailbox, slots, 2u);
    z_reactor_init(&reactor, &(ZReactorConfig){ &machine, &mailbox, &context });
    id = z_reactor_register(&kernel, &reactor, 1u);
    z_reactor_start(&kernel, id);
    z_topic_table_init(&topics, &kernel);
    z_topic_subscribe(&topics, 1u, id);
    CHECK(z_topic_unsubscribe(&topics, 1u, id) == Z_OK);
    CHECK(z_topic_unsubscribe(&topics, 1u, id) == Z_NOT_FOUND);
    z_message_static_init(&message, 1u, NULL, 0u);
    unsigned enters_before_post = port_state.enters;
    post_static_message(&mailbox, &message);
    CHECK(port_state.enters > enters_before_post &&
          port_state.leaves >= port_state.enters);
    CHECK(port_state.reschedules != 0u);
    ZMsg const *taken = NULL;
    unsigned enters_before_take = port_state.enters;
    CHECK(z_mailbox_take(&mailbox, &taken) == Z_OK);
    CHECK(taken == &message && port_state.enters > enters_before_take);

    {
        ZPort silent_port = { 0 };
        ZKernel silent_kernel;
        ZTopicTable silent_topics;
        ZStateNode silent_root;
        HandlerContext silent_context = { 0 };
        ZMachine silent_machine;
        ZMsg const *silent_slots[1];
        ZMailbox silent_mailbox;
        ZReactor silent_reactor;
        ZMsg silent_message;
        ZReactorId silent_id;
        z_kernel_init(&silent_kernel, &silent_port);
        silent_root = make_root(NULL);
        silent_context.kernel = &silent_kernel;
        z_machine_init(&silent_machine, &silent_root, &silent_context);
        z_mailbox_init(&silent_mailbox, silent_slots, 1u);
        z_reactor_init(&silent_reactor, &(ZReactorConfig){
            &silent_machine, &silent_mailbox, &silent_context
        });
        silent_id = z_reactor_register(&silent_kernel, &silent_reactor, 1u);
        z_reactor_start(&silent_kernel, silent_id);
        z_topic_table_init(&silent_topics, &silent_kernel);
        z_topic_subscribe(&silent_topics, 1u, silent_id);
        z_message_static_init(&silent_message, 1u, NULL, 0u);
        post_static_message(&silent_mailbox, &silent_message);
        CHECK(z_kernel_run_one(&silent_kernel) == Z_WORK_DONE);
        CHECK(silent_context.handled == 1u);
    }
    g_port_state = NULL;
}

int main(void)
{
    test_public_surface();
    test_pool_and_payload();
    test_failure_paths();
    test_claims_and_static_messages();
    test_mailbox_behavior();
    test_machine_behavior();
    test_machine_fast_transitions();
    test_complex_machine_hierarchy();
    test_actor_composition();
    test_kernel_behavior();
    test_shared_stack_priority_gate();
    test_topic_behavior();
    test_timer_behavior();
    test_timer_cancellation();
    test_timer_corner_cases();
    test_port_behavior();
    return 0;
}
