#ifndef ZEF_INTERNAL_H
#define ZEF_INTERNAL_H

#include "zef.h"

#include <stdlib.h>
#include <string.h>

_Noreturn void zef_fail(ZPanicReason reason, void const *object, uintptr_t detail);

ZBool zef_message_is_readable(ZMsg const *message);

void zef_kernel_mailbox_changed(ZMailbox *mailbox);
ZReactor *zef_kernel_reactor(ZKernel *kernel, ZReactorId id);
ZBool zef_kernel_is_registered(ZKernel const *kernel, ZReactorId id);
ZCriticalState zef_port_enter(ZKernel const *kernel);
void zef_port_leave(ZKernel const *kernel, ZCriticalState state);

ZBool zef_mailbox_full(ZMailbox const *mailbox);
void zef_mailbox_enqueue_message(ZMailbox *mailbox, ZMsg const *message,
                                ZBool add_claim);

#endif
