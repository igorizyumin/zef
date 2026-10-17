#ifndef ZEF_CONFIG_H
#define ZEF_CONFIG_H

#include <stddef.h>
#include <stdint.h>
#include <limits.h>

/** Alignment probe used for default message-pool alignment. */
typedef union ZefMaxAlign {
    long double long_double_value; /**< Member imposing long-double alignment. */
    void *pointer_value; /**< Member imposing pointer alignment. */
    uint64_t integer_value; /**< Member imposing 64-bit integer alignment. */
} ZefMaxAlign;

/**
 * @ingroup zef_macros
 * Cooperative scheduler execution mode.
 */
#define ZEF_EXECUTION_COOPERATIVE 0
/**
 * @ingroup zef_macros
 * Shared-stack preemptive scheduler execution mode.
 */
#define ZEF_EXECUTION_PREEMPTIVE 1

/**
 * @ingroup zef_macros
 * Scheduler execution mode; applications may override this before including zef.h.
 */
#ifndef ZEF_EXECUTION_MODE
#define ZEF_EXECUTION_MODE ZEF_EXECUTION_COOPERATIVE
#endif

/**
 * @ingroup zef_macros
 * Maximum number of reactors supported by one kernel (8, 16, or 32).
 */
#ifndef Z_MAX_REACTORS
#define Z_MAX_REACTORS 8u
#endif

/**
 * @ingroup zef_macros
 * Maximum number of topic identifiers supported by one topic table.
 */
#ifndef Z_MAX_TOPICS
#define Z_MAX_TOPICS 64u
#endif

/**
 * @ingroup zef_macros
 * Maximum number of pools in one message-pool set.
 */
#ifndef Z_MAX_MESSAGE_POOLS
#define Z_MAX_MESSAGE_POOLS 3u
#endif

/**
 * @ingroup zef_macros
 * Maximum depth of a hierarchical state-machine path.
 */
#ifndef Z_MAX_STATE_DEPTH
#define Z_MAX_STATE_DEPTH 8u
#endif

/**
 * @ingroup zef_macros
 * Maximum number of slots in one mailbox.
 */
#ifndef Z_MAX_MAILBOX_CAPACITY
#define Z_MAX_MAILBOX_CAPACITY 64u
#endif

/**
 * @ingroup zef_macros
 * Unsigned integer type used by ZTick.
 */
#ifndef ZEF_TICK_TYPE
#define ZEF_TICK_TYPE uint32_t
#endif

/**
 * @ingroup zef_macros
 * Alignment required for message-pool storage and blocks.
 */
#ifndef ZEF_POOL_ALIGNMENT
#define ZEF_POOL_ALIGNMENT _Alignof(ZefMaxAlign)
#endif

/**
 * @ingroup zef_macros
 * Enable runtime precondition and invariant checks when nonzero.
 */
#ifndef ZEF_ENABLE_ASSERTIONS
#define ZEF_ENABLE_ASSERTIONS 1
#endif

#if ZEF_ENABLE_ASSERTIONS != 0 && ZEF_ENABLE_ASSERTIONS != 1
#error "ZEF_ENABLE_ASSERTIONS must be 0 or 1"
#endif

/**
 * @ingroup zef_macros
 * Unsigned integer type used for dynamic-message ownership claims.
 */
#ifndef ZEF_CLAIM_TYPE
#define ZEF_CLAIM_TYPE uint16_t
#endif

#if ZEF_EXECUTION_MODE != ZEF_EXECUTION_COOPERATIVE && \
    ZEF_EXECUTION_MODE != ZEF_EXECUTION_PREEMPTIVE
#error "ZEF_EXECUTION_MODE must be ZEF_EXECUTION_COOPERATIVE or ZEF_EXECUTION_PREEMPTIVE"
#endif

/**
 * @ingroup zef_macros
 * Number of bits in ZEF_TICK_TYPE.
 */
#define ZEF_TICK_BITS (sizeof(ZEF_TICK_TYPE) * CHAR_BIT)
/**
 * @ingroup zef_macros
 * Largest supported forward timer span for wrap-safe comparisons.
 */
#define ZEF_MAX_TIMER_SPAN ((ZEF_TICK_TYPE)(((ZEF_TICK_TYPE)~(ZEF_TICK_TYPE)0u) >> 1u) - (ZEF_TICK_TYPE)1u)
/**
 * @ingroup zef_macros
 * Number of bits in one topic subscription bitmap word.
 */
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
