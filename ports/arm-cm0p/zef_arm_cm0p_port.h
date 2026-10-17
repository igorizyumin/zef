#ifndef ZEF_ARM_CM0P_PORT_H
#define ZEF_ARM_CM0P_PORT_H

#include "zef.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Bind the single Kernel instance used by the Cortex-M0+ exception port and
 * configure PendSV as the lowest-priority system handler. The application
 * still owns the vector table and must install the handlers supplied by this
 * port.
 */
void zef_arm_cm0p_port_init(ZKernel *kernel);
ZPort const *zef_arm_cm0p_port(void);

#ifdef __cplusplus
}
#endif

#endif
