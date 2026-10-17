#include "demo_app.h"

#include <string.h>

enum {
    DEMO_MESSAGE_BLOCK_SIZE = 128u,
    DEMO_MESSAGE_BLOCK_COUNT = 24u
};

static _Alignas(ZEF_POOL_ALIGNMENT)
    unsigned char g_message_storage[DEMO_MESSAGE_BLOCK_SIZE *
                                    DEMO_MESSAGE_BLOCK_COUNT];

typedef struct {
    char const *name;
    ZTopic topic;
    NucleoTimerId timer;
    uint32_t period_ms;
    ZReactorId reactor_id;
} DemoWorkerConfig;

static DemoWorkerConfig const g_worker_config[DEMO_WORKER_COUNT] = {
    { .name = "FAST", .topic = TOPIC_FAST_TICK, .timer = NUCLEO_TIMER_2,
      .period_ms = 137u, .reactor_id = 1u },
    { .name = "MEDIUM", .topic = TOPIC_MEDIUM_TICK, .timer = NUCLEO_TIMER_6,
      .period_ms = 293u, .reactor_id = 2u },
    { .name = "SLOW", .topic = TOPIC_SLOW_TICK, .timer = NUCLEO_TIMER_7,
      .period_ms = DEMO_SLOW_PERIOD_TICKS * DEMO_CLOCK_TICK_MS,
      .reactor_id = 3u }
};

void demo_app_init(DemoApp *app, ZPort const *port)
{
    ZEF_REQUIRE(app != NULL && port != NULL,
                Z_PANIC_INVALID_ARGUMENT, app, 0u);
    memset(app, 0, sizeof(*app));
    z_kernel_init(&app->kernel, port);
    z_message_pool_init(&app->message_pool, g_message_storage,
                        sizeof(g_message_storage), DEMO_MESSAGE_BLOCK_SIZE,
                        DEMO_MESSAGE_BLOCK_COUNT);
    z_message_pool_set_init(&app->message_pools, &app->message_pool, 1u);
    z_message_factory_init(&app->message_pools);
    demo_serial_actor_init(&app->serial);

    for (uint16_t index = 0u; index < DEMO_WORKER_COUNT; ++index) {
        DemoWorkerConfig const *config = &g_worker_config[index];
        demo_worker_actor_init(&app->workers[index], &app->serial,
                               config->name, config->topic,
                               config->reactor_id, config->period_ms);
        (void)z_actor_register(&app->kernel, &app->workers[index].actor,
                               config->reactor_id);
    }
    (void)z_actor_register(&app->kernel, &app->serial.actor,
                           DEMO_SERIAL_REACTOR_ID);

    z_clock_init(&app->clock, &app->kernel);
    z_timer_init_topic(&app->slow_timer, &app->clock,
                       g_worker_config[DEMO_SLOW_WORKER_INDEX].reactor_id,
                       TOPIC_SLOW_TICK);

    for (uint16_t index = 0u; index < DEMO_WORKER_COUNT; ++index) {
        z_actor_start(&app->workers[index].actor);
    }
    z_actor_start(&app->serial.actor);

    for (uint16_t index = 0u; index < DEMO_DIRECT_TIMER_COUNT; ++index) {
        DemoWorkerConfig const *config = &g_worker_config[index];
        demo_timer_source_init(&app->timers[index], config->timer,
                               config->period_ms, config->topic,
                               &app->workers[index].actor.mailbox);
    }
}

void demo_app_post_banner(DemoApp *app)
{
    static char const banner[] = "ZEF Nucleo-L073RZ demo ready\r\n";
    ZMsg *message = NULL;
    SerialLine *line = Z_MSG_NEW(TOPIC_SERIAL_LINE, SerialLine, &message);
    memcpy(line->text, banner, sizeof(banner) - 1u);
    line->length = sizeof(banner) - 1u;
    Z_MSG_POST(&app->serial.actor, message);
}

void demo_app_start_timers(DemoApp *app)
{
    z_timer_arm(&app->slow_timer, DEMO_SLOW_PERIOD_TICKS,
                DEMO_SLOW_PERIOD_TICKS);
    demo_timer_sources_start(app->timers, DEMO_DIRECT_TIMER_COUNT);
    nucleo_timer_start(NUCLEO_TIMER_7, DEMO_CLOCK_TICK_MS);
}

void demo_app_clock_tick_isr(DemoApp *app)
{
    if (nucleo_timer_irq_status(NUCLEO_TIMER_7) != 0u) {
        nucleo_timer_irq_clear(NUCLEO_TIMER_7);
        z_clock_tick(&app->clock);
    }
}
