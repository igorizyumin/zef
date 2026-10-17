#include "zef.h"

/* A target port supplies strong definitions when interrupt exclusion is
 * required. The no-op default keeps the generic host library usable without
 * a processor-specific port. */
#if defined(__GNUC__) || defined(__clang__)
#define ZEF_WEAK __attribute__((weak))
#else
#define ZEF_WEAK
#endif

ZEF_WEAK ZCriticalState z_port_critical_enter(void)
{
    return 0u;
}

ZEF_WEAK void z_port_critical_leave(ZCriticalState state)
{
    (void)state;
}
