#include "demo_timer_source.h"

void demo_timer_source_init(DemoTimerSource *source,
                            NucleoTimerId timer,
                            uint32_t interval_ms,
                            ZTopic topic,
                            ZMailbox *destination)
{
    ZEF_REQUIRE(source != NULL && destination != NULL &&
                    interval_ms != 0u,
                Z_PANIC_INVALID_ARGUMENT, source, 0u);
    source->timer = timer;
    source->interval_ms = interval_ms;
    source->topic = topic;
    source->destination = destination;
    source->sequence = 0u;
}

void demo_timer_sources_start(DemoTimerSource *sources, uint16_t count)
{
    ZEF_REQUIRE(sources != NULL && count != 0u,
                Z_PANIC_INVALID_ARGUMENT, sources, count);
    for (uint16_t index = 0u; index < count; ++index) {
        nucleo_timer_start(sources[index].timer, sources[index].interval_ms);
    }
    nucleo_timer_irqs_enable();
}

void demo_timer_source_irq(DemoTimerSource *source)
{
    if (nucleo_timer_irq_status(source->timer) != 0u) {
        nucleo_timer_irq_clear(source->timer);
        ZMsg *message = NULL;
        TimerEvent *event = Z_MSG_NEW(source->topic, TimerEvent, &message);
        event->sequence = ++source->sequence;
        event->interval_ms = source->interval_ms;
        z_mailbox_post(source->destination, &message);
    }
}

