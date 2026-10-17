#include "zef_internal.h"

static uint32_t zef_mailbox_count32(ZMailbox const *mailbox)
{
    return mailbox->tail - mailbox->head;
}

static void zef_mailbox_validate(ZMailbox const *mailbox)
{
    ZEF_REQUIRE(mailbox != NULL && mailbox->initialized && mailbox->slots != NULL &&
                    mailbox->capacity != 0u &&
                    mailbox->capacity <= Z_MAX_MAILBOX_CAPACITY,
                Z_PANIC_INVALID_ARGUMENT, mailbox, 0u);
}

static ZCriticalState zef_mailbox_enter(ZMailbox const *mailbox)
{
    zef_mailbox_validate(mailbox);
    return z_port_critical_enter();
}

static void zef_mailbox_leave(ZMailbox const *mailbox, ZCriticalState state)
{
    (void)mailbox;
    z_port_critical_leave(state);
}

ZBool zef_mailbox_full(ZMailbox const *mailbox)
{
    zef_mailbox_validate(mailbox);
    return zef_mailbox_count32(mailbox) >= mailbox->capacity;
}

/* @relation(ZEF-IF-013, scope=bounded-ring-mailbox) */
void z_mailbox_init(ZMailbox *mailbox, ZMsg const **slots, uint16_t capacity)
{
    ZEF_REQUIRE(mailbox != NULL && slots != NULL && capacity != 0u &&
                    capacity <= Z_MAX_MAILBOX_CAPACITY,
                Z_PANIC_CONFIGURATION, mailbox, capacity);
    mailbox->slots = slots;
    mailbox->capacity = capacity;
    mailbox->head = 0u;
    mailbox->tail = 0u;
    mailbox->min_free = capacity;
    mailbox->owner = NULL;
    mailbox->initialized = 1u;
}

void zef_mailbox_enqueue_message(ZMailbox *mailbox, ZMsg const *message,
                                ZBool add_claim)
{
    zef_mailbox_validate(mailbox);
    ZEF_REQUIRE(zef_message_is_readable(message),
                Z_PANIC_LIFECYCLE, message, 0u);
    if (zef_mailbox_count32(mailbox) >= mailbox->capacity) {
        ZEF_PANIC(Z_PANIC_MAILBOX_FULL, mailbox, mailbox->capacity);
    }
    if (add_claim) {
        z_message_retain(message);
    }

    uint32_t position = mailbox->tail++;
    mailbox->slots[position % mailbox->capacity] = message;
    uint16_t free_space = (uint16_t)(mailbox->capacity - zef_mailbox_count32(mailbox));
    if (free_space < mailbox->min_free) {
        mailbox->min_free = free_space;
    }
    zef_kernel_mailbox_changed(mailbox);
}

static void zef_mailbox_enqueue_front_handle(ZMailbox *mailbox,
                                              ZMsg const *message)
{
    zef_mailbox_validate(mailbox);
    ZEF_REQUIRE(zef_message_is_readable(message),
                Z_PANIC_LIFECYCLE, message, 0u);
    if (zef_mailbox_count32(mailbox) >= mailbox->capacity) {
        ZEF_PANIC(Z_PANIC_MAILBOX_FULL, mailbox, mailbox->capacity);
    }
    uint32_t position = --mailbox->head;
    mailbox->slots[position % mailbox->capacity] = message;
    uint16_t free_space = (uint16_t)(mailbox->capacity - zef_mailbox_count32(mailbox));
    if (free_space < mailbox->min_free) {
        mailbox->min_free = free_space;
    }
    zef_kernel_mailbox_changed(mailbox);
}

static ZMsg *zef_consuming_message(ZMsg **message)
{
    ZEF_REQUIRE(message != NULL && *message != NULL && (*message)->valid &&
                    ((*message)->kind == Z_MSG_KIND_DYNAMIC ||
                     (*message)->kind == Z_MSG_KIND_STATIC),
                Z_PANIC_LIFECYCLE, message, 0u);
    ZMsg *consumed = *message;
    *message = NULL;
    return consumed;
}

void z_mailbox_post(ZMailbox *mailbox, ZMsg **message)
{
    ZCriticalState state = zef_mailbox_enter(mailbox);
    /* Check fullness before consuming the caller's reference. */
    if (zef_mailbox_count32(mailbox) >= mailbox->capacity) {
        ZEF_PANIC(Z_PANIC_MAILBOX_FULL, mailbox, mailbox->capacity);
    }
    ZMsg *handle = zef_consuming_message(message);
    zef_mailbox_enqueue_message(mailbox, handle, 0u);
    zef_mailbox_leave(mailbox, state);
}

void z_mailbox_post_front(ZMailbox *mailbox, ZMsg **message)
{
    ZCriticalState state = zef_mailbox_enter(mailbox);
    if (zef_mailbox_count32(mailbox) >= mailbox->capacity) {
        ZEF_PANIC(Z_PANIC_MAILBOX_FULL, mailbox, mailbox->capacity);
    }
    ZMsg *handle = zef_consuming_message(message);
    zef_mailbox_enqueue_front_handle(mailbox, handle);
    zef_mailbox_leave(mailbox, state);
}

void z_mailbox_postpone(ZMailbox *mailbox, ZMsg const *message)
{
    zef_mailbox_validate(mailbox);
    ZEF_REQUIRE(mailbox->owner == NULL,
                Z_PANIC_INVALID_ARGUMENT, mailbox, 0u);
    ZCriticalState state = z_port_critical_enter();
    if (zef_mailbox_count32(mailbox) >= mailbox->capacity) {
        ZEF_PANIC(Z_PANIC_MAILBOX_FULL, mailbox, mailbox->capacity);
    }
    zef_mailbox_enqueue_message(mailbox, message, 1u);
    zef_mailbox_leave(mailbox, state);
}

ZResult z_mailbox_recall(ZMailbox *destination, ZMailbox *source)
{
    ZEF_REQUIRE(destination != source,
                Z_PANIC_INVALID_ARGUMENT, destination, 0u);
    ZCriticalState state = z_port_critical_enter();
    zef_mailbox_validate(destination);
    zef_mailbox_validate(source);
    ZEF_REQUIRE(source->owner == NULL,
                Z_PANIC_INVALID_ARGUMENT, source, 0u);
    if (zef_mailbox_count32(source) == 0u) {
        z_port_critical_leave(state);
        return Z_EMPTY;
    }
    if (zef_mailbox_count32(destination) >= destination->capacity) {
        ZEF_PANIC(Z_PANIC_MAILBOX_FULL, destination, destination->capacity);
    }

    /* Recall the newest deferred entry and put it ahead of the destination's
     * next entry. Queue ownership is transferred without changing claims. */
    uint32_t source_position = --source->tail;
    uint16_t source_slot = (uint16_t)(source_position % source->capacity);
    ZMsg const *message = source->slots[source_slot];
    source->slots[source_slot] = NULL;
    uint32_t destination_position = --destination->head;
    destination->slots[destination_position % destination->capacity] = message;

    uint16_t destination_free = (uint16_t)(destination->capacity -
                                           zef_mailbox_count32(destination));
    if (destination_free < destination->min_free) {
        destination->min_free = destination_free;
    }
    zef_kernel_mailbox_changed(destination);
    z_port_critical_leave(state);
    return Z_OK;
}

ZResult z_mailbox_take(ZMailbox *mailbox, ZMsg const **message_out)
{
    ZEF_REQUIRE(message_out != NULL,
                Z_PANIC_INVALID_ARGUMENT, mailbox, 0u);
    ZCriticalState state = zef_mailbox_enter(mailbox);
    if (zef_mailbox_count32(mailbox) == 0u) {
        zef_mailbox_leave(mailbox, state);
        return Z_EMPTY;
    }
    uint32_t position = mailbox->head++;
    uint16_t slot = (uint16_t)(position % mailbox->capacity);
    ZMsg const *message = mailbox->slots[slot];
    mailbox->slots[slot] = NULL;
    *message_out = message;
    zef_kernel_mailbox_changed(mailbox);
    zef_mailbox_leave(mailbox, state);
    return Z_OK;
}

uint16_t z_mailbox_count(ZMailbox const *mailbox)
{
    ZCriticalState state = zef_mailbox_enter(mailbox);
    uint16_t count = (uint16_t)zef_mailbox_count32(mailbox);
    zef_mailbox_leave(mailbox, state);
    return count;
}

uint16_t z_mailbox_capacity(ZMailbox const *mailbox)
{
    ZCriticalState state = zef_mailbox_enter(mailbox);
    uint16_t capacity = mailbox->capacity;
    zef_mailbox_leave(mailbox, state);
    return capacity;
}

uint16_t z_mailbox_free(ZMailbox const *mailbox)
{
    ZCriticalState state = zef_mailbox_enter(mailbox);
    uint16_t free_space = (uint16_t)(mailbox->capacity - zef_mailbox_count32(mailbox));
    zef_mailbox_leave(mailbox, state);
    return free_space;
}

uint16_t z_mailbox_min_free(ZMailbox const *mailbox)
{
    ZCriticalState state = zef_mailbox_enter(mailbox);
    uint16_t min_free = mailbox->min_free;
    zef_mailbox_leave(mailbox, state);
    return min_free;
}

uint16_t z_mailbox_remove_if(ZMailbox *mailbox, ZMsgPredicate predicate, void *context)
{
    ZEF_REQUIRE(predicate != NULL,
                Z_PANIC_INVALID_ARGUMENT, mailbox, 0u);
    ZCriticalState state = zef_mailbox_enter(mailbox);

    uint32_t original_head = mailbox->head;
    uint32_t original_count = zef_mailbox_count32(mailbox);
    uint32_t write = original_head;
    uint16_t removed = 0u;
    for (uint32_t offset = 0u; offset < original_count; ++offset) {
        uint32_t read_position = original_head + offset;
        uint16_t read_slot = (uint16_t)(read_position % mailbox->capacity);
        ZMsg const *message = mailbox->slots[read_slot];
        if (predicate(message, context)) {
            ++removed;
            z_message_release(message);
        } else {
            uint16_t write_slot = (uint16_t)(write++ % mailbox->capacity);
            mailbox->slots[write_slot] = message;
        }
    }
    mailbox->tail = write;
    zef_kernel_mailbox_changed(mailbox);
    zef_mailbox_leave(mailbox, state);
    return removed;
}
