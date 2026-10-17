#include "zef_internal.h"

static void zef_timer_validate(ZTimer const *timer)
{
    ZEF_REQUIRE(timer != NULL && timer->initialized && timer->clock != NULL &&
                    timer->clock->initialized,
                Z_PANIC_INVALID_ARGUMENT, timer, 0u);
}

static void zef_timer_validate_destination(ZClock const *clock, ZReactorId destination)
{
    ZEF_REQUIRE(zef_kernel_is_registered(clock->kernel, destination),
                Z_PANIC_INVALID_ARGUMENT, clock, destination);
}

static void zef_timer_unlink(ZTimer *timer)
{
    ZClock *clock = timer->clock;
    ZTimer *previous = NULL;
    ZTimer *cursor = clock->timers;
    while (cursor != NULL && cursor != timer) {
        previous = cursor;
        cursor = cursor->next;
    }
    if (cursor == NULL) {
        ZEF_PANIC(Z_PANIC_INTERNAL, timer, 0u);
    }
    if (previous == NULL) {
        clock->timers = cursor->next;
    } else {
        previous->next = cursor->next;
    }
    if (clock->timer_tail == timer) {
        clock->timer_tail = previous;
    }
    timer->next = NULL;
}

static void zef_timer_link_tail(ZTimer *timer)
{
    ZClock *clock = timer->clock;
    timer->next = NULL;
    if (clock->timer_tail == NULL) {
        clock->timers = timer;
        clock->timer_tail = timer;
    } else {
        clock->timer_tail->next = timer;
        clock->timer_tail = timer;
    }
}

static void zef_timer_maybe_release_base(ZTimer *timer)
{
    if (timer->cancelled && timer->base_claim) {
        timer->base_claim = 0u;
        z_message_release(timer->message);
    }
}

static ZBool zef_timer_message_matches(ZMsg const *message, void *context)
{
    ZTimer const *timer = (ZTimer const *)context;
    return message == timer->message;
}

static uint16_t zef_timer_remove_pending(ZTimer *timer);

/* @relation(ZEF-IF-032, scope=clock-and-timers) */
void z_clock_init(ZClock *clock, ZKernel *kernel)
{
    ZEF_REQUIRE(clock != NULL && kernel != NULL && kernel->initialized,
                Z_PANIC_CONFIGURATION, clock, 0u);
    memset(clock, 0, sizeof(*clock));
    clock->kernel = kernel;
    clock->initialized = 1u;
}

void z_timer_init_topic(ZTimer *timer, ZClock *clock, ZReactorId destination,
                        ZTopic topic)
{
    ZEF_REQUIRE(timer != NULL && clock != NULL && clock->initialized,
                Z_PANIC_CONFIGURATION, timer, 0u);
    zef_timer_validate_destination(clock, destination);
    memset(timer, 0, sizeof(*timer));
    z_message_static_init(&timer->topic_message, topic, NULL, 0u);
    timer->clock = clock;
    timer->destination = destination;
    timer->message = &timer->topic_message;
    timer->initialized = 1u;
}

void z_timer_init_payload(ZTimer *timer, ZClock *clock, ZReactorId destination,
                          ZMessagePoolSet *pool_set, ZTopic topic,
                          void const *payload, size_t payload_size)
{
    ZEF_REQUIRE(timer != NULL && clock != NULL && clock->initialized &&
                    pool_set != NULL && (payload != NULL || payload_size == 0u),
                Z_PANIC_CONFIGURATION, timer, payload_size);
    zef_timer_validate_destination(clock, destination);

    memset(timer, 0, sizeof(*timer));
    ZMsg *message = NULL;
    void *message_payload = z_message_begin(pool_set, topic, payload_size, &message);
    if (payload_size != 0u) {
        memcpy(message_payload, payload, payload_size);
    }
    timer->clock = clock;
    timer->destination = destination;
    timer->message = message;
    timer->initialized = 1u;
    timer->base_claim = 1u;
}

void z_timer_arm(ZTimer *timer, ZTick delay, ZTick period)
{
    zef_timer_validate(timer);
    ZCriticalState state = z_port_critical_enter();
    ZEF_REQUIRE(!timer->cancelled, Z_PANIC_STATE, timer, 0u);
    ZEF_REQUIRE(!timer->armed, Z_PANIC_STATE, timer, 0u);
    ZEF_REQUIRE(delay <= ZEF_MAX_TIMER_SPAN && period <= ZEF_MAX_TIMER_SPAN,
                Z_PANIC_OVERFLOW, timer,
                delay > ZEF_MAX_TIMER_SPAN ? delay : period);
    timer->period = period;
    timer->due = timer->clock->now + (delay == 0u ? (ZTick)1u : delay);
    timer->armed = 1u;
    zef_timer_link_tail(timer);
    z_port_critical_leave(state);
}

ZResult z_timer_stop(ZTimer *timer)
{
    zef_timer_validate(timer);
    ZCriticalState state = z_port_critical_enter();
    if (!timer->armed) {
        z_port_critical_leave(state);
        return Z_NOT_RUNNING;
    }
    zef_timer_unlink(timer);
    timer->armed = 0u;
    z_port_critical_leave(state);
    return Z_OK;
}

ZResult z_timer_cancel(ZTimer *timer)
{
    zef_timer_validate(timer);
    ZCriticalState state = z_port_critical_enter();
    if (timer->cancelled) {
        z_port_critical_leave(state);
        return Z_NOT_RUNNING;
    }
    if (!timer->armed && !timer->base_claim) {
        uint16_t removed = zef_timer_remove_pending(timer);
        if (removed == 0u) {
            z_port_critical_leave(state);
            return Z_NOT_RUNNING;
        }
        timer->cancelled = 1u;
        z_port_critical_leave(state);
        return Z_OK;
    }
    if (timer->armed) {
        zef_timer_unlink(timer);
        timer->armed = 0u;
    }
    timer->cancelled = 1u;
    zef_timer_remove_pending(timer);
    zef_timer_maybe_release_base(timer);
    z_port_critical_leave(state);
    return Z_OK;
}

static uint16_t zef_timer_remove_pending(ZTimer *timer)
{
    ZCriticalState state = z_port_critical_enter();
    ZReactor *reactor = zef_kernel_reactor(timer->clock->kernel, timer->destination);
    if (reactor == NULL) {
        ZEF_PANIC(Z_PANIC_INVALID_ARGUMENT, timer, timer->destination);
    }
    uint16_t removed = z_mailbox_remove_if(reactor->mailbox,
                                           zef_timer_message_matches, timer);
    z_port_critical_leave(state);
    return removed;
}

void z_clock_tick(ZClock *clock)
{
    ZEF_REQUIRE(clock != NULL && clock->initialized,
                Z_PANIC_INVALID_ARGUMENT, clock, 0u);
    ZCriticalState state = z_port_critical_enter();
    ++clock->now;
    ZTimer *cursor = clock->timers;
    while (cursor != NULL) {
        ZTimer *next = cursor->next;
        if (cursor->armed && cursor->due == clock->now) {
            ZReactor *reactor = zef_kernel_reactor(clock->kernel, cursor->destination);
            if (reactor == NULL) {
                ZEF_PANIC(Z_PANIC_INVALID_ARGUMENT, cursor, cursor->destination);
            }
            if (zef_mailbox_full(reactor->mailbox)) {
                ZEF_PANIC(Z_PANIC_MAILBOX_FULL, reactor->mailbox, cursor->destination);
            }
            if (cursor->message->kind == Z_MSG_KIND_DYNAMIC) {
                z_message_retain(cursor->message);
            }
            zef_mailbox_enqueue_message(reactor->mailbox, cursor->message, 0u);
            if (cursor->period == 0u) {
                zef_timer_unlink(cursor);
                cursor->armed = 0u;
            } else {
                cursor->due = clock->now + cursor->period;
                zef_timer_unlink(cursor);
                zef_timer_link_tail(cursor);
            }
        }
        cursor = next;
    }
    z_port_critical_leave(state);
}
