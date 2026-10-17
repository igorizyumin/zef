#ifndef ZEF_H
#define ZEF_H

#include "zef_config.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @file
 *  @brief Public API for the ZEF actor, state-machine, and messaging runtime.
 *
 *  ZEF provides fixed-memory messages, bounded mailboxes, hierarchical state
 *  machines, strict-priority reactors, topics, and tick-driven timers. Unless
 *  otherwise noted, invalid arguments and lifecycle violations are reported
 *  through the application's @ref z_panic hook.
 */

/** @defgroup zef_macros Configuration parameters
 *  @brief Build-time configuration parameters for the ZEF runtime.
 */

/** @defgroup zef_functions Public functions
 *  @brief Top-level functions provided by the ZEF runtime and target-port hooks.
 */

/** Boolean value used by the ZEF ABI. */
typedef uint8_t ZBool;
/** Application-defined message topic identifier. */
typedef uint16_t ZTopic;
/** Reactor identifier and strict-priority value. */
typedef uint16_t ZReactorId;
/** Strict-priority value; it is equal to the owning reactor's ID. */
typedef uint8_t ZPriority; /* strict priority value; equal to the Reactor ID */
/** Clock tick type selected by the configurable ZEF_TICK_TYPE macro. */
typedef ZEF_TICK_TYPE ZTick;
/** Opaque processor interrupt/critical-section state. */
typedef uint32_t ZCriticalState;

/** Result returned by non-panicking operations. */
typedef enum {
    Z_OK = 0,       /**< Operation completed successfully. */
    Z_EMPTY,        /**< The source contained no item. */
    Z_NOT_RUNNING,  /**< The requested operation was not active. */
    Z_NOT_FOUND     /**< The requested item or subscription was absent. */
} ZResult;

/** Reason supplied to the application's panic hook. */
typedef enum {
    Z_PANIC_INVALID_ARGUMENT, /**< An argument or identifier is invalid. */
    Z_PANIC_CONFIGURATION,    /**< An object or build setting is invalid. */
    Z_PANIC_NO_MEMORY,        /**< No suitable message-pool block exists. */
    Z_PANIC_MAILBOX_FULL,     /**< A bounded mailbox has no free slot. */
    Z_PANIC_OWNERSHIP,        /**< A message claim or ownership rule was violated. */
    Z_PANIC_LIFECYCLE,        /**< An operation was used in the wrong lifecycle state. */
    Z_PANIC_STATE,            /**< A state-machine or timer state is invalid. */
    Z_PANIC_OVERFLOW,         /**< A bounded counter or timer range overflowed. */
    Z_PANIC_PAYLOAD_SIZE,     /**< A payload size did not match the requested type. */
    Z_PANIC_INTERNAL          /**< An internal invariant was violated. */
} ZPanicReason;

/** Details passed to @ref z_panic when the runtime detects a fatal error. */
typedef struct {
    ZPanicReason reason; /**< Fatal-error category. */
    void const *object;  /**< Object associated with the failure, if any. */
    uintptr_t detail;    /**< Context-specific numeric detail. */
    char const *file;    /**< Source file where the failure was detected. */
    uint32_t line;       /**< Source line where the failure was detected. */
} ZPanicInfo;

/** Optional function-pointer type for applications that wrap panic handling. */
typedef void (*ZPanicHandler)(ZPanicInfo const *info);

/**
 *  @ingroup zef_functions
 *  @brief Application callback for unrecoverable framework or application errors.
 *
 *  Applications must provide this non-returning callback. It is responsible
 *  for putting the hardware and application into a safe state, preserving or
 *  reporting the error information as needed, and deciding how to recover the
 *  system. When this callback is invoked, the application is in an undefined
 *  state, so the callback must not rely on normal application invariants or
 *  attempt to continue ordinary execution. Possible recovery actions include
 *  entering a diagnostic handler or resetting the target.
 *
 *  @param info Description of the failure; never NULL when called by ZEF.
 *  @note This callback must not return. If it does return, ZEF cannot guarantee
 *        the system remains in a safe state.
 */
_Noreturn void z_panic(ZPanicInfo const *info);

/**
 *  @ingroup zef_functions
 *  Source filename form stored in @ref ZPanicInfo.
 */
/* GCC and Clang provide the basename-only extension; retain portability for
 * compilers such as MSVC that only provide the standard __FILE__. */
#if defined(__FILE_NAME__)
#define ZEF_SOURCE_FILE __FILE_NAME__
#else
#define ZEF_SOURCE_FILE __FILE__
#endif

/**
 *  @ingroup zef_functions
 *  Invoke @ref z_panic with source-location metadata.
 *
 *  @param reason_ A @ref ZPanicReason value.
 *  @param object_ Object associated with the failure, or NULL.
 *  @param detail_ Context-specific numeric detail.
 *  @note This macro does not return when the panic hook is implemented as
 *        required.
 */
#define ZEF_PANIC(reason_, object_, detail_)                                      \
    do {                                                                          \
        ZPanicInfo zef_panic_info_ = {                                            \
            (reason_), (object_), (detail_), ZEF_SOURCE_FILE, (uint32_t)__LINE__  \
        };                                                                        \
        z_panic(&zef_panic_info_);                                                \
    } while (0)

/**
 *  @ingroup zef_functions
 *  Validate a condition and panic when assertions are enabled.
 *
 *  In assertion-disabled builds the condition is not evaluated and no panic
 *  metadata is produced.
 *
 *  @param condition_ Boolean expression to validate.
 *  @param reason_ A @ref ZPanicReason value used when the check fails.
 *  @param object_ Object associated with the failure, or NULL.
 *  @param detail_ Context-specific numeric detail.
 */
#if ZEF_ENABLE_ASSERTIONS
#define ZEF_REQUIRE(condition_, reason_, object_, detail_)                       \
    do {                                                                          \
        if (!(condition_)) {                                                      \
            ZEF_PANIC((reason_), (object_), (detail_));                           \
        }                                                                         \
    } while (0)
#else
/* Disabled builds omit both the condition evaluation and panic metadata. */
#define ZEF_REQUIRE(condition_, reason_, object_, detail_)                       \
    do {                                                                          \
        (void)sizeof(condition_);                                                 \
    } while (0)
#endif

/** Fixed-size storage from which dynamic messages are allocated. */
typedef struct ZMessagePool ZMessagePool;
/** Ordered collection of message pools used for size-based allocation. */
typedef struct ZMessagePoolSet ZMessagePoolSet;
/** Reference-counted dynamic or immutable static message. */
typedef struct ZMsg ZMsg;
/** Bounded FIFO mailbox containing borrowed message references. */
typedef struct ZMailbox ZMailbox;
/** Node in a hierarchical state-machine definition. */
typedef struct ZStateNode ZStateNode;
/** Runtime state for a hierarchical state machine. */
typedef struct ZMachine ZMachine;
/** Registered scheduler participant backed by a machine and mailbox. */
typedef struct ZReactor ZReactor;
/** Convenience composition of one reactor, mailbox, and machine. */
typedef struct ZActor ZActor;
/** Scheduler state and reactor registry. */
typedef struct ZKernel ZKernel;
/** Topic subscription registry associated with a kernel. */
typedef struct ZTopicTable ZTopicTable;
/** Tick source and timer list associated with a kernel. */
typedef struct ZClock ZClock;
/** One-shot or periodic timer. */
typedef struct ZTimer ZTimer;
/** Target-port callbacks used by the kernel. */
typedef struct ZPort ZPort;

/** Storage/ownership form of a message. */
typedef enum {
    Z_MSG_KIND_DYNAMIC = 0, /**< Allocated from a message pool. */
    Z_MSG_KIND_STATIC = 1,  /**< Application-owned immutable storage. */
    Z_MSG_KIND_RECLAIMED = 2 /**< No longer readable; pool block is available. */
} ZMessageKind;

/* Framework-owned members are reserved; use the public operations below. */
struct ZMessagePool {
    void *storage; /**< Caller-owned backing storage for pool blocks. */
    size_t storage_size; /**< Size of @c storage in bytes. */
    size_t block_size; /**< Size of every block in bytes. */
    uint16_t block_count; /**< Total number of blocks in the pool. */
    uint16_t free_count; /**< Number of blocks currently available. */
    uint16_t min_free; /**< Lowest observed value of @c free_count. */
    void *free_list; /**< Framework-managed head of the free-block list. */
    ZBool initialized; /**< Nonzero after successful initialization. */
};

struct ZMessagePoolSet {
    ZMessagePool *pools; /**< Array of initialized pools. */
    uint16_t count; /**< Number of entries in @c pools. */
};

/* A message is mutable only while held through its construction pointer.
 * Handlers receive borrowed const pointers; retain extends their lifetime. */
struct ZMsg {
    ZMessageKind kind; /**< Storage and ownership form of the message. */
    ZTopic topic; /**< Application-defined topic identifier. */
    size_t payload_size; /**< Payload size in bytes. */
    void const *payload; /**< Pointer to immutable or construction payload data. */
    ZEF_CLAIM_TYPE claims; /**< Active ownership-claim count for dynamic messages. */
    ZMessagePool *pool; /**< Pool that owns a dynamic message block. */
    ZBool valid; /**< Nonzero while the message can be read. */
};

struct ZMailbox {
    ZMsg const **slots; /**< Caller-owned circular array of message pointers. */
    uint16_t capacity; /**< Number of entries available in @c slots. */
    uint32_t head; /**< Framework-managed position of the oldest entry. */
    uint32_t tail; /**< Framework-managed position after the newest entry. */
    uint16_t min_free; /**< Lowest observed free-slot count. */
    ZReactor *owner; /**< Owning reactor, or NULL before registration. */
    ZBool initialized; /**< Nonzero after successful initialization. */
};

/** Result returned by a state handler. */
typedef enum {
    Z_ACTION_UNHANDLED, /**< Let the parent state try the message. */
    Z_ACTION_HANDLED,   /**< Consume the message without changing state. */
    Z_ACTION_TRANSITION /**< Transition to the action's target state. */
} ZActionKind;

/** State-handler decision returned from a @ref ZStateHandler callback. */
typedef struct {
    ZActionKind kind; /**< Decision kind. */
    ZStateNode const *target; /**< Destination for a transition, or NULL otherwise. */
} ZAction;

/**
 *  @ingroup zef_functions
 *  @memberof ZAction
 *  Return an action that allows a parent state to handle the message.
 *  @return An action with kind @ref Z_ACTION_UNHANDLED and no target.
 */
static inline ZAction z_action_unhandled(void)
{
    return (ZAction){ .kind = Z_ACTION_UNHANDLED, .target = NULL };
}

/**
 *  @ingroup zef_functions
 *  @memberof ZAction
 *  Return an action that consumes the message in the current state.
 *  @return An action with kind @ref Z_ACTION_HANDLED and no target.
 */
static inline ZAction z_action_handled(void)
{
    return (ZAction){ .kind = Z_ACTION_HANDLED, .target = NULL };
}

/**
 *  @ingroup zef_functions
 *  @memberof ZAction
 *  Return an action that transitions to @p target after dispatch.
 *  @param target Non-NULL destination state belonging to the same hierarchy.
 *  @return An action with kind @ref Z_ACTION_TRANSITION.
 */
static inline ZAction z_action_transition(ZStateNode const *target)
{
    return (ZAction){ .kind = Z_ACTION_TRANSITION, .target = target };
}

/**
 *  Callback that handles a message in a state.
 *  @param context Application context supplied during machine initialization.
 *  @param message Borrowed readable message; the callback must not release it.
 *  @return A handled, unhandled, or transition action.
 */
typedef ZAction (*ZStateHandler)(void *context, ZMsg const *message);
/**
 *  Callback invoked when entering or leaving a state.
 *  @param context Application context supplied during machine initialization.
 */
typedef void (*ZStateHook)(void *context);

/** @name Hierarchical state machines */
/** @{ */

/**
 *  @ingroup zef_functions
 *  Define a state handler with a typed local context pointer.
 *  @param name_ Handler function identifier to define.
 *  @param Type_ Application context type.
 *  @note The macro opens the function body; provide the body statements after
 *        the macro invocation and close it with `}`. The generated function
 *        has the `ZStateHandler` signature and `ctx` has type @p Type_.
 *
 *  @par Example
 *  The macro defines the callback used by @ref ZStateNode::handle
 *  "ZStateNode::handle" and casts its `void *context` argument to `ctx`:
 *  @code
 *  typedef struct {
 *      unsigned received;
 *  } AppContext;
 *
 *  Z_STATE_HANDLER(app_handle, AppContext)
 *      (void)message;
 *      ++ctx->received;
 *      return z_action_handled();
 *  }
 *
 *  static ZStateNode const app_state = {
 *      .parent = NULL,
 *      .initial = NULL,
 *      .handle = app_handle,
 *      .enter = NULL,
 *      .leave = NULL
 *  };
 *  @endcode
 */
#define Z_STATE_HANDLER(name_, Type_)                                             \
    static ZAction name_(void *context_, ZMsg const *message)                     \
    {                                                                              \
        Type_ *ctx = (Type_ *)context_;

/**
 *  @ingroup zef_functions
 *  Define a state hook with a typed local context pointer.
 *  @param name_ Hook function identifier to define.
 *  @param Type_ Application context type.
 *  @note The macro opens the function body; provide the body statements after
 *        the macro invocation and close it with `}`. The generated function
 *        has the `ZStateHook` signature and `ctx` has type @p Type_.
 *
 *  @par Example
 *  The macro defines the callback used by @ref ZStateNode::enter
 *  "ZStateNode::enter" or @ref ZStateNode::leave "ZStateNode::leave":
 *  @code
 *  typedef struct {
 *      unsigned entries;
 *  } AppContext;
 *
 *  Z_STATE_HOOK(app_enter, AppContext)
 *      ++ctx->entries;
 *  }
 *
 *  static ZStateNode const app_state = {
 *      .parent = NULL,
 *      .initial = NULL,
 *      .handle = NULL,
 *      .enter = app_enter,
 *      .leave = NULL
 *  };
 *  @endcode
 */
#define Z_STATE_HOOK(name_, Type_)                                                 \
    static void name_(void *context_)                                              \
    {                                                                              \
        Type_ *ctx = (Type_ *)context_;

/** @} */

struct ZStateNode {
    ZStateNode const *parent; /**< Parent state, or NULL for the root. */
    ZStateNode const *initial; /**< Initial child entered whenever this state becomes active, including startup and transitions; set to NULL to remain in this state. */
    ZStateHandler handle; /**< Message handler, or NULL to defer to the parent. */
    ZStateHook enter; /**< Entry hook, or NULL. */
    ZStateHook leave; /**< Exit hook, or NULL. */
};

struct ZMachine {
    ZStateNode const *root; /**< Root state supplied at initialization. */
    ZStateNode const *current; /**< Current leaf state during dispatch. */
    void *context; /**< Application context passed to callbacks. */
    ZBool initialized; /**< Nonzero after successful initialization. */
    ZBool started; /**< Nonzero after the initial-state chain is entered. */
};

/** Inputs used to initialize a reactor. */
typedef struct {
    ZMachine *machine; /**< State machine dispatched by the reactor. */
    ZMailbox *mailbox; /**< Mailbox from which the reactor consumes messages. */
    void *context;     /**< Application context passed to callbacks. */
} ZReactorConfig;

struct ZReactor {
    ZMachine *machine; /**< State machine dispatched by this reactor. */
    ZMailbox *mailbox; /**< Mailbox consumed by this reactor. */
    void *context; /**< Application context associated with the reactor. */
    ZKernel *kernel; /**< Owning kernel after registration, otherwise NULL. */
    ZReactorId id; /**< Assigned reactor identifier after registration. */
    ZPriority priority; /**< Strict priority; equal to @c id. */
    ZBool initialized; /**< Nonzero after successful initialization. */
    ZBool registered; /**< Nonzero after registration with a kernel. */
    ZBool started; /**< Nonzero after the reactor starts dispatching. */
};

/** A convenience composition of one machine, mailbox, and reactor. */
struct ZActor {
    ZReactor reactor; /**< Embedded reactor. */
    ZMailbox mailbox; /**< Embedded mailbox. */
    ZMachine machine; /**< Embedded state machine. */
};

struct ZPort {
    void (*request_reschedule)(void); /**< Request a scheduler activation. */
    void (*idle)(void); /**< Optional hook called when no work is ready. */
};

/** Result of one scheduler step. */
typedef enum {
    Z_NO_WORK = 0, /**< No started reactor had a message ready. */
    Z_WORK_DONE    /**< One message was dispatched. */
} ZRunResult;

struct ZKernel {
    ZPort const *port; /**< Target-port callback table. */
    ZReactor *reactors[Z_MAX_REACTORS]; /**< Registered reactors indexed by ID. */
    uint32_t runnable; /**< Bitmap of reactors with queued work. */
    uint16_t reactor_count; /**< Number of registered reactors. */
    uint16_t reactor_limit; /**< One past the highest assigned reactor ID. */
    ZPriority active_priority; /**< Priority currently being dispatched. */
    ZBool dispatching; /**< Nonzero while a message is being dispatched. */
    ZBool initialized; /**< Nonzero after successful initialization. */
};

struct ZTopicTable {
    ZKernel *kernel; /**< Kernel whose reactors may subscribe. */
    uint32_t subscribers[Z_MAX_TOPICS]; /**< Subscriber bitmap for each topic. */
    ZBool initialized; /**< Nonzero after successful initialization. */
};

struct ZClock {
    ZKernel *kernel; /**< Kernel receiving timer messages. */
    ZTick now; /**< Current clock tick. */
    ZTimer *timers; /**< Head of the armed-timer list. */
    ZTimer *timer_tail; /**< Tail of the armed-timer list. */
    ZBool initialized; /**< Nonzero after successful initialization. */
};

struct ZTimer {
    ZClock *clock; /**< Owning clock. */
    ZReactorId destination; /**< Reactor receiving timer messages. */
    ZMsg const *message; /**< Message emitted when the timer expires. */
    ZMsg topic_message; /**< Embedded static storage for topic timers. */
    ZTick due; /**< Next expiration tick while armed. */
    ZTick period; /**< Repeat interval; zero for one-shot timers. */
    ZBool initialized; /**< Nonzero after successful initialization. */
    ZBool armed; /**< Nonzero while linked into the clock's armed list. */
    ZBool base_claim; /**< Timer's ownership claim on a dynamic message. */
    ZBool destroyed; /**< Nonzero after destruction; prevents rearming. */
    ZTimer *next; /**< Next timer in the clock's armed list. */
};

/** Predicate used by the ZMailbox remove-if operation. */
typedef ZBool (*ZMsgPredicate)(ZMsg const *message, void *context);

/** @name Port critical-section hooks */
/** @{ */

/* These target-port hooks are global to the processor, not owned by a Kernel.
 * They must preserve and restore the previous interrupt/critical state and
 * support nesting. The library supplies a no-op host default when a target
 * port does not override them. */
/**
 *  @ingroup zef_functions
 *  @memberof ZPort
 *  Enter a critical section and return the state to restore on exit.
 *  @return Previous processor interrupt/critical-section state.
 *  @note Implementations must support nesting and restore exactly the state
 *        supplied to the matching critical-section leave operation.
 */
ZCriticalState z_port_critical_enter(void);
/**
 *  @ingroup zef_functions
 *  @memberof ZPort
 *  Leave a critical section using a previously saved state.
 *  @param state State returned by the matching enter operation.
 */
void z_port_critical_leave(ZCriticalState state);

/** @} */

/** @name Message pools and payloads */
/** @{ */

/**
 *  @ingroup zef_functions
 *  @memberof ZMessagePool
 *  @brief Initialize a fixed-size message pool.
 *
 *  The storage must be aligned to the configured ZEF_POOL_ALIGNMENT and contain at
 *  least @p block_count blocks of @p block_size bytes. A block must be large
 *  enough for a @ref ZMsg and its payload.
 *
 *  @param pool Pool object to initialize.
 *  @param storage Caller-owned backing storage.
 *  @param storage_size Size of @p storage in bytes.
 *  @param block_size Size of each equally-sized block.
 *  @param block_count Number of blocks available.
 *  @pre @p storage is aligned to ZEF_POOL_ALIGNMENT.
 *  @pre @p block_size is nonzero, alignment-compatible, and at least
 *       sizeof(ZMsg).
 *  @pre @p block_count is nonzero and fits within @p storage_size.
 *  @note The storage remains owned by the caller for the lifetime of @p pool.
 */
void z_message_pool_init(ZMessagePool *pool, void *storage, size_t storage_size,
                         size_t block_size, uint16_t block_count);
/**
 *  @ingroup zef_functions
 *  @memberof ZMessagePoolSet
 *  Associate a list of initialized pools with a pool set.
 *  @param pool_set Pool-set object to initialize.
 *  @param pools Array of initialized pools.
 *  @param count Number of entries in @p pools; must be in the range
 *               1..Z_MAX_MESSAGE_POOLS.
 *  @pre Every pool in @p pools was initialized with
 *       the message-pool initialization function.
 */
void z_message_pool_set_init(ZMessagePoolSet *pool_set, ZMessagePool *pools,
                              uint16_t count);
/**
 *  @ingroup zef_functions
 *  @memberof ZMessagePoolSet
 *  Select the application-wide pool set used by the Z_MSG_NEW macro.
 *  @param pool_set Initialized pool set that remains valid until replaced.
 */
void z_message_factory_init(ZMessagePoolSet *pool_set);
/**
 *  @ingroup zef_functions
 *  @memberof ZMsg
 *  Allocate a message and return its mutable payload.
 *
 *  The returned message handle is stored through @p message with one caller
 *  claim. The smallest pool block that can hold the message and payload is
 *  selected.
 *
 *  @param pool_set Initialized pool set to search.
 *  @param topic Topic stored in the message.
 *  @param payload_size Payload size in bytes.
 *  @param message Output handle receiving the caller's initial claim.
 *  @return Mutable payload storage, or no return on an allocation/configuration
 *          failure because the default panic hook is non-returning.
 *  @pre @p message and @p pool_set are non-NULL and the pool set remains valid.
 *  @pre @p payload_size plus sizeof(ZMsg) fits in size_t and in one pool block.
 */
void *z_message_begin(ZMessagePoolSet *pool_set, ZTopic topic, size_t payload_size,
                      ZMsg **message);
/**
 *  @ingroup zef_functions
 *  @memberof ZMsg
 *  Begin a message using the pool set configured by the message-factory
 *  initialization function.
 *  @param topic Topic stored in the message.
 *  @param payload_size Payload size in bytes.
 *  @param message Output handle receiving the caller's initial claim.
 *  @pre The message factory was initialized with a valid pool set.
 *  @see z_message_begin
 */
void *z_message_begin_default(ZTopic topic, size_t payload_size, ZMsg **message);
/**
 *  @ingroup zef_functions
 *  @memberof ZMsg
 *  Initialize an immutable message backed by application-owned payload data.
 *  @param message Caller-owned message object to initialize.
 *  @param topic Topic stored in the message.
 *  @param payload Read-only payload storage, or NULL when @p payload_size is 0.
 *  @param payload_size Payload size in bytes.
 *  @pre The message object and payload storage remain valid while the message
 *       is in use.
 */
void z_message_static_init(ZMsg *message, ZTopic topic,
                           void const *payload, size_t payload_size);
/**
 *  @ingroup zef_functions
 *  Declare an immutable, zero-length topic message with static storage.
 *  @param name_ Identifier for the generated static message object.
 *  @param topic_ Topic stored in the message.
 *  @note @p name_ must be a valid identifier; the macro is intended at file or
 *        block scope and creates a const static message.
 */
#define Z_MSG_STATIC(name_, topic_) \
    static ZMsg const name_ = { \
        .kind = Z_MSG_KIND_STATIC, \
        .topic = (topic_), \
        .payload_size = 0u, \
        .payload = NULL, \
        .claims = 0u, \
        .pool = NULL, \
        .valid = 1u \
    }
/**
 *  @ingroup zef_functions
 *  @memberof ZMsg
 *  Return the mutable payload of a producer-owned dynamic message.
 *
 *  This is a producer-side access path for code that has a generic
 *  @c ZMsg * construction handle but no longer has the typed pointer returned
 *  by @ref z_message_begin. Use @ref Z_MSG_BEGIN or @ref Z_MSG_NEW for the
 *  initial construction lookup.
 *
 *  @param message Dynamic message held through a valid construction claim.
 *  @return Mutable payload pointer.
 *  @pre @p message is not static, reclaimed, or already released.
 *  @warning Do not call this on the borrowed @c ZMsg const * received by a
 *           state handler, or after posting or publishing the message.
 */
void *z_message_payload_mut(ZMsg *message);
/**
 *  @ingroup zef_functions
 *  @memberof ZMsg
 *  Return a producer-owned mutable payload after checking its exact size.
 *
 *  Use this checked accessor when a helper receives a generic @c ZMsg * and
 *  needs to re-access its payload as a known type during message construction.
 *  It is not a handler-side mutation facility.
 *
 *  @param message Dynamic message held through a valid construction claim.
 *  @param expected_size Exact payload size required by the caller.
 *  @return Mutable payload pointer.
 *  @pre @p expected_size equals the message payload size.
 *  @warning Do not call this on the borrowed @c ZMsg const * received by a
 *           state handler, or after posting or publishing the message.
 */
void *z_message_payload_mut_checked(ZMsg *message, size_t expected_size);
/**
 *  @ingroup zef_functions
 *  @memberof ZMsg
 *  Return a message's topic.
 *  @param message Readable static or dynamic message.
 *  @return Stored topic.
 *  @pre A dynamic message has at least one active claim.
 */
ZTopic z_message_topic(ZMsg const *message);
/**
 *  @ingroup zef_functions
 *  @memberof ZMsg
 *  Return a message's payload size in bytes.
 *  @param message Readable static or dynamic message.
 *  @return Payload size.
 */
size_t z_message_payload_size(ZMsg const *message);
/**
 *  @ingroup zef_functions
 *  @memberof ZMsg
 *  Return a read-only pointer to a message's payload.
 *  @param message Readable static or dynamic message.
 *  @return Read-only payload pointer.
 */
void const *z_message_payload(ZMsg const *message);
/**
 *  @ingroup zef_functions
 *  @memberof ZMsg
 *  Return a read-only payload after checking its exact size.
 *  @param message Readable static or dynamic message.
 *  @param expected_size Exact payload size required by the caller.
 *  @return Read-only payload pointer.
 *  @pre @p expected_size equals the message payload size.
 */
void const *z_message_payload_checked(ZMsg const *message, size_t expected_size);
/**
 *  @ingroup zef_functions
 *  @memberof ZMsg
 *  Add one ownership claim to a dynamic message; static messages are unaffected.
 *  @param message Valid readable message.
 *  @pre A dynamic message has an active claim and its claim counter is not at
 *       its maximum value.
 */
void z_message_retain(ZMsg const *message);
/**
 *  @ingroup zef_functions
 *  @memberof ZMsg
 *  Release one ownership claim and reclaim the block when the count reaches zero.
 *  @param message Valid readable message.
 *  @pre The caller owns one claim on a dynamic message.
 *  @note The handle must not be used after releasing its final claim.
 */
void z_message_release(ZMsg const *message);

/**
 *  @ingroup zef_functions
 *  Begin a typed message from an explicit pool set.
 *  @param pool_set_ Initialized pool set.
 *  @param topic_ Message topic.
 *  @param Type_ Complete payload type; sizeof(Type_) must fit a pool block.
 *  @param msg_ ZMsg* lvalue receiving the new message handle.
 */
#define Z_MSG_BEGIN(pool_set_, topic_, Type_, msg_) \
    ((Type_ *)z_message_begin((pool_set_), (topic_), sizeof(Type_), (msg_)))
/**
 *  @ingroup zef_functions
 *  Begin a typed message from the default pool set.
 *  @param topic_ Message topic.
 *  @param Type_ Complete payload type supported by the configured pools.
 *  @param msg_ ZMsg* lvalue receiving the new message handle.
 *  @pre The message factory was initialized first.
 */
#define Z_MSG_NEW(topic_, Type_, msg_) \
    ((Type_ *)z_message_begin_default((topic_), sizeof(Type_), (msg_)))
/**
 *  @ingroup zef_functions
 *  Read a typed message payload, checking its size against @p Type_.
 *  @param message_ Readable message handle.
 *  @param Type_ Complete type whose size must equal the payload size.
 */
#define Z_MSG_PAYLOAD(message_, Type_) \
    ((Type_ const *)z_message_payload_checked((message_), sizeof(Type_)))
/**
 *  @ingroup zef_functions
 *  Re-access a producer-owned mutable payload as @p Type_, checking its size.
 *
 *  This macro is intended for producer-side helper functions that still hold
 *  a dynamic message's construction claim but only have a generic @c ZMsg *.
 *  Prefer @ref Z_MSG_BEGIN or @ref Z_MSG_NEW when initially allocating the
 *  message, since those macros already return the typed mutable payload.
 *
 *  @param msg_ Mutable dynamic message handle with its construction claim.
 *  @param Type_ Complete type whose size must equal the payload size.
 *  @warning Do not use this with the borrowed @c ZMsg const * supplied to a
 *           state handler, with a static message, or after posting/publishing.
 */
#define Z_MSG_PAYLOAD_MUT(msg_, Type_) \
    ((Type_ *)z_message_payload_mut_checked((msg_), sizeof(Type_)))

/** @} */

/** @name Mailboxes */
/** @{ */

/**
 *  @ingroup zef_functions
 *  @memberof ZMailbox
 *  Initialize a bounded FIFO mailbox using caller-provided message slots.
 *  @param mailbox Mailbox object to initialize.
 *  @param slots Array of message-pointer slots owned by the caller.
 *  @param capacity Number of slots; must be in the range
 *                  1..Z_MAX_MAILBOX_CAPACITY.
 *  @pre @p slots contains at least @p capacity entries and remains valid for
 *       the mailbox lifetime.
 */
void z_mailbox_init(ZMailbox *mailbox, ZMsg const **slots, uint16_t capacity);
/**
 *  @ingroup zef_functions
 *  @memberof ZMailbox
 *  @brief Post a message to the back of a mailbox.
 *
 *  This operation is safe to call from an interrupt context when the target
 *  port supplies appropriate critical-section hooks. On success, ownership
 *  of @p *message is transferred and the caller's pointer is set to NULL.
 *
 *  @param mailbox Initialized destination mailbox.
 *  @param message Non-NULL pointer to a valid message handle owned by caller.
 *  @pre The mailbox has a free slot; otherwise the panic hook is called.
 *  @note The mailbox takes the caller's existing claim; it does not add one.
 */
void z_mailbox_post(ZMailbox *mailbox, ZMsg **message);
/**
 *  @ingroup zef_functions
 *  Post a message to an actor's mailbox and clear the caller's handle.
 *  @param actor_ Initialized actor whose mailbox receives the message.
 *  @param msg_ Message-handle lvalue; it must refer to a valid message and is
 *              set to NULL after successful transfer.
 */
#define Z_MSG_POST(actor_, msg_)                                      \
    do {                                                              \
        ZMsg *zef_post_message_ = (ZMsg *)(msg_);                    \
        z_mailbox_post(&(actor_)->mailbox, &zef_post_message_);       \
        (msg_) = zef_post_message_;                                  \
    } while (0)
/**
 *  @ingroup zef_functions
 *  @memberof ZMailbox
 *  Transfer a message to the front of a mailbox and clear the caller's handle.
 *  @param mailbox Initialized destination mailbox.
 *  @param message Non-NULL pointer to a valid caller-owned message handle.
 *  @pre The mailbox has a free slot.
 */
void z_mailbox_post_front(ZMailbox *mailbox, ZMsg **message);
/**
 *  @ingroup zef_functions
 *  @memberof ZMailbox
 *  Copy a borrowed message reference into an unassigned/deferred mailbox.
 *  @param mailbox Initialized mailbox that is not owned by a reactor.
 *  @param message Readable message whose claim is retained by the mailbox.
 *  @pre The mailbox has a free slot and has not been registered with a reactor.
 */
void z_mailbox_postpone(ZMailbox *mailbox, ZMsg const *message);
/**
 *  @ingroup zef_functions
 *  @memberof ZMailbox
 *  Move the newest message from an unassigned source to the front of destination.
 *  @param destination Initialized mailbox with a free slot.
 *  @param source Initialized, unassigned mailbox providing the message.
 *  @return @ref Z_OK, or @ref Z_EMPTY when the source has no messages.
 *  @note Ownership claims are transferred without being changed.
 */
ZResult z_mailbox_recall(ZMailbox *destination, ZMailbox *source);
/**
 *  @ingroup zef_functions
 *  @memberof ZMailbox
 *  Remove the oldest message from a mailbox without releasing it.
 *  @param mailbox Initialized mailbox to read.
 *  @param message_out Non-NULL output receiving the caller-owned message claim.
 *  @return @ref Z_OK, or @ref Z_EMPTY when the mailbox is empty.
 */
ZResult z_mailbox_take(ZMailbox *mailbox, ZMsg const **message_out);
/**
 *  @ingroup zef_functions
 *  @memberof ZMailbox
 *  Return the current number of queued messages.
 *  @param mailbox Initialized mailbox to inspect.
 *  @return Number of queued messages, at most the mailbox capacity.
 */
uint16_t z_mailbox_count(ZMailbox const *mailbox);
/**
 *  @ingroup zef_functions
 *  @memberof ZMailbox
 *  Return the mailbox capacity in message slots.
 *  @param mailbox Initialized mailbox to inspect.
 *  @return Configured slot count.
 */
uint16_t z_mailbox_capacity(ZMailbox const *mailbox);
/**
 *  @ingroup zef_functions
 *  @memberof ZMailbox
 *  Return the number of currently free message slots.
 *  @param mailbox Initialized mailbox to inspect.
 *  @return Current free-slot count.
 */
uint16_t z_mailbox_free(ZMailbox const *mailbox);
/**
 *  @ingroup zef_functions
 *  @memberof ZMailbox
 *  Return the lowest free-slot count observed since initialization.
 *  @param mailbox Initialized mailbox to inspect.
 *  @return Historical minimum free-slot count.
 */
uint16_t z_mailbox_min_free(ZMailbox const *mailbox);
/**
 *  @ingroup zef_functions
 *  @memberof ZMailbox
 *  Release and remove every queued message for which @p predicate is true.
 *  @param mailbox Initialized mailbox to compact.
 *  @param predicate Non-NULL predicate called once for each queued message.
 *  @param context Opaque context passed to @p predicate.
 *  @return Number of removed messages.
 *  @note The predicate runs while the mailbox is protected; it should be
 *        short, non-blocking, and must not mutate the mailbox.
 */
uint16_t z_mailbox_remove_if(ZMailbox *mailbox, ZMsgPredicate predicate, void *context);

/** @} */

/** @name Hierarchical state machines */
/** @{ */

/**
 *  @ingroup zef_functions
 *  @memberof ZMachine
 *  Initialize a state machine at its root state.
 *  @param machine Machine object to initialize.
 *  @param root Root state; its parent must be NULL and its hierarchy must not
 *              contain parent cycles.
 *  @param context Application context passed to handlers and hooks; may be NULL.
 *  @pre The state hierarchy depth is at most Z_MAX_STATE_DEPTH.
 */
void z_machine_init(ZMachine *machine, ZStateNode const *root, void *context);
/**
 *  @ingroup zef_functions
 *  @memberof ZMachine
 *  Enter the initial-state chain and make the machine dispatchable.
 *  @param machine Initialized machine that has not already been started.
 *  @pre Every initial child points back to its parent and the initial chain
 *       fits within Z_MAX_STATE_DEPTH.
 */
void z_machine_start(ZMachine *machine);
/**
 *  @ingroup zef_functions
 *  @memberof ZMachine
 *  Dispatch one borrowed message through the current state hierarchy.
 *  @param machine Started machine.
 *  @param message Readable message borrowed for the duration of dispatch.
 *  @note The machine does not retain or release @p message.
 */
void z_machine_dispatch(ZMachine *machine, ZMsg const *message);
/**
 *  @ingroup zef_functions
 *  @memberof ZMachine
 *  Return the machine's current leaf state after it has been started.
 *  @param machine Started machine.
 *  @return Current state node.
 */
ZStateNode const *z_machine_state(ZMachine const *machine);

/** @} */

/** @name Reactors, actors, and scheduling */
/** @{ */

/**
 *  @ingroup zef_functions
 *  @memberof ZReactor
 *  Initialize a reactor from a machine, mailbox, and application context.
 *  @param reactor Reactor object to initialize.
 *  @param config Non-NULL configuration containing initialized machine and
 *                mailbox objects.
 *  @pre The machine and mailbox are not already owned by another reactor.
 */
void z_reactor_init(ZReactor *reactor, ZReactorConfig const *config);
/**
 *  @ingroup zef_functions
 *  @memberof ZReactor
 *  Register a reactor with a kernel.
 *
 *  The explicit ID is also the reactor's strict priority; larger IDs run
 *  before lower IDs. IDs are unique within a kernel.
 *
 *  @param kernel Initialized kernel receiving the reactor.
 *  @param reactor Initialized, unregistered reactor.
 *  @param id Unique reactor ID in the range 0..Z_MAX_REACTORS-1.
 *  @return The assigned ID, equal to @p id.
 *  @pre The reactor's mailbox is not already assigned to a reactor.
 */
ZReactorId z_reactor_register(ZKernel *kernel, ZReactor *reactor, ZReactorId id);
/**
 *  @ingroup zef_functions
 *  @memberof ZReactor
 *  Start a registered reactor and its state machine.
 *  @param kernel Kernel that owns the reactor.
 *  @param id ID of a registered, not-yet-started reactor.
 */
void z_reactor_start(ZKernel *kernel, ZReactorId id);
/**
 *  @ingroup zef_functions
 *  @memberof ZActor
 *  Initialize the composed machine, mailbox, and reactor in an actor.
 *  @param actor Actor object to initialize.
 *  @param root Root state for the actor's machine.
 *  @param context Application context passed to state callbacks.
 *  @param mailbox_slots Caller-owned array of message slots.
 *  @param mailbox_capacity Number of slots; must not exceed
 *                          Z_MAX_MAILBOX_CAPACITY.
 *  @pre @p mailbox_slots remains valid for the actor lifetime.
 */
void z_actor_init(ZActor *actor, ZStateNode const *root, void *context,
                  ZMsg const **mailbox_slots, uint16_t mailbox_capacity);
/**
 *  @ingroup zef_functions
 *  @memberof ZActor
 *  Register the reactor contained in an actor.
 *  @param kernel Initialized destination kernel.
 *  @param actor Initialized, unregistered actor.
 *  @param id Unique reactor ID and priority.
 *  @return The assigned ID, equal to @p id.
 */
ZReactorId z_actor_register(ZKernel *kernel, ZActor *actor, ZReactorId id);
/**
 *  @ingroup zef_functions
 *  @memberof ZActor
 *  Start the reactor contained in an actor.
 *  @param actor Registered, not-yet-started actor.
 *  @pre The actor must already be registered with a kernel.
 */
void z_actor_start(ZActor *actor);
/**
 *  @ingroup zef_functions
 *  @memberof ZKernel
 *  Initialize a kernel with target-port callbacks.
 *  @param kernel Kernel object to initialize.
 *  @param port Non-NULL port callback table; individual callbacks may be NULL.
 *  @pre The port table remains valid while the kernel is used.
 */
void z_kernel_init(ZKernel *kernel, ZPort const *port);
/**
 *  @ingroup zef_functions
 *  @memberof ZKernel
 *  Dispatch at most one ready message, returning whether work was performed.
 *  @param kernel Initialized kernel.
 *  @return @ref Z_WORK_DONE when one message was dispatched, otherwise
 *          @ref Z_NO_WORK.
 */
ZRunResult z_kernel_run_one(ZKernel *kernel);
/**
 *  @ingroup zef_functions
 *  @memberof ZKernel
 *  Run the scheduler forever, calling the port's idle hook when no work exists.
 *  @param kernel Initialized kernel with a valid port table.
 *  @note This function does not return; use the one-step kernel run operation for a bounded
 *        scheduler step.
 */
void z_kernel_run_forever(ZKernel *kernel);
/**
 *  @ingroup zef_functions
 *  @memberof ZKernel
 *  Return the priority currently being dispatched, if any.
 *  @param kernel Initialized kernel.
 *  @param priority_out Output for the active priority when the return value is
 *                      @ref Z_OK; it may be NULL when no dispatch is active.
 *  @return @ref Z_OK while dispatching, or @ref Z_NOT_RUNNING otherwise.
 */
ZResult z_kernel_current_priority(ZKernel const *kernel, ZPriority *priority_out);

/** @} */

/** @name Topics */
/** @{ */

/**
 *  @ingroup zef_functions
 *  @memberof ZTopicTable
 *  Initialize a topic table for a kernel.
 *  @param table Topic-table object to initialize.
 *  @param kernel Initialized kernel whose reactor IDs will be subscribed.
 *  @pre The table remains associated with the same kernel for its lifetime.
 */
void z_topic_table_init(ZTopicTable *table, ZKernel *kernel);
/**
 *  @ingroup zef_functions
 *  @memberof ZTopicTable
 *  Subscribe a registered reactor to a topic.
 *  @param table Initialized topic table.
 *  @param topic Topic ID in the range 0..Z_MAX_TOPICS-1.
 *  @param reactor_id ID of a reactor already registered with the table's kernel.
 *  @note Repeating an existing subscription is harmless.
 */
void z_topic_subscribe(ZTopicTable *table, ZTopic topic, ZReactorId reactor_id);
/**
 *  @ingroup zef_functions
 *  @memberof ZTopicTable
 *  Remove a reactor's subscription and report whether it existed.
 *  @param table Initialized topic table.
 *  @param topic Topic ID in the range 0..Z_MAX_TOPICS-1.
 *  @param reactor_id Registered reactor ID.
 *  @return @ref Z_OK when removed, or @ref Z_NOT_FOUND when absent.
 */
ZResult z_topic_unsubscribe(ZTopicTable *table, ZTopic topic, ZReactorId reactor_id);
/**
 *  @ingroup zef_functions
 *  @memberof ZTopicTable
 *  Publish a dynamic message to all subscribers of its topic.
 *
 *  On success, ownership of @p *message is transferred to the publication
 *  operation and the caller's pointer is set to NULL. The return value is
 *  the number of subscribed reactors that received the message.
 *
 *  @param kernel Initialized kernel associated with @p table.
 *  @param table Initialized topic table associated with @p kernel.
 *  @param message Non-NULL pointer to a dynamic message handle. The message's
 *                 topic must be in the configured topic range.
 *  @return Number of subscribing reactors that received the message; zero when
 *          there are no subscribers (the message is released in that case).
 *  @pre Every subscribed destination mailbox has capacity for the message.
 */
uint16_t z_topic_publish(ZKernel *kernel, ZTopicTable *table, ZMsg **message);

/** @} */

/** @name Clocks and timers */
/** @{ */

/**
 *  @ingroup zef_functions
 *  @memberof ZClock
 *  Initialize a clock associated with a kernel.
 *  @param clock Clock object to initialize.
 *  @param kernel Initialized kernel that owns timer destinations.
 */
void z_clock_init(ZClock *clock, ZKernel *kernel);
/**
 *  @ingroup zef_functions
 *  @memberof ZTimer
 *  Initialize a timer that emits a zero-payload static topic message.
 *  @param timer Timer object to initialize; it must not be an active timer.
 *  @param clock Initialized clock that owns the timer.
 *  @param destination Registered reactor ID receiving each tick message.
 *  @param topic Topic stored in the static message.
 *  @pre The timer remains valid until stopped/destroyed and the clock remains
 *       valid while the timer exists.
 */
void z_timer_init_topic(ZTimer *timer, ZClock *clock, ZReactorId destination,
                        ZTopic topic);
/**
 *  @ingroup zef_functions
 *  @memberof ZTimer
 *  Initialize a timer that emits a copied payload message.
 *  @param timer Timer object to initialize; it must not be an active timer.
 *  @param clock Initialized clock that owns the timer.
 *  @param destination Registered reactor ID receiving each message.
 *  @param pool_set Initialized pool set used to store the copied payload.
 *  @param topic Topic stored in the message.
 *  @param payload Source bytes to copy, or NULL when @p payload_size is zero.
 *  @param payload_size Number of source bytes to copy.
 *  @pre The pool set contains a block large enough for the message and payload.
 */
void z_timer_init_payload(ZTimer *timer, ZClock *clock, ZReactorId destination,
                          ZMessagePoolSet *pool_set, ZTopic topic,
                          void const *payload, size_t payload_size);
/**
 *  @ingroup zef_functions
 *  @memberof ZTimer
 *  Arm a timer after @p delay ticks, optionally repeating every @p period ticks.
 *  @param timer Initialized, stopped, non-destroyed timer.
 *  @param delay Initial delay in ticks; zero means the next clock tick.
 *  @param period Repeat interval in ticks; zero makes the timer one-shot.
 *  @pre @p delay and @p period are at most ZEF_MAX_TIMER_SPAN.
 *  @note A periodic timer remains armed until stopped or destroyed.
 */
void z_timer_arm(ZTimer *timer, ZTick delay, ZTick period);
/**
 *  @ingroup zef_functions
 *  @memberof ZTimer
 *  Stop a timer and optionally remove its queued deliveries.
 *  @param timer Initialized, non-destroyed timer.
 *  @param clear_pending Nonzero to remove pending deliveries generated by this
 *                       timer from its destination mailbox; zero to preserve them.
 *  @return @ref Z_OK when the timer was armed or pending deliveries were
 *          cleared, or @ref Z_NOT_RUNNING when neither condition applied.
 *  @note Clearing pending deliveries preserves the timer's base message claim,
 *        so the timer remains reusable and may be armed again.
 */
ZResult z_timer_stop(ZTimer *timer, ZBool clear_pending);
/**
 *  @ingroup zef_functions
 *  @memberof ZTimer
 *  Permanently destroy a timer and release its timer-owned message.
 *  @param timer Initialized, non-destroyed timer.
 *  @return @ref Z_OK when destroyed, or @ref Z_NOT_RUNNING when it was already
 *          destroyed and had nothing left to release.
 *  @note Destruction disarms the timer, removes all pending timer deliveries,
 *        releases the base message claim, and prevents rearming. Reinitialize
 *        the timer before using it again.
 */
ZResult z_timer_destroy(ZTimer *timer);
/**
 *  @ingroup zef_functions
 *  @memberof ZClock
 *  Advance the clock by one tick and enqueue due timer messages.
 *  @param clock Initialized clock.
 *  @pre Every due timer's destination mailbox has a free slot.
 *  @note Call once per elapsed tick; timer expiry is exact at the clock's
 *        tick boundary and does not catch up missed ticks.
 */
void z_clock_tick(ZClock *clock);

/** @} */

#ifdef __cplusplus
}
#endif

#endif
