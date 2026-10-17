#ifndef ZEF_CONFIG_H
#define ZEF_CONFIG_H

#include <stddef.h>
#include <stdint.h>
#include <limits.h>

typedef union ZefMaxAlign {
    long double long_double_value;
    void *pointer_value;
    uint64_t integer_value;
} ZefMaxAlign;

/* Scheduler execution model. Applications may override this before including
 * zef.h, or provide it as a compiler definition for the whole build. */
#define ZEF_EXECUTION_COOPERATIVE 0
#define ZEF_EXECUTION_PREEMPTIVE 1

#ifndef ZEF_EXECUTION_MODE
#define ZEF_EXECUTION_MODE ZEF_EXECUTION_COOPERATIVE
#endif

/* Build-time limits. Applications may override these before including zef.h. */
#ifndef Z_MAX_REACTORS
#define Z_MAX_REACTORS 8u
#endif

#ifndef Z_MAX_TOPICS
#define Z_MAX_TOPICS 64u
#endif

#ifndef Z_MAX_MESSAGE_POOLS
#define Z_MAX_MESSAGE_POOLS 3u
#endif

#ifndef Z_MAX_STATE_DEPTH
#define Z_MAX_STATE_DEPTH 8u
#endif

#ifndef Z_MAX_MAILBOX_CAPACITY
#define Z_MAX_MAILBOX_CAPACITY 64u
#endif

#ifndef ZEF_TICK_TYPE
#define ZEF_TICK_TYPE uint32_t
#endif

#ifndef ZEF_POOL_ALIGNMENT
#define ZEF_POOL_ALIGNMENT _Alignof(ZefMaxAlign)
#endif

#ifndef ZEF_ENABLE_ASSERTIONS
#define ZEF_ENABLE_ASSERTIONS 1
#endif

#if ZEF_ENABLE_ASSERTIONS != 0 && ZEF_ENABLE_ASSERTIONS != 1
#error "ZEF_ENABLE_ASSERTIONS must be 0 or 1"
#endif

#ifndef ZEF_CLAIM_TYPE
#define ZEF_CLAIM_TYPE uint16_t
#endif

#if ZEF_EXECUTION_MODE != ZEF_EXECUTION_COOPERATIVE && \
    ZEF_EXECUTION_MODE != ZEF_EXECUTION_PREEMPTIVE
#error "ZEF_EXECUTION_MODE must be ZEF_EXECUTION_COOPERATIVE or ZEF_EXECUTION_PREEMPTIVE"
#endif

#define ZEF_TICK_BITS (sizeof(ZEF_TICK_TYPE) * CHAR_BIT)
#define ZEF_MAX_TIMER_SPAN ((ZEF_TICK_TYPE)(((ZEF_TICK_TYPE)~(ZEF_TICK_TYPE)0u) >> 1u) - (ZEF_TICK_TYPE)1u)
#define ZEF_TOPIC_BITMAP_WORD_BITS 32u

#if Z_MAX_REACTORS != 8u && Z_MAX_REACTORS != 16u && Z_MAX_REACTORS != 32u
#error "Z_MAX_REACTORS must be 8, 16, or 32"
#endif
#if Z_MAX_TOPICS == 0u || Z_MAX_TOPICS > UINT16_MAX
#error "Z_MAX_TOPICS must be in the range 1..65535"
#endif
#if Z_MAX_MESSAGE_POOLS == 0u || Z_MAX_MESSAGE_POOLS > UINT16_MAX
#error "Z_MAX_MESSAGE_POOLS must be in the range 1..65535"
#endif
#if Z_MAX_STATE_DEPTH == 0u || Z_MAX_STATE_DEPTH > 255u
#error "Z_MAX_STATE_DEPTH must be in the range 1..255"
#endif
#if Z_MAX_MAILBOX_CAPACITY == 0u || Z_MAX_MAILBOX_CAPACITY > UINT16_MAX
#error "Z_MAX_MAILBOX_CAPACITY must be in the range 1..65535"
#endif

_Static_assert(ZEF_POOL_ALIGNMENT >= _Alignof(ZefMaxAlign),
               "ZEF_POOL_ALIGNMENT must satisfy maximum scalar alignment");
_Static_assert(sizeof(ZEF_TICK_TYPE) == 1u || sizeof(ZEF_TICK_TYPE) == 2u ||
                   sizeof(ZEF_TICK_TYPE) == 4u || sizeof(ZEF_TICK_TYPE) == 8u,
               "ZEF_TICK_TYPE must have an exact-width integer size");
_Static_assert(sizeof(ZEF_CLAIM_TYPE) == 1u || sizeof(ZEF_CLAIM_TYPE) == 2u ||
                   sizeof(ZEF_CLAIM_TYPE) == 4u || sizeof(ZEF_CLAIM_TYPE) == 8u,
               "ZEF_CLAIM_TYPE must have an exact-width integer size");

#endif
