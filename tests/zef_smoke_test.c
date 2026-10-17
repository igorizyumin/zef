#include "zef.h"

#include <stdalign.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(expression_) do { if (!(expression_)) abort(); } while (0)

_Noreturn void z_panic(ZPanicInfo const *info)
{
    (void)info;
    abort();
}

typedef struct {
    unsigned handled;
} TestContext;

static void request_reschedule(void) {}
static void idle(void) {}

static ZAction handle_message(void *context, ZMsg const *message)
{
    TestContext *test = context;
    if (z_message_topic(message) == 1u || z_message_topic(message) == 3u) {
        ++test->handled;
        return (ZAction){ Z_ACTION_HANDLED, NULL };
    }
    return (ZAction){ Z_ACTION_UNHANDLED, NULL };
}

static void test_end_to_end(void)
{
    ZPort port = { request_reschedule, idle };
    ZKernel kernel;
    z_kernel_init(&kernel, &port);

    ZStateNode root = { NULL, NULL, handle_message, NULL, NULL };
    TestContext context = { 0u };
    ZMachine machine;
    z_machine_init(&machine, &root, &context);

    ZMsg const *slots[4];
    ZMailbox mailbox;
    z_mailbox_init(&mailbox, slots, 4u);
    ZReactor reactor;
    z_reactor_init(&reactor, &(ZReactorConfig){ &machine, &mailbox, &context });
    ZReactorId id = z_reactor_register(&kernel, &reactor, 2u);
    z_reactor_start(&kernel, id);

    _Alignas(ZEF_POOL_ALIGNMENT) unsigned char storage[256];
    ZMessagePool pool;
    z_message_pool_init(&pool, storage, sizeof(storage), 64u, 4u);
    ZMessagePoolSet set;
    z_message_pool_set_init(&set, &pool, 1u);

    ZMsg *message = NULL;
    uint32_t *payload = Z_MSG_BEGIN(&set, 1u, uint32_t, &message);
    *payload = 42u;
    z_mailbox_post(&mailbox, &message);
    CHECK(message == NULL);
    CHECK(z_kernel_run_one(&kernel) == Z_WORK_DONE);
    CHECK(context.handled == 1u);
    CHECK(z_kernel_run_one(&kernel) == Z_NO_WORK);

    ZMsg static_message;
    ZMsg *static_message_pointer = NULL;
    z_message_static_init(&static_message, 1u, NULL, 0u);
    static_message_pointer = &static_message;
    z_mailbox_post(&mailbox, &static_message_pointer);
    CHECK(static_message_pointer == NULL);
    ZMsg const *taken = NULL;
    CHECK(z_mailbox_take(&mailbox, &taken) == Z_OK);
    CHECK(taken == &static_message);
    z_message_release(taken);

    ZClock clock;
    z_clock_init(&clock, &kernel);
    ZTimer timer;
    z_timer_init_topic(&timer, &clock, id, 1u);
    z_timer_arm(&timer, 1u, 0u);
    z_clock_tick(&clock);
    CHECK(z_mailbox_count(&mailbox) == 1u);
    CHECK(z_kernel_run_one(&kernel) == Z_WORK_DONE);
    CHECK(context.handled == 2u);

    /* Clearing pending delivery removes the timer message, not the ordinary entry. */
    ZMsg const *deferred_slots[1];
    ZMailbox deferred;
    z_mailbox_init(&deferred, deferred_slots, 1u);
    z_mailbox_postpone(&deferred, &static_message);
    ZTimer clear_pending_timer;
    z_timer_init_topic(&clear_pending_timer, &clock, id, 2u);
    z_timer_arm(&clear_pending_timer, 1u, 0u);
    z_clock_tick(&clock);
    CHECK(z_mailbox_count(&mailbox) == 1u);
    CHECK(z_timer_stop(&clear_pending_timer, 1u) == Z_OK);
    CHECK(z_mailbox_count(&mailbox) == 0u);
    CHECK(z_mailbox_take(&deferred, &taken) == Z_OK);
    CHECK(taken == &static_message);
    z_message_release(taken);
}

static void test_topic_ordering(void)
{
    ZPort port = { request_reschedule, idle };
    ZKernel kernel;
    z_kernel_init(&kernel, &port);

    ZStateNode root = { NULL, NULL, handle_message, NULL, NULL };
    TestContext first_context = { 0u };
    TestContext second_context = { 0u };
    ZMachine first_machine;
    ZMachine second_machine;
    z_machine_init(&first_machine, &root, &first_context);
    z_machine_init(&second_machine, &root, &second_context);

    ZMsg const *first_slots[2];
    ZMsg const *second_slots[2];
    ZMailbox first_mailbox;
    ZMailbox second_mailbox;
    z_mailbox_init(&first_mailbox, first_slots, 2u);
    z_mailbox_init(&second_mailbox, second_slots, 2u);
    ZReactor first;
    ZReactor second;
    z_reactor_init(&first, &(ZReactorConfig){ &first_machine, &first_mailbox, &first_context });
    z_reactor_init(&second, &(ZReactorConfig){ &second_machine, &second_mailbox, &second_context });
    ZReactorId first_id = z_reactor_register(&kernel, &first, 1u);
    ZReactorId second_id = z_reactor_register(&kernel, &second, 3u);
    z_reactor_start(&kernel, first_id);
    z_reactor_start(&kernel, second_id);

    ZTopicTable topics;
    z_topic_table_init(&topics, &kernel);
    z_topic_subscribe(&topics, 3u, first_id);
    z_topic_subscribe(&topics, 3u, second_id);

    _Alignas(ZEF_POOL_ALIGNMENT) unsigned char storage[128];
    ZMessagePool pool;
    z_message_pool_init(&pool, storage, sizeof(storage), 64u, 2u);
    ZMessagePoolSet set;
    z_message_pool_set_init(&set, &pool, 1u);
    ZMsg *message = NULL;
    (void)Z_MSG_BEGIN(&set, 3u, uint32_t, &message);
    CHECK(z_topic_publish(&kernel, &topics, &message) == 2u);
    CHECK(message == NULL);
    CHECK(z_kernel_run_one(&kernel) == Z_WORK_DONE);
    CHECK(second_context.handled == 1u);
    CHECK(first_context.handled == 0u);
    CHECK(z_kernel_run_one(&kernel) == Z_WORK_DONE);
    CHECK(first_context.handled == 1u);
}

int main(void)
{
    test_end_to_end();
    test_topic_ordering();
    return 0;
}
