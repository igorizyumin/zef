#ifndef DEMO_TIMER_SOURCE_H
#define DEMO_TIMER_SOURCE_H

#include "demo_messages.h"
#include "nucleo_l073rz_hw.h"

typedef struct {
    NucleoTimerId timer;
    uint32_t interval_ms;
    ZTopic topic;
    ZMailbox *destination;
    volatile uint32_t sequence;
} DemoTimerSource;

void demo_timer_source_init(DemoTimerSource *source,
                            NucleoTimerId timer,
                            uint32_t interval_ms,
                            ZTopic topic,
                            ZMailbox *destination);
void demo_timer_sources_start(DemoTimerSource *sources, uint16_t count);
void demo_timer_source_irq(DemoTimerSource *source);

#endif
