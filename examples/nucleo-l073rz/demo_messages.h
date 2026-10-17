#ifndef DEMO_MESSAGES_H
#define DEMO_MESSAGES_H

#include <stdint.h>

#include "zef.h"

enum {
    TOPIC_FAST_TICK = 1u,
    TOPIC_MEDIUM_TICK = 2u,
    TOPIC_SLOW_TICK = 3u,
    TOPIC_SERIAL_LINE = 10u,
    TOPIC_SERIAL_DMA_COMPLETE = 11u
};

typedef struct {
    uint32_t sequence;
    uint32_t interval_ms;
} TimerEvent;

typedef struct {
    uint16_t length;
    char text[62];
} SerialLine;

#endif
