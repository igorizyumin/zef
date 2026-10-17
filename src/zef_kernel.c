#include "zef_internal.h"

static void zef_kernel_validate(ZKernel const *kernel)
{
    ZEF_REQUIRE(kernel != NULL && kernel->initialized,
                Z_PANIC_INVALID_ARGUMENT, kernel, 0u);
}

ZReactor *zef_kernel_reactor(ZKernel *kernel, ZReactorId id)
{
    if (kernel == NULL || !kernel->initialized || id >= kernel->reactor_limit ||
        kernel->reactors[id] == NULL) {
        return NULL;
    }
    return kernel->reactors[id];
}

ZBool zef_kernel_is_registered(ZKernel const *kernel, ZReactorId id)
{
    return kernel != NULL && kernel->initialized && id < kernel->reactor_limit &&
           kernel->reactors[id] != NULL && kernel->reactors[id]->registered;
}

void zef_kernel_mailbox_changed(ZMailbox *mailbox)
{
    if (mailbox == NULL || mailbox->owner == NULL) {
        return;
    }
    ZReactor *reactor = mailbox->owner;
    if (reactor->kernel == NULL || !reactor->kernel->initialized) {
        return;
    }
    ZKernel *kernel = reactor->kernel;
    if (reactor->mailbox != mailbox) {
        ZEF_PANIC(Z_PANIC_INTERNAL, mailbox, reactor->id);
    }
    if (zef_kernel_reactor(kernel, reactor->id) != reactor) {
        ZEF_PANIC(Z_PANIC_INTERNAL, mailbox, reactor->id);
    }
    uint32_t bit = (uint32_t)1u << reactor->id;
    if (reactor->started && z_mailbox_count(mailbox) != 0u) {
        kernel->runnable |= bit;
    } else {
        kernel->runnable &= ~bit;
    }
    /*
     * A shared-stack Port only needs an activation request when this mailbox
     * can produce work above the currently active priority.  Lower and equal
     * priorities remain ready, but cannot preempt the current activation.
     * Outside a dispatch there is no active priority, so any newly runnable
     * Reactor can wake the Kernel.
     */
    if (reactor->started && z_mailbox_count(mailbox) != 0u &&
        (!kernel->dispatching || reactor->priority > kernel->active_priority) &&
        kernel->port != NULL && kernel->port->request_reschedule != NULL) {
        kernel->port->request_reschedule();
    }
}

/* @relation(ZEF-IF-022, scope=reactor-lifecycle) */
void z_reactor_init(ZReactor *reactor, ZReactorConfig const *config)
{
    ZEF_REQUIRE(reactor != NULL && config != NULL && config->machine != NULL &&
                    config->mailbox != NULL && config->mailbox->initialized &&
                    config->machine->initialized,
                Z_PANIC_CONFIGURATION, reactor, 0u);
    reactor->machine = config->machine;
    reactor->mailbox = config->mailbox;
    reactor->context = config->context;
    reactor->kernel = NULL;
    reactor->id = 0u;
    reactor->priority = 0u;
    reactor->initialized = 1u;
    reactor->registered = 0u;
    reactor->started = 0u;
}

ZReactorId z_reactor_register(ZKernel *kernel, ZReactor *reactor, ZReactorId id)
{
    zef_kernel_validate(kernel);
    ZEF_REQUIRE(reactor != NULL && reactor->initialized && !reactor->registered,
                Z_PANIC_LIFECYCLE, reactor, id);
    ZEF_REQUIRE(id < Z_MAX_REACTORS,
                Z_PANIC_INVALID_ARGUMENT, reactor, id);
    ZEF_REQUIRE(kernel->reactors[id] == NULL &&
                    kernel->reactor_count < Z_MAX_REACTORS,
                Z_PANIC_LIFECYCLE, reactor, id);
    ZEF_REQUIRE(reactor->mailbox->owner == NULL,
                Z_PANIC_LIFECYCLE, reactor->mailbox, id);
    ++kernel->reactor_count;
    if (id >= kernel->reactor_limit) {
        kernel->reactor_limit = (uint16_t)id + 1u;
    }
    reactor->kernel = kernel;
    reactor->id = id;
    reactor->priority = (ZPriority)id;
    reactor->registered = 1u;
    kernel->reactors[id] = reactor;
    reactor->mailbox->owner = reactor;
    return id;
}

void z_reactor_start(ZKernel *kernel, ZReactorId id)
{
    zef_kernel_validate(kernel);
    ZReactor *reactor = zef_kernel_reactor(kernel, id);
    ZEF_REQUIRE(reactor != NULL && reactor->registered && !reactor->started,
                Z_PANIC_LIFECYCLE, kernel, id);
    reactor->started = 1u;
    z_machine_start(reactor->machine);
    zef_kernel_mailbox_changed(reactor->mailbox);
}

/* @relation(ZEF-IF-030, scope=cooperative-kernel) */
void z_kernel_init(ZKernel *kernel, ZPort const *port)
{
    ZEF_REQUIRE(kernel != NULL && port != NULL,
                Z_PANIC_CONFIGURATION, kernel, 0u);
    memset(kernel, 0, sizeof(*kernel));
    kernel->port = port;
    kernel->initialized = 1u;
}

static ZReactor *zef_kernel_select(ZKernel *kernel, ZBool higher_only)
{
    for (ZReactorId id = kernel->reactor_limit; id != 0u; ) {
        --id;
        uint32_t bit = (uint32_t)1u << id;
        if ((kernel->runnable & bit) == 0u) {
            continue;
        }
        ZReactor *candidate = kernel->reactors[id];
        if (candidate == NULL || z_mailbox_count(candidate->mailbox) == 0u ||
            !candidate->started) {
            kernel->runnable &= ~bit;
            continue;
        }
        if (higher_only && candidate->priority <= kernel->active_priority) {
            continue;
        }
        return candidate;
    }
    return NULL;
}

/* @relation(ZEF-IMP-072, scope=shared-stack-activation-eligibility) */
static ZRunResult zef_kernel_run_one_internal(ZKernel *kernel, ZBool higher_only)
{
    zef_kernel_validate(kernel);
    ZCriticalState state = z_port_critical_enter();
    ZReactor *reactor = zef_kernel_select(kernel, higher_only);
    if (reactor == NULL) {
        z_port_critical_leave(state);
        return Z_NO_WORK;
    }

    ZMsg const *message = NULL;
    if (z_mailbox_take(reactor->mailbox, &message) != Z_OK) {
        kernel->runnable &= ~((uint32_t)1u << reactor->id);
        z_port_critical_leave(state);
        return Z_NO_WORK;
    }
    z_port_critical_leave(state);
    ZBool previous_dispatching = kernel->dispatching;
    ZPriority previous_priority = kernel->active_priority;
    kernel->dispatching = 1u;
    kernel->active_priority = reactor->priority;
    z_machine_dispatch(reactor->machine, message);
    z_message_release(message);
    zef_kernel_mailbox_changed(reactor->mailbox);
    kernel->dispatching = previous_dispatching;
    kernel->active_priority = previous_priority;
    return Z_WORK_DONE;
}

ZRunResult z_kernel_run_one(ZKernel *kernel)
{
    return zef_kernel_run_one_internal(kernel, 0u);
}

void zef_kernel_activate_higher(ZKernel *kernel)
{
    zef_kernel_validate(kernel);
    ZBool higher_only = kernel->dispatching;
    while (zef_kernel_run_one_internal(kernel, higher_only) == Z_WORK_DONE) {
        /* Continue until no eligible Reactor is ready. */
    }
}

void z_kernel_run_forever(ZKernel *kernel)
{
    zef_kernel_validate(kernel);
    for (;;) {
        if (z_kernel_run_one(kernel) == Z_NO_WORK) {
            if (kernel->port->idle != NULL) {
                kernel->port->idle();
            }
        }
    }
}

ZResult z_kernel_current_priority(ZKernel const *kernel, ZPriority *priority_out)
{
    zef_kernel_validate(kernel);
    if (!kernel->dispatching) {
        return Z_NOT_RUNNING;
    }
    ZEF_REQUIRE(priority_out != NULL,
                Z_PANIC_INVALID_ARGUMENT, kernel, 0u);
    *priority_out = kernel->active_priority;
    return Z_OK;
}

/* @relation(ZEF-IF-031, scope=topic-subscriptions-and-publication) */
void z_topic_table_init(ZTopicTable *table, ZKernel *kernel)
{
    ZEF_REQUIRE(table != NULL && kernel != NULL && kernel->initialized,
                Z_PANIC_CONFIGURATION, table, 0u);
    memset(table, 0, sizeof(*table));
    table->kernel = kernel;
    table->initialized = 1u;
}

static void zef_topic_validate(ZTopicTable const *table, ZTopic topic, ZReactorId id)
{
    ZEF_REQUIRE(table != NULL && table->initialized && table->kernel != NULL &&
                    topic < Z_MAX_TOPICS &&
                    zef_kernel_is_registered(table->kernel, id),
                Z_PANIC_INVALID_ARGUMENT, table,
                ((uintptr_t)topic << 16u) | id);
}

void z_topic_subscribe(ZTopicTable *table, ZTopic topic, ZReactorId reactor_id)
{
    zef_topic_validate(table, topic, reactor_id);
    ZCriticalState state = z_port_critical_enter();
    table->subscribers[topic] |= (uint32_t)1u << reactor_id;
    z_port_critical_leave(state);
}

ZResult z_topic_unsubscribe(ZTopicTable *table, ZTopic topic, ZReactorId reactor_id)
{
    zef_topic_validate(table, topic, reactor_id);
    ZCriticalState state = z_port_critical_enter();
    uint32_t bit = (uint32_t)1u << reactor_id;
    if ((table->subscribers[topic] & bit) == 0u) {
        z_port_critical_leave(state);
        return Z_NOT_FOUND;
    }
    table->subscribers[topic] &= ~bit;
    z_port_critical_leave(state);
    return Z_OK;
}

uint16_t z_topic_publish(ZKernel *kernel, ZTopicTable *table, ZMsg **message)
{
    ZEF_REQUIRE(table != NULL && table->initialized && table->kernel == kernel &&
                    message != NULL && *message != NULL && (*message)->valid &&
                    (*message)->kind == Z_MSG_KIND_DYNAMIC &&
                    (*message)->pool != NULL && (*message)->topic < Z_MAX_TOPICS,
                Z_PANIC_INVALID_ARGUMENT, table, 0u);
    ZMsg *handle = *message;
    *message = NULL;
    uint16_t count = 0u;
    ZCriticalState state = z_port_critical_enter();
    uint32_t bits = table->subscribers[handle->topic];
    for (ZReactorId id = kernel->reactor_limit; id != 0u; ) {
        --id;
        if ((bits & ((uint32_t)1u << id)) == 0u) {
            continue;
        }
        ZReactor *reactor = zef_kernel_reactor(kernel, id);
        if (zef_mailbox_full(reactor->mailbox)) {
            ZEF_PANIC(Z_PANIC_MAILBOX_FULL, reactor->mailbox, id);
        }
        zef_mailbox_enqueue_message(reactor->mailbox, handle, count != 0u);
        ++count;
    }
    if (count == 0u) {
        z_message_release(handle);
    }
    z_port_critical_leave(state);
    return count;
}
