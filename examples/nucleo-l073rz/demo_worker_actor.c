#include "demo_worker_actor.h"

static uint16_t append_text(char *buffer, uint16_t at, uint16_t capacity,
                            char const *text)
{
    while (*text != '\0' && at < capacity) {
        buffer[at++] = *text++;
    }
    return at;
}

static uint16_t append_u32(char *buffer, uint16_t at, uint16_t capacity,
                           uint32_t value)
{
    char reversed[10];
    uint16_t count = 0u;
    do {
        reversed[count++] = (char)('0' + (value % 10u));
        value /= 10u;
    } while (value != 0u && count < sizeof(reversed));
    while (count != 0u && at < capacity) {
        buffer[at++] = reversed[--count];
    }
    return at;
}

/* -------------------------------------------------------------------------- */
/* Worker state                                                               */
/* -------------------------------------------------------------------------- */

Z_STATE_HANDLER(DemoWorkerActor, worker_handle)
{
    if (z_message_topic(message) != ctx->topic) {
        return z_action_unhandled();
    }

    TimerEvent const *event = Z_MSG_PAYLOAD(message, TimerEvent);
    ZMsg *line_message = NULL;
    SerialLine *line = Z_MSG_NEW(TOPIC_SERIAL_LINE, SerialLine, &line_message);
    uint16_t at = 0u;
    at = append_text(line->text, at, sizeof(line->text), ctx->name);
    at = append_text(line->text, at, sizeof(line->text), " event seq=");
    at = append_u32(line->text, at, sizeof(line->text), event->sequence);
    at = append_text(line->text, at, sizeof(line->text), " period=");
    at = append_u32(line->text, at, sizeof(line->text), event->interval_ms);
    at = append_text(line->text, at, sizeof(line->text), "ms prio=");
    at = append_u32(line->text, at, sizeof(line->text), ctx->priority);
    at = append_text(line->text, at, sizeof(line->text), "\r\n");
    line->length = at;
    Z_MSG_POST(&ctx->serial->actor, line_message);
    ++ctx->handled;
    return z_action_handled();
}

static ZStateNode const g_worker_state = {
    .handle = worker_handle_zef_adapter
};

void demo_worker_actor_init(DemoWorkerActor *worker,
                            DemoSerialActor *serial,
                            char const *name,
                            ZTopic topic,
                            ZReactorId priority)
{
    ZEF_REQUIRE(worker != NULL && serial != NULL && name != NULL,
                Z_PANIC_INVALID_ARGUMENT, worker, 0u);
    worker->serial = serial;
    worker->name = name;
    worker->topic = topic;
    worker->priority = priority;
    worker->handled = 0u;
    z_actor_init(&worker->actor, &g_worker_state, worker,
                 worker->mailbox_slots, 8u);
}
