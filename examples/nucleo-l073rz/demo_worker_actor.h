#ifndef DEMO_WORKER_ACTOR_H
#define DEMO_WORKER_ACTOR_H

#include "demo_messages.h"
#include "demo_serial_actor.h"

typedef struct {
    ZActor actor;
    ZMsg const *mailbox_slots[8];
    DemoSerialActor *serial;
    char const *name;
    ZTopic topic;
    ZReactorId priority;
    uint32_t handled;
} DemoWorkerActor;

void demo_worker_actor_init(DemoWorkerActor *worker,
                            DemoSerialActor *serial,
                            char const *name,
                            ZTopic topic,
                            ZReactorId priority);

#endif
