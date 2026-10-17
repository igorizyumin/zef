#ifndef ZEF_PORT_INTERNAL_H
#define ZEF_PORT_INTERNAL_H

#include "zef.h"

/* Entry used by a target Port after it has restored a shared-stack activation. */
void zef_kernel_activate_higher(ZKernel *kernel);

#endif
