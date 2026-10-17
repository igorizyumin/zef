#ifndef DEMO_APP_H
#define DEMO_APP_H

#include "demo_serial_actor.h"
#include "demo_timer_source.h"
#include "demo_worker_actor.h"

enum {
    DEMO_WORKER_COUNT = 3u,
    DEMO_SERIAL_REACTOR_ID = 7u,
    DEMO_SLOW_WORKER_INDEX = 2u,
    DEMO_DIRECT_TIMER_COUNT = DEMO_SLOW_WORKER_INDEX,
    DEMO_CLOCK_TICK_MS = 10u,
    DEMO_SLOW_PERIOD_TICKS = 100u
};

typedef struct {
    ZKernel kernel;
    ZMessagePool message_pool;
    ZMessagePoolSet message_pools;
    ZClock clock;
    ZTimer slow_timer;
    DemoWorkerActor workers[DEMO_WORKER_COUNT];
    DemoSerialActor serial;
    DemoTimerSource timers[DEMO_WORKER_COUNT];
} DemoApp;

void demo_app_init(DemoApp *app, ZPort const *port);
void demo_app_post_banner(DemoApp *app);
void demo_app_start_timers(DemoApp *app);
void demo_app_clock_tick_isr(DemoApp *app);

#endif
