#ifndef DEMO_SERIAL_ACTOR_H
#define DEMO_SERIAL_ACTOR_H

#include "demo_messages.h"

typedef struct {
    ZActor actor;
    ZMailbox deferred_mailbox;
    ZMsg const *mailbox_slots[8];
    ZMsg const *deferred_slots[16];
    ZMsg const *active_message;
} DemoSerialActor;

void demo_serial_actor_init(DemoSerialActor *serial);
void demo_serial_actor_dma_complete_isr(DemoSerialActor *serial);

#endif
