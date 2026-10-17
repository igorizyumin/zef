#include "demo_serial_actor.h"

#include "nucleo_l073rz_hw.h"

/* -------------------------------------------------------------------------- */
/* Shared serial-state helpers                                                */
/* -------------------------------------------------------------------------- */

static ZStateNode const g_serial_root;
static ZStateNode const g_serial_idle;
static ZStateNode const g_serial_busy;

static void serial_start_dma(DemoSerialActor *serial, ZMsg const *message);

static void serial_finish_dma(DemoSerialActor *serial)
{
    nucleo_uart_dma_stop();
    /* active_message owns the claim that keeps the DMA payload alive after
     * the dispatch that started the transfer has released its borrow. */
    if (serial->active_message != NULL) {
        z_message_release(serial->active_message);
        serial->active_message = NULL;
    }
    nucleo_led_toggle();
}

static void serial_start_dma(DemoSerialActor *serial, ZMsg const *message)
{
    SerialLine const *line = Z_MSG_PAYLOAD(message, SerialLine);
    if (line->length == 0u) {
        ZEF_PANIC(Z_PANIC_STATE, message, TOPIC_SERIAL_LINE);
    }

    /* The handler's message is borrowed. Keep a separate claim for the
     * asynchronous DMA operation; the caller releases its claim separately. */
    z_message_retain(message);
    serial->active_message = message;
    nucleo_uart_dma_start(line->text, line->length);
}

static ZBool serial_start_deferred_dma(DemoSerialActor *serial)
{
    ZMsg const *message = NULL;
    if (z_mailbox_take(&serial->deferred_mailbox, &message) != Z_OK) {
        return 0u;
    }
    /* take does not release the deferred-mailbox claim. start_dma adds the
     * DMA claim, so this claim can be released after the transfer starts. */
    serial_start_dma(serial, message);
    z_message_release(message);
    return 1u;
}

/* -------------------------------------------------------------------------- */
/* Idle state                                                                 */
/* -------------------------------------------------------------------------- */

Z_STATE_HANDLER(DemoSerialActor, serial_idle_handle)
{
    switch (z_message_topic(message)) {
    case TOPIC_SERIAL_LINE:
        /* Keep the borrowed dispatch reference until busy_enter can retain it
         * for the DMA operation. The Kernel releases the dispatch claim only
         * after the transition and handler return complete. */
        ctx->active_message = message;
        return z_action_transition(&g_serial_busy);
    default:
        return z_action_handled();
    }
}

static ZStateNode const g_serial_idle = {
    .parent = &g_serial_root,
    .handle = serial_idle_handle_zef_adapter
};

/* -------------------------------------------------------------------------- */
/* Busy state                                                                 */
/* -------------------------------------------------------------------------- */

Z_STATE_HOOK(DemoSerialActor, serial_busy_enter)
{
    if (ctx->active_message == NULL) {
        ZEF_PANIC(Z_PANIC_STATE, ctx, 0u);
    }
    serial_start_dma(ctx, ctx->active_message);
}

Z_STATE_HANDLER(DemoSerialActor, serial_busy_handle)
{
    switch (z_message_topic(message)) {
    case TOPIC_SERIAL_LINE:
        /* Add a deferred-mailbox claim while the dispatch still owns its
         * temporary borrow. The handler's claim is released by the Kernel. */
        z_mailbox_postpone(&ctx->deferred_mailbox, message);
        return z_action_handled();

    case TOPIC_SERIAL_DMA_COMPLETE:
        serial_finish_dma(ctx);
        if (serial_start_deferred_dma(ctx)) {
            return z_action_handled();
        }
        return z_action_transition(&g_serial_idle);

    default:
        return z_action_unhandled();
    }
}

static ZStateNode const g_serial_busy = {
    .parent = &g_serial_root,
    .handle = serial_busy_handle_zef_adapter,
    .enter = serial_busy_enter_zef_adapter
};

/* -------------------------------------------------------------------------- */
/* Root state                                                                 */
/* -------------------------------------------------------------------------- */

static ZStateNode const g_serial_root = {
    .initial = &g_serial_idle
};

/* -------------------------------------------------------------------------- */
/* Actor wiring                                                               */
/* -------------------------------------------------------------------------- */

void demo_serial_actor_init(DemoSerialActor *serial)
{
    ZEF_REQUIRE(serial != NULL,
                Z_PANIC_INVALID_ARGUMENT, serial, 0u);
    serial->active_message = NULL;
    z_actor_init(&serial->actor, &g_serial_root, serial,
                 serial->mailbox_slots, 8u);
    z_mailbox_init(&serial->deferred_mailbox,
                   serial->deferred_slots, 16u);
}

void demo_serial_actor_dma_complete_isr(DemoSerialActor *serial)
{
    Z_MSG_STATIC(dma_complete_message, TOPIC_SERIAL_DMA_COMPLETE);

    ZMsg const *message = &dma_complete_message;
    Z_MSG_POST(&serial->actor, message);
}
