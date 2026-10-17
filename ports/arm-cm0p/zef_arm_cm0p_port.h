#ifndef ZEF_ARM_CM0P_PORT_H
#define ZEF_ARM_CM0P_PORT_H

#include "zef.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 *  @ingroup zef_functions
 *  @memberof ZPort
 *  @brief Bind the kernel used by the Cortex-M0+ exception port.
 *
 *  This also configures PendSV as the lowest-priority system handler. The
 *  application owns the vector table and must install the handlers supplied
 *  by this port.
 *
 *  @param kernel Initialized kernel that will be activated by the port.
 */
void zef_arm_cm0p_port_init(ZKernel *kernel);
/**
 *  @ingroup zef_functions
 *  @memberof ZPort
 *  Return the Cortex-M0+ port callbacks for kernel initialization.
 *  @return Pointer to a static, read-only port callback table.
 *  @pre The port initialization function has been called before the kernel uses
 *       the returned port for preemptive activation.
 */
ZPort const *zef_arm_cm0p_port(void);

#ifdef __cplusplus
}
#endif

#endif
