#ifndef ZEF_NUCLEO_EXECUTION_CONFIG_H
#define ZEF_NUCLEO_EXECUTION_CONFIG_H

/* Select the example's execution model here. Change this one definition to
 * ZEF_NUCLEO_EXECUTION_COOPERATIVE to build the cooperative variant. */
#define ZEF_NUCLEO_EXECUTION_COOPERATIVE 0
#define ZEF_NUCLEO_EXECUTION_PREEMPTIVE 1
#define ZEF_NUCLEO_EXECUTION_MODE ZEF_NUCLEO_EXECUTION_PREEMPTIVE

/* zef_config.h consumes ZEF_EXECUTION_MODE for every target translation unit.
 * Keep this header force-included by the example build so the framework,
 * target Port, and application all use the same mode. */
#define ZEF_EXECUTION_MODE ZEF_NUCLEO_EXECUTION_MODE

#endif
