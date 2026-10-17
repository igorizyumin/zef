#include "zef_internal.h"

/* @relation(ZEF-IF-023, scope=actor-composition) */
void z_actor_init(ZActor *actor, ZStateNode const *root, void *context,
                  ZMsg const **mailbox_slots, uint16_t mailbox_capacity)
{
    ZEF_REQUIRE(actor != NULL && root != NULL && mailbox_slots != NULL &&
                    mailbox_capacity != 0u,
                Z_PANIC_CONFIGURATION, actor, mailbox_capacity);

    z_machine_init(&actor->machine, root, context);
    z_mailbox_init(&actor->mailbox, mailbox_slots, mailbox_capacity);
    z_reactor_init(&actor->reactor, &(ZReactorConfig){
        &actor->machine, &actor->mailbox, context
    });
}

ZReactorId z_actor_register(ZKernel *kernel, ZActor *actor, ZReactorId id)
{
    ZEF_REQUIRE(actor != NULL, Z_PANIC_INVALID_ARGUMENT, actor, id);
    return z_reactor_register(kernel, &actor->reactor, id);
}

void z_actor_start(ZActor *actor)
{
    ZEF_REQUIRE(actor != NULL && actor->reactor.initialized &&
                    actor->reactor.registered && actor->reactor.kernel != NULL,
                Z_PANIC_LIFECYCLE, actor, 0u);
    z_reactor_start(actor->reactor.kernel, actor->reactor.id);
}
